# openQ4 UI Visual Design

Specification version 1.39, 9 November 2026 (1.38 on 5 November, 1.37 on 3 November, 1.36 and 1.35 on 1 November, 1.29 to 1.34 on 31 October, 1.28 on 26 October, 1.27 on 25 October, 1.26 on 24 October, 1.25 on 23 October, 1.24 on 21 October, 1.22 on 11 October, 1.19 on 7 October, 1.15 and 1.16 earlier that day, 1.14 on 1 October, 1.10 to 1.13 the same day, 1.7 to
1.9 on 29 September, 1.6 on 28 September, 1.4 and 1.5 on 27 September, 1.1 to
1.3 on 26 September, 1.0 on 8 September 2026). Status:
implementation target; the replacement has not shipped. Applies to every
player-facing GUI and to the visual editor's Quake 4 preview. Implementation is
coordinated in the [development roadmap](roadmap.md#new-user-interface), with
acceptance and evidence in the [product register](ui/product-requirements.md).
The [archived replacement plan](archive/2026-10-18/plans/idtech5-ui.md) preserves
the original scope and historical stage evidence.

Version 1.19 adds smoothly fading Gaussian scene focus beneath the weapon wheel and HUD, superseding the former no-blur backing. Version 1.18 preserves the weapon wheel's original right-side placement and adds an archived horizontal offset, with zero centered and narrow-view safety fitting. This supersedes the centered default in earlier wheel specifications.

Version 1.24 reconciles Awakening's HUD scope with the project's single-player
expansion policy. It preserves its thirteen-weapon single-player HUD and ordinary
Quake 4 multiplayer requirements; it does not require an expansion multiplayer
module. The register retains superseded requirements and their evidence as history.

Version 1.25 lets cinematic subtitles hang just below the cinematic artwork on
screens narrower than 4:3 when two lines fit there. The picture is too short there
for enlarged captions, which covered the title text a cinematic draws in it.

Version 1.26 keeps the largest caption sizes there too: where only one line fits
below the artwork, a lone caption pages one line at a time below it. A speaker's
name no longer runs under the words; it is left out where it would take more than
half the line.

Version 1.27 gives a second speaker at those sizes the matching line above the
artwork, so two overlapping captions also stay clear of the picture.

Version 1.28 adds objective points of interest to the multiplayer HUD: flag
bases, assault points and Dead Zone zones marked in the world, with distance as
their only text, on the desktop and on the stereo VR HUD plane.

Version 1.1 added measured references from a survey of the effective retail
GUI scripts, interface art, fonts and materials, and corrected anatomy that did
not match them. Version 1.2 adds the [modern interface
direction](#13-modern-interface-direction): viewing profiles, adaptive layout,
controller, touch and Android behavior, and modern screen patterns. Version 1.3
measures the framing-band motion, the HUD and the pop-up frames, adds detailing
rules, a pop-up frame catalogue and modern components, and gives the HUD its
own [section](#14-hud-and-in-game-overlays), including the weapon wheel. Version
1.4 measures the objectives display, the boss, vehicle and scope displays and
the multiplayer HUD and scoreboard, gives the Strogg HUD its own weapon wheel,
and keeps the reticle grid, grain and light band to menu backdrops. Version
1.5 lays the HUD out as a center screen with side screens and bands, centers
the status bar, rebuilds the multiplayer HUD's top, chat and kill feed,
catalogues every crosshair and item icon as vector art, and gives both weapon
wheels the stock's translation and transmission effects. Version 1.6 adds the
loading screens and the tabbed multiplayer menus, and redraws the EKG and the
transmission waveform from their textures. Version 1.7 restores the Quake
emblem to the title screen, sets the single-player pause menu apart from it and
adds a Strogg pause menu. Version 1.8 rebuilds the initializing screen from
separate layers and turns its rings slowly. Version 1.9 replaces the modal
scrim with a soft focus of the screen beneath. Version 1.10 adds a sub-page
level beneath the menu pages, with its own band step and path crumb. Version
1.11 rebuilds the scopes from their stock layers, catalogues the squad patches,
brings The Awakening's weapons, icons, crosshairs and vehicle displays into the
HUD sections, lifts the multiplayer chat clear of the status bar and takes the
waveform out of the weapon wheel. Version 1.12 adds the 9:16 portrait view, a
multiplayer weapon wheel, the Competitive HUD's match bar and a spectator follow
camera, outlines the modal title and widens its glow, and gives the initializing
screen Remastered accents. Version 1.13 refines the vehicle displays: HUD
prompts replace the bracketed exit and boost lines, the transmission display
keeps the right-hand screen's corner on every HUD, and the MCC side gun's
readout is confirmed as the ship's hull. Version 1.14 measures the modal against
the stock: its title hangs in the lit slot, its silhouette is inset in its
rectangle, and its body is placed.
[Appendix C](#appendix-c-change-record) lists every change and the
requirement-register rows it affects.

## 1. Purpose, authority and reference

Deliver a high-definition, high-fidelity Quake 4 interface at every supported
display density and aspect ratio. Preserve the game's military instrument
character, information hierarchy, interactions and sound vocabulary. Replace
low-resolution interface furniture with authored vector geometry. Every menu,
HUD, terminal, scope, loading screen and vehicle display is in scope.

The user's `idtech5-ui` objective authorizes translating the entire GUI set
and replacing interface furniture with vector artwork. This supersedes the
old requirement to draw all furniture from retail bitmaps. It does not
authorize a different game's visual identity or bundling extracted retail art.
Existing bitmap references identify appearance, not implementation assets.
The [legacy guide](ui-legacy-gui-reference.md) records old layout conventions
and script traps for translators; it is not the replacement design contract.

Read this specification before composing a screen, component, icon or editor
preview. An unstyled prototype cannot establish visual completion.

### Source hierarchy

1. Installed Quake 4 PK4s, with their actual filesystem precedence, establish
   stock visuals, animation, behavior, fonts and sounds.
2. Repository `content/baseoq4/` changes establish openQ4's current features.
3. This specification establishes scalable geometry, density, layout, state and
   motion treatment while retaining both sets of behavior.
4. Existing engine-render-target captures establish comparison views. Record
   map, mode, state, language, renderer, resolution and capture command.

Use retail `guis/mainmenu.gui`, `guis/mpmain.gui`, settings pages and their
material references for the Marine menu family. Use each original HUD,
terminal and vehicle GUI for that family's distinct appearance. Do not apply
the Marine olive menu palette indiscriminately to Strogg and diegetic displays.

The effective copy of a stock file is the one in the last archive in load
order. `guis/mainmenu.gui` exists in nine retail archives, and the copy in
`pak025.pk4` wins. Survey the effective copy, never the first one found.

### Measuring stock sources

Stock GUIs author every rectangle on a 640x480 virtual canvas. Record stock
measurements in source units (`u`) on that canvas and convert them last: at
the 1280x720 dp reference, `1 u = 1.5 dp` on both axes. The stock 4:3 canvas
maps to the central 960x720 dp; aspect expansion supplies the remaining width.
In the 9:16 portrait view the 720x1280 dp reference puts the canvas across the
full width at `1 u = 1.125 dp`, and expansion supplies the remaining height
(section 14.4).

- Interface bitmaps are stretched non-uniformly into window rectangles; a
  512x32 texel row plate is drawn at 377x25 u. Measure proportions in the
  texture (visible bounds, cut length, rail width, fade span) and scale them by
  the drawn rectangle. A proportion survives the stretch; a texel count does not.
- A `textscale` of `s` draws an em of `48 s` u, which is `72 s` dp at the
  reference, from the 12, 24 or 48 point atlas (`s` up to 0.30, up to 0.60,
  above). Take cap and x-heights from glyph ink: `.fontdat` rectangles include
  a one-texel border on every side.
- Record colors together with their material blend. The same texture looks
  different under `blend add` or a darkening blend (section 4).
- Record motion from `transition` durations (optional acceleration and
  deceleration fractions give the `accel` profile of section 8), absolute and
  `+N` relative `onTime` keys,
  `guitable_*` pulse tables, and material `scroll` and `rotate` stages.
- Record numbers and descriptions only. Extracted art, fonts and captures stay
  outside tracked source. [Appendix A](#appendix-a-stock-survey-method)
  describes the 1.1 survey.

## 2. Visual identity

Quake 4 menus resemble military instrumentation: dark translucent plates,
thin olive edge rails, 45-degree cuts, restrained highlights, technical labels
and warm orange interaction cues. The world and level imagery remain visible
behind the instrument layer. Interfaces are precise and compact without
being cramped. Decorative detail supports hierarchy or retains a specific
recognizable feature of the original.

Avoid pill buttons, rounded cards, large generic web headings, cartoon shadows,
rainbow gradients, gratuitous neon, indiscriminate glass blur and blank spacer
areas. A panel's shape, contents and ornament are deliberately composed. Do
not obtain fidelity by putting a low-resolution screenshot into a vector box.

### Recognition traits

These measured traits make an interface read as Quake 4. A reconstruction of
a stock screen that drops one of them is not visually complete.

1. **A lit instrument field.** The menu backdrop is dark scene imagery under an
   additive olive light band (peak `#616F26`, applied at 70%), black top and
   bottom vignettes, a `+` reticle grid on a 30 u (45 dp) pitch at 4% opacity
   and drifting static grain at about 2.4%.
2. **Framing bands.** Opaque black bands frame the screen. Their inner edges
   step through 45-degree risers and three raised notches, and a soft olive rim
   light traces every inner edge. The bands move between the home and page
   states.
3. **Open plates.** Action plates carry rails on the leading edge, the cut and
   the bottom edge only. There is no top rail, no trailing rail and no closed
   outline. Fill and bottom rail dissolve toward the trailing end.
4. **The lower-leading cut.** Every action plate loses its lower-leading corner
   to a 45-degree cut through roughly half of its visible height.
5. **The ◥ marker.** A small right triangle filling the upper-trailing half of
   its square, with a soft halo, precedes action labels inside their cap band.
   Flipped vertically, it marks sort direction.
6. **Two faces, graded text.** Marine small capitals carry navigation, actions
   and titles; Lowpixel carries rows, values, lists and body text. Labels are
   white at 80%, setting values amber-yellow at 80%, titles white at 50% and
   headings white at 40%.
7. **Warm, instant response.** Hover or focus turns label and marker orange,
   brightens the plate and grows the label about 3%, all at once. Value rows
   brighten only their plate. Release returns over 300 ms.
8. **Asymmetric panels.** Cards cut two diagonally opposite corners deeply and
   the other two slightly. Modals are dark silhouettes over an additive glow
   field, with a recessed title slot and a deep lower-leading chamfer.
9. **Meaningful color.** Faction marks, team colors and the weapon and item
   color code carry gameplay information and never serve as decoration.

### Divergences to avoid

The survey also found recurring departures from the stock vocabulary in newer
screens. Do not introduce them or carry them into the replacement:

- closed outlines around fields, rows or buttons, including bordered
  rectangles used as furniture;
- centered labels on action plates; stock action labels lead, after the marker;
- identical cuts on all four corners, rounded rectangles and pill shapes;
- a large page heading; the stock screen title is a quiet caption in the
  top band, and the navigation carries the hierarchy;
- orange setting values; orange means interaction, values are amber-yellow;
- text below the type ramp's floor (section 5);
- extra accent hues outside the family palette, such as a second orange;
- a flat gray-green panel fill where the stock uses black plates or
  olive-tinted light plates.

### Families

| Family | Identity | Required fidelity |
| --- | --- | --- |
| Marine menus | Olive/black plates, orange focus, yellow values, Marine title face | Stepped/notched bands, open cut plates, triangular markers, additive light, original scene imagery |
| Marine HUD | Tactical overlays, bright critical values, compact instruments | Weapon/ammo/health/armor hierarchy, sheared gauges, feedback and original spatial relationships |
| Strogg HUD and terminals | Original alien glyphs, mechanical paths and family colors | Each source's geometry and animation; never generic Marine panels |
| World terminals | Local device panel and in-world illumination | Surface mapping, interaction targets, stateful scripts and intended viewing distance |
| Weapon displays | Gun-mounted ammo counters on green, amber and red state fields | Digit legibility at view-model distance, per-weapon art, state colors and pulse cadence |
| Objectives (PDA) | Pale-green open plates beside a leading-edge gradient bar | Objective imagery, entry cadence, notices, Marine and Strogg variants |
| Vehicles and turrets | Weapon-specific reticles, gauges and status | Cockpit alignment, targeting, damage and cooldown states |
| Scopes | Optical/technical marks in source shape and color | Exact aim center, FOV relationship, masks and tick cadence |
| Loading/cinematic overlays | Restrained frame, level identity and progress | Background composition, subtitle safe area, fades and cinematic bars |
| Credits and intros | Rune-to-Latin decode reveals, additive orange credit bars | Reveal choreography, rune/Latin pairing, logo light-up |
| Multiplayer overlays | Tactical tables with team and spectator identity | Score sorting, timers, join choices and authoritative match state |

Family tokens extend shared geometry, motion and interaction tokens. Their
colors and motifs must be measured from their sources and recorded in their
migration entries before implementation. An invented palette applied to all
terminals does not satisfy full GUI replacement.

### Family reference values

Measured from the effective sources named in each row. These values seed the
family tokens; individual terminals and vehicles still need per-source entries.

| Family (sources) | Measured palette | Type (stock `textscale`) | Geometry and motion |
| --- | --- | --- | --- |
| Marine menus (`mainmenu`, `mpmain`, `buymenu`, `restart`, `msg`, `netmenu`) | Section 4 Marine tokens | Marine, Lowpixel; Profont for tabular detail | Section 6 vocabulary; section 8 choreography |
| Marine HUD (`hud`) | Readouts `#DDEBC3`; gauge track `#B3D06E` at 20% and fill at 35% (50% in multiplayer) over black 50%; icons `#A8A360` at 60%; weapon render `#C7BD78` at 40%; weapon name `#B0C891`; selected weapon `#FF8000` in a `#B2CC80` ring; low health `#FF4C00` | Chain 0.50 numerals and 0.32 reserve, trailing-aligned; Marine 0.25 weapon name, 0.20 pickup and radio labels | Rounded gauge plates with a stepped foot, trailing-anchored notched fills, ten-cell armor fill, 0.22 shear about each element's center; EKG; 2 Hz low-health rim (section 14) |
| Strogg HUD (`hud_strogg`) | Readouts `#FCFFC8`; gauge fill `#FF9000` at 20–35%; icons `#FFCC00` at 60%; selected weapon `#FFCC00`, others `#F59512` at 60%, ring `#F2AD0B` | R_Strogg for all HUD text, 0.12–0.50; numerals 0.50 and unsheared | 30–33° shoulders and small notches; 0.22 backward shear; masked grain over each gauge; low health below 25% |
| Multiplayer (`mphud`, `scoreboard`, `summary`, `mpmsgmode`, the multiplayer parts of `hud`) | Marine team `#6AA42B`; Strogg team `#FF7B04`; spectators `#999999`; notices `#FFFF8D`; team names and team chat `#AAE355` and `#FF8E00`; deathmatch and Tourney headers `#8B964B`; your Tourney arena `#5EB987` at 40%, finished arenas `#3E57B7`; health icon `#F7C004` and armor icon `#FA2B05`, both at 80% (the stock define names are swapped); ammo fill in the weapon's color | Lowpixel 0.16–0.50; Chain 0.50 gauge numerals; Marine for the chat line's SEND and the summary tabs | Score slabs fading to a sheared cut, team-tinted header bands, faction emblems, award medals; drop-shadowed text over the match (section 14.9) |
| World terminals (`guis/maps`, `monitors`, `common`, `movers`) | Per source. Common Strogg values: teal `#59AD87`, orange `#E6540F`, amber `#F0960D`, cyan `#2ECCCC`. Marine devices: `#7A9957`, `#BFE375` | R_Strogg and Strogg on Strogg devices; Marine on Marine devices | CRT surface stack (section 6); heavy shear, rotation and mirroring; `guisound_beep2` |
| Weapon displays (`guis/weapons/*_ammo`) | Machinegun and shotgun: normal `#73A640`; low `#CA8F15`, 3 Hz shimmer 80–100%; empty `#C72C1B`, 2 Hz pulse 100–50%; idle flicker 92–100% at 3 Hz. Other weapons in section 14.13 | Black Marine digits at very large size, tight tracking | Per-weapon art; five weapons have no display |
| Scopes (`guis/weapons/*_scope`) | Machinegun black marks; railgun marks `#00FFFF` at 50% and clip cells `#99FFFF`; nailgun amber `#EE8E00` | R_Strogg nailgun readouts | Circle, diamond and circle apertures, a yaw compass, a lock-on arrow, ring rotation (section 14.13) |
| Objectives (`wristcomm`, `wristcomm_strogg`) | Frames `#B0CD6B` at 0.40 on a black 0.80 bar; titles white; descriptions `#D0DEB6`; serial `#D9E7BF` at 0.25; headings `#FF8000`; failure `#FF0000`. Strogg frames `#FF9000`, markers `#FF9900`, notice headings `#FFCC00` | Marine 0.14 serial, 0.25 headings, 0.50 failure; Lowpixel 0.20 | Open plates with the cut in the foot; objective shots; entry 150 + 100 ms (section 14.11) |
| Vehicles (`guis/vehicles/hud`) | Gauges `#B3D06E` at 20% and 35%; labels and icons `#A8A360` at 60%; weapon silhouettes at 80% and 40%; charge `#FF9E1A`; warnings `#FF3300` at 60%; entry messages `#FF8000` | Lowpixel 0.14 labels; Marine 0.22 messages; Chain 0.70 rocket count | The Marine gauge art without numerals and a chamfered weapon panel (section 14.12) |
| Loading (`guis/loading`) | Bands black 80% plus additive `#181D0A`; corner brackets additive `#3A3A3A`; progress `marine.progress` | Marine 0.36 level name, 0.40 status | Stepped bands with one 45-degree riser each; four corner brackets |
| Credits and intros (`cinematic`, `gameover`, `intro`) | Text `#CCFF99` at 40%; credits `#D5FFA7`; logos `#A2C550`; credit bars additive `#FF8000` | Strogg runes paired with Lowpixel | 60 u letterbox bars; decode reveal (section 8) |

## 3. Coordinates, density and aspect expansion

Author in density-independent design pixels (`dp`). Components use local
coordinates; documents use responsive layout constraints. Imported 640x480
coordinates are source measurements, not a permanent screen-sized render
target or a reason to stretch the completed interface.

The platform supplies window coordinates, drawable pixel dimensions, content
scale and the active UI viewport separately. For layout/render coordinates in
drawable pixels, `physicalPixels = dp * displayScale * userScale`; incoming
window coordinates first map to drawable pixels using pixel density. Do not
apply content scale twice. Include the viewport origin in the transform.
Use the inverse of the actual composed transform for hit testing.

| Setting | Contract |
| --- | --- |
| UI scale | Default 100%; player-adjustable 75–200%, independently persisted |
| Text scale | Default 100%; 100–200% independently of furniture; reflow labels/rows |
| Desktop reference | 1280x720 dp composition reference; never a raster framebuffer limit |
| Portrait reference | 720x1280 dp for the 9:16 portrait view: the 640 u canvas spans the width at 1.125 dp a unit (sections 13.6 and 14.4) |
| Standard margins | 32 dp horizontally, 24 dp vertically; 16 dp in compact mode |
| Content maximum | 1440 dp for forms/tables; backgrounds and structural rails stay full bleed |
| Spacing scale | 2, 4, 8, 12, 16, 24, 32, 48 dp |
| Minimum interactive height | 36 dp desktop; 48 dp touch presentation (section 13) |
| Compact presentation | Available width below 960 dp or height below 600 dp; keep navigation, scroll bodies |
| Smallest qualification viewport | 640x480 physical at 100% scale; larger scale settings remain recoverable |

At wide aspect ratios, expand tables, allow two columns, extend framing and
separate navigation/content. Preserve stroke width, corner angles, circles
and glyph proportions. At 32:9, contain forms and body text to a comfortable
center region while imagery/framing reach the edges. At 4:3 or large text
scale, stack columns and scroll bodies. Essential actions cannot leave the
visible safe area. Section 13 adds viewing profiles for desk, couch, handheld
and Android presentation, refines the compact presentation into size classes
and defines safe-area rules for cutouts and system gestures.

HUD anchor groups expand toward the corresponding viewport edges, with a
player-selectable safe inset. Crosshairs, scopes and targeting markers remain
at the true gameplay projection center. HUD and menu scales are independent
where their ergonomic purposes differ. World-space GUIs use surface coordinates
and projected render density, not desktop physical sizing.

Monitor/DPI changes update layout, font density, vector tessellation, clipping
and cursor mapping together without reopening a menu. Resizing during a
transition preserves progress/current values. UI renders at output resolution
after world upscaling; dynamic world resolution cannot blur text or change
input targets. Clamping extreme scale settings must preserve a reachable reset.

### Stock grid

| Stock element | Source | Reference |
| --- | --- | --- |
| Canvas | 640x480 u | Central 960x720 dp |
| Navigation plate: pitch / visible plate | 30 / 24.4 u | 45 / 37 dp |
| Settings row: pitch / visible plate | 24 / 19.5 u | 36 / 29 dp |
| Action button (Back, Yes, No): rectangle / visible plate | 30 / 23.4 u | 45 / 35 dp |
| List row: standard / server browser | 20 / 18 u | 30 / 27 dp |
| Hover-card list row | 12–15 u | 18–22 dp |
| Background `+` grid pitch | 30 u | 45 dp |
| Stock hit strips: navigation / rows / Back | 26 / 20 / 23 u | 39 / 30 / 35 dp |

In every stock action list the visible plate fills about 80% of its pitch;
keep that ratio. The 36 dp minimum target equals the stock settings pitch.
Stock hit strips were separate windows shorter than the pitch; they are not a
precedent for smaller targets, and the replacement's target is the full pitch.

## 4. Color, alpha and contrast

These are sRGB authoring colors; alpha is independent. Renderer adapters must
agree on color conversion and premultiplication. Do not feed premultiplied
colors through straight-alpha blending.

| Token | sRGB hex / source value | Use |
| --- | --- | --- |
| `marine.olive` | `#8B964B` / approximately 0.545, 0.588, 0.294 | Frame rails and plates |
| `marine.marker` | `#909A49` / approximately 0.564, 0.603, 0.286 | Triangle markers |
| `marine.orange` | `#E38900` / approximately 0.890, 0.537, 0 | Active interaction and selection |
| `surface.panel` | `#171C12` / 0.09, 0.11, 0.07 | Main plate, alpha 0.88 |
| `surface.inset` | `#090C08` | Fields/list troughs, alpha 0.90 |
| `text.primary` | `#FFFFFF`, alpha 0.80 | Normal title/button/body baseline |
| `text.secondary` | `#B8C29E` | Supporting information |
| `text.tertiary` | `#8C947A` | Nonessential metadata |
| `text.disabled` | `#FFFFFF`, alpha 0.40 | Disabled labels |
| `status.warning` | `#E3AD36` | Warning plus icon/explanation |
| `status.error` | `#E46D56` | Error plus icon/explanation |
| `status.success` | `#B5C784` | Confirmation plus icon/explanation |

Status colors are openQ4 semantic additions, not claims about exact retail
samples. Validate their composited contrast in all backgrounds. Team colors
and family overrides come from gameplay/source tokens; color is never the
only distinction between teams, enabled states or errors.

`surface.panel`, `surface.inset`, `text.secondary` and `text.tertiary` are
openQ4 tokens for cards and dense data that need a solid readable backing.
Stock Marine plates are black or olive-tinted white, and stock supporting text
is white at reduced alpha.

### Measured Marine tokens

| Token | sRGB hex / source value | Use |
| --- | --- | --- |
| `marine.value` | `#FFBE23` / 1, 0.745, 0.137, alpha 0.80 | Setting values, slider ticks and thumb, key names, editable values |
| `marine.rail.dark` | `#CCCC51`, baked into dark plates | Rails of navigation plates and chat box |
| `marine.rail.card` | `#A0A040`, baked into card frames | Card and tooltip frame rail |
| `marine.rim` | `#626934`, alpha 0.37 falling linearly to 0 across 19 u, with a faint tail to 25 u | Rim light outside framing band edges |
| `marine.glow` | `#616F26` peak, applied at 70% | Additive backdrop light band |
| `marine.glow.modal` | `#46501B` peak | Additive glow field behind modals |
| `marine.list` | `#FF9C00` | List hover band 16% and selection band 41%, 50% in server lists; the row rule is white at 50% |
| `marine.list.alt` | `#9A9C6E`, alpha 0.29 | Server-browser hover band |
| `marine.check` | `#586A08` | Filled square of a set check or radio box |
| `marine.sort` | `#9BA545` | Sortable column hover highlight |
| `marine.plinth` | `#3E4A21`, alpha 0.30 | Plinth under the secondary links |
| `marine.progress` | `#E06C00`; track 30%, fill 50% | Loading and refresh progress |
| `marine.scrim` | `#000000`, alpha 0.94 | The stock modal scrim; openQ4 draws it only for the opaque-backing option and where soft focus is unavailable |
| `modal.softfocus` | A 5 u (7.5 dp) Gaussian blur and 0.80 saturation of the screen beneath; never dimmed | Modal backdrop (openQ4) |
| `text.title` | `#FFFFFF`, alpha 0.50 | Screen titles, secondary links |
| `text.heading` | `#FFFFFF`, alpha 0.40 | Section and column headings |

Olive-tinted light plates are white art multiplied by `marine.olive`; dark
plates are black art with `marine.rail.dark` rails. Both rest at 40%.

### Opacity

| Layer | Rest | Interaction |
| --- | --- | --- |
| Action and navigation plate, light or dark | 0.40 | Hover or focus: 0.80 for primary navigation and action buttons, 1.00 for section navigation and value rows; 1.00 for the current page |
| Marker | 0.40 `marine.marker` | 1.00 `marine.orange`, with the label |
| Action label | 0.80 white | 1.00 `marine.orange`; about 3% larger |
| Screen title | 0.50 white | Stable |
| Section and column headings | 0.40 white | Stable |
| Secondary link | Label 0.50, marker 0.40, no plate | Label and marker orange |
| Separators | Olive 0.26 on pages and 0.32 in cards; white 0.10 in multiplayer tables and entry dialogs, up to 0.30 in the HUD statistics panel | Never compete with labels |
| Card frame | 0.94 black body inside its rail | Stable |
| Modal frame | 0.70 black silhouette over the glow field | Stable |
| Modal backdrop | The screen beneath in `modal.softfocus`, never dimmed; the 0.94 `marine.scrim` only with the opaque-backing option | Ramps in over 200 ms, out over 250 ms |
| Framing bands | 1.00 in the front end; 0.40 in the in-game MP menu | Move; never fade |
| Table header band | 0.60 olive | Stable; no perpetual pulsing |

The stock marks a primary action by size, not opacity: START GAME is a
356x43 u light plate with a 24 dp label. openQ4's in-game partial panels (the
join card and the multiplayer menus of section 14.18) rest the primary action
at 0.62, other actions at 0.28 and the header wash at 0.16, all over the
`b6_light` and `b3_light` constructions, so the live scene stays readable; a
focused action rises to 1.00. That variant is limited to in-game partial
panels.

Node opacity applies to the completed node and its descendants as one isolated
group. Overlapping controls, text, washes and rails must retain their internal
appearance throughout a fade. Nested opacity composes inside-out; paint alpha
remains intrinsic to each primitive. Crossing opacity 1 cannot reorder siblings.
Editor previews use the same composition and clipping rules as gameplay.

### Light and composition modes

Stock menus are lit, not only painted. Measured material blends:

- **Additive** (`blend add`): the backdrop light band, the modal glow field,
  the Quake 4 wordmark, loading-band light and corner brackets, credit bars,
  and terminal scanlines, glows and reflections.
- **Darkening** (`GL_ZERO, GL_ONE_MINUS_SRC_COLOR`): the Q emblem on the home
  screen multiplies the backdrop by the inverse of its color, so it reads as a
  shadow cast in the light band.
- **Straight alpha**: plates, rails, markers, text and everything else.
- **Masked additive** (HUD): a mask pass writes a shape into destination
  alpha, and the layer is then added only inside it. The EKG, the radio
  waveform and the Strogg gauge grain use it; the low-health rim and powerup
  emblems are plain additive.

The vector renderer provides additive and darkening composition per primitive
or layer on every backend. Group opacity scales a layer's contribution without
changing its blend equation. Do not bake light into opaque color: an additive
band must brighten the imagery beneath it. Opaque accessibility backing
replaces the light only behind the text it supports.

Shaped content clipping and reveals use editable vector alpha masks in the
owning node's border-box coordinates. Masks move and scale with that node;
fixed dp chamfers retain their proportions as the frame expands. Mask coverage
applies once to the completed subtree, including text and nested panels. Curved
holes and partially transparent reveals must preserve clean edges without color
fringes. Mask RGB never changes the content color or coverage. Nested masks
compose inside-out, and an explicitly empty mask exposes no content. The editor
must display and edit the same mask geometry used by runtime rendering.

Body text must remain readable over the brightest permitted scene. Target
4.5:1 composited contrast for essential normal text and 3:1 for large text and
essential control boundaries. If stock opacity fails, strengthen the local
plate or use the existing text-backing accessibility setting. Preserve the
original look where it meets the target. High-contrast mode uses opaque local
backing and independently strengthens rails, selection and focus. The stock
40% and 50% text roles (headings, titles, secondary links) are the likeliest
failures over the additive band; measure them first.

## 5. Typography and language

Use shipped scalable equivalents of Marine for headings/actions and Lowpixel
for supporting text where available. Preserve the Marine small-cap character.
Resolve fonts from installed assets; do not copy proprietary faces into Git.
Scalable fallback faces require a compatible licence and explicit attribution.

### Faces

| Face | Character | Stock use |
| --- | --- | --- |
| Marine | Wide, extended geometric small capitals; lower case renders as small capitals | Titles, navigation, action labels, loading titles |
| Lowpixel | Bold neo-grotesque, mixed case | Row labels and values, lists, body, notices |
| Profont | Monospaced | Tabular server detail |
| Chain | Condensed square technical face; its retail space has zero advance | Marine HUD numerals |
| R_Strogg | Angular all-capital Latin, backslanted 11.4 degrees like the Strogg shear | Strogg HUD numerals and Strogg terminals |
| Strogg | Alien runes mapped case-insensitively onto Latin; backslanted 11.7 degrees, straight-edged, one full-height spine per glyph, no closed counters, punctuation mostly blank | Strogg devices before Kane is stroggified; the credits translation and the intro decode (section 8) |

Measured ink cap height per em: Marine 0.50, Lowpixel 0.75, Chain 0.63,
Profont 0.75, R_Strogg 0.67, runes 0.69. Size tokens are ems; compare faces by
cap height. New rune-like glyphs follow the rune face's construction: a stroke
of 0.18 of the cap height, horizontal cuts at stem ends, narrow glyphs 0.61 of
the cap height wide on 0.09-cap gaps, and 10–24 vertices each.

### Type ramp

| Role | Face | Size / line height (dp) | Stock `textscale` | Color |
| --- | --- | --- | --- | --- |
| Screen title | Marine | 18 / 22 | 0.25 | `text.title` |
| Primary navigation | Marine | 24 / 28 | 0.33 | `text.primary` |
| Section navigation | Marine | 22 / 26 | 0.31 | `text.primary` |
| Action button, modal title | Marine | 20 / 24 | 0.28 | `text.primary` |
| Action row | Marine | 19 / 23 | 0.26 | `text.primary` |
| Secondary link | Marine | 16 / 20 | 0.22 | `text.title` |
| Row label, value, modal body | Lowpixel | 17 / 21; wrapped 17 / 22 | 0.24 | `text.primary`; values `marine.value` |
| List row | Lowpixel | 16 / 20 in a 30 dp row | 0.22 | `text.primary` |
| Section heading | Lowpixel | 14 / 18, upper case | 0.20 | `text.heading` |
| Column heading, metadata | Lowpixel | 13 / 16 | 0.18 | `text.heading` |
| Tabular detail | Profont | 13 / 16 | 0.18 | `text.primary` |
| Loading level name, status | Marine | 26 / 30, 29 / 34 | 0.36, 0.40 | White 0.80, 1.00 |
| HUD readout | Chain (Marine), R_Strogg (Strogg) | Source-derived and recorded per group; Marine health and ammo 36 dp | 0.50 | Family readout color; stable digit alignment |

Screen titles read as paths, for example `SETTINGS - SYSTEM`. Stock line
spacing is a font cell of about 1.17 em; the ramp opens it slightly for larger
displays and long translations. 13 dp is the floor for essential text; stock
0.16 captions (11.5 dp) rise to it.

### Tracking, case and alignment

Use each face's own spacing by default; 94% of stock main-menu text does.
Modal titles and dense hover-card lines tighten by 1 u per glyph (about
-0.075 em for Marine titles, -0.1 em for small Lowpixel lines), and the
loading level name by 2 u (about -0.1 em). Express tracking in em; do not
import a fixed bitmap texel offset.

Navigation and action strings are upper case in the language tables; setting
labels and values use sentence case; section headings are upper-case Lowpixel.
Marine renders lower case as small capitals, so mixed-case translations stay
consistent.

Stock labels lead (99% of main-menu text). Numerals align to the trailing
edge of fixed boxes. Centered text is reserved for overlay notices, the HUD
weapon name, tab labels and cinematic credits. Never center an action label on
a plate. Text over gameplay or imagery takes the stock drop shadow: a black copy
1.5 dp toward the trailing bottom at the text's own alpha.

Baselines and cap height determine label/icon alignment. Rasterize glyphs for
their output size; snapping cannot destroy kerning. Scale animations use
sufficient atlas density for their largest size.

All display strings use the language system, including editor menus, tooltips,
validation and accessibility labels. Preserve `#str_*` keys, format arguments,
inline icons and color escapes. Support UTF-8 decoding, glyph fallback,
composition/IME and text selection. Integrate and test shaping/bidirectional
support before claiming languages that require them. Do not invent translations
or use silent English fallback as evidence of completed localization.

Test long existing translations and a 40% expansion fixture. Wrap text, grow
rows and prioritize labels over decoration. Truncate only nonessential list
metadata and expose its full value through detail/tooltip. Never truncate an
action verb, confirmation choice or essential error instruction.

## 6. Framing and vector geometry

Draw paths, polygons, strokes, gradients and masks through the engine vector
UI renderer. Static axis-aligned rails snap to physical pixel centers; moving
geometry remains continuous. Diagonals/curves need coverage antialiasing at all
scales. Stroke width uses dp with a one-physical-pixel lower bound for essential
rails. Tessellation error is measured after transforms in output pixels.

### Stock furniture vocabulary

Every element below is reconstructed as editable vector geometry from the
measured construction. Appendix B holds the texel measurements.

Plates and markers:

| Element (stock art) | Measured construction |
| --- | --- |
| Light plate (`b3_light`, `b4_light`, `b6_light`) | Visible band 25/32 of the rectangle height; lower-leading 45-degree cut through 56% of the band; one-texel rails on the leading edge, cut and bottom; fill 49%; fill and bottom rail full to 33–50% of the width, then linear to zero at the trailing end; white art tinted `marine.olive` |
| Dark plate (`b1_dark`, `b2_dark`, `b5_dark`) | Visible band 30/64 of the rectangle height; cut through 47% of the band; opaque black fill to 40–57% of the width, zero at 78–94%; rails in `marine.rail.dark` |
| Header band (`header`, `scoreheader`) | Upper-leading cut of 8 texels, drawn about 9 u on the browser header and 7 u on team headers; top and leading rails; fill 49%, full to half the width, half at 75% and 10% at 95%; olive at 0.60 in menus; on scoreboards the team colors, `#8B964B` for deathmatch and Tourney and `#999999` for spectators, all at 1.00 |
| Link plinth (`bg2`) | Solid band with a lower-leading cut and a trailing end cut at 45 degrees, parallel to it; `marine.plinth` |
| Marker (`corner`) | Right triangle filling the upper-trailing half of its square (◥), legs half the tile, halo about 20% of the tile beyond the solid; mirrored vertically (◢) as the sort marker |

Frames and bands:

| Element (stock art) | Measured construction |
| --- | --- |
| Card frame (`tooltip_edge`, `tooltip_mid`) | Black body at 0.94; 1 u `marine.rail.card` rail on every edge; 2 u top-leading cut and 8 u top-trailing cut; the bottom cap is the top cap rotated 180 degrees; the art sits 2 u inside its window, and a 128-texel variant keeps the 8 u cut on small cards |
| Modal frame (`popup_top`, `popup_mid`, `popup_btm`, `popup_bg`) | Black silhouette at 0.70 over an additive glow column; leading tooth, recessed title slot and raised trailing section along the top; deep lower-leading chamfer; square trailing corners; no rails. The glow column is exactly the dialog's width, uniform across it and graded only vertically (half at 28% and 72% of its height), reaching about 87 u above and 108 u below the standard frame, so a 6 dp lit margin and the lit slot outline the silhouette |
| Tab strip (`ctrls_tab*`, `admin_tab*`, `summ_tab*`) | Baseline rail, broken under the active tab; the active tab rises about 22 u with a small leading flare and a 12 u 45-degree trailing shoulder; fill `#5A652A` from 1.00 at the top to 0.60 at the baseline, then a wash across the whole strip fading to zero over one tab height; centered Marine labels; each texture bakes one active position, widths fitted to the labels |
| Chat box (`chatbox_edge`, `chatbox_mid`) | Rails in `marine.rail.dark` along the top, bottom and leading edges with small chamfers at both leading corners; the top and bottom rails dissolve toward the open trailing side (full to 40%, half at 65%); black fill 0.49; the whole box at 0.40 |
| Framing bands (`topbar`, `btmbar`) | Opaque black silhouettes (Appendix B.2) with `marine.rim` light |
| Loading edges (`load_top_edge`, `load_btm_edge`, `*_edgeadd`, `load_corner`) | Black 0.80 bands with one 45-degree riser each and additive olive light (full at the edge, half at 41 u, gone by 80 u); corner brackets in 52 u windows with about 24 u legs, a 0.8 u white core and a warm 7 u halo, additive at `#3A3A3A` |
| Separators (`horiz_line2`, `vert_line`, `vert_line2`) | A two-texel line, about 2 dp, centered in a 5–6 u strip; `vert_line` fades over its last 6% at each end, the others end square |

Fields, lists and backdrop:

| Element (stock art) | Measured construction |
| --- | --- |
| Slider (`slider_bg`, `slider_bar`) | Tick ramp: alternating tall and short ticks rising from 2 to 9 u across the 71 u range above a baseline; 4x8 u bar thumb with a bright border and 40% interior; `marine.value` |
| Check or radio box (`box`, `box_check`) | Square outline, stroke 1/16 of the box (18 dp box in stock); set state is an inset filled square 69% of the box in `marine.check` |
| Scrollbar (`scrollbarv`, `scrollbar_thumb`) | 16 u gutter with no arrow buttons (the stock loads arrow art but never draws it); a fixed 16 u thumb tile with top, bottom and trailing rails and an open leading side, tinted by the list's text color and white while hovered; a trough of one light line with a faint fill in the list's color. The server browser adds its own `arrow6` buttons on 22x16 u tiles above and below the gutter |
| List bands (`bg_hover`, `bg_focus`, `bg_line`, `*2`) | One band per row, in priority selected, greyed, hovered, then the resting line, drawn 3 u below the row top; hovered text turns white. Menu lists: `marine.list` at 16% hover and 41% selection with soft ends and a white 50% hairline under every row. Server and multiplayer lists (`*2`): no hairline; a faint `#9A9C6E` line band at 8%, hover `#9A9C6E` at 29%, selection `marine.list` at 50%, greyed `#787E87` at 41%, ends fading over 22% |
| Progress (`load_bar`, `load_bar3`) | Cylindrical shading with a bright center line; `load_bar` carries a lower-leading cut; `marine.progress` |
| Backdrop (`screen`, `bg_darkgrad*`, `bg_grid`, `static1`) | Additive light band peaking at half its height; vertical vignette from 97% black at top and bottom to clear at the middle; `+` grid of 20 u one-texel crosses; page backing clear across its leading 19%, full from about 35%, applied at 60% |

### Plate and button anatomy

Action plates are open shapes. Rails run along the leading edge, around the
lower-leading cut and along the bottom; there is no top or trailing rail, and
the fill and bottom rail dissolve toward the trailing end, so a plate never
reads as a closed box. Rails are 1.5 dp at the reference, never thinner than
one physical pixel.

| Measure | Navigation plate | Action button | Settings row |
| --- | --- | --- | --- |
| Construction | Dark plate | Light plate | Light plate |
| Visible plate / target | 37 / 45 dp | 35 / 45 dp | 29 / 36 dp |
| Lower-leading cut | 17 dp (47%) | 20 dp (56%) | 16 dp (56%) |
| Marker | 9 dp | 8 dp | None on value rows; 9 dp on action rows |
| Label | Marine 24 or 22 dp after the marker | Marine 20 dp after the marker | Lowpixel 17 dp, 21 dp in; value column at 64% of the row; action rows Marine 19 dp after the marker |

Minimum width is 112 dp. The marker's legs sit inside the label's cap band,
its visible leading edge 12–14 dp after the plate's leading rail; the label
follows 4 dp after the marker, with at least 16 dp before the fade region. A
45-degree cut always has equal horizontal and vertical extent after layout.
Stock bitmaps skew it whenever a plate is stretched; reconstruct the 45 degrees.
Plate, marker and label form one semantic control and one target. Widen the
straight span without deforming the cut.

At rest the plate is 0.40, the marker olive 0.40 and the label white 0.80.
Hover raises the plate (section 4) and turns label and marker orange, and the
label grows about 3% (stock +0.01 `textscale`) through a local transform that
never reflows neighbors. The current page keeps the same treatment with its
plate at 1.00. Value rows brighten only their plate; their label and value
keep their colors. Focus adds a fine orange inset rail and solid marker,
which keeps keyboard focus distinct from the current page; the stock drew both
identically. Press briefly emphasizes the inner edge and moves contents 1 dp
inward; release restores current focus/hover state. Movement never changes
layout or the hit target.

### Panel anatomy

Choose the panel construction by purpose, not size.

**Card** (hover cards, tooltips, in-game partial panels such as the
multiplayer menus of section 14.18):
a black body at 0.94 inside a single `marine.rail.card` rail; 12 dp 45-degree
cuts at the top-trailing and bottom-leading corners and 3 dp cuts at the
top-leading and bottom-trailing corners. Small popovers keep this construction
with the same cuts. Content inset is 24 dp, 16 dp in compact mode and
10 dp on hover cards (stock 6–7 u). A panel header is 40 dp, with a 1 dp
rule below and an 8 dp triangle before its title; the header wash fades
toward the trailing edge. Body height follows content; scroll regions clip
inside the rail. Footer actions have 16 dp separation and align to the
action edge. Small cards keep the 12 dp major cut; only one-line hints shrink
it, to 10 dp.

**Modal** (front-end confirmations, entry, list and advanced-settings dialogs):
the screen beneath stays in view, in `modal.softfocus`, where the stock covered
it with the `marine.scrim`. The dialog is a black 0.70 silhouette inset in its
rectangle, as the stock art is (`popup_top` from row 4 within columns 7 to 504
of 512): 7/512 of the width in from each side, 6.6 dp at 480 dp, with 3.6 dp
above the raised sections and 5.3 dp below the bottom edge. The widths below are
the rectangles'. An additive glow peaking in `marine.glow.modal` stands behind
the dialog. Across the rectangle the glow is graded only vertically: half
strength at its top and bottom edges, gone 40 dp beyond them. The rectangle's
inset around the silhouette is the lit margin. Past the rectangle the glow fades
out over a further 16 dp at each side. The lit slot and margin outline the
silhouette, and the light spreads softly past the dialog instead of standing as
a hard-edged strip across the softened screen; over the scrim it keeps the stock
column, exactly the rectangle's width, where the inset alone is the margin.
Along the silhouette's top edge a 6 dp leading tooth, a title slot opening
across about 73% of the width with 45-degree flanks down to a floor about 66%
wide, and a raised trailing section; a deep lower-leading chamfer; square
trailing corners; no rail lines, because the glow outlines the silhouette. The
title (Marine 20 dp, tracking -0.075 em) starts at the foot of the slot's
leading flank, and its capitals hang from the raised sections' top line into the
lit slot, above the empty floor: the baseline sits about 6 dp above the floor,
13.5 dp below the rectangle's top in the confirmation. A thin black outline at
0.85 extrudes 1 dp around the title's strokes, drawn behind its fill, so the
capitals keep a crisp edge where the glow is brightest. Body text is inset
33 dp, its first baseline 87.5 dp below the rectangle's top. Actions sit 30 dp
from the leading edge and 33 dp from the trailing edge, about 16 dp above the
bottom, the affirmative action leading. Stock dialogs come in five widths, and
the art stretches with them:

| Dialog | Width | Slot depth | Chamfer |
| --- | --- | --- | --- |
| Confirmation, entry and list (136 u tall at y 155) | 480 dp | 17 dp | 38 dp |
| System message, connection and death menu | 540 dp | 20 dp | 46 dp |
| Ban list | 548 dp | 22 dp | 50 dp |
| Advanced video, audio and server options | 612–642 dp | 25 dp | 56 dp |

Titles are upper case, a noun or a question (EXIT GAME, OVERWRITE SAVE?);
bodies are sentence case; actions are short verbs (YES and NO, CONNECT and
CANCEL, OK, CLOSE).

**Black frame** (version 1.38, the product owner's rule): every pop-up carries
a neat, thin and elegant opaque black frame, drawn on top of the glow without
hiding it.

- **Modal frames** (the modal, system message and countdown frames under
  Pop-up frames below) trace the silhouette's outline in black at 1.00, 3 dp
  wide in the Marine family; in the Strogg family it is 4.5 dp, beneath the
  rail, so the rail keeps its color with a black edge either side.
- **The title's tab.** The frame fills the title slot in opaque black from the
  raised top line, behind the title, and steps down to the slot's floor just
  past the title's end at 45 degrees (the Strogg family's 30), so the title
  stands on its own background. The tab's run follows the running language's
  title plus 10 dp, capped where the slot's trailing flank begins; a title
  that is not a localization key takes the whole slot. The tab's top and step
  carry the frame's edge (and, in the Strogg family, its rail).
- **Plate dialogs and dropdown lists** (the DISPLAY page's dialogs, every
  dropdown list) take a 4 dp black stroke beneath their body, so 2 dp of black
  shows outside their own outline or border.

It adds to each construction: the silhouette, the glow, the lit margin, the
slot's floor and trailing flank, and each plate's and list's own outline are
unchanged, and the glow still shows around the frame.

**Band**: the framing bands and loading edges below.

Card, with minor cuts at the top-leading and bottom-trailing corners and major
cuts at the other two (the outline is rotationally symmetric):

```text
  ______________________
 /                      \
|                        \
|                         \
|                          |
 \                         |
  \                        |
   \______________________/
```

Modal silhouette, with the title set in the recessed slot and no rails:

```text
 _   LOAD DEFAULTS      _______
| \____________________/       |
|                              |
|                              |
 \                             |
  \                            |
   \___________________________|
```

### Pop-up frames

Every transient surface uses one of these constructions. Stock rows restate
measured constructions; openQ4 rows reuse stock parts.

| Frame | Construction | Placement | Motion | Dismissal |
| --- | --- | --- | --- | --- |
| Modal (stock) | Modal silhouette and its glow over the soft-focused screen | Centered horizontally, 17 u above center; one of the five stock widths; the body scrolls beyond 70% of the safe height | `modal.enter`, `modal.leave` | An explicit choice, or Back as the negative action; never an outside click |
| Card (stock) | Card | Anchored to its invoker, or docked as the detail area | `content.in`, `content.out` | Focus leaves the invoker, or Back |
| Hover card (stock) | Card with a 10 dp inset | 6 dp from the pointer, moving to its leading side near the trailing edge | Appears at once | The pointer leaves |
| Tooltip | Small popover | 12 dp after and 15 dp below the pointer | `tooltip` after 300 ms | The pointer leaves, or any input |
| Statistic and award cards (stock) | Card, 128 or 256 u wide | 6 dp below and after the pointer; in game, award cards move to its leading side | After 500 ms, fading to 0.94 over 250 ms | The pointer leaves |
| System message (stock) | Modal, 540 dp wide | Centered | None: it appears and closes at once | An explicit choice |
| Dropdown list | The stock kick-player list rebuilt in the popover construction: as wide as the value column, eight rows, then it scrolls | Unfolds downward from its value, or upward when the space below is short; never over its own row | `list.unfold`; a pick closes it at once | A selection, Back or an outside click, which goes no further |
| Action popover (openQ4) | Small popover with a glyph on every row | Anchored to the invoking prompt or row | `popover.enter`, `popover.leave` | A selection or Back |
| Notice (openQ4) | Header-band plate (section 13.14) | The top band's trailing section | `notice.enter`, `notice.leave` | Timeout or resolution |
| Countdown (openQ4) | Modal with a tick ramp | As the modal | As the modal | Keep, Revert or timeout |

**Placement.** A frame stays 16 dp inside the safe area. It flips to the other
side of its anchor before it clips, and it never covers the crosshair, the
prompt bar or the touch controls. A frame opened from the keyboard or a
controller anchors to the focused control; one opened by the pointer anchors
to the pointer.

**Layering.** From bottom to top: HUD, in-game partial panels, menu screens,
cards and popovers, modals, notices, and the pointer. One modal shows at a
time; a second request waits until the first closes. No frame blacks out
what it covers: a modal soft-focuses everything beneath it, whether a menu
screen, the view or a panel over the view, and cards, popovers and tooltips
cover only their own area.

**Focus.** A modal contains focus and returns it to its invoker. A popover
opened from the keyboard or a controller takes focus and returns it on close.
Hover cards and tooltips never take focus, and the detail area replaces them
for controller and touch players.

### Framing bands

The home and page states use the same two band paths in different positions
(Appendix B.2). The top band's inner edge runs straight, rises through three
9 u notches with 45-degree flanks (36 u across the top, 97 u pitch), and
steps up by 35 u at a 45-degree riser. The bottom band's inner edge rises
30 u, later drops 43 u, and carries two 9 u steps near its ends, all at
45 degrees. `marine.rim` light falls from 37% to zero across 19 u outside
every inner edge, measured by distance from the edge so flanks, risers and
steps glow as evenly as the flats. The light is part of the band's own art in
straight alpha, not an additive layer; only the loading-screen edges carry a
separate additive light.

Keep notches, risers and steps as named path segments so expansion lengthens
only the straight spans. On wide displays the flat spans extend to the
viewport edges while notches, risers and steps keep their stock positions
relative to the 4:3 content region. A generic beveled rectangle is not a
substitute for these silhouettes. The stock art already reaches 400 u beyond
the canvas's leading edge; openQ4's bitmap menus repeat edge tiles, which the
vector bands replace.

### HUD and diegetic geometry

The 45-degree motif belongs to the Marine menus. Other families keep their
own measured geometry:

- **Marine HUD gauges** (125x59 u): a backing body at black 0.50, 123x40 u
  with 1–1.4 u corner radii, and a foot 6.9 u deep under its trailing end, 32%
  of the body wide at the top and 25% at the bottom, reached by a 45-degree
  flank. The fill plate is inset 4.4 u at the sides and 4.6 u at top and bottom
  and bevelled in its own tint (58% brightness inside, 97% at every edge over
  about 7 u). Its lower-trailing corner is notched: a ledge at a third of the
  fill height runs 14.2 u in from the trailing edge, then a concave arc of
  radius 16.6 u meets the bottom edge 26 u from the trailing edge. The item icon
  sits in the notch over the foot. The armor fill is 19% brighter and divided
  into ten cells by nine soft dividers, 1.5 u wide at 61% alpha on an 11.7 u
  pitch. The health gauge masks the EKG. Plates and numerals are sheared by
  0.22, about 12 degrees, so their tops lean toward the trailing edge.
- **Strogg HUD gauges** (129x67 u): a raised tab at the top-leading area and a
  30-degree leading shoulder, no foot; the fill steps down its leading edge in
  30–33-degree diagonals and is cut away below its lower-trailing third. They are
  sheared the opposite way, with unsheared R_Strogg numerals and a masked grain
  layer over each gauge.
- **Weapon-select strip**: 24.4 u rounded plates on a 30 u pitch, centered, each
  sheared about its own center, with upright 18 u icons and the weapon name
  centered below (section 14.5).
- **World terminals**: a layered CRT surface of content, additive scanlines
  drifting 0.2 texture heights per second, dirt and scratches, static, a bezel
  vignette with a thin bright edge, and an additive glass reflection.

Shear and rotation pivot on each element's own rectangle center in virtual
space. An element's transform replaces its container's instead of composing
with it, and clipping applies to the unsheared rectangle first. A sheared gauge
therefore stays in place: its top edge moves 6.5 u toward the trailing side and
its bottom edge 6.5 u toward the leading side. Reproduce the drawn geometry and
verify it against captures instead of re-deriving it from authored rectangles.

### Icons and paths

Author simple symbols as editable paths: triangle, chevrons, close, back,
settings gear, checkbox/tick, radio, lock, favorite, dedicated server,
spectator, repeater, sorting, warning/error, keyboard/controller prompts and
the original HUD's simple instrument symbols. Use an optical 24x24 dp master
with 16/20/32 dp placements; 1.5 dp nominal stroke with deliberate caps/joins.
Use filled silhouettes where strokes cannot preserve the original symbol.

Each icon has a semantic name, bounds, anchor, source-reference description and
state variations. Do not replace game symbols with font glyphs or emoji.
Vector source stays editable; compiled geometry is derived. Preserve holes,
cutouts, gradients and important asymmetry. The editor exposes path nodes,
handles, joins, stroke alignment, fills and gradient stops.

| Symbol (stock art) | Stock form | Meaning |
| --- | --- | --- |
| Marker (`corner`) | ◥ with halo | Action and section marker; ◢ sort direction |
| Arrow (`common/arrow6`) | Filled downward triangle | Spinner arrows when rotated 90 degrees on dark squares |
| Chevrons (`common/arrow1`, `arrow5`) | Thick single chevron; double-stroke outline chevron | Back and next; expand |
| Go (`icon_arrow`) | Shaft and head with bevel shading | Leave, exit, go |
| Create (`createserver_arrow`) | Arrow entering a bracket | Create server, launch |
| Gear (`common/gear1`, `gear1_still`) | Toothed ring | Settings; the animated variant turns once per 10 s |
| Favorite (`icon_favorite`) | Gold star with a red rim glow | Server list |
| Locked (`icon_locked`) | Shaded padlock | Password required |
| Dedicated (`icon_dedserver`) | Server case with a green power light | Dedicated server |
| Repeater (`icon_repeater`) | Television with antennas and color bars | Q4TV repeater |
| PunkBuster (`icon_pb`) | Service mark | Discontinued service; no replacement column required |
| Faction marks (`marinelogo`, `strogglogo`) | Hexagonal spearhead; winged lightning emblem | Teams |
| Quake emblem (`q4logo`) | Flat Q emblem | Home-screen watermark (darkening) |
| Pointer (`guicursor_arrow`, `guicursor_hand`) | Shaded arrow; hand | Pointer states |
| HUD and items (`gfx/guis/hud/icons`) | Flat glyphs, shaded gray weapon renders and colored powerup emblems | Health, armor, ammunition, weapons, powerups, flags and status; `simpleicons` are world pickup sprites. All are vector reconstructions (section 14.16) |

Server-list symbols sit on a small gray tile with a cut upper-leading corner;
the tile is part of the symbol. Multi-color symbols keep their colors. The
weapon and item color code (Appendix B.4) is gameplay information.

### Bitmap exceptions

Only complex pictorial content uses bitmaps/video: levelshots, scene backdrops,
portraits, photographic/painted imagery, cinematics and detailed illustrations
whose character depends on textured content. Resolve installed assets and
sample at appropriate quality. Logos/diagrams require individual classification;
complexity, not convenience, justifies an exception. Simple logo outlines,
frames, reticles and checkboxes require vector work.

| Stock content | Classification |
| --- | --- |
| Levelshots, backdrop images, save previews, objective shots | Bitmap exception |
| MP award medals; publisher logos (id, Raven, Activision, Miles) | Bitmap exception |
| Multiplayer symbols: flags, stopwatch, infinity, ready, speaker, friend, skull, swirl, Quake emblem | Vector |
| Weapon renders (`gun_*`), powerup emblems, crosshairs and context cursors | Vector (sections 14.15 and 14.16), by the product owner's decision; the use hand and the medic and tech badges included |
| Quake 4 wordmark (`q4text`) | Vector: flat letterforms traced from the texture's white core. The orange halo becomes vector light: the letters dilated 3.7 u, blurred with a 4.4 u sigma and added in #FF8F40 at 0.40 (the initializing screen's halo is dimmer, section 14.17) |
| Initializing picture (`gfx/splashScreen`) | Vector, rebuilt as separate layers (section 14.17) |
| Quake emblem, faction marks, simple HUD silhouettes | Vector |
| CRT dirt, scratches, glass reflections | Bitmap exception |
| Static grain | Procedural noise; a bitmap only where the look depends on the stock grain |
| Light bands, vignettes, page backing, list bands, progress shading | Vector gradients |
| Scanlines, reticle grid, tick ramps and waveform traces | Vector or procedural patterns |
| HUD EKG | Installed `ekg`, `s_ekg`, `s_ekg2` bitmaps; approved performance exception, 11 October 2026 |
| Frames, plates, markers, boxes, arrows, scrollbars, tabs | Vector |

Record each exception with purpose and source in the migration inventory.
Generated glyph coverage atlases are a text-rendering detail, not permission
to reuse low-resolution bitmap type. Native paths must not become a fixed-size
atlas. If an SVG feature is unsupported, diagnose it; do not silently rasterize
furniture to hide the limitation.

### Detailing

Detail decides whether a reconstruction reads as authored or traced. These
rules apply to every family and every size class.

- **Rails and joins.** A rail is one continuous stroke through every cut,
  mitered at 45 degrees, with no gap and no brightening where segments meet.
  Rails are 1.5 dp and never thinner than one physical pixel; static
  axis-aligned rails snap to pixel centers.
- **Cuts.** Use the measured cut ladder: 2 and 3 dp minor cuts, 10 dp on
  one-line hints, 12 dp card cuts, about 14 dp on header bands, 17 and 20 dp
  plate cuts, and the modal chamfer, 38 dp on the standard dialog and up to
  56 dp on the widest. A cut keeps equal horizontal and vertical extent at
  every size and never falls below 3 dp on an essential control.
- **Fades.** Fill and rail fades are linear in straight alpha across the stated
  span. On 8-bit targets, dither any fade wider than 128 physical pixels so it
  never bands.
- **Markers.** A marker sits inside its label's cap band; its halo reaches 20%
  of the tile beyond the solid and never touches a rail.
- **Separators.** A two-texel line, about 2 dp, centered in a 5–6 u strip:
  olive at 0.26 on pages and 0.32 in cards, white at 0.10 in multiplayer tables.
  In a card a rule starts 8 dp inside the leading rail and stops about 25 dp
  short of the trailing rail, and a divider stops 6 dp short of the rule and the
  bottom rail. A separator never touches a rail.
- **Locked and unavailable.** Unavailable content rests at 0.40 and states its
  reason in the detail area. Locked content adds the lock symbol and a
  45-degree hatch of 1 dp lines on an 8 dp pitch at 0.10 over its plate. Hatch
  never marks an enabled control.
- **Numbers and units.** Use tabular figures wherever digits change or align.
  A space separates a value from its unit (`144 Hz`, `16 ms`, `1.2 GB`), except
  for percentages and multipliers (`125%`, `4x`). Resolutions use the
  multiplication sign with spaces (`2560 × 1440`), ranges an en dash
  (`75–200%`) and durations `m:ss`. Decimal and grouping separators follow the
  language.
- **Text fitting.** Labels wrap before they shrink. Metadata may truncate with
  an ellipsis when the detail area shows the full value; an action never
  truncates.
- **Light.** Glows are additive, capped at the family's light peak, and never
  stack where controls overlap; focus light and rim light share one light
  layer per surface.
- **Texture.** Grain, scanlines and the reticle grid are screen-space layers
  below text. They never move with content, and depth shift (section 13.8)
  moves them only as whole layers. The reticle grid, grain and light band
  belong to menu backdrops: the HUD, in-game overlays and world displays never
  draw them, and neither do previews of those surfaces.
- **Text shadow.** Text over gameplay or imagery carries the stock drop shadow
  (section 5); text on plates and cards never does.
- **Headings over lists.** A light plate flipped vertically, its cut at the
  upper-leading corner and its rail along the top, heads a list or a group.

Stock details that openQ4 reuses:

| Detail | Construction | Use |
| --- | --- | --- |
| Dot matrix | Ten by two squares of 4 u on a 9 u pitch, `#1C2613` | Loading screens, top-leading corner |
| Corner brackets | L legs of about 24 u with a white core and a warm halo, additive | Loading picture corners |
| Credit bars | Additive `#FF8000` ramps brightening toward the name | Credits and game over |
| Level meter | Amber track at 0.40 and a full-strength bar squeezed to the level | Microphone level |
| Soft backing | A black slab with feathered sides and rounded corners at 0.60–0.90 | Scoreboard, summary, statistics |
| Name strip | A black gradient under a trailing-aligned title | Loading level name |
| Swatch | A small square with a chipped corner | Color choices, spinner tiles |
| Picture frame | A 1 u `#414624` border over black 0.60 | Save, server and map previews |

## 7. Components and interaction states

Controls declare default, hover, keyboard/gamepad focus, pressed, selected/
checked, disabled, busy and error states where applicable. Precedence is
disabled > error/busy constraints > pressed > focus > hover > selected >
default. Selection remains visible under focus; errors remain visible while
editing. Hidden controls receive no input.

Accept activates on a matched release. Multiple physical inputs mapped to one
action hold one logical press; releasing one source cannot activate while
another remains held. Navigation moves immediately, repeats after 320 ms, then
every 110 ms, independently of OS key repeat. Accept and Back do not repeat.
Focus loss, device removal, replacement and modal changes cancel pending activation. Controls
held across a menu/gameplay handoff must be released (or an axis returned to
neutral) before the next owner acts on them. A stall must not replay a burst of
navigation steps.

| Component | Visual and behavior contract |
| --- | --- |
| Action | Cut plate, marker, clear verb; one activation per action; disabled reason where useful |
| Checkbox | Square outline and inset filled square (the stock mark); shared label/box target; mixed state is an inset bar |
| Radio | The same square box; a set option fills its box; one selected, directional group navigation |
| Slider | Tick-ramp track, bar thumb, numeric value in `marine.value`; openQ4 drops ticks past the thumb to 40% to show the filled span; keyboard increments and precise entry |
| Choice/dropdown | Value in the value column that cycles in place; openQ4 dropdowns add a chevron and unfold, like the stock kick-player list, into a constrained scrollable popup; selected value visible |
| Text/numeric field | Recessed plate, caret/selection/composition; errors preserve edit and explain correction |
| Key binding | Keycap/controller glyph; distinct capture state; cancel and explicit conflict resolution |
| Tabs | Shared rail, active notch/marker; inactive pages receive no input; focus differs from selection |
| List/table | Stable headers, growing rows, sort indicator, selected rail, designed empty/loading/error states |
| Scrollbar | Narrow trough, cut usable thumb; paging, wheel and touch; no overlap with ornaments |
| Gauge/progress | Source-derived segments/path; truthful values; bounded quiet indeterminate sweep |
| Tooltip | Compact cut plate; 300 ms delay, immediate replacement within a group; avoid cursor/content |
| Modal | Strong title/body/action hierarchy; contained focus, restoration and no click-through |
| Status/toast | Contextual placement; no aim/critical-HUD obstruction; unresolved errors persist |
| Tree | Disclosure markers, selection and guides; preserve expansion/focus while updating |
| Path/color editor | Direct manipulation plus precise numbers; undoable edits and textual properties |

Server tables retain sorting, filtering, favorites, compatibility and selection
during refresh. Chat retains history, channels, caret movement and scroll
ownership; new messages cannot steal inspection position. Scoreboards identify
teams/spectators/local player with text or geometry as well as color; the stock marked the local player by brightness alone. Empty,
offline, disconnected and failed states cannot be unstyled debug output.
Section 13 defines each component's controller and touch behavior, the
prompt bar and the detail area.

### Stock control conventions

| Control | Stock construction |
| --- | --- |
| Setting row | Light plate; Lowpixel label 21 dp in; value in `marine.value` at 64% of the row |
| Boolean or enumeration | A value that cycles on activation and steps back on the secondary action; no arrows, no checkbox |
| Filter spinner | Value between ◀ and ▶ arrows (`arrow6`, orange 0.80) on 22x16 u black 0.40 tiles; hover turns an arrow `marine.value` at once, leaving returns it over 300 ms, and a press flashes white fading to yellow over 300 ms |
| Exclusive pair (Internet/LAN, Player/Clan) | Square boxes; the set option fills its box |
| Slider | Tick ramp at the value column, numeric value after it at 85% of the row |
| Text field | Entry fields in dialogs sit on a light plate without an outline, Lowpixel white 0.80 (`marine.value` for passwords); the save name and the in-game chat line use dark plates |
| Key binding | Key names in `marine.value` in the value column |
| List | Lowpixel column headings at 40% (30% on the controls page) with their baseline 7 u above an olive rule; 30 dp rows with `marine.list` bands and a rule under every row; multiplayer tables adapt their pitch to the entries (section 14.14), use drop-shadowed text and server-list bands, and have no hover band on the scoreboard |
| Dropdown | The only stock dropdown, the kick-player list, unfolds under the value column over 150 ms: a black body with a 1 u `#B2CD43` border at 20%, 14 u rows and server-list bands; a pick closes it at once |
| Disabled | Action plates and markers gray at 0.40 with labels at 0.40–0.50; value rows at 0.40; entries a server forbids at 0.20 |
| Server list | Olive 0.60 header band holding 28 dp column symbols; 27 dp rows; sortable columns highlight in `marine.sort`; flipped marker beside the sort notice; progress bar under the list |
| Hover card | Card frame at 0.94 that appears at once and follows the pointer 6 dp away; once the pointer passes 555 dp from the canvas's leading edge the card moves to its leading side, its trailing edge 28 dp past the pointer; Lowpixel 14 dp and Profont 13 dp content |
| Tooltip | Stock sort hints sit 12 dp right of and 15 dp below the pointer, appear after 1000 ms and fade in over 250 ms; multiplayer statistic and award cards appear after 500 ms and fade to 0.94 over 250 ms; the replacement shortens the delay (table above) |

Stock hit targets were invisible windows separate from the art. The replacement
makes each control one target spanning its full pitch. The stock drew the
current page's navigation plate exactly like hover; keep that for selection and
add the focus rail for keyboard and gamepad focus.

## 8. Motion, transitions and sound

Use a continuous monotonic presentation clock sampled each rendered frame.
Motion continues while gameplay is paused and cannot inherit simulation tick
rate. Game-state events retain authoritative time. Keys specify time, property,
value and easing; interpolate continuously at 144/240 Hz as well as 60 Hz.

Stock transitions take optional acceleration and deceleration arguments as
fractions of the duration, and the engine rescales them in proportion when they
sum past it. Every stock use does, so no stock transition has a constant-speed
phase. `accel(a, d)` below means constant acceleration for `a` ms, then constant
deceleration for `d` ms, with `a + d` the duration: stock `"500" "150" "150"`
is `accel(250, 250)` and peaks at twice the mean speed, and `"300" "100"
"150"` is `accel(120, 180)`. Unmarked stock transitions are linear. The stock
sampled motion at most once per 16 ms game frame; openQ4 samples every
presented frame.

| Motion token | Timing | Curve / use |
| --- | --- | --- |
| `hover.enter` | 0 ms | Plate, label and marker respond at once (stock) |
| `hover.leave` | 300 ms | Linear return (stock) |
| `press` | 60 ms | Fast ease-out, 1 dp inset movement (openQ4) |
| `focus` | 80 ms | Ease-out rail/marker; never delay navigation (openQ4) |
| `frame.dock` | 500 ms | `accel(250, 250)`; framing bands move between home and page positions, 50 ms after activation toward a page and at once on Back (stock) |
| `screen.depart` | 500 ms | `accel(250, 250)`; the home layer sweeps 640 u toward the trailing edge from 50 ms (stock) |
| `screen.return` | 300 ms | Linear; a page sweeps 640 u toward the leading edge on Back, carrying its plates (stock) |
| `content.out` | 250 ms | Linear fade of departing home plates, labels, markers and message of the day; secondary links and the plinth take 50 ms; a departing page drops its title at once, its Back action over 100 ms and its backing over 250 ms (stock) |
| `content.in` | 150 ms | Linear fade of the arriving title, backing, Back action and home content; a page's own plates appear at rest at once (stock) |
| `menu.fade` | 250 ms | Linear fade from black as the menu opens, and to black as the in-game menu closes (stock) |
| `band.slide` | 150 ms | Linear; in-game multiplayer and buy menu bands slide in from off-canvas, and out after a 50 ms hold (stock) |
| `row.flash` | 250 ms | Linear; row plates settle from 0.60 to rest when a Controls tab changes (stock) |
| `page.enter` | 220 ms | Cubic (0.16, 1, 0.3, 1); 12 dp slide and opacity, between sibling pages of one screen (openQ4) |
| `page.leave` | 140 ms | Cubic (0.4, 0, 1, 1); 8 dp departure and opacity (openQ4) |
| `modal.enter` | 200 ms | The soft focus comes in, the glow brightens from black and the frame rises to 0.70; title, body and actions appear together at 200 ms, because their stock fades ran while hidden; no scale (stock) |
| `modal.leave` | 50 + 250 ms | Contents hide at once; after 50 ms the soft focus and glow release over 250 ms and the frame over 200 ms (Exit, Load Defaults, Delete, Disconnect and Overwrite) or 250 ms (other dialogs) (stock) |
| `light.up` | 150 ms in, 250 ms out | Logos and glow fields fade from and to black rather than transparency (stock) |
| `row.reveal` | 120 ms | Opacity, optional 15 ms stagger capped at 90 ms total (openQ4) |
| `value.change` | 100 ms | Local emphasis; no perpetual blinking |
| `tooltip` | 100 ms | Opacity after initial delay |
| `popover.enter` | 100 ms | Linear opacity and 4 dp away from the invoker (openQ4) |
| `popover.leave` | 80 ms | Linear opacity (openQ4) |
| `list.unfold` | 150 ms | Linear height from its row downward; a pick closes it at once (stock) |
| `notice.enter` | 160 ms | Cubic (0.16, 1, 0.3, 1); 12 dp in from the trailing edge with opacity (openQ4) |
| `notice.leave` | 200 ms | Linear opacity; the stack closes up with `page.enter` easing (openQ4) |
| `wheel.open` | 150 ms to readable, 300 ms in all | Marine: slit power-on, deploy, static burst, tune-in; Strogg: boot cascade and translation (section 14.6) (openQ4) |
| `wheel.select` | 150–350 ms | Marine: a transmission cut; Strogg: the heading translation (section 14.6) (openQ4) |
| `wheel.close` | 150–170 ms | Marine: sign-off to the slit; Strogg: scrambled drop-out (section 14.6) (openQ4) |
| `title.carry` | 500 ms, or 300 ms going deeper | With `frame.dock`, or `frame.step` going deeper: the activated label travels into the title slot and scales to the title size (openQ4) |
| `frame.step` | 300 ms | `accel(150, 150)`; only the top band moves one notch pitch (97 u) toward the trailing edge as a page opens a sub-page, from 50 ms, and back at once on Back; the bottom band stays docked (openQ4) |
| `title.crumb` | 150 ms | Linear; going deeper, the page title rises 7 u into the top band's thin span, steps down to 80% of its size and dims to 0.40 as the parent in the path; on Back it returns over the last 150 ms of `frame.step`, once the step has passed back under it (openQ4) |

These are authored defaults. Story-driven timing, terminal sequences, weapon
reticles and scripted effects remain faithful to sources. Marine menu screen
changes follow the stock choreography below; other major transitions may
choreograph frame/content groups, normally totaling less than 300 ms. Stock
hover growth (+0.01 `textscale`, about 3%) uses a local transform without
reflowing the button or moving neighbors.

### Stock choreography

**Home to page.** Activation plays `main_menu_selection` once. At 0 ms the
home content fades out (`content.out`) and the wordmark and emblem fade to
black (`light.up`). At 50 ms the whole home layer sweeps 640 u toward the
trailing edge (`screen.depart`) while the top band moves 323 u toward the
trailing edge and 63 u up and the bottom band 373 u toward the trailing edge
and 35 u down (`frame.dock`). At 550 ms the page appears in place: its own
navigation plates show at once at rest, and its title (to 0.50), backing and
Back action fade in (`content.in`). Every destination docks the bands at the
same position on the same timing. Destinations differ only in their backing:
0.60 for most pages, 0.80 for Friends, 0.90 for the server browser, opaque for
Credits and none for the Multiplayer hub. Save, the server browser and Friends
also raise a second, lower additive light band to 0.40.

**Back.** The page title disappears at once, the Back action fades over 100 ms
and the backing over 250 ms, and the page sweeps toward the leading edge with
its plates (`screen.return`). The bands start home at once (`frame.dock`),
without the 50 ms hold. At 500 ms the home content, already back in place,
fades in (`content.in`) and the wordmark and emblem light up from black.

**Within a screen.** The bands stay docked. Going deeper from the Multiplayer
hub fades the hub's plates over 150 ms while the hub sweeps 640 u toward the
trailing edge over 300 ms; returning sweeps the sub-page toward the leading
edge. The arriving page appears at 500 ms and fades in its title, backing and
Back action over 150 ms. Back always climbs one level. Settings categories
switched instantly in stock, without a fade; the 150 ms title fade happens only
on entry from home. Controls tabs switched instantly and flashed their row
plates (`row.flash`). `page.enter` and `page.leave` now cover sibling pages.

**Page to sub-page (openQ4).** In Remastered, a section that leads deeper
opens its sub-page one level down (section 13.7). Accept plays
`main_menu_selection` once. At 0 ms the page's plates fade over 150 ms while
the page sweeps 640 u toward the trailing edge over 300 ms, as the stock did
within the Multiplayer screen, and the page title becomes the crumb
(`title.crumb`). From 50 ms only the top band steps one notch pitch toward
the trailing edge (`frame.step`), so its 45-degree step slides under the crumb
and becomes the separator in the path. At 350 ms the sub-page appears: its
own plates show at once at rest, and its title (to 0.50, at the step's foot)
and backing fade in over 150 ms (`content.in`). The bottom band and Back never
move.

**Sub-page to page (openQ4).** The sub-page title disappears at once, and the
sub-page sweeps 640 u toward the leading edge over 300 ms with its plates
(`screen.return`) while the top band steps back at once. The crumb returns to
the title slot over the last 150 ms of the step, after the band's step has
passed back under it. At 300 ms the page, re-placed while hidden, fades in its
plates and content over 150 ms, with the focus on the section that led down.
Classic keeps the stock: the bands stay docked, the page sweeps out over
300 ms and the sub-page appears at 500 ms under a path title.

**Opening, closing and modals.** The menu opens under `menu.fade` from black
with the bands already at home; the in-game menu closes with `menu.fade` to
black, and single-player RETURN TO GAME closes at once. Modals never move the
bands. The Mods and Exit modals also dim the wordmark to 40% gray over 200 ms
and restore it over 150 ms.

**In-game multiplayer and buy menus.** There is no home state. The bands, at
0.40, slide in from fully off-canvas (`band.slide`) together with a 0.80 black
scrim over the match, the vignette, the light band, a 0.80 backing and the
grid, and slide out after a 50 ms hold. Pages switch by sweeping the outgoing
page toward the trailing edge over 150 ms and showing the next at 200 ms, with
no band motion.

The stock locked input for the whole choreography: 700 ms from home to a page
and back, 650 ms within the Multiplayer screen, 200 ms for a modal to open and
300 ms for it to close. The replacement stays responsive: input during a
transition retargets it instead of being dropped, and a control that is not
yet, or no longer, presented never activates. Reduced motion places bands and
pages at their destinations and cross-fades content within 80 ms.

### Band motion

The framing bands are the menu's largest moving surfaces; these rules keep
their motion legible at every size and refresh rate.

- **Dock states.** Each band has two dock states per menu, home and page
  (Appendix B.2). Remastered adds a sub-page state, in which only the top band
  moves on, one notch pitch (97 u) toward the trailing edge; the bottom band
  and Back stay docked, so nothing leaves the 4:3 canvas. Bands move along the
  straight lines between states. Notches, risers and steps travel with the
  band and never deform;
  aspect expansion lengthens only the flat spans, which stay attached to the
  viewport edges throughout the move.
- **Profile.** `frame.dock` is `accel(250, 250)` for both bands, sampled every
  presented frame. The Classic layout keeps the stock start times; Remastered
  keeps the same curve and durations.
- **Retargeting.** Input that reverses a screen change starts the opposite
  move from the bands' current positions. The new move keeps the `accel`
  profile and takes a duration proportional to the remaining distance, at least
  150 ms and at most 500 ms, so position never jumps. A reversed `frame.step`
  takes 150–300 ms the same way.
- **Title carry.** In Remastered, the activated navigation label travels into
  the title slot during `frame.dock` (`title.carry`) and becomes the path title
  (section 13.8). Going deeper, it travels into the slot at the step's foot
  during `frame.step`, and the page title becomes the crumb (`title.crumb`).
- **Light.** The rim light is part of the band and moves with it; nothing
  pulses or flares on arrival.
- **Compact classes.** Dock states follow the band depth limits of section
  13.3. The page state keeps its measured notch and step geometry; only the
  travel distance shrinks. The sub-page step stays one notch pitch at the
  class's scale.
- **Rate.** A screen change renders at the display rate. The Phone profile's
  30 Hz cap applies to a menu at rest, never to a change in progress.
- **Sound.** `main_menu_selection` plays once at activation; nothing plays
  during motion.

### Translation and transmission

The stock plays three different rune-to-readable translations and one
complete Marine transmission. openQ4 reuses them by name; the weapon wheel
(section 14.6) plays short forms of the credits translation and the Marine
transmission.

| Effect | Stock source | Construction |
| --- | --- | --- |
| Credits translation | Main-menu and end-game credits | See below |
| Intro decode | The opening logos (`cinematic.gui`) | The rune line fades in over 500 ms. The Lowpixel line then opens from a 1 u slit at the center through a hard-edged clip over 1000 ms while settling from white to its color, and the rune line fades out over the same 1000 ms |
| Changeover | The medlabs implant monitor | The in-fiction translation of runes into R_Strogg. Every 200 ms two or three letters, in scrambled order, cross-fade in place over 250 ms under an additive #B6D3AB flash that fades over 500 ms, with `guisound_beep2` at each step |
| Marine transmission | The MCC briefing screen | See below |

**Credits translation.** Each line exists twice: the same text in the rune
face and in Lowpixel. The rune copy sits 3–5 u lower, so the baselines meet,
at about 1.12 times the Lowpixel scale.

- **Runes.** The runes fade in over 2000 ms to 0.80.
- **Cross-fade.** Each line then cross-fades from runes to Latin, one line
  every 250 ms: headings over 1000 ms, names over 500 ms (50 ms after their
  heading) and list rows over 200 ms.
- **Scale.** As each cross-fade starts, both copies pop 0.02 `textscale`
  larger, about 5–6%, and settle back over it.
- **Names.** Names reach full white and settle to #D5FFA7 at 0.80 over
  500 ms.
- **Scan bar.** Behind each heading a scan bar slides in from the leading edge
  over 300 ms `accel(120, 180)`. It is an additive #FF8000 ramp that brightens
  linearly to a rounded head, with soft edges. It rests with its head about
  12 u past the heading, centered on the letters, and dims to black over
  1000 ms.
- **Exit.** A screen leaves by sliding 640 u toward the trailing edge over
  500 ms `accel(250, 250)` while its text fades over 300 ms.

**Marine transmission.** The MCC briefing screen plays a complete
transmission.

- **Power-on.** It powers on from a centered slit over 150 ms, with a
  one-frame grid flash.
- **Static.** A static burst of torn video bands rises to 0.40 in 50 ms and
  decays over 500 ms at every cut; it rests at 0.06.
- **Picture.** The picture opens from a 2 u line over 200 ms, then jolts to
  90% and back in 100 ms.
- **Labels.** Labels arrive white for 100 ms and settle over 100 ms.
- **Close.** The screen closes on ">> END TRANSMISSION <<" and powers off back
  to the slit over 150 ms.

The Strogg HUDs contain no runes, and the stock never translates anything on
the HUD. The wheel's Strogg translation is an openQ4 use of the credits
construction. Reduced motion keeps each effect's cross-fade, up to 80 ms, and
drops slides, flashes, bursts and jolts.

### Ambient loops and alarms

| Stock loop | Measured | Treatment |
| --- | --- | --- |
| Backdrop levelshots | Cross-fade over 2000 ms every 5000 ms, four images | Decorative |
| Static grain | Scrolls 1 texture width and 5 texture heights per second at 2.4% | Decorative |
| Animated gear | One turn per 10 s | Decorative |
| Terminal scanlines | Drift 0.2 texture heights per second | Decorative |
| EKG trace | Two beats per width at 0.2 widths per second, 24 beats a minute (section 14.3) | Instrument; its information stays under reduced motion |
| Idle display flicker | 92–100% at 3 Hz; the 9 Hz overlay flicker of the machinegun and shotgun displays is capped at 3 Hz | Decorative |
| Warning shimmer (low ammo) | 80–100% at 3 Hz | Alarm |
| Alarm pulse (empty weapon display, low-health rim, boss shield) | 100–50% at 2 Hz | Alarm |
| Missile warning | 70–30% at 2.5 Hz | Alarm |
| Settings notice | 100–50% at 1 Hz | Alarm |
| Loading status | 100% to 50% over 500 ms, back over 200 ms | Decorative |
| Weapon display states | Hyperblaster random flicker 68–100%, low pulse at 3 Hz and RELOAD at 2 Hz; nailgun empty 100–40% at 2 Hz; rocket cells 80–100% at 3 Hz; railgun flicker 92–100% | Decorative; alarms where they mark low or empty |
| Vehicle warnings | Hull and shield 60% × 100–50% at 2 Hz; electric damage a 1 Hz sawtooth 70–30%; hits an additive edge flash over 500 ms | Alarm |

Alarms keep their stock cadence, all at or below 3 Hz, and always pair with a
static cue. Reduced motion stops decorative loops and replaces alarm pulses
with a steady emphasized state. No loop runs on essential text.

Retarget interrupted animations from current sampled values. Reversal cannot
jump to an endpoint. Different properties can animate concurrently; the most
recent explicit owner wins per property. Define activation, cancellation,
completion and teardown semantics. Modal input ownership starts before its
first visible frame. Handle rapid back/forward, repeated clicks, resizing,
theme changes and map shutdown.

Stock menus play `main_menu_mouseover` on each hover entry of an enabled
control and `main_menu_selection` on each activation, including value cycling,
sorting and closing a modal. Nothing plays on hover exit or during animation.
World terminals play `guisound_beep2` on actions, objective notices
`guisounds_click` once, scopes `snd_zoomin` and `snd_zoomout`, and the loading screen `load_screen_ready` and
`load_screen_click`; modals have no sound of their own. The front end plays
`main_menu` music and the in-game menu `main_menu_gameplay`. Use these through
existing sound declarations where appropriate. Play once per semantic event.
Gamepad focus gets equivalent feedback without duplicate hover sounds. Never
sound each animation frame or server-list refresh.

Reduced motion removes translation, scale, stagger, shake and decorative loops;
use immediate state changes or opacity fades up to 80 ms. Preserve essential
progress/gameplay information. Motion speed never determines whether a command
executes or a saved state restores correctly.

## 9. Screen composition and gameplay ownership

Main menu preserves complex level/scene imagery and recognizable rails.
Navigation occupies a stable leading region; the page uses remaining safe
width. Title/context/back affordances stay predictable. Settings align labels
and values, group related options, explain consequences, retain Apply/Revert
where needed and offer a recoverable video-mode confirmation countdown.

Pause retains the world and correct semantics: SP may pause; MP must not pretend
to pause the server. Partial in-game panels use established scene softening
rather than an indiscriminate full-screen dimmer. Modals soft-focus the screen
beneath them instead of the stock `marine.scrim` (section 6). Effect ownership
survives stacked panels and releases on every exit/shutdown. Offer opaque local
backing as an accessibility alternative.

Save/load shows slot title, timestamp and complex preview, with overwrite/delete
confirmation. Preserve selection through refresh and show failures in context.
Demo browser/player retains playback and camera controls. Arena, buy and Match
Control retain progression, authority, teams, queues and spectator behavior.

HUD values update without rebuilding the document; digits do not jitter and
decorative motion cannot obscure aim. Separate source alarm/damage events from
application transitions. Vehicle/scope views retain aim geometry and clipping.
Aspect expansion cannot distort cinematic bars or subtitle placement.

World GUIs preserve per-entity instances, named events, input-ray mapping,
trigger commands, save/restore and visibility. Port instrument/game widgets as
functional components. Screenshots or decorative approximations do not count.
Inspect ordinary and story-critical terminal flows in gameplay.

### Stock screen reference

Positions are source units on the 4:3 canvas. Expansion follows section 3.

**Main menu, home state.** The top band's inner edge runs at 104 u, rises to
95 u across three notches (41–77, 138–173 and 234–268 u) and steps up to
68.5 u through a riser between 315 and 352 u. The bottom band's inner edge
runs at 400 u from the leading edge to 345 u, drops to 444 u by 390 u and
rises to 435 u between 583 and 592 u. The wordmark (additive) sits at 6,119 u
(376x92 u) and the emblem watermark (darkening) at 380,125 u (260x260 u) on
the trailing side. Navigation plates bleed off the leading edge on a 30 u
pitch from 202 u, marker at 32 u, label at 44 u. In game, SAVE GAME and QUIT
CURRENT GAME replace NEW GAME and MULTIPLAYER, and RETURN TO GAME adds a fifth
plate. The message of the day sits at 44,364 u. Secondary links (MODS,
UPDATES, CREDITS, EXIT; EXIT alone in game) stand on the plinth at
401–426 u.

**Page state.** The top band moves 323 u toward the trailing edge and 63 u up;
the bottom band moves 373 u toward the trailing edge and 35 u down. The top
band then carries the screen title at 39,19 u and its notches over the
content column; the bottom band's raised trailing section holds Back at
532,441 u (109x30 u).

**Sub-page state (openQ4).** The top band moves on one notch pitch, 97 u
toward the trailing edge; the bottom band and Back stay docked. The band's
step now falls at 122–136 u, its notches move one pitch along over the
content, and its riser moves into the trailing side screen. The parent page's
title is the crumb: it rises 7 u to 39,12 u, into the band's thin leading
span, at 80% of the title size and 0.40, and shrinks to fit that span before
it truncates. The band's 45-degree step separates it from the sub-page's
title, which starts at the step's foot, 136,19 u. The sub-page keeps the page
layout: section navigation from 14 u and content from 228 u.

**Settings.** Section navigation plates run from 14 u across 208 u on a 30 u
pitch from 172 u. The content column starts at 228 u: section headings at
259 u, 14 u above their first row; rows on a 24 u pitch; labels at row +31 u;
values at row +240 u; slider ramps at row +244 u with the numeric value at row
+322 u; action rows with markers follow the rows of their group. The page
backing darkens the content column fully and fades across the navigation
column. openQ4 adds a scrollbar at the content column's trailing edge.

**New game.** Difficulty choices use section navigation plates beside a
description paragraph; START GAME is the enlarged primary plate.

**Save and load.** A 183x137 u preview with a 1 u olive border (`#414624`)
and its date and time below it on the leading side; the list on the trailing
side under Lowpixel column headings and an olive rule; Load and Delete as
section navigation plates.

**Server browser.** Header band (587x37 u) with column symbols, 18 u rows
across 604 u, a sort notice with the flipped marker and a CLEAR SORTING
action under the list, then a full-width refresh progress bar.

**Modal.** 320 u wide and 136 u tall at y 155, centered horizontally and 17 u
above center; body text at +22 u; actions of 120x30 u, 20 u from the leading
end and 22 u from the trailing end. Entry, list and settings dialogs grow to
146–400 u tall, and the system message box offers leading, center and trailing
actions.

**Loading.** Full-bleed levelshot; top band thick on the leading side with its
riser near the middle, bottom band thick on the trailing side; the level name
trails at the top (Marine 26 dp, tracking -0.1 em) over a gradient that fades
toward the leading side; progress bar from 235 to 640 u at 431 u with
`LOADING` trailing-aligned across it; corner brackets at the four corners of
the picture; the `+` grid over everything. A ten-by-two dot matrix marks the
top-leading corner. LOADING pulses from 100% to 50% over 500 ms and back over
200 ms with a drop shadow, then reads "- CLICK TO CONTINUE -". The
multiplayer loading screen raises the bottom band to 227 u, moves the progress
bar to 313 u and the lower brackets to 256 u, and adds the server name and
address, game type and limits, a message line over a 50% gradient and a row of
24 u item icons.

**Cinematic.** Black bars of 60 u top and bottom frame a 16:9 picture on the
4:3 canvas. Preserve that picture aspect rather than the bar height.

**Marine HUD.** Ammo (13 u), health (190 u) and armor (326 u) gauges, each
125x59 u at 420 u, with trailing-aligned numerals and 16 u icons in each gauge's
notch below them; the weapon render and reserve count on the ammo gauge; the
weapon-select strip at 370 u with the weapon name centered below at 392 u.
Section 14 records the rest of the HUD.

**In-game multiplayer menu.** The Marine menu vocabulary with framing bands at
0.40 docked 2 u lower and to the trailing side of the front-end page state,
over a 0.80 black scrim, the vignette and a 0.80 backing. It has no home state
(section 8); the buy menu uses the same construction.

## 10. Visual editor

The editor is a first-class desktop tool using the game's document evaluator,
vector renderer, fonts, components, layout and timeline code. Matching preview
is an invariant, including clipping/alpha. It creates, edits, saves, reopens and
packages complete interfaces without proprietary Flash authoring software.

### Workspace

Resizable/dockable hierarchy, canvas, inspector, asset/component browser,
timeline, diagnostics and event/state panels. Multiple documents, undo/redo,
dirty markers, recoverable autosave, atomic saves, open-recent and external
change conflicts are required. Never overwrite external work silently. Editor
data stays outside runtime staging; preserve source comments and unknown
extension data during round trips.

### Canvas and layout

Pan/zoom/fit, rulers, guides, grids, snapping, marquee/multiselect, locked/hidden
layers, breadcrumbs and isolation. Move/resize/rotate/anchor handles show exact
numbers and affected constraints. Align/distribute, group, duplicate, reorder
and reparent are undoable. Dragging edits actual constraints, not disposable
preview overrides. Independently show layout/content bounds, baselines, clips
and hit regions.

### Components, properties and vector art

Searchable library with reusable templates, instances, variants and explicit
overrides. Inspector includes layout, type, localization, bindings, focus order,
accessibility, events, materials, paths, clips and motion. Vector pen/node
editing supports line/quadratic/cubic segments, holes, fill rules, caps/joins,
stroke alignment, gradients, transforms and supported SVG import/export.
Unsupported input produces actionable diagnostics, never silent raster fallback.

### Motion and behavior

Timeline tracks by node/property, key insertion/move/delete, multi-key selection,
easing curves, scrubbing, range/loop preview, rate, labels and event markers.
State graphs expose transitions/input ownership. Preview invokes named events,
changes state variables, expands localization and loads recorded gameplay
fixtures. These are internal application events, not OS input injection. Show
which binding/animation owns a value and why a node is hidden/disabled.

### Preview and diagnostics

Presets for 4:3, 16:9, 16:10, 21:9, 32:9, Steam Deck and touch; custom output
dimensions, DPI, UI/text scale and safe areas. Compare states side by side.
Capture exact engine render targets to files, never OS screen grabs. Hot reload
preserves useful selection/state and the last valid document during syntax
errors. Diagnostics link to source/node/property and cover localization,
overflow, contrast, inaccessible controls, bitmap exceptions, draw counts,
tessellation/cache cost and texture memory.

### Delivery gate

Demonstrate a full round trip: open a translated screen, alter layout and a
vector path, edit a transition, add a localized control/binding, save, reopen,
package and run in SP and MP. Demonstrate undo/redo and external-change recovery.
A property table or web mockup with separate rendering is not this editor.

## 11. Qualification and completion

Maintain a machine-readable inventory of every effective GUI, include, widget,
script/event, material and bitmap exception. Entries record replacements,
behavior fixtures, dependencies and evidence. Require zero unclassified or
silently skipped resources. Conversion success alone proves neither visual
nor behavioral equivalence.

Use windowed engine screenshots on OpenGL/Vulkan at 720p, 1080p, 4K;
100/125/150/200% DPI; representative 4:3, 16:10, 21:9, 32:9;
100/150/200% text scale; and all supported languages. Use a documented covering
matrix and targeted extremes rather than an unexamined Cartesian product.
Validate monitor changes, viewport offsets, fractional strokes, pointer mapping
and IME. Test motion at 30/60/144/240 Hz, interruption and pause using measured
samples and visual captures. OS input control requires specific user permission.

For each translated stock screen, compare the replacement with the stock GUI at
the same 4:3 output. Plate bands, cuts, markers, rails, baselines and band
silhouettes agree within 1.5 dp; flat fills agree within 2/255 per channel
away from edges and light layers; stock-derived timings agree within one frame
at 60 Hz. Record every deliberate departure with its section of this
specification.

Qualify every screen three times: with mouse and keyboard only, with a
controller only and with touch only. Cover each viewing profile and size class
of section 13, every controller glyph family with its confirm convention, and
device switching mid-screen. Android qualification needs physical phones and
tablets for touch targets, the touch gameplay overlay and its editor, the
input method, system Back, cutouts and suspension; an emulator or desktop GLES
run does not establish touch usability.

Gameplay covers SP HUD/scopes/vehicles/terminals/checkpoints/manual saves; MP
join/team/scoreboard/chat/buy/arena/Match Control; loading/cinematics; and editor
round trips. Follow existing Linux/macOS/Android-GLES workflows. Missing platform
evidence remains incomplete. Dedicated builds retain functioning UI stubs.

Reject blurry furniture, fixed-size raster vectors, distorted cuts, displaced
hit targets, clipped translations, missing widget functions, generic restyling,
broken saves and mismatched editor preview. All current GUI must run through
the new system before claiming replacement complete.

## 12. Design review record

For each family/component/screen record source GUI/material identifiers,
reference state/capture, measured geometry/colors, bitmap exceptions, editable
vectors, all states, motion timing/rationale, language/scale behavior, output
captures, behavior verification and remaining gaps. Evidence includes exact
revisions. Keep retail extractions and temporary captures out of tracked source.

Cite the measured value used for every stock-derived dimension, color and
timing, either from this specification or from a newer measurement recorded
with its method. A disputed value is re-measured from the installed archives,
not estimated from a screenshot.

Stages can contain unfinished work but cannot mark it visually accepted or
rewrite this specification around its current limitations.

## 13. Modern interface direction

Sections 2–12 fix what must stay recognizably Quake 4. This section defines
how the replacement behaves as a current game interface on every device openQ4
targets: desktop monitors, televisions, Steam Deck and other handheld PCs, and
Android phones and tablets. Mouse and keyboard, controller and touch are equal
primary inputs. Everything added here that the stock never had is drawn in the
stock vocabulary, and ART-002's rejection of generic web, pill, card, neon and
blur restyling applies to every new pattern.

### 13.1 Principles

1. **Stock identity, current behavior.** Keep every recognition trait and add
   capability in the same vocabulary: a prompt bar is a band segment, a filter
   tag is a small cut plate, an on/off setting is a cycling value.
2. **Every input is primary.** Each screen is complete with mouse and keyboard
   alone, a controller alone, or touch alone. Controllers never steer a cursor
   through menus, and touch never depends on hover (INP-001).
3. **One visible focus.** While a controller or keyboard is in use, exactly one
   control holds focus, and the prompt bar and detail area explain it.
4. **Adapt, don't shrink.** Layout reflows by size class and viewing distance.
   A desktop composition is never scaled down to fit a phone.
5. **Immediate and interruptible.** Input changes the interface in the next
   presented frame; transitions retarget instead of blocking.
6. **Nothing is lost.** Focus, scroll position, selection and unfinished edits
   survive navigation, device switches, resizing, suspension and resumption.
7. **Accessible by default.** Remapping, size, contrast, motion and
   color-independent cues are available from the first launch.
8. **Quiet at rest.** Light and motion mark change and focus. Ambient motion
   stays subtle and can be turned off.

### 13.2 Viewing profiles

A viewing profile sets the defaults for scale, target size and presentation.
openQ4 chooses one at startup from `com_platformProfile`, host signals and the
display, and players can change it under Interface settings. Players' own UI
and text scale choices always override the profile defaults.

| Profile | Devices | Viewing distance | Primary input | UI / text scale | Minimum target |
| --- | --- | --- | --- | --- | --- |
| Desk | Monitors, laptops, ChromeOS windows | 50–80 cm | Mouse and keyboard | 100% / 100% | 36 dp |
| Couch | Televisions, Steam Big Picture | 2–3 m | Controller | 150% / 125% | Focus-driven; 44 dp rows |
| Handheld | Steam Deck and similar PCs | 30–40 cm | Controller, touch | 125% / 100% | 48 dp for touch |
| Phone | Android phones, in landscape or portrait | 25–35 cm | Touch, controller | 100% / 100% at native density | 48 dp |
| Tablet | Android tablets and foldables | 35–50 cm | Touch, controller, keyboard | 100% / 100% at native density | 48 dp |

These defaults keep the 17 dp body em within roughly 22–31 arc-minutes of
visual angle on every profile. On Android, 1 dp follows the platform's
density-independent pixel, so a phone renders the type ramp at its own density
rather than at a desktop scale, and the text size starts from the system font
scale within the supported 100–200% range. Root-menu fitting never reduces a
touch profile below its default density to reach a 640x480 dp area; the
compact layouts below need only 640x360 dp. Couch adds a screen-edge inset of
0–5%, set against a calibration frame, for televisions that crop the picture.
An Android device connected to a television can choose the Couch profile.

### 13.3 Size classes and adaptive structure

Size classes are evaluated on the safe area in dp after UI scale, in this
order. They refine the compact presentation in section 3.

| Class | Condition | Structure |
| --- | --- | --- |
| Compact height | Height below 600 dp and width at least 720 dp | Phones in landscape and short windows. A navigation rail on the leading edge, the content column, and a detail column when the width reaches 840 dp. The top band stays within 40 dp and the bottom band within 56 dp |
| Compact width | Width below 960 dp | Phones in portrait and narrow windows. One content column. Navigation becomes a tab strip in the top band; details expand in place |
| Regular | Width 960–1439 dp | Navigation column and content column; details appear under the focused row or in the prompt band |
| Expanded | Width 1440 dp and above | Navigation, content (forms within 1440 dp) and a trailing detail column |

Compact layouts keep both framing-band states and the stock choreography, but
limit band depth as stated; notches, risers and steps keep their measured
dimensions and move only by the smaller distance between the compact states.

Essential content, text and targets stay inside the platform's window safe
area. Bands, backdrop and imagery extend full-bleed under display cutouts,
rounded corners and system bars. Swipe-driven controls keep 24 dp clear of the
screen edges so they never compete with the system's back and home gestures.
Folding, multi-window and orientation changes are ordinary resizes under
section 3 and preserve state.

### 13.4 Input model, prompts and details

**Modality.** The most recent input device sets the presentation: hover and a
pointer for mouse, focus and glyph prompts for controller and keyboard, press
feedback and larger action plates for touch. Switching is immediate and never
moves focus or drops an edit. Moving the mouse shows hover without clearing
controller focus; the next navigation input resumes from the focused control.

**Focus.** Initial focus goes to the screen's primary action, or to the control
that held focus when the screen was last left. Each container remembers its
last focus, and a modal contains focus and returns it to its invoker. Focus
never lands on a hidden, disabled or clipped control. Navigation is spatial by
default, with authored overrides where geometry misleads. Menus of up to seven
items and tab strips wrap; scrolling lists do not.

**Prompt bar.** The bottom band's raised trailing section, where the stock Back
action sits, becomes the prompt bar. It lists the actions the focused control
accepts as a glyph and a verb. Back stays in the stock Back position at the
trailing end, the primary action sits beside it, and contextual actions lead.
The bar updates in the same frame as focus, hides actions that cannot run and
shows at most five; further actions move behind a More action. Glyphs follow
the active device: controller buttons, keycaps for the keyboard, clickable
plates for the mouse, and 48 dp tappable plates for touch. In play the HUD
names controls in the same construction (section 14.8).

**Detail area.** Every setting, list row and action can explain itself: a
description, current and default values, consequences such as a required
restart or a performance cost, a preview, and the reason an unavailable action
is refused. Expanded layouts dock the detail area at the trailing edge using
the card construction, regular layouts place it under the content, and compact
layouts expand it in place. It replaces stock hover cards and tooltips for
controller and touch users.

### 13.5 Controller

Actions map to button positions:

| Control | Menu action |
| --- | --- |
| South face button | Accept |
| East face button | Back; closes popups and menus |
| West face button | Secondary: reset to default, remove, refresh |
| North face button | Tertiary: details, search, sort, filters |
| Left and right bumpers | Previous and next tab or category |
| Left and right triggers | Page up and down in lists; coarse slider steps |
| D-pad, left stick | Move focus; left and right change the focused value |
| Right stick | Scroll the detail area; rotate model previews |
| Menu button | Pause from play; Apply on a screen with pending changes; otherwise close the menus and resume |
| View button | Scoreboard in multiplayer; show or hide details in menus |

Prompts draw the label printed at each position, as SDL reports it.
Nintendo-layout controllers default to confirming on the east button, and a
Swap Confirm and Back setting overrides any default. Glyph sets cover Xbox,
PlayStation, Nintendo, Steam Deck and generic controllers. They are vector
symbols in the family text color, whose shape and letter carry the identity;
keyboard keycaps are small plates with a 45-degree cut. The glyph family
follows the controller SDL reports, the `steamdeck` platform profile selects
Steam Deck glyphs for the built-in controls, and a Prompt Style setting forces
a family when a remapping layer hides the real device. Rear paddles and extra
buttons are bindable for play; no menu requires them.

**Values.** Left and right step a cycling value; Accept opens the full list
when it has more than seven choices. Sliders step with left and right,
accelerate after the navigation repeat delay, jump ten steps with the
triggers, and open exact numeric entry on Accept. Accept toggles an on/off
value. Binding capture starts on Accept and takes the next input; holding
Back for one second cancels, so Back itself can be bound, and conflicts offer
a swap or clear (WID-007).

**Text entry.** Use the platform keyboard where the session provides one, such
as Steam on Steam Deck and in Big Picture, or the Android input method.
Otherwise openQ4 shows its own vector keyboard: a key grid in the Marine
vocabulary with the current language's layout and accented characters. The
D-pad or left stick moves, south types, west deletes, north adds a space, the
bumpers move the caret and Menu finishes. A field always scrolls clear of any
keyboard that would cover it.

**Feedback and devices.** Focus movement never vibrates. A short light pulse
marks a slider limit, a refused action or an error, scaled by the existing
rumble settings and silent when rumble is off. A controller disconnect pauses
single player behind a reconnect prompt and shows a notice in multiplayer; a
low controller battery shows a notice.

**In game.** Holding a bound button opens the weapon wheel (section 14.6): a
ring of HUD plates showing every weapon's icon and ammunition, the selection
marked with the weapon strip's orange icon and ring, and the weapon name at the
center. Either stick selects and release equips; a tap swaps to the previous
weapon; the game keeps running. The same component provides a quick-chat wheel
of localized phrases in multiplayer.
Holding View shows the scoreboard. At world terminals the crosshair stays the
terminal cursor and Fire clicks, as in the stock. While the crosshair rests on
a terminal, the interactive region under it highlights, and an optional
terminal assist slows the crosshair over interactive regions.

Menus never need a virtual cursor. Free-placement editors, such as the level
editor and the touch layout editor, may offer one on the right stick, and a
controller touchpad may drive the menu cursor when players choose
`in_touchpadMode 1`.

**Controller settings.** Draw each stick's dead zone and response curve over
its live position, trigger thresholds on live trigger bars, a gyro test
reticle and a rumble test, with presets and a southpaw preview. These present
the existing `in_joystick*`, `in_gyro*` and `in_touchpad*` settings.

### 13.6 Touch and Android

**Touch fundamentals.**

- Hit regions are at least 48 dp with 8 dp between neighbors. A plate may look
  smaller; its authored hit extension grows instead (INP-007).
- Touch-down shows the hover and focus treatment at once, plus the press inset.
  Lifting outside the target or moving more than 8 dp cancels; activation
  happens on release (INP-002).
- A 500 ms long-press opens the detail area. Lists scroll by drag with inertia,
  and at either end their rim light brightens instead of stretching the
  content. A horizontal swipe on a tab strip changes category.
- Nothing depends on hover: hover cards become the detail area, and tooltips
  appear on long-press or from an information symbol.
- Text uses the Android input method. The focused field scrolls above the
  keyboard through the SDL text input area, and the keyboard's action key
  submits.
- The system Back button or gesture is Back everywhere. In gameplay it opens
  the pause menu; it never quits without the menu's confirmation.
- Menus follow one touch at a time; the gameplay overlay tracks every finger
  independently.
- A light haptic tick on press follows the system's touch-feedback setting.
- Play runs in landscape, either way up, and in portrait, with the 9:16 view as
  the portrait reference (section 14.4). Turning the device is an ordinary
  resize (section 13.3) that keeps the state, and each orientation keeps its own
  touch layout.

**Lifecycle.** Leaving the app, or suspending a handheld, pauses single player,
mutes audio and shows the pause menu on return, never dropping the player back
into combat. Multiplayer reconnects or shows a connection-lost state. Saves,
settings drafts and edits survive backgrounding and process death. The screen
stays awake during play and cinematics, and may sleep in menus.

**First run.** A device without game data, which includes every fresh Android
install, starts with a guided setup in the Marine vocabulary, which players can
reopen from Settings:

1. Language, with a live text sample.
2. Game data: explain that the player's own Quake 4 1.4.2 data is required,
   open the system folder picker, verify the required archives against the
   [official checksums](official-pk4-checksums.md), and copy or index them with
   progress, size and time remaining. A missing language archive or optional
   archive is reported without blocking. Setup resumes after an interruption,
   and every failure names its fix: a missing archive, a wrong version or too
   little storage.
3. Controls: the touch layout preset or the detected controller, and look
   sensitivity with a live test.
4. Interface: the viewing profile and text size, with a live sample.

**Touch gameplay controls.** The default Android build has no touch gameplay
overlay, because the external Sigma Touch host cannot be distributed with GPLv3
builds (see [Android, GLES and Sigma Touch](android-build.md)). openQ4
therefore provides a first-party overlay built with this UI system:

| Control | Default placement | Behavior |
| --- | --- | --- |
| Move stick | Leading half; the origin is wherever the thumb lands | Analog; an outer ring marks the run threshold |
| Look | Trailing half | Drag to turn, with sensitivity and acceleration; optional gyro |
| Fire | Trailing thumb zone, 88 dp | Hold to fire; dragging from it keeps turning while firing |
| Jump, crouch | Trailing cluster, 64 dp | Crouch holds or toggles |
| Weapon | Trailing cluster | Tap for the next weapon; hold for the weapon wheel |
| Reload, zoom | Beside Fire | Shown when the current weapon supports them |
| Pause | Top leading corner, 48 dp | Opens the pause menu |
| Scoreboard, chat | Top edge | Multiplayer only |

Controls are drawn in the Marine HUD family: stick bases as thin-railed rings,
buttons as cut plates around centered vector symbols, 35% opaque at rest and
80% with the orange marker while pressed. Play shows no labels. Controls never
cover the crosshair, and the Touch HUD preset moves readouts clear of them.
Quake 4 has no use key: as in the stock, Fire clicks the terminal region under
the crosshair.

A layout editor, reachable from Settings and the pause menu, moves, resizes
(75–150%), fades and hides each control on a snapping grid. It offers Default,
Compact and Left-handed presets and a reset, stores layouts per device and
shows labels only while editing.

**Portrait.** The 9:16 portrait view uses a 720x1280 dp reference. The 640 u
center screen spans its width at `1 u = 1.125 dp`, with a 370 dp band above and
below it (section 14.4), and the bottom band becomes the thumb deck. The move
stick floats in the deck's leading half. Fire, weapon, jump, crouch, zoom and
reload gather in its trailing half: Fire in the middle with the weapon control
above it, jump and crouch outside it and zoom and reload inside it. Every other
drag looks, in the deck or in the view above it. The Touch HUD preset raises the
readouts to the center screen's bottom edge, above the deck. Pause keeps the top
leading corner, and the scoreboard and chat move to the top trailing corner,
clear of a camera cutout. Sizes, spacing, states and the editor are as in
landscape. Taller screens deepen the deck. Menus take the compact-width
structure (section 13.3), and the multiplayer menu card keeps its dp size
(section 14.18).

**World terminals.** A tap on a terminal's projected surface within use range
activates the region under the finger through the projected ray (SUR-004). A
region within 16 dp of the finger counts as hit when no other region is closer.

**Other input on Android.** A connected controller hides the touch overlay and
switches to the controller presentation; touching the screen brings the
overlay back. A pointer device, as on ChromeOS or a tablet with a mouse,
enables hover.

**Mobile rendering.** The GLES renderer omits some desktop post effects, so no
interface element depends on them. Where scene softening or soft focus is
unavailable or too costly, partial panels and modals use a darkening scrim and
vignette instead. Keep to two full-screen blended layers on tile-based GPUs by
merging the backdrop's light band and vignette. A static menu redraws only on
change, at most 30 times per second. The Phone profile starts with ambient loops
and parallax off.

### 13.7 Screen patterns

- **Title.** The emblem watermark keeps the trailing side (section 13.8). When a
  save exists, Continue leads the navigation, and the message line under it (the
  stock message-of-the-day slot at 44,364 u) shows its detail: a levelshot
  thumbnail, the mission, difficulty and play time. The same line explains
  whichever item has focus. SINGLE PLAYER (a page leading to Campaign and
  Arena), LOAD GAME, MULTIPLAYER and SETTINGS follow, and the secondary links
  stay on the plinth.
- **Pause.** Resume leads. Single player offers Save, Load, Settings, Restart
  Level, Objectives and Quit to Menu. Multiplayer uses the tabbed Escape menu
  of section 14.18 (Team, Players, Vote, Match, Settings, Voice, Server and
  Admin) and never pauses the server. The panel sits over the softened scene
  (section 9).
  - **Single player.** The pause menu keeps the title screen's frame but never
    reads as the title screen. GAME PAUSED replaces the wordmark, the
    navigation starts at 172 u to hold its seven actions, and only EXIT stays on
    the plinth. The current level block takes the emblem's place: a card with
    the levelshot, the mission and difficulty, the objectives with their state,
    and the time in the mission, the total time and the last save.
  - **Strogg.** After Kane's stroggification the pause menu takes the Strogg
    family. The bands keep their frame but trade the 45-degree notches for
    30-degree shoulders and downward teeth. Rails, rim light and the circuit
    traces etched in the bands are #F59512, plates end in a 30-degree shoulder,
    markers are slanted, and labels are R_Strogg in #FCFFC8, #FFCC00 on focus.
    Every label arrives in runes and plays a short form of the credits
    translation (section 8): the title over 500 ms under the scan bar, the
    actions over 200 ms each and 45 ms apart, and the level block's lines after
    them. The focused plate runs the scan bar under its label, and the level
    block takes a chamfered frame with the masked grain. Input works from the
    first frame, and reduced motion shows the translated labels at once.
  - **Arena.** A single-player Arena Campaign match pauses like a story level,
    so Escape opens the single-player pause frame in place of the multiplayer
    Escape menu. MATCH PAUSED replaces GAME PAUSED; Resume, Restart Match,
    Settings and Leave Match sit on the stock main menu's rows from 214 u, and
    nothing stays on the plinth. The match block takes the level block's place:
    the tier and role in its header, the map's levelshot, the match, its game
    type and map, then the score with the paused clock in `marine.value`.
    Restart Match and Leave Match ask first in the stock modal. Settings opens
    the settings pages over the paused match and returns to this menu.
- **Settings.** The SETTINGS page holds the categories on the home
  navigation plates, and each category is a sub-page built as Create Server
  is: SETTINGS as the crumb, the category's title at the step's foot and the
  content over a backing that darkens all of it. A category's sections are
  tabs in the Escape card's strip (section 14.18) across the content's top,
  switched with Q and E, the bumpers, a swipe or a click; a category that fits
  one screen has no strip, only the header band carrying its columns'
  headings. A tab holds at most two columns of setting rows on the stock 24 u
  pitch, never more, at most 14 rows to a column, the values lined up in one
  value column; Up and Down read down the first column and on down the
  second, and Up from the first row reaches the tab. Above 100 % text size
  the columns stand one under the other in a single scrolling column whose
  rows grow with the text (section 2's reflow), while the strip, the prompt
  bar and the titles keep their size as furniture. Search runs across every category when the player
  types, presses north, or taps the search symbol, and each result shows its
  category path. A modified row's leading rail turns `marine.value`; the west
  button resets the focused row to its default. Rows that need a restart carry
  a warning tag, and rows with a performance cost show a three-tick impact
  gauge drawn from the slider's tick ramp. Crosshair, HUD scale, text size,
  brightness and color-vision rows preview live. Apply and Discard live in the
  prompt bar, except on DISPLAY, whose settings apply together as one draft:
  its rows edit the draft, APPLY CHANGES stands under the columns beside the
  draft's status as Create Server's START does, the Menu button applies too,
  and Back over changes not applied asks whether to apply, keep editing or
  discard them. Display changes keep the 15-second Keep or Revert countdown.
- **Menu levels.** Home, pages, sub-pages and detail pages (sections 8 and 9). Single Player
  is a page whose sections, Campaign and Arena, each open a sub-page: Campaign
  includes a campaign choice (Quake 4 and installed Awakening content),
  then New Campaign (the difficulty choice) and Chapters (the reached
  missions), and Arena holds the tier ladder. Multiplayer's Join Game (the
  server browser), Create Server and Demos (the demo list) open sub-pages too,
  and Player Setup edits in place. A section that leads deeper summarizes itself
  in the content column, with a levelshot where one fits, until Accept opens it.
  Back always climbs one level. At a text size above 100 % (section 2) the
  plates' labels, titles, plinth links and prompt bar keep their size as
  furniture, while the content column's description widens to the 4:3 view's
  margin and rises between the docked bands as its lines need; the home's
  message line runs on to the same margin and the plinth steps down to clear
  its lines; a confirmation grows with its body; and the pause's level block
  gives its picture's room to its growing lines inside the same card. In a
  portrait view tall enough for the column to read at least at its landscape
  size (a view of 3:4 and taller in any case) a menu level stands its parts
  in one column across the canvas, scaled together as large as the height
  between the bands allows, up to 1.6 times: the home's wordmark, plates,
  message line and plinth, a pause's title, actions, EXIT and level block, or
  a page's plates and description under them; its confirmations stand at 1.6
  times their size. A page that grows a fixed amount (Restart Level, the
  campaign's difficulty, the restart screen, Credits) takes its column in a
  portrait view that holds it; the list pages and the Arena keep 3:4.
- **Fourth level.** Every child route beneath a sub-page uses the same detail
  construction. The top band moves another 97 u, to a 517,-63 u translation;
  the bottom band remains docked. Two parent crumbs occupy the thin spans at
  39 and 136 u; the current title starts at 233,19 u. Each crumb uses the same
  language fitting, 13 dp floor and clipping as the third level. Descending
  takes 350 ms: outgoing content fades over 150 ms and sweeps 640 u over
  300 ms, the band begins its 300 ms step at 50 ms, and the chosen label
  carries into the new title slot. Back takes 300 ms and restores the immediate
  parent title during its last 150 ms. Back during descent reverses the
  presented pose over 150–300 ms; repeated activation cannot dispatch twice.
  Reduced motion places the new level immediately with an 80 ms arrival.
  Both Quake 4 and Awakening difficulty setup use this route, including entry
  after a game-directory/module switch. Difficulty and Turbo are drafts until
  Start Game. Chapters or other future child pages must use the same helper;
  sibling tabs, Arena tier selection and modal dialogs do not add a level.
- **Lists** (servers, saves, demos). Filters are small cut tags built like the
  header band. Sort from the column header or with north. The detail column
  shows the levelshot, players and rules; Join, Spectate and Favorite sit in
  the prompt bar; west refreshes. Empty, loading and failed states follow
  section 7. In a view of 3:4 and taller a list page stands in one column
  across the canvas: its filters under the top band, then the selected item's
  picture with its details and actions beside it, then the list to the bottom
  band, each row in two lines (the name over the rest) and scrolling; the
  parts under the list take a line each, its confirmations grow with the
  rows, and the page's backing darkens the whole width. Create Server stands
  its map picture and list over its options the same way.
- **Arena Campaign.** On the Arena sub-page, tiers form a vertical ladder of
  plates in the section navigation. Locked tiers are dimmed and carry the lock
  symbol, boss matches use a larger plate, and the bumpers switch tier. In a
  view of 3:4 and taller the page stands in two blocks, one under the other:
  the ladder with the chosen tier's briefing beside it and the progress at its
  foot, then the tier's matches with the difficulty and Start Match under
  them and the chosen match's levelshot and details beside them, grown
  together as far as the column's width allows.
- **Match Control.** Keep its contract: unavailable actions stay visible, and
  the refusal reason shows in the detail area. The bumpers move between
  sections, and destructive actions use the stock modal.
- **Demo playback.** Controls sit in the bottom band and hide after 3 s without
  input. The timeline is a progress bar with round markers. On a controller,
  south pauses, the triggers skip 10 s, the bumpers skip 30 s, north changes
  camera, west hides the overlay and east leaves. On touch, a tap shows or
  hides the controls and the timeline drags.
- **Loading.** The level name and levelshot lead; localized tips run in the
  bottom band; the continue prompt shows the active device's glyph, or "Tap to
  continue" on touch (section 14.17).
- **Notices.** Short notices appear in the top band's trailing section: a
  controller disconnect, a low battery, match events. They never cover the
  crosshair or touch controls, and unresolved errors persist.

### 13.8 Visual refinements

- **Depth.** The backdrop, light, grid and frame layers may shift by up to 6 dp
  with the pointer, the right stick or device tilt. Content never moves.
  Reduced motion disables the shift, and the Phone profile starts with it off.
  On the retained title, apply pointer depth to the scene image only; the
  vector light, grid and framing stay stationary so fractional pointer movement
  does not rebuild their analytic coverage. Their scripted band motion remains.
- **Focus light.** The focused plate gains a soft additive glow in
  `marine.glow`, about 24 dp across, beside its focus rail. High-contrast mode
  replaces the glow with a solid rail.
- **Emblem light.** The title screen's emblem watermark keeps the stock
  darkening blend (section 4). A rim light follows its outline at 0.20, and a
  glint 12% of the outline long runs around it every 9 s. Reduced motion keeps
  only the rim.
- **Title continuity.** Activating a navigation item carries its label into the
  screen-title slot, where it becomes the path title, within the stock
  choreography timings.
- **Symbols.** Extend the vector set in the stock drawing style (1.5 dp strokes,
  45-degree terminations): search, filter, refresh, information, reset,
  restart, warning, controller, keyboard, touch, gyro, battery, lock, star and
  segmented network-quality bars.
- **Tags.** Modified, Restart, New and Locked are small header-band plates,
  never pills.
- **Color vision.** Offer alternative team and item palettes, such as a blue
  and orange team pair, and always pair team color with emblems and shape.
- **Dynamic range.** Menus and the HUD composite in standard range after tone
  mapping; openQ4's HDR path is scene-only. If display HDR output is added, the
  interface renders at a player-set paper-white level with additive light
  capped relative to it.

### 13.9 HUD presets

| Preset | Contents |
| --- | --- |
| Classic | The stock layout, shear and palette |
| Remastered | The stock elements anchored to the safe area, scalable, with vector symbols |
| Minimal | Health, armor, ammunition and crosshair only |
| Competitive | Multiplayer: the match bar at the top center (your side, the timer, the other side), team status at the leading edge, readouts enlarged by 15% and network quality at all times (section 14.9) |
| Touch | Readouts moved clear of the touch controls and enlarged |

HUD settings cover an independent HUD scale, the safe inset, the HUD width
(16:9, 21:9 or Full, section 14.4), opacity, color vision, and the crosshair
editor. The editor offers the stock set of twenty (nineteen designs), the five
stock sizes from 24 to 72 dp and sizes between, free color with the stock
menu's eight swatches as presets, and a live preview. HUD settings also cover
hit markers in multiplayer, the damage direction indicator, pickup messages
and the weapon strip. Couch and Handheld raise the default HUD scale. Section
14 defines each element.

### 13.10 Accessibility baseline

- Remap every action for keyboard and mouse, controller and touch; offer hold
  or toggle for crouch and zoom.
- Subtitles and captions with size, background opacity and speaker names,
  placed in the cinematic picture area.
- UI scale, text size, high contrast and the text-backing option in Interface
  settings; `gui_textBackground` is console-only today.
- Reduced motion (section 8), plus reduced flashing and camera-shake strength
  where the game provides them.
- The color-vision palettes above.
- A separate interface volume and mono audio.
- A semantic label and role on every control, so platforms that offer
  text-to-speech can read menus.
- No menu timeouts except the display confirmation countdown, which shows the
  time left and accepts Keep from any input device.

### 13.11 Performance and power

Interface CPU and GPU work each stay within the budgets in the
[product completion plan](archive/2026-10-18/plans/ui-product-completion.md): 10% of the target
frame interval at p95 and 20% at p99 on the selected support tier. On phones
and tablets the target interval is the device's refresh rate while playing and
30 Hz for a static menu. Warm screens never retessellate unchanged paths or
rebuild unchanged text. Transitions do not allocate whole documents. Thermal
throttling reduces ambient effects before it reduces responsiveness.

### 13.12 Classic and Remastered layouts

Remastered, described in this section, is the default layout. Classic keeps
the stock composition, choreography and density of the Marine menus, and is
the same reconstruction that section 11 compares against the stock. Both share
components, tokens and the input model, so Classic still has focus, prompts
and touch targets. Touch-first profiles always use Remastered compact layouts.
Players choose the layout under Interface settings.

### 13.13 Modern components

Each component below is new to openQ4. It is built from the stock parts: cut
plates, header bands, markers, the card and modal constructions, the value
color and the tick ramp. It never introduces a construction of its own.

| Component | Construction | Behavior |
| --- | --- | --- |
| Notice | A header-band plate in the top band's trailing section, 320–480 dp wide: status symbol, Lowpixel 14 dp text, optional action glyph | Enters with `notice.enter` and holds 4 s, or 8 s when it offers an action, then leaves with `notice.leave`. Errors and unresolved states persist until resolved. At most three stack downward, newest first; further notices queue. Never covers the crosshair, the touch controls or the prompt bar |
| Search | A recessed text field with the search symbol in its leading slot; results are list rows with their category path, such as `SYSTEM - DISPLAY`, in `text.heading` | Results update with every keystroke and match labels, values and language-table synonyms. Accept moves to the setting, and its plate brightens once with `value.change`. An empty search names what was searched |
| Filter tag | A small header-band plate with a close cross; set tags rest at 0.60, unset tags at 0.28 | Accept toggles. The prompt bar offers Clear filters while any tag is set |
| Countdown confirmation | The modal with a tick ramp under the body that loses one tick per second, and the remaining seconds in `marine.value` | Display changes use 15 s. Keep leads and Revert trails; timing out reverts. Accept from any device keeps |
| Binding conflict | The modal naming the action that already holds the input, with its category path | Swap, Clear the other binding, or Cancel. Cancel restores the capture state |
| Transfer progress | The progress bar with the transferred size, rate and time remaining in Profont 13 dp | First-run data setup and downloads. Pausing, suspension and restarts keep progress |
| Network quality | Four bars rising like slider ticks, colored by the worse of ping and loss, beside the ping in ms | Four bars to 60 ms, three to 120 ms, two to 200 ms, one above; each 2% of loss removes a bar. Server rows, the scoreboard and the Competitive HUD |
| Device status | The controller symbol with a three-cell battery | Shown when the platform reports the charge; a notice at 10% |
| More actions | A popover of the actions the prompt bar cannot show, each with its glyph | Opens from the More prompt; Back closes it |
| Undo | A notice with an Undo action after a reversible reset or removal | Offered for 8 s. Deleting a save still uses the confirmation modal |
| Empty state | A section heading, one Lowpixel sentence saying why the list is empty, and an action row that fixes it | For example, no saved games offers New game |
| Loading rows | Row plates at rest opacity without labels | Content replaces them with `row.reveal`. No shimmer sweep and no spinner over content |
| Crosshair editor | A live preview over a captured scene tile, the stock crosshair set in a grid in the stock cycle order, a size slider (24–72 dp: the five stock sizes and steps between), the stock menu's eight swatches (white, red, orange, yellow, green, cyan, blue, pink) as small cut squares, and opacity and hit-marker values | Section 13.9; a color change applies at once |
| Stick response | A plot of the dead-zone disc, the outer threshold ring, the response curve and the live stick position | Section 13.5 controller settings |
| Palette preview | Team colors and the item color code shown side by side under the selected color-vision palette | Section 13.8 |
| Text backing | An opaque local plate in the text's own row shape, black at 0.85 | The accessibility backing option |

Notices, popovers and the wheel have motion tokens in section 8. Every
component takes focus, prompts and touch targets from sections 13.4–13.6.

### 13.14 Patterns to avoid

- Storefront, advertising or engagement patterns; nagging dialogs.
- Generic platform components: rounded cards, floating action buttons, pill
  chips, toggle switches or bottom sheets with rounded corners.
- Emoji or font glyphs standing in for symbols.
- Gestures without a visible alternative.
- A controller-driven cursor in menus.
- Portrait layouts that shrink the landscape composition instead of
  rearranging it.

## 14. HUD and in-game overlays

The HUD is its own family (section 2). It never borrows the menu's 45-degree
plates: its stock construction is rounded plates, sheared gauges and bevelled
fills. This section records the stock HUD and defines how openQ4 lays it out,
extends it and keeps it legible.

openQ4 also runs The Awakening, the unreleased expansion (`q4xbase`). Its
content adds three weapons, powerups, crosshairs and vehicle displays, and the
paragraphs marked *The Awakening* cover them, with openQ4 designs where the
expansion ships placeholders or no art at all.

### 14.1 Composition and order

The game draws the weapon's scope display first, then the crosshair, then the
HUD, then the multiplayer HUD and its overlays. A vehicle's own display
replaces the HUD while the crosshair stays. The objectives display hides the
HUD, the crosshair and any vehicle HUD but not a zoomed scope, and an objective
notice also hides a vehicle HUD; the held scoreboard hides the whole HUD,
crosshair included. The HUD runs on real time and keeps animating while the
game is paused; the objectives display and the weapon displays run on game
time.

Shear and rotation pivot on each element's own rectangle center, and an
element's transform replaces its container's instead of composing with it
(section 6). Gauge plates and numerals carry the 0.22 shear; icons and weapon
renders stay upright.

### 14.2 Stock layout

Positions are source units on the 4:3 canvas.

| Element | Rect (u) | Shown |
| --- | --- | --- |
| Ammo gauge | 13,420 125x59 | Always |
| Health gauge | 190,420 125x59 | Always |
| Armor gauge | 326,420 125x59 | While armor is above zero |
| Weapon strip | Slots at y 370–396 centered on x 320; name box y 392–432 | For 1000 ms after a weapon change; hidden at once when the player fires |
| Pickup message | 21,401 230x24 | For 3000 ms after a pickup; the next pickup replaces it |
| Radio chatter | x 468–633, y 5–52 | While a transmission plays |
| Obituaries (multiplayer) | 351,5 284x70 | Four lines, two in Tourney |
| Powerups and carried flag (multiplayer) | Trailing column, x 557–641, y 260–474 | Per powerup |
| Boss bar | Plate x 227–413, y 42–89; 20 u toward the trailing side in a vehicle | During boss fights |
| Exit, quicksave and mission-failed notices | Top center and center | Timed |
| Hit indicator | Full canvas, rotated about its center | For 600 ms after each hit |
| Interactive brackets | The focused world GUI's projected bounds plus 10 u | While a world GUI has focus |
| New-objective notice | x 0–247, y 31–208 | For 2.1–5.1 s |
| Objective-complete notice | x 390–640, y 53–119, below the radio | For 2.1–5.1 s |
| Objectives display | The leading 45%, centered vertically | While the scores button is held |

The ammo gauge stands apart from the health and armor pair: the gaps are 52 u
and 11 u. The Strogg HUD places its 129x67 u gauges at 10, 190 and 330 u on
y 419. Remastered centers the three gauges as one status bar (section 14.4).

### 14.3 Gauges

- **Legacy parity (11 October 2026 review).** Keep the group centered, with
  the legacy gauge sizes, gaps, vertical positions and internal offsets.
  Match the background's colour, saturation and transparency after texture
  modulation, not just the GUI tint. Marine fill interiors multiply that tint
  by 148/255, Strogg by 171/255, and armor by 175/255, with the original soft
  bevel. Armor dividers modulate alpha to 155/255; do not paint dark lines or
  add an extra opaque rim. The black backing remains at 0.50 by default.
- **Fill.** The fill is anchored at its trailing end and drains toward the
  numerals. The drained part stays visible as the track, the same art at 20%.
  Below about 23% only the band above the notch remains.
- **Ammo.** For a weapon with a clip of two or more, the clip count is large
  (legacy textscale 0.5), the reserve small (0.32 Marine, 0.26 Strogg),
  and the fill shows the clip.
  Otherwise the total shows alone and the fill shows it against the maximum.
  Weapons with unlimited ammunition show no number and a full fill. The
  selected weapon's original installed bitmap (its `mtr_icon`, section 14.16) sits
  behind the clip count at #C7BD78, 0.40, where the stock draws its
  right-aligned texture into the 106x26 u window, and changes with the weapon;
  the multiplayer gauge has none.
  Multiplayer always shows the total and tints the ammo fill in the weapon's
  color at 20% and 50%.
- **Numerals.** As corrected on 9 October 2026, the on-foot status bar uses
  the legacy TTF layout exactly: Chain for Marine, r_strogg for Strogg, whole
  proportional strings aligned right in the original text rectangles. Preserve
  the two-unit inset, atlas selection, glyph advances, line height, clipping
  and each Marine text window's own shear origin. The centered status-bar
  anchor and user HUD scale apply to the complete gauge group. Do not use
  tabular digit cells or a separately estimated text baseline.
- **Gains.** A rising value pops its numeral to 120% in white, settling to 100%
  and the readout color over 250 ms, and its icon flashes white. Damage never
  animates a gauge; it drives the hit indicator.
- **Low health.** Below 26% of maximum health (25% on the Strogg HUD) the
  health track and fill switch at once to #FF4C00, and an additive rim along the
  inside of the backing pulses its red between 100% and 50% at 2 Hz. Recovery
  restores everything at once.
- **EKG.** A trace added through the fill's own outline, beneath the fill: two
  beats per width moving toward the trailing edge at 37.5 dp/s (24 beats a
  minute), #161B0E, and #4C0D00 at low health. It is an instrument, so reduced
  motion keeps it moving.
  The product owner's 11 October 2026 performance review approves the installed
  legacy bitmap for this instrument. Repeat each complete texture with UV
  scrolling under the existing vector mask, retaining its tint, glow and speed.
  Marine uses `ekg.tga`; Strogg uses `s_ekg.tga` and `s_ekg2.tga` at 0.2 and
  0.38 widths/s. Do not extract or redistribute the installed images.
  - **Trace.** Draw the measured trace, never a smoothed curve. The baseline
    sits at 58% of the gauge height. Each 62.5 u beat has a triangular P wave
    rising 3.5 u (11.2–18.6 u), a 2.5 u Q dip, an R spike rising 17.5 u and an
    S trough 6.2 u below the baseline (22.7–29.2 u), then triangular T and U
    waves rising 6.2 u and 3.0 u (37.4–49.6 u and 51.8–58.6 u); the T wave has
    a short flat crest.
  - **Line.** A 0.95 u core with a halo falling from 0.24 to nothing over
    3.7 u. Added at the stock color it reads as a faint olive line, about 15
    levels over the drained track.

**Air (MP-06).** Below full air, show the remaining percentage in a small
rounded, 0.22-sheared instrument immediately above health. It shares the
Marine/Strogg fill and numeral families, a trailing-anchored fill and a localized
AIR label; at 25% or less its fill and number use the low warning colour.
The status anchor and HUD scale carry it in Remastered/Minimal. Classic uses
the same vector component on the bottom-attached stock canvas above its
health gauge; it overlays the original HUD rather than replacing it. Hide it
at full air, on death, during HUD suppression and in a vehicle. Values come
from the followed player's authoritative readout in multiplayer.

### 14.4 Remastered layout

**Screens.** The Remastered HUD keeps the 4:3 stock canvas as its center screen
and expands outward from it. At 100% HUD scale the center screen keeps the stock
proportions and fills the view's height in views 4:3 and wider, and its width in
narrower ones.

- **Horizontal expansion.** A view wider than 4:3 adds a leading and a trailing
  screen: 160 dp each at 16:9, 96 dp at 16:10 and 373 dp at 21:9 (64:27).
- **Vertical expansion.** A view narrower than 4:3 adds a top band and a bottom
  band: 16 u each at 5:4, and 329 u each in the 9:16 portrait view, where the
  center screen spans the width (370 dp at the 720x1280 dp portrait reference).
  Taller portrait screens deepen the bands. Top groups ride the top edge and
  bottom groups the bottom edge, with the view between them; on touch the bottom
  band is the thumb deck instead (section 13.6).

**Anchors.** Every group belongs to one of nine anchors: the four corners, the
four edge midpoints and the center. It keeps its stock offset from that anchor,
so at 4:3 the Remastered layout lands on the stock rectangles, apart from the
centered status bar.

- Horizontal expansion moves leading and trailing groups out with their edges.
- Vertical expansion moves top and bottom groups out with theirs.
- Center-screen groups stay centered.
- HUD scale grows or shrinks each group about its anchor.

| Anchor | Groups |
| --- | --- |
| Center | Crosshair, hit indicator, interactive brackets, aim text, awards and the respawn prompt, on the true projection center |
| Bottom center | The status bar: the ammunition, health and armor gauges with their stock gaps; the pickup lines above them; the weapon strip above those; a spectator's follow card in their place |
| Top center | Boss bar, timed notices, multiplayer notices and warm-up text, and in Classic the stock spectator lines; the Competitive match bar |
| Top leading | New-objective notice; the multiplayer standing, team panels and timer, or in Competitive the vote |
| Top trailing | Radio chatter on every HUD, vehicles included, with the objective-complete notice or a vehicle's panel below it; the kill feed |
| Leading edge | Objectives display, vote; Competitive team status |
| Trailing edge | Statistics; a Competitive spectator's second team list |
| Bottom leading | Chat and chat input |
| Bottom trailing | Powerups and the carried flag; HUD prompts |

**Status bar.** The three gauges, with the stock 52 u and 11 u gaps, form one
438 u group centered on the bottom edge: ammunition at 101 u, health at 278 u
and armor at 414 u on the 4:3 canvas. Positions never shift while armor comes
and goes. A vehicle's gauges and weapon panel take the status bar's place
(section 14.12).

**Room.**

- **Wide views.** By default the side screens stop growing at 21:9: edge
  groups stop at the 21:9 edges and the rest of the view stays clear. The HUD
  width setting (16:9, 21:9 or Full) sets that limit.
- **No overlap.** Groups never overlap. The chat box always clears the status
  bar vertically (section 14.9); below 16:9, where the leading screen is too
  narrow for it beside the status bar, it also rises clear of the weapon strip.
- **Side-screen groups.** They extend into the center screen only as far as
  their content needs, and never with a backing wider than their content.
  Notices, the kill feed and chat carry their backing inside their own
  outlines.

**Scale and inset.** HUD scale runs 50–200% independently of the menus, and
Couch and Handheld start at 125%. The player sets a safe inset of 0–10%, which
moves every edge anchor inward; the center anchor never moves. Elements keep
their stock shape and shear at every scale, and nothing stretches.
On a narrow screen the status bar's effective scale is limited to the available
safe width so all three gauges remain visible; this does not change the saved
HUD-scale preference. The other anchor groups retain the requested scale.

**Classic.** The Classic preset keeps the stock 4:3 rectangles centered in the
view, with no expansion: the gauges return to 13, 190 and 326 u, the pickup line
to the ammunition gauge, and the notices' back bars return.

### 14.5 Weapon strip

The stock strip lists the owned weapons in slot order with no gaps: blaster
(gauntlet in multiplayer), machinegun, shotgun, hyperblaster, grenade launcher,
nailgun, rocket launcher, railgun, lightning gun, dark matter gun and napalm
launcher. Each 24.4 u plate sits on a 30 u pitch, black at 0.50 and sheared
about its own center, with an upright 18 u icon. The pending weapon's icon turns
#FF8000 inside a hard 1.6 u #B2CC80 ring, a closed outline the HUD uses although
the menus never do; the other icons are #A8A360 at 60%. A weapon without
ammunition keeps the dim icon under a red prohibition sign. The weapon name,
Marine 0.25 in #B0C891, is centered below. In multiplayer the icons keep the
weapon color code instead: the pending weapon at full alpha, the rest at 0.60,
and never the single-player orange. The strip appears in the frame of the change
without motion. Remastered moves the name above the slots and the strip above
the pickup lines (section 14.8); the wheel adds a second way to choose.

*The Awakening* gives the player thirteen weapons. The Core Cannon, the napalm
launcher and the Freeze Gun take its slots 10 to 12, after the stock ten, and
its HUD adds two strip plates for the last two. openQ4's strip lists them in
slot order like the rest, on the Strogg HUD too, where the expansion's copy
has no plates for them. This thirteen-weapon scope applies to the `q4xbase`
single-player campaign. Multiplayer keeps its own Quake 4 roster; the former
requirement for expansion multiplayer ammunition, accuracy and summary rows is
superseded by the project's single-player expansion policy.

### 14.6 Weapon wheel

The wheel is an openQ4 addition for controller and touch play (section 13.5).
It speaks the weapon strip's language in a ring: HUD plates with rounded 2 dp
corners and no 45-degree cuts, the strip's selection ring and colors, the
weapon color code, and the Marine gauge and scope constructions.

| Measure | Value at 100% HUD scale |
| --- | --- |
| Ring | Inner radius 104 dp, outer radius 232 dp, vertically centered |
| Position | `ui_weaponWheelOffset` is an archived integer percentage of viewport width measured from the projection center: default `27` keeps the existing 77% horizontal position, `0` centers, negative moves left; range −40 to +40. Settings > Game > Controller exposes the value beside wheel sensitivity. On narrow views, fit the isotropic wheel and clamp its center inside the view without changing the saved preference. Apply the setting to Marine, Strogg, multiplayer and Classic wheels |
| Slots | Every weapon of the game mode in strip order, clockwise from the top. Unlike the compacted strip, positions never move: an unowned weapon keeps its segment as an outline at 0.15 and cannot be selected. Quake 4 has eleven positions and *The Awakening* thirteen; the segments narrow to keep one ring |
| Segment | Black at 0.50 with 2 dp corner radii and 6 dp gaps of constant width |
| Contents | The weapon glyph of section 14.16 at the family's icon color and 60%, the weapon number in Lowpixel 12 dp at the inner edge, and the reserve in Chain 17 dp |
| Ammunition band | A curved gauge along the outer edge, 14 dp deep, in the weapon's color code with the Marine fill construction: track at 0.20, fill at 0.35 with its bevel at 0.45, eight cells between soft dividers at 0.32, filled in proportion to the reserve |
| Bezel | A tick ramp just outside the ring: 120 fine ticks every 3 degrees from 247 to 252 dp at 0.18, and a long tick at each segment boundary from 242 to 253 dp at 0.50, in #B3D06E |
| Selected | The icon turns the family's selected color (#FF8000; Strogg #FFCC00) and the strip's ring outlines the segment, 2.4 dp of #B2CC80 (Strogg #F2AD0B). The backing rises to 0.70, an additive #616F26 light at 0.35 fills the segment, and the segment extends 8 dp outward |
| Previous weapon | A notch on the inner edge marks the weapon a tap returns to |
| No ammunition | The dim icon under the red prohibition sign. Releasing on it keeps the current weapon and gives the refusal feedback of section 13.5 |
| Hub | A black 0.55 disc of 86 dp inside the machinegun scope's double ring (86 and 80 dp, #B3D06E at 0.35) and its 16 compass dots at 93 dp, turned so the bright dot faces the selection. The disc is a signal screen: the briefing's torn video bands at their 0.06 rest level and additive scanlines at 0.11 roll down it |
| Center | The weapon name in Marine 12 dp and #B0C891, on two lines when it would not fit on one, and the ammunition as `clip / reserve` in Chain 21 dp, over the signal screen. Nothing else: the radio's waveform belongs to the transmission display, not to a weapon selector. A pointer on the inner ring faces the selection |
| Backing | A black radial wash at 0.35 out to 280 dp over high-quality scene soft focus: separable Gaussian sigma 8 dp, saturation 0.94, eased with the wheel blend. Filter the full wide/tall scene before the HUD and wheel artwork so their vectors and text stay sharp. Apply in SP and MP without changing MP simulation speed; respect the opaque-backing fallback and submit no focus pass once closed |

**Selection.** A stick selects the segment under its angle beyond 35%
deflection, with 4 degrees of hysteresis at boundaries; inside the dead zone
the selection holds. The mouse moves an invisible selector from the center and
selects once it passes 32 dp. On touch the drag starts at the weapon button.
Releasing equips the selection; Back, or the wheel button pressed again,
closes without switching. A tap shorter than 180 ms swaps to the previous weapon
without opening the wheel.

**Transitions.** Each family's wheel opens, selects and closes with its own
signal: the Marine wheel as a transmission, the Strogg wheel as a translation.
Both come from section 8, "Translation and transmission". They never delay
input: the wheel selects from its first frame, plates and icons are readable
150 ms after it opens, and the rest is decoration.

| Transition | Marine: transmission (the MCC briefing) | Strogg: translation (the credits) |
| --- | --- | --- |
| Open, `wheel.open` | The hub powers on from a slit (55% by 6%) to full size over 150 ms. Its double ring flashes white and settles over 50 ms. Segments deploy clockwise from the top, 8 ms apart, each fading in and growing from 90% over 90 ms. A static burst rises to 0.40 at 150 ms and decays over 500 ms. The name tunes in from a 2 dp line over 200 ms, arriving white and settling to #B0C891 100 ms later over 100 ms. The hub jolts to 90% and back at 250–350 ms | The plates boot in scrambled order, one every 12 ms, each under an additive #FFD06F flash that fades over 150 ms, after the stroggification monitor's cascade. The rings spin up over 300 ms, the circuit paths trace out from the core over 150 ms, and the grain settles from 0.60 to 0.20. Every label appears in runes, then translates: the ring's readouts clockwise from the top, 18 ms apart, as list rows, and the name as a heading |
| Select, `wheel.select` | A cut: a static burst of 0.40 decaying over 300 ms, the name re-tuning from a 2 dp line over 120 ms and settling from white, and the selection ring flashing from white to #B2CC80 over 150 ms | The new name shows in runes and translates with the heading recipe: after 40 ms both copies pop 6% larger and settle over 300 ms while the runes cross-fade into R_Strogg, and the scan bar slides in behind the name over 90 ms `accel(36, 54)`, resting with its head about 12 dp past it and dimming over 300 ms. The ammunition translates as a name, 15 ms later over 150 ms, reaching white and settling to #FCFFC8 |
| Close, `wheel.close` | Sign-off: the segments drop out over 60 ms while the hub collapses to the slit over 150 ms under a static burst falling from 0.80 | The plates flash and drop out in scrambled order, 8 ms apart over 60 ms each, then the core, rings and grain fade over 120 ms |
| Reduced motion | An 80 ms opacity change; the name changes at once | The same; no runes, flashes or bar |

Selection is silent, and equipping plays the stock weapon-switch sound. The
game keeps running, and multiplayer is never slowed.

**Quick chat.** The same component with up to eight localized phrases in
Lowpixel 16 dp instead of icons. The center names the channel, All or Team,
and releasing sends the phrase. On the Strogg wheel the phrases translate as
list rows when it opens.

Multiplayer holds it open on its own control, D-pad Down and V by default,
alive, dead or spectating. It opens on Team in a team game and on All
otherwise; in a team game the zoom control switches the channel, and the hub
shows that control's key and Channel under ALL in #FFFF8D or TEAM in your
team's text color. The phrases, clockwise from the top: Attack!, Defend!,
Need backup!, Enemy incoming!, On my way!, Thanks!, Nice shot! and Good
game!, sent in the sender's language. Unselected phrases read at 0.60, the
selection at full alpha in the selection construction above. Releasing
inside the dead zone sends nothing.

**Strogg HUD.** On the Strogg HUD the wheel takes the Strogg construction, not
a recolored Marine ring.

- **Plates.** Each segment is an angular plate: a raised leading tooth with
  30-degree shoulders, a chamfered trailing corner and a notched inner edge,
  all straight-edged. The ring twists 4 degrees backward, the polar form of
  the Strogg shear.
- **Reserve.** Eight slanted cells along the outer edge in #FF9000, under a
  masked grain layer that drifts over the plates like the gauges' static.
- **Core.** An octagonal plate with a lower tab, after the boss bar, circled
  by two dashed rings that turn at the Strogg terminals' 0.2 and 0.5 turns a
  second in opposite directions. Thin circuit paths join the core to every
  owned plate and brighten for the selection.
- **Text.** Text is R_Strogg with its 11.4-degree backslant. Every label is
  drawn twice, in runes and in R_Strogg, so it can translate (section 8): the
  rune copy is 1.2 times the R_Strogg cap height, on the same baseline.
- **Colors.** They follow the Strogg strip: icons #F59512 at 60%, the
  selection #FFCC00 in a #F2AD0B ring, readouts #FCFFC8, and the scan bar
  additive #FF8000.
- **Reduced motion.** The rings and grain stop, and names change at once.

**Multiplayer.** Multiplayer draws every team's HUD in the Marine family, and
its wheel is the Marine wheel with these changes:

- **Roster.** The multiplayer weapons in strip order, the gauntlet in the
  blaster's place: eleven positions. The Awakening's thirteen-position wheel
  belongs to its single-player HUD; there is no expansion multiplayer module.
- **Icons.** The weapon color code, as in the multiplayer strip (section 14.5):
  the pending weapon at full alpha and the rest at 0.60, never the single-player
  orange.
- **Ammunition.** Multiplayer weapons have no clips, so the segments and the hub
  read the reserve alone. The hub holds the name and the reserve and nothing
  else.
- **Team color.** Your team's text color takes the selection ring and its flash,
  the selected number, the bezel's ticks, the hub's double ring, the compass's
  bright dot and the pointer: #AAE355 for Marines and #FF8E00 for Strogg, whose
  selection light is additive #6B3A08 instead of #616F26. Deathmatch and Tourney
  keep the stock olive, #B2CC80 and #B3D06E.
- **Timing.** The game is never slowed, and releasing equips at once.

### 14.7 Crosshair and hit feedback

- **Crosshairs.** Every weapon has a default crosshair, and every crosshair and
  context cursor is a vector reconstruction (section 14.15).
  - **Custom set.** Twenty entries, nineteen designs, cycling in the stock
    order; the picker steps both ways.
  - **Size.** 16, 24, 32, 40 or 48 u (24–72 dp).
  - **Color.** Free; white by default.
  - **Remastered editor.** It adds sizes between the stock five, offers the
    stock menu's eight swatches as presets, and applies a color change at
    once. On PC the stock applied it at the next weapon change.
  - **Vehicles.** A vehicle always shows its weapon's crosshair.
- **Hit feedback.** On every hit the stock crosshair grows 10 u and returns
  over 150 ms; single player also tints it red for 100 ms, and multiplayer
  plays a hit sound. openQ4 keeps this as the default feedback.
- **Hit markers.** openQ4 adds optional multiplayer hit markers, off in
  Classic. They are four 6 dp ticks on the diagonals, starting half the
  crosshair size plus 4 dp from the center, so they never cover a design such
  as the lightning gun's diagonal darts. They are white for a hit and
  `status.error` for a kill, for 150 ms.
- **Damage direction.** The stock hit indicator is a soft #FF1A1A dome of
  light about 227 dp from the center, rotated toward the damage source, fading
  from 0.70 to zero over 500 ms. openQ4 scales it with the HUD, caps it at 0.40
  under the reduced-flashing option, and adds a chevron at its peak so
  direction never depends on color.
- **Context states.** Use, talk, world-GUI, vehicle, locked and friendly
  cursors keep their stock colors and short animations (section 14.15).

### 14.8 Messages and notices

- **Pickups.** The stock shows one line: a rounded black 0.50 bar, the item
  icon and its name in Marine 14 dp, for 3000 ms, replaced by the next pickup.
  Remastered centers up to two lines over the status bar, newest nearest the
  gauges, and merges repeats into one line with a count ("ARMOR SHARD ×3"). The
  weapon strip sits above the pickup lines with the weapon name above its slots,
  so the two never overlap. Classic keeps the stock line over the ammunition
  gauge.
- **Objectives.** The objective notices and the objectives display are
  section 14.11.
- **Radio chatter.** "Incoming" and "transmission" in Marine 0.20 on a rounded
  bar, their baselines at 19 and 26 u. Remastered anchors it to the top-trailing
  corner on every HUD: on foot, on the Strogg HUD and in every vehicle, turret
  and ride. It therefore always sits at the right-hand edge, in the trailing
  screen on a wide view and in the top band in portrait. A vehicle panel that
  shares the corner, such as the space cannon's ship panel or the MCC readout,
  stacks below it. Where a vehicle's own HUD has none (the air-defence cannon,
  the MCC side gun), Remastered adds it, so no line goes unseen. Classic keeps
  each GUI's stock corner of the 4:3 canvas.
  - **Timing.** It appears and disappears in one frame and stays up for the
    voice line plus 150 ms, or 5 s.
  - **Classic waveform.** Its motion is the additive #FF8000 carrier waveform, the
    stock `waveform.tga`: a recorded voice trace with a 4-texel core and
    bursts of spikes up to 32% of its height above and below it, flat in 37%
    of its columns. The product owner's 11 October bitmap exception uses the
    installed image directly; a generated zigzag does not read as speech.
  - **Mask and Classic scroll.** A right trapezoid 19.2 u wide at the top and 23.1 u
    at the bottom, its leading side leaning with the plate. The trace scrolls
    toward the leading edge at 24.6 u/s.
  - **Transmission graph.** Classic keeps its measured bitmap and canned table.
    The product owner's 11 October 2026 follow-up replaces the approximate
    preset with the installed legacy `waveform.tga`. Remastered repeats that
    bitmap at 0.6 texture widths/s with the stock 21-sample `wavTable` centred
    UV scaling, including its duplicated endpoint and separate wrap sample.
    The full 41x25 u radio image sits under the existing trapezoid with the same
    additive colour; do not squeeze it to the mask's narrower bounds. Do not
    extract or redistribute the image. It indicates transmission activity,
    not measured loudness. Chatter events still show/hide the display; Reduced
    Motion holds the pattern still. No per-frame PCM bindings are required.
  - **Squad emblem.** When the line addresses the player, the player's squad
    stencil (#FFFF00) and its black 0.40 plate shrink into place beside the
    bar over 500 ms, the stencil from 42x39 u to 22x22 u. Every new line
    restarts the shrink. Quake 4's is Rhino's, traced from `radio_rhino`;
    *The Awakening*'s player squad is Falcon, whose stencil is built the same
    way from its patch (section 14.16).
  - **Strogg HUD.** It uses R_Strogg 0.16 labels and a #FFCC00 waveform in a
    backslanted mask.
- **Timed notices.** EXIT under a chevron that rises 11 u with `accel(500, 500)`
  while fading from 0.50, for 5000 ms; "Game Saved..." for 2000 ms, fading over
  1000 ms (the stock drew two copies on foot; openQ4 draws one). The HUD's
  "Mission Failed" sequence has no stock sender (section 14.11).
- **HUD prompts.** Where the HUD names a control, it uses the menus' prompt
  construction (section 13.4) instead of a bracketed line such as
  "[ Press JUMP to exit ]", which names an action rather than its control. Each
  item shows the active device's glyph, then the action in Lowpixel 16 dp: a
  keycap plate with the 45-degree cut naming the bound key, the controller's
  button from the active glyph family, or on touch the symbol of the overlay
  control that performs it. Several actions share one sheared HUD plate at black
  0.50, never wider than its content, contextual actions leading and Exit
  trailing, as Back trails the prompt bar. An action that cannot run shows its
  state instead: a lock and Vehicle locked in #FF8000. The prompt follows the
  device in the frame it changes and sits at the bottom-trailing corner. Classic
  sets the same prompt at the stock line's place, with the stock drop shadow and
  no plate.
- **Interactive brackets.** Open #AFDE90 brackets around the focused world GUI,
  labelled "Interactive"; the terminal highlight of section 13.5 uses them.
- **Subtitles.** The stock has none. openQ4 subtitles use Lowpixel 17 dp in
  white on a text backing at 0.60, with the speaker's name in #B0C891 (the words
  start after it; a name wider than half the line is left out), at most two
  lines in the lower third of the cinematic picture area, and never cover the
  crosshair. On a screen narrower than 4:3, where two lines fit below the 640x480
  cinematic artwork, they hang 12 u below it instead, clear of title text drawn in
  the picture or its bands. Where only one line fits there, a lone caption pages a
  line at a time below the artwork, and the newer of two overlapping captions takes
  the matching line 12 u above the artwork.

### 14.9 Multiplayer HUD

The multiplayer HUD draws over the gauges. The stock 1.4.2 game runs six
modes: deathmatch, Tourney, Team DM, CTF, Arena CTF and Dead Zone; one-flag CTF
is in the HUD but cannot be selected. openQ4 adds one-flag CTF, Arena One Flag
CTF, Duel, Clan Arena, Freeze Tag and Red Rover. A free-flying spectator sees
the stock HUD without the gauges, kill feed or powerups; a following spectator
sees the followed player's HUD.

| Element | Modes | Stock construction |
| --- | --- | --- |
| Leader and own rows | Deathmatch | Two 242x28 u slabs at the top-leading corner, black fading to a faint cut that leans with the 0.22 shear at 70–73% of the width, each with a #FFFF8D band at 0.40 with bright rims; rank, name and score in Lowpixel 0.22 with a drop shadow. Row 1 is the leader; row 2 is you, or second place when you lead or spectate |
| Timer | All | A #FFFF8D stopwatch and the time in Lowpixel 0.25 below the rows or panels, counting down M:SS. ∞ replaces it when the match is untimed (the stock default, so for the whole match), in warm-up, sudden death and review. "Warmup N" appears during the countdown and each Tourney arena's warm-up, "Pre-game N" only during the tournament-wide countdown. Nothing changes as time runs out |
| Team panels | Team modes | Two 102x58 u slabs in the team colors at 0.80 over black, with bright top and bottom rims fading toward the center and a sheared cut; faction mark and score in Lowpixel 0.36 without a shadow; your team always on top |
| Flags | Capture the Flag, Arena CTF | 28 u team flags sheared 0.22 beside the panels; "!" when taken, pulsing 28–34 u once a second, "?" when dropped |
| One flag | One-flag CTF | Not selectable in the stock. A neutral #FFFF8D flag joined to both panels by a gray arc; when taken it pulses and its color swings to the carrier's team |
| Dead Zone | Dead Zone | Team panels whose score jumps to pure #00FF00 (Marine) or #FF8000 (Strogg) on every control tick and relaxes to white, multiplied by 0.95 each frame |
| Tourney | Tourney | A bracket strip across the top (8, 4, 2 and 1 cells), a black 0.60 message bar with a 45-degree end, and round banners that slide 34 u in, hold and slide out over 500 ms; your arena aqua #5EB987 at 0.40, a finished arena blue #3E57B7 settling from 1.00 to 0.40. The last round is "Finals" on the banner and "Final Match" in the bar |
| Kill feed | All | Top-trailing, four lines (two in Tourney, own arena): attacker, a Quad mark when it applies, the weapon icon at 16.5 dp in the weapon color code, then the victim; a skull for suicides and world deaths, and a teal mark of inward chevrons, arcs and an X for telefrags; team modes color the names #AAE355 and #FF8E00. The top line holds 2000 ms and fades over 1000 ms, and lines leave one every 3.1 s. An over-long line loses the end of the victim's name; free-flying spectators see no feed |
| Notices | All | The frag notice ("You fragged …") in Lowpixel 0.36 at 81 u and the main notice (rank, warm-up, sudden death, flag events) in 0.32 at 102 u, #FFFF8D, centered; 2000 ms hold, 500 ms fade |
| Spectator and warm-up text | All but Tourney | Two centered #FFFF8C lines at 123 and 141 u, breathing over 900 ms: follow status and cycle keys, ready reasons, "STARTING GAME IN N" |
| Chat | All | Four lines at the leading edge from y 302, Lowpixel 0.25 #FFFF8D on a 15 u pitch, pre-wrapped at 547 u, each piece taking a line; team chat colors the whole message #AAE355 or #FF8E00; Q4TV viewers' chat is gray #C6C6C6; spectator chat reaches only spectators during play |
| Aim text | All | The aimed player's name and clan 20 u below the crosshair, with a teammate's health and armor; in over 200 ms after 100 ms, out over 500 ms |
| Awards | All | The earned medal repeated once per time earned, up to nine (then one medal and a count), centered at 143–175 u; 2500 ms hold, 500 ms fade, queued 3 s apart |
| Vote | All | The call and up to six field lines at the leading edge with the Yes and No keys; after voting, the tally replaces the prompt |
| Voice | All | You ("Transmitting...") at 263 u and one talker at 286 u, each on a bar fading from black 0.42 to 0.07 with a slanted end at 87%, with a 10x12 u additive #FFFF8D transmission trace (canned height) and the name in Lowpixel 14 dp |
| Statistics | All | Held on a key: kills, deaths, per-weapon accuracy and award counts on a soft backing at the trailing edge |
| Chat input | All | "SEND" in Marine over a dark plate at 0.80 with #FFFF8D entry text; all and team chat look the same. It takes 128 characters, shows about 35, and scrolls sideways under a `|` (insert) or `_` (overstrike) cursor blinking about every 267 ms |

Stock slips that openQ4 does not reproduce:

- The Marine viewer's dropped Strogg flag keeps pulsing.
- Flags pulse from the HUD's load until the game initializes them, and one
  can keep a stale "!" into sudden death.
- The Tourney kill feed never fades.
- The warm-up banner grows from a corner instead of sliding.
- "(Buy Zone)" shows everywhere.
- The Dead Zone flash is cut short on dedicated servers.
- Completion notices can stick (section 14.11).

**Remastered additions.** The stock lacks these; they use its vocabulary.

- **Objective points of interest (23 October 2026).** CTF and One Flag bases,
  including their Arena variants, use a compact flag in a 45-degree diamond;
  assault points and DeadZone scoring volumes use open square brackets in the
  same diamond. Keep permanent base anchors while flags travel. Distance is
  the only text: use solid/hollow pennants and upward/downward cues for flag
  availability, empty/filled/split zone cores for control, and a small padlock
  for artifact eligibility. Preserve faction colours without relying on colour
  alone for status. Keep outlines quiet and steady; no flashing or alert text.
  Place the straight-line distance below the symbol. Use a dark backing and text shadow over bright
  scenes. Off-screen bearings terminate inside a safe frame with outward
  chevrons, including targets behind the camera. Dim at the crosshair, suppress
  colliding distances and bound the visible set. Do not show carrier locations.
  Hide during menus, scoreboards, zoom, death and free-camera spectating.
  Apply HUD opacity, safe inset and bounded sizing. Both Classic and retained
  routes use the same geometry. In stereo VR, project head-relative bearings
  onto the compositor's physical HUD plane, including its controller-aiming
  height offset. Use the corrected tracked head origin and axis, independent
  of weapon aim and eye FOV. Keep symbols at HUD depth, quiet around the forward
  gaze, and inside the HUD's safe frame. Inactive VR uses the desktop projection.
  `hud_objectiveMarkers` is an archived client opt-out, defaulting to enabled.

- **You, marked without color.** Kill-feed and chat lines that involve you sit
  on a header-band plate at 0.30 with your name in white; your deathmatch row
  and scoreboard row carry the ◥ marker.
- **Respawn.** While dead, "Respawn" with the active device's glyph centered in
  the lower third, and a countdown when the server delays respawning.
- **Connection.** When the connection lags, the network symbol and the ping
  appear as a notice (section 13.13); Competitive shows network quality at all
  times.
- **Warm-up and votes.** Warm-up shows the ready count ("Ready 3/8") in the
  timer; a vote shows its remaining time as a tick ramp and the Yes and No
  tallies as cells, with the active device's glyphs, on a plate at the leading
  edge.

**Remastered top of the screen.** The stock builds its top elements from
full-height slabs that fade from the screen edge. It leaves notices, the
timer, chat and the kill feed with only a 1 u drop shadow, and it stacks the
center-top notices over one another. Remastered rebuilds the area from compact
HUD plates: rounded corners, the 0.22 shear, black at 0.50, never wider than
their content. The table records what each group shows.

| Group | Anchor | Remastered construction |
| --- | --- | --- |
| Standing (deathmatch, duel) | Top leading | Two 150x18 u plates, 3 u apart, from y 36: the ordinal (Lowpixel 700, the leader's in #FFBE23), the name (Lowpixel 16 dp, cut with an ellipsis at 76 u) and the score (Chain 20 dp, tabular, trailing). Row 1 is the leader and shows the frag limit ("24 /30"); row 2 is you, or 2nd place when you lead or spectate. Your row carries the stock #FFFF8D band and rims at 0.40 and the ◥ marker, and after the plate a signed gap: your lead over 2nd place, or your deficit to the leader. A tie for the lead adds a TIED tag |
| Team panels | Top leading | Two 150x22 u plates, your team first with the marker: a team block in the team color at 0.80 holding the faction mark, the team name in the team's text color, and the score in Chain 26 dp against the limit ("3 /5"; Dead Zone "47 /120"; round modes the rounds won) |
| Flags | In the team panels | The flag at the trailing end of each panel: at base at 0.45, taken filled with "!" pulsing, dropped dashed with "?". The carrier's name follows the panel in the carrier's team color |
| Dead Zone | Below the panels | The score flash of section 14.10, plus a ZONE plate naming the holding team with its color block, CONTESTED, or EMPTY |
| Round modes | In the timer | Clan Arena, Freeze Tag and Red Rover add alive-player pips after each panel when the mode reports them, and a ROUND n plate with the round clock under the timer |
| Timer | Top leading, under the standing or panels | A 70x16 u plate: the stopwatch in #FFFF8D and the time in Chain 19 dp, tabular. One readout per state: M:SS counting down; elapsed time beside ∞ when the match is untimed; WARM-UP with the ready count; STARTS IN and the countdown, stated only here; #FFBE23 in the last minute and #E46D56 with a 1 Hz pulse in the last ten seconds; SUDDEN DEATH with ∞; OVERTIME with the time added |
| Tourney | Top center, within the center screen | The bracket strip on plates at 0.55: names cut with an ellipsis before the score, scores in Chain, your row aqua #5EB987 at 0.40 with the marker, the winner of a finished arena checked and the loser at 0.50 (the blue settle stays, no longer the only cue). The message bar keeps its 45-degree end and names the round with one word throughout; the timer and a ROUND and ARENA plate sit under it |
| Center column | Top center | Frag notice, main notice, warm-up lines and awards stack in that order from y 81 and move down for each other with `page.enter` easing instead of overlapping; spectators get the follow card instead of the stock lines. Each line sits on its own plate at 0.35; rank ordinals take #FFBE23 instead of pure blue, red and yellow. The countdown is not repeated here |
| Kill feed | Top trailing | Trailing-aligned lines on their own plates at 0.35. Names longer than 84 u end in an ellipsis, so the victim is never lost. Your lines use the header-band plate at 0.30 with the marker, and a team kill carries a TEAM tag. The Tourney feed sits under the strip and fades like the rest; spectators see the feed |
| Match context | Top trailing, under the kill feed | openQ4's managed-match lines in the card construction: the phase heading, then series, timeouts, readiness and proposals. It collapses to its phase line and one summary while play is live, and the scoreboard holds the full record |

Positions never depend on which elements are visible: a group keeps its slot,
and only the center column reflows.

**Competitive (Remastered).** The Competitive preset (section 13.9) moves the
standing or team panels and the timer into one match bar at the top center: your
side, the timer, the other side. The kill feed, the center column, the powerups
and the managed-match card keep their places.

- **Match bar.** Three sheared HUD plates from y 6, 26 u tall and 2 u apart: a
  60 u timer plate at black 0.72 between two 122 u side plates at 0.55. Each
  side's outer end is a 25 u block. In team modes it is the team color at 0.85
  holding the faction mark, followed by the team name in the team's text color,
  and your team leads. In deathmatch and Tourney it holds the rank ordinal, the
  leader's in #FFBE23, followed by the name cut with an ellipsis: you lead with
  the ◥ marker, facing the leader, 2nd place when you lead, or your Tourney
  opponent. Each side's score sits at its inner end in Chain 21 dp.
- **Timer.** The time in Chain 15.5 dp with the Remastered timer's states, and
  under it, in Lowpixel 700 at 0.55, the limit (TO 30), the round (ROUND 3) or
  the arena (ARENA 2).
- **Under the bar.** Each side's state hangs from its inner end on a small
  plate: its flag (AT BASE, TAKEN with the carrier's name, or DROPPED) or the
  round modes' alive-player pips. The Dead Zone holder sits under the timer,
  with CONTESTED or EMPTY.
- **Network.** Network quality (section 13.13), the ping beside four bars,
  follows the bar's trailing end at all times.
- **Team status.** In team modes your team's list sits at the leading edge from
  y 150, under the team name in its text color: one 138x14.5 u plate per player
  on a 16.5 u pitch with the weapon icon, the name cut with an ellipsis, health
  in Chain (#E46D56 at 25 and below, #FFBE23 above 100) and armor at 0.60. Your
  row uses the header-band plate and the marker; the dead keep their row at 0.40
  under an ✕.
- **Readouts.** The status bar grows by 15% about the bottom edge, with the
  weapon strip and pickups above it, and the chat box keeps its 8 u clearance
  above it. The box never rises toward the team status: where it cannot span
  the space beside the status bar it is 200 u wide, or as wide as the space
  beside the widest strip allows, down to 120 u (4:3).
- **Vote.** The vote takes the top-leading corner that the standing left, clear
  of the team status.
- **Tourney.** The bracket strip leaves the top; the scoreboard's bracket tree
  (section 14.14) holds it.
- **Narrow views.** Below about 16:9 at the HUD scale in use, the kill feed's
  longest line would meet network quality, so the kill feed, the managed-match
  card and the lag notice step 38 u down, under the bar.

**Spectators (Remastered).** Spectators get a follow camera presentation instead
of the stock's breathing lines in the center column.

- **Following.** The followed player's status bar and powerups show, but not
  their pickup lines or weapon strip. In their place, directly above the status
  bar, sits the follow card: a 236x30 u HUD plate at black 0.58 with a 7 u block
  of the followed player's team color at its leading end (none in deathmatch and
  Tourney). It holds FOLLOWING in Lowpixel 700 at 0.58 over the name in Lowpixel
  13 dp, and at its trailing end the rank ordinal in #FFBE23 and the score in
  Chain. Chevrons beside the card's ends mark the cycle, and under it the active
  device's prompts read Next, Previous and Free camera.
- **Free camera.** The status bar, crosshair and powerups go, and the card drops
  toward the bottom edge, reading SPECTATING over Free camera with Follow a
  player and Scores prompts. Players in view carry name tags: a small plate at
  0.50 with a 4 u team-color block and the name, pointing down at the player.
- **Competitive.** Both teams' status lists flank the view, the Marines at the
  leading edge and the Strogg at the trailing edge, trailing-aligned. The
  followed player's row is lit in the team's text color at 0.22 with the marker,
  and held statistics take the trailing list's place. In deathmatch and Tourney
  the match bar's leading side is the followed player. Name tags add a health
  line.
- **Classic** keeps the stock lines: SPECTATING - FOLLOWING with the name, then
  the cycle and exit keys, in #FFFF8C at 123 and 141 u, breathing over 900 ms.

**Chat (Remastered).** The chat box and its input live on the leading side.

- **Placement.** The box sits on the leading edge and always clears the
  status bar vertically: its bottom is 8 u above the status bar's top edge,
  the gauges' top, 51 u above the bottom edge. Opening the input pushes the
  lines up; the box never lowers. Where the space between the leading edge and
  the status bar is at least 180 u (from 16:9 at 100% HUD scale), the box
  spans it. Where it is not, the box is 200 u (300 dp) wide and rises until its
  bottom is 150 u above the bottom edge, clear of the weapon strip too. The
  voice bars sit directly above the box.
- **Width.** Spanning the leading space, the box is as wide as the space
  beside the status bar, up to 300 u (450 dp): about 190 u (285 dp) at 16:9,
  covering the leading screen and the first 93 u of the center screen, and
  300 u at 21:9, within the leading screen.
- **Lines.** Four messages at rest, newest at the bottom, in Lowpixel 16 dp on
  a 20 dp pitch, with the stock timing. A long message wraps inside the box and
  keeps all its lines together, up to eight lines in all; the stock let one
  message's pieces push out its own first line.
- **Line backing.** Each line sits on its own sheared HUD plate at black 0.35,
  as wide as its text. Nothing wider darkens the view.
- **Line content.** A line reads name, then message.
  - Team messages open with a TEAM tag in the header-band construction and
    the team's text color, and their text takes that color.
  - All-chat text is #FFFF8D, and names are white in deathmatch.
  - Server and vote lines carry the information symbol, with white text at
    0.70.
  - Your own lines use the header-band plate at 0.30 and the ◥ marker.
- **Input.** Opening the input adds the entry field below the lines, with a
  prompt row between them, and shows up to eight lines of history while it is
  open, as many as the space above allows.
  - **Field.** The stock chat plate: black at 0.80 with #CCCC51 rails along
    the leading side, the lower-leading cut and the bottom.
  - **Channel.** A channel tag replaces the stock SEND label: ALL in #FFFF8D,
    or TEAM in the team's text color. The channel switches while typing.
  - **Entry.** Lowpixel 16 dp in #FFFF8D with the text caret. Once 80% of the
    stock message limit is used, a count of the remaining characters appears
    at the trailing end.
  - **Prompts.** Send, Channel and Cancel, with the active device's glyphs.
  - **Keyboards.** Controller and touch players get the platform or on-screen
    keyboard (section 13.5).
  - **Room.** The history stands from the input's prompt row up to 6 u under
    the leading group above the box (Competitive's team status, or the vote),
    and shows only whole messages: a message the room would cut fades out.
    Where the box is too narrow for the three prompts they wrap onto a
    second row.
- **Classic.** Classic keeps the stock lines at 8,302 u and the stock input.

**Voice (Remastered).** One plate per talker stacks directly above the chat
box, newest nearest it: a sheared HUD plate at black 0.50 holding the
transmission trace (section 14.8) in the talker's team text color, or #FFFF8D
in deathmatch, then the speaker symbol and the name. Your own transmission
reads YOU on the header-band plate at 0.30 with a microphone. The approved
performance fallback uses the same preset activity pattern as radio, with
40 visible columns and no per-frame audio-level bindings. Talker visibility,
names and team colours continue to follow game events. Reduced Motion holds
the pattern still. Restoring the multiplayer microphone capture,
codec and playback backend is a separate unfinished integration dependency.

### 14.10 Alarms and motion

| Loop | Measured | Reduced motion |
| --- | --- | --- |
| Low-health rim | Red 100–50% at 2 Hz | Steady at 100% |
| Missile warning | 70–30% at 2.5 Hz | Steady at 70% |
| Hit indicator | 0.70 to zero over 500 ms | Kept; capped at 0.40 with reduced flashing |
| Boss shield warning | 100–50% at 2 Hz | Steady at 100% |
| Carried flag | Grows 22 to 28 u over 150 ms, back over 500 ms | Steady at 28 u |
| Flag taken | Grows 28 to 34 u over 250 ms, back over 750 ms | Steady at 34 u |
| Exit chevron | Rises 11 u with `accel(500, 500)` and fades once a second | Static |
| One-flag color | Neutral to the carrier's team over 250 ms, back over 750 ms | Steady team color |
| Dead Zone score | Jumps to the team color on each control tick and relaxes to white | A steady team color for 1 s |
| Timer, last ten seconds | #E46D56 with a 1 Hz pulse (Remastered) | Steady #E46D56 |
| Tourney arena finished | Blue 1.00 to 0.40 over 500 ms | Immediate 0.40 |
| Radio/voice waveform | Installed legacy trace and height table, 1667 ms loop | Stationary pattern |
| Vehicle warnings | 2 Hz hull and shield, 1 Hz electric | Steady warning colors |
| Spectator text | 900 ms breathing | Steady |
| EKG | 37.5 dp/s | Kept: it is an instrument |

Every HUD alarm pairs its motion with a static cue: the color change, the
symbol or the text.

### 14.11 Objectives display

**Stock.** Holding the scores button shows the objectives display; releasing
it closes it at once, and nothing plays either way. The game keeps running
and the player keeps control. The display hides the HUD, the crosshair and any
vehicle HUD, but a zoomed weapon's scope stays.

| Part | Stock construction |
| --- | --- |
| Back bar | A black 0.80 gradient covering the leading 45% of the screen: opaque to 190 u, half at 285 u, clear by 380 u. The rest of the view is not darkened |
| Heading | ">>MISSION OBJECTIVES" in Marine 0.25, #FF8000, tracking -1, 8 u above the first entry |
| Entries | At most the three newest objectives, newest at the top, on a 143 u pitch, the whole list centered on the canvas's vertical center. Older objectives are unreachable, and completed ones leave the list |
| Entry frame | An open plate 600x112 u with a 600x20 u foot in #B0CD6B at 0.40: leading rail, a lower-leading cut through the whole foot, a bottom rail, and a fill dissolving toward the trailing side (full to 30%, gone by 56%) |
| Entry contents | The marker; the title in Lowpixel 0.20 white on one line (overflow is dropped); a 134x100 u screenshot with a 1 u black border; the description in Lowpixel 0.20 #D0DEB6 wrapping at 178 u, seven lines at most |
| Serial | "SMC MILCOMM 45B-43556-BGX7J54" in Marine 0.14, #D9E7BF at 0.25, 12 u below the last entry |
| Empty | One frame with a static screenshot and "No current mission objectives." in Lowpixel 0.31 |
| Entry motion | 0–150 ms: the back bar slides about 220 u in and fades to 0.80, frames fade to 0.50. 150–250 ms: frames settle to 0.40 while heading, text, markers and screenshots fade in. No exit animation |

The Strogg display keeps the geometry and timing with #FF9000 frames, #FF9900
markers, R_Strogg headings and a "STROGG NET" serial.

**Remastered.** The construction stays; the limits go.

- Every active objective is reachable. Three entries show at the stock pitch,
  and the list scrolls beyond them with the right stick, the wheel or a drag;
  newest stays first.
- Completed and failed objectives move to a COMPLETED section below the active
  ones: title only, at 0.40, with a check or cross symbol, newest first.
- Titles wrap to two lines and descriptions wrap without a line limit; nothing
  is dropped. The serial rises to the 13 dp type floor.
- An Objectives setting chooses hold (stock) or toggle; toggle adds a Close
  prompt. On touch the objectives symbol in the top band toggles the display.
- Reduced motion replaces the slide with an 80 ms fade.

**Notices.** ">>NEW OBJECTIVE" sits at the top-leading corner: the back bar
widens in, the frame grows from a strip (150 ms), then the heading, the title
(Lowpixel 0.20 white, two lines of 134 u) and the screenshot fade in (100 ms).
It never shows the description. ">>OBJECTIVE COMPLETE" sits at the top-trailing
corner below the radio chatter, with no screenshot. A notice samples the
player's position 2 s after it appears and closes once the player has moved
64 units from there, or after 5 s, with a 100 ms exit; new-objective notices
wait until 5 s after spawning. The stock closes only the most recent kind and
plays the completion click twice; openQ4 closes each notice on its own and plays
the click once. A notice also hides a vehicle HUD while it is up.

**Remastered notices.** Remastered anchors ">>NEW OBJECTIVE" to the leading
screen and ">>OBJECTIVE COMPLETE" to the trailing screen, below the radio
chatter. The completion notice is mirrored: its rail and cut sit on the
trailing side, its fill dissolves toward the center and its text aligns to
the trailing edge.

- **No back bars.** The stock back bars darkened 247 u of the view from the
  screen edge; Remastered drops them. Each plate carries a black 0.50 backing
  inside its own outline and fade, and the headings take the drop shadow.
- **Width.** Plates are 200 u wide instead of 279 u: full to 66%, half at 80%,
  gone at the trailing end. At 16:9 the new-objective text and screenshot
  therefore end within the first tenth of the center screen.
- **Unchanged.** Entry, exit and timing are the stock ones without the bar.
  Classic keeps the stock rectangles and back bars.

**Failure.** "OBJECTIVE FAILED:" in Marine 0.50 #FF0000 with the objective's
title in white below it, centered at 200–280 u, appears at once and stays while
the view fades to black over 12 s; the restart menu follows. The HUD's "Mission
Failed" sequence has no sender in the stock and is not reproduced.
*The Awakening*'s HUD adds a failed-objective group with the objective's title,
a 3 s death fade and a commented-out compass. None of them has a sender, and
openQ4 reproduces none: the notice above already names the failed objective.

### 14.12 Boss and vehicle displays

**Boss bar.** A black 0.50 elongated octagon, 185.8x43.7 u with 14.2 u
45-degree chamfers and a 35.8 u tab at the bottom center, centered at the top
(x 227–413, y 42–89 on foot; 20 u further toward the trailing side in a
vehicle). Inside, the health bar is a 146x18.7 u hexagon with 45-degree points,
bevelled in its own tint, #EA8F15 at 20% track and 35% fill, draining toward its
leading point. Two C-shaped shield brackets, 6.9 u thick with chevron points,
face each other around it in #6A8C8A at 20% and 35%, each half draining from the
center toward its outer point; below 1000 shield points they turn #FF4000 and
pulse 100–50% at 2 Hz. A white health icon at 0.20 sits over the tab. Nothing
is sheared and there are no numerals.

**Walker and hover tank.** Two gauges on the Marine gauge art without its
numeral zone: the hull, labelled ARMOR, with ten cells, and SHIELDS, labels in
Lowpixel 0.14 #A8A360 at 60%, trailing-anchored fills in #B3D06E. At 25% hull, or
no shield, the gauge turns #FF3300 at 60% and pulses at 2 Hz. A weapon panel
with a 26 u upper-trailing chamfer shows the current weapon's outlined
silhouette at 80% and the other at 40%, a rocket count in Chain, and an
#FF9E1A charge bar while a weapon recharges. On entry, ">>MACHINEGUN
INITIALIZED..." style lines wipe open in Marine 0.22 #FF8000 inside moving
#FF8000 brackets, turn white and fade over 5.5 s. Hits flash an additive
#550000 edge vignette out over 500 ms; electrical damage shows red curved
brackets and a lightning bolt pulsing once a second for 2.5 s. The HUD carries
its own copy of the transmission display (section 14.8). The stock's
"[ Press JUMP to exit ]", or "[ Vehicle Locked ]" in orange, sat
trailing-aligned at the bottom; openQ4 shows the HUD prompt instead (section
14.8): the jump control's glyph and Exit, or a lock and Vehicle locked in
#FF8000 while a script locks the vehicle. The weapon panel draws the traced
shell, twin-cartridge and rocket glyphs.

**Other rides.** The flatbed and the tram replace the three gauges with a
vehicle-silhouette gauge. The air-defence cannon draws a static amber scope
overlay with two load bars and its own reticle, and centers its exit line; its
HUD has no transmission display. The Stroggification table and the MCC body
table run scripted Strogg interface sequences of hexagon flashes, readouts and
static. openQ4 keeps each source's construction and timing and lays them out
with the HUD anchor rules; vehicle labels and messages come from the language
tables. On foot and in a vehicle alike, the gauges and weapon panel are the
status bar. Remastered centers them on the bottom edge, keeps the HUD prompt at
the bottom-trailing corner and the transmission display at the top-trailing
corner, and adds the display to a vehicle HUD that has none.

**The Awakening.** The expansion adds four vehicle displays and a state for the
walker's cockpit monitor. Each keeps its measured construction, takes the HUD
anchor rules and the language tables, and starts in its real state rather than
whatever its first frame happened to show.

- **Space cannon.** The m03 dropship guns keep the on-foot HUD's radio, boss bar
  and notices and hide its gauges, strip and powerups. Their display is the
  ship-status panel, top-trailing below the transmission display (the stock
  corner at 512,32 u touched it): an olive disc 58.75 u in radius, #1F2A13 with
  a #4D682F to #64883D rim, opaque in Classic and at 0.85 in Remastered, holding
  the dropship from above in #475736. Its five sections turn #A13C2B at 80, 60,
  40, 20 and 10% of the ship's health, left wing first and mid body last, and
  clear again on repair (the M03 repair helper now sends the missing clear
  events). Gold objective markers,
  broken rings 16 u across, spin at 0.35 turn a second over the place that needs
  the player. Eight red darts on the rim point in from attackers, pulsing at
  2 Hz while they fade over 3 s. An additive olive cone 52 degrees wide shows
  where the gun looks relative to the ship's nose; the stock bound it to the
  absolute view yaw, which nothing wrote. On foot the same panel takes the
  markers, damage and warnings the script also sends to the player HUD, which
  the expansion's HUD cannot show. The turret HUD never shows how to leave the
  gun; Remastered's HUD prompt adds Exit.
- **Cockpit screens.** Three on-model screens report the weapons. The selector
  lights the active weapon's row and shows ACTIVE in #D6E98C or JAMMED in
  #FF0000 above the kill count. The rocket rack frames the lock in gray, in
  #F7C211 while acquiring and in red once locked, with LOCKED in Marine 1.0
  #FFFFB6 between arrows pulsing at 4 Hz, above five rockets that reload one
  every 0.5 s. The gun heat bar grows from 40 to 337 u and warms from #8B8B48 to
  #FF0000.
- **MCC cannon.** The MCC's side gun keeps the space cannon's reticule and
  screens, and like that gun it cannot be damaged: it inherits `noDamage`,
  `health` 0 and god mode for its gunner, so there is no turret health to
  report. Its HUD reports what the player defends, the MCC's hull: the gauge
  plate scaled to 136x70 u under U.S.S. Hannibal in Marine 0.25, and the hull in
  Marine 0.50 #F1F0C9, the MCC's health divided by 10, 2000 when whole.
  Remastered stacks the readout under the transmission display, which the side
  gun's HUD lacks, fills the track in proportion (the stock track is static)
  and, at or below 25%, turns it #FF3300 at 0.60 with a 2 Hz pulse, as the
  vehicle gauges warn. Exit shows in the HUD prompt.
- **Chaingun turret.** The mannable Strogg chaingun of m01's trench and m04's
  prison cannot be damaged, but its gunner stands exposed behind it, and the
  expansion gave it the plain on-foot HUD with its ammunition gauge left empty.
  openQ4's turret HUD (`guis/vehicles/turret_hud.gui`) reports the gunner and
  the gun's barrel heat. Remastered keeps the on-foot Marine status bar: health
  and armor in their places and, on the gauge where ammunition shows, the heat
  in the same construction, track 0.20 and fill 0.35 in #FF9E1A, the heat as a
  percentage of the gun's limit in Chain 0.5 where the clip shows, the gun's
  glyph where the weapon render sits and, in the notch where the other gauges
  carry their icons, a 16 u symbol of three rising heat waves in the icon
  tint (#FF3300 at 0.80 when hot). From the warning level (75%) and while the gun vents, the track and
  fill turn #FF3300 at 0.35 and 0.60 and pulse at 2 Hz and the figures turn
  #FF3300. Once the barrel is warm, a 40x3 u bar at black 0.45 sits 22 u under
  the crosshair, filling from its leading edge in #FF9E1A at 0.85, #FF3300 at
  the warning level; while the gun vents, OVERHEATED in Marine #FF3300 with
  the stock drop shadow pulses between 100% and 55% at 2 Hz beneath it.
  Minimal keeps the gauges and the crosshair's heat; the transmission display,
  damage direction, notices and Exit in the HUD prompt complete it. Classic
  builds it on the stock vehicle HUD: the hull and shield gauges become HEALTH
  and ARMOR, their values in Chain 0.5 where the on-foot gauges put theirs
  (the gauge's 66, 9 u, 52x26 u, right-aligned); the weapon panel carries the
  gun's glyph where the vehicle HUD draws the current machinegun, HEAT on the
  panel's top row where it names the other weapon, OVERHEATED in its place
  while the gun vents, and the heat in the charge bar.
- **Speeder bike.** The m07 race keeps the stock vehicle HUD and centers a
  progress bar at the top: a 45% black plate whose raised tab carries the race
  timer, gold Marine 0.65 seconds and Marine 0.55 hundredths, and ten
  forward-leaning pips, #4E5B1C dark and #798D2A lit, in two groups of five, lit
  one per checkpoint. Remastered sets the bar 18 u lower, clear of the
  transmission display at 4:3, pads the hundredths and turns the seconds #FF3333
  over 100 ms in the last 10 s, a state the content never sends. The status bar
  is the stock vehicle one: Remastered restores the hull and shield warnings the
  expansion removed and shows the machinegun's glyph for the machinegun (the
  bike lists the machinegun first, so the expansion's HUD showed the cannon's
  panel for it). While boosting, a red hazard icon at 70% and Boosting... in
  Marine 0.25 #FF0000 show trailing, which the content's own timers kept hidden;
  Remastered sets them in the HUD prompt and streaks the view's edges along the
  bike's path with the expansion's square-frame speed blur, never under reduced
  motion. The expansion's "[ Press CROUCH to boost ]" line took the exit line's
  window, so its HUD never says how to get off. The HUD prompt carries Boost
  while a boost is ready and Exit at its trailing end; Classic keeps the boost
  line and Boosting... at the expansion's places and Exit at the stock exit
  line's. The bike's speedometer reads its speed in Marine 3.2 over kph and
  scrolls the road through its bezel; openQ4 narrows a third digit to fit.
- **GEV.** Its HUD names ten materials that neither game ships, so openQ4 draws
  them in the stock vehicle construction, bottom-leading in Classic and centered
  on the bottom edge in Remastered, as every status bar is: plates at black 0.60
  sheared 0,-.32; a vertical #FF9E1A health bar that drains from the top (the
  stock divides an already normalized value by 1000 and reads empty); the hull
  from above in #B0C891, turning with the view relative to it under a fixed
  turret; three shield stages as arcs, #D1AF47 from 80%, #C6C251 from 40% and
  #A2B85C to empty, the inner arcs pulsing #FF591A to #80591A every 500 ms once
  the shields fail; and the weapon glyph turned 60 degrees with an infinity mark
  or the rocket count over the #FF9E1A charge bar. Remastered keeps its
  transmission display at the top-trailing corner and adds Exit to the HUD
  prompt. No map places the GEV.
- **Walker monitor.** In m04 the walker's cockpit monitor can show SYSTEM
  OFFLINE: a red wash pulsing between 25% and 50% every 750 ms under two lines
  of Marine 2.0 #FF0000 with a shadow.

### 14.13 Scopes and weapon displays

Scopes appear and vanish with the zoom, without fades, above everything else in
the HUD, and the field of view changes over 100 ms.

| Scope | Aperture | Marks and motion |
| --- | --- | --- |
| Machinegun (and the hover tank's cannon) | A lens of radius 182 u. Beyond r 184 u the view is pulled outward and smeared toward the center, averaged with 20% gray, under a #363636 veil at 40% | In black: a double ring (r 177.7 u, 2.9 u wide, and r 183.1 u, 2.2 u wide), four posts from r 163 u broken at r 98 u, fine ticks from r 20 to 40 u, and the lens darkening from r 60 u to 32% at the ring. An additive #1B270E glow rises from r 84 u to the rim. Sixteen 9.2 u dots at 34% on r 174 u turn with the view's yaw like a compass |
| Railgun | A diamond, \|dx\| + \|dy\| < 362 u, inside a black edge line at 87%. Within the edge, a 26% black band follows the aperture's rounded ends, shoulders and the ring's bite; beyond it, a gray haze thickens from 25% to 51% toward the corners. The stock's 86% is the edge line alone | An additive #082828 glow along the inside of the edge; hairlines with 108 u and 126 u gaps; #00FFFF bars at 50% beside the horizontal hair; a split ring turning 40 degrees a second; one sheared #99FFFF cell per round, the top cell first; and a rounded diamond sweeping outward every 2 s |
| Nailgun (seeker modification only) | An amber lens of radius 255 u inside a surround of three bands (black 50%, a black line at r 272 u, black 71%) and a gray haze beyond, with four hooks reaching in to r 160 u | An additive #241700 glow rising toward the rim (#533200 when locked), a notched ring turning with the yaw, an additive #3C2500 hairline at r 218 u, a lock arrow that lights #623A00 toward the target, lines that spread on a lock, and R_Strogg "// LOCKED //", range and countdown readouts once locked |

**Refinements.** At any aspect each scope keeps its aperture's shape around
the aim point, and its outermost layer reaches the screen edges: the
machinegun's veil, the railgun's haze or the nailgun's surround.
Nothing stretches, and the marks are vector geometry at every resolution.
Curved scope outlines use smooth arcs/Bezier curves, not stair steps traced
around source texels. Preserve the rail aperture's shoulders and ring bite,
the nailgun's sharp hook tips and the rotating ring's deliberate notches.
Remastered applies a separable Gaussian soft focus outside every scope, with
13.5 dp sigma and 0.86 saturation. A 38 u feather follows each aperture's
circle or diamond, leaving the sight interior sharp and covering every outer
screen band. The mask rises through 0.08, 0.50 and 0.92 at 8, 19 and 30 u
beyond the aperture; it reaches full strength at 38 u. There is no radial
stretch or stepped smear. Remastered adds the railgun's diamond sweep at 8%
instead of the stock's 2%, which no screen shows. *The Awakening*'s Core
Cannon takes the machinegun scope through its scope modification.

Weapon displays are the gun-mounted screens of the view models.

| Weapon | Display |
| --- | --- |
| Machinegun, shotgun | Black Marine digits on a #73A640 plate with a 3 Hz shimmer; low (below 10 and 3) #CA8F15; empty #C72C1B pulsing 100–50% at 2 Hz; the shotgun adds a scan band every 2 s |
| Hyperblaster | Digits on a #85B4FA plate with a random 68–100% flicker; below 10 it pulses at 3 Hz and a RELOAD plate flashes #FE4203 |
| Nailgun | No digits: an #E8AB1F level in six slanted cells; empty turns #E63300 pulsing 100–40% at 2 Hz |
| Rocket launcher | Three #44D29D cells that go dark (#422300) as rounds are spent, 80–100% at 3 Hz |
| Railgun | Teal #009F9F charge arcs that turn orange-red for 2 s after the last shot, turning rings and scan pulses |
| Napalm launcher | Animated fire art without a readout; *The Awakening*'s copy draws one static additive fire layer |
| Core Cannon (*The Awakening*) | None of its own; its scope modification adds the machinegun's display and sight |
| Freeze Gun (*The Awakening*) | The hyperblaster's display |

The blaster, grenade launcher, lightning gun, dark matter gun and gauntlet have
no display. The machinegun and shotgun overlays' 9 Hz flicker is capped at
3 Hz (section 8), and the hyperblaster's RELOAD plate, a bitmap with baked
English, becomes a vector plate with localized text.

### 14.14 Scoreboard and match summary

**Stock scoreboard.** Held on the scores button, or forced for 5 s when a
tournament ends. It appears and disappears at once and hides the whole HUD,
crosshair included. It is not interactive.

| Part | Stock construction |
| --- | --- |
| Backing | The soft backing at black 0.60, opaque over the central 509x399 u; the Tourney board uses a flat black 0.60 fill |
| Header | Server name and address in Lowpixel 0.22 #FFFF8D |
| Panels | Your team first, then the other team, then spectators. Each opens with a header band at 1.00 in the team color (#8B964B for deathmatch and Tourney, #999999 for spectators) carrying the faction mark, MARINE, STROGG, SPECTATORS or PLAYERS in Lowpixel 0.31 and the team score; headings in Lowpixel 0.16 at 0.40 over a white 0.10 rule, standing in the strip between the band (drawn 3–24.5 u below the panel's top) and the first row with their baseline on the first band's top edge, so a heading never shares a row with an entry |
| Columns | Ready, speaker, friend and flag or rune icons; name; clan; score; kills (flag modes); minutes connected; ping, trailing-aligned |
| Rows | Lowpixel 0.22 with a drop shadow on server-list bands: every row 8%, your row 29%, eliminated Tourney players greyed. The pitch adapts to the entries, up to 16: 15–30 u in team modes, 17–35 u in deathmatch, 13–26 u in Tourney |
| Footer | A white 0.10 rule, game type and map, the frag, capture or control limit, the time limit and the timer |
| Tourney | A bracket tree of closed 1 u outlines at 0.40 (the only closed boxes in the family) joined by elbow connectors, over the players and spectators lists |

Past 16 entries the stock spills over its footer, and the local player is
marked by band brightness alone.

**Remastered scoreboard.** It stays instant and non-interactive while held. It
is the in-game card of section 14.18, never the stock's soft backing: a 0.94
black body inside one rail with the 12 and 3 dp cuts, 744 dp wide, centred
over the view, which stays sharp because play goes on, and as tall as its
lists, up to 630 dp.

- The 40 dp header carries the triangle, the server's name in Marine, your
  place ("3RD OF 12") and the clock in the value color. The match line under it
  holds the mode and map in Marine and the limits.
- The column headings stand once, in Lowpixel at 0.40 over a white 0.10 rule.
  Each panel opens 6 dp down with the Players page's band in its team color
  (section 14.18): the title in Marine and the team score in the value color.
- Rows are the Players page's, 24 dp: a band at 0.08, the speaker crossed when
  muted, the friend symbol, then a skull while dead, the flag a carrier holds
  or the warm-up's ready check, the name, the clan or a BOT tag in the
  header-band construction, SCORE, KILLS in flag modes, DEATHS and minutes,
  and ping beside the network-quality bars (section 13.13).
- Your row adds the orange ◥ marker to a band at 0.29, so it never depends on
  brightness or color alone. Dead or eliminated players' rows drop to 0.50.
- The server's address runs under the lists. Past the card's height the rows
  scroll by whole rows with the right stick or the wheel while the board is
  held; each panel heads the rows it shows, and a downward triangle counts the
  rows below beside the address.
- Tourney's card is 928 dp wide. Under its TOURNAMENT BRACKETS header, the
  match line carries the mode, map, frags to win a round and the tournament
  count, and the bracket chart replaces the stock's closed boxes above the
  lists:
  - **Arenas.** Each arena is an open plate (section 2, trait 3) with its two
    players in Lowpixel over a hairline: 110x38 dp, the final 136x56 dp. The
    chart's second half mirrors the plates so every leading edge faces out.
  - **Layout.** The halves converge on the final in the card's centre. The
    round titles stand above their columns in Marine at 0.50, the final's in
    the value color.
  - **Links.** Links are 1.5 dp lines at 0.22 that light to 0.70 once the
    arenas they leave are decided.
  - **States.** Your arena's rails turn aqua (#5EB987) and your half takes an
    aqua band; an arena you follow fills aqua. A decided arena turns blue
    (#3E57B7): its winner's score takes the value color, its loser drops to
    0.45, and the final's champion gains the ◥ marker. An empty arena reads
    EMPTY.
- On touch, the scoreboard button at the top edge toggles it instead of
  holding.

**Stock match summary.** It opens at the end of the match with the cursor:
SUMMARY, STATISTICS and SCOREBOARD tabs on the tab strip, the soft backing at
0.90, and "[ Press ESC to return to the game ]". Deathmatch lists the top ten
with each player's three most accurate weapons as icons, the first three rows
tinted blue, red and yellow at 0.15; team modes put the winning team's panel
on top. The chat history and entry follow, and STATISTICS shows each player's
kills, deaths, accuracy grid, medals and end-game awards. openQ4 fixes the
stock's report of a tied match as a Strogg win (it reads DRAW), prompts with
the active device's glyphs, and marks your row as the scoreboard does.

**Remastered match summary.** With the retained menus it is the multiplayer
card of section 14.18, never the stock's soft backing: the Escape card's
construction and size over the softened view, opened on its SUMMARY tab with
the chat field focused. The header holds the map, then the mode, the time to
the next map in the value color and the score. The tab strip holds SUMMARY,
STATISTICS and SCOREBOARD, and the prompt bar Return to game on the back
button, Tabs and Select.

- **Result.** A header band across the page in the winners' color (olive for
  deathmatch, Tourney and a draw) holds the result in Marine: MARINE TEAM
  WINS, STROGG TEAM WINS, DRAW, the leader's "<name> wins!" or a managed
  match's own result. Its detail follows (your place, or MATCH FORFEITED), then
  the final score in the value color.
- **Standings.** Beside the chat stand the stock summary's rows in the
  scoreboard's construction: outside team modes the top ten with their places,
  in team modes each team's top five with the winners first. You take the last
  row when you would not be listed. A row holds the name, the clan, the three
  most accurate weapons of more than ten shots as icons in their color code
  (Appendix B.4) and the score. The first three places keep the stock's blue,
  red and yellow bands at 0.15; your row adds the orange ◥ marker to a band at
  0.29.
- **Chat.** CHAT HISTORY stands over the newest messages, oldest first and
  wrapping, in the chat box's line construction (section 14.9). A message the
  room cuts at the top fades under the card. The entry field is the stock chat
  plate with its channel tag, and at rest an empty field invites a message.
  - **Typing.** Choosing the field (Enter, a click or accept) starts typing.
    Enter sends and keeps typing, and an empty Enter stops. Tab switches the
    channel in team modes. Esc stops and clears.
  - **Prompts.** While typing, the prompt bar reads Send, Channel and Cancel.
    From 80% of the 128-character limit the characters left show.
- **Statistics.** The Players page's lists and statistics (section 14.18), with
  the selected player's end-of-match awards in place of Mute and Friend.
- **Scoreboard.** The scoreboard's rows and columns: speaker, friend, name, the
  clan or a BOT tag, SCORE, KILLS in flag modes, DEATHS, minutes, and ping
  beside the network-quality bars. Each list opens with its band.

### 14.15 Crosshairs

Every stock crosshair and context cursor is redrawn as vector paths. The stock
has 31 textures in three generations: 32-texel art from 2005, exact 2x
upscales, and native 64-texel redraws of the weapon crosshairs.

- **Master.** Each design is authored on a 64x64 master centered on the aim
  point. Line art uses true circles and straight strokes. Organic art uses
  fitted curves, not pixel outlines: the hand, the nailgun brackets, the napalm
  splatter and the badges.
- **Symmetry.** Designs are symmetric where the stock meant symmetry.
- **Ink.** White ink takes the crosshair color by multiplication. Black
  outline and shadow ink stays black at every tint, as in the stock. A
  two-tone band is one layer with one opacity, so its parts never seam.
- **Strokes.** No stroke draws thinner than one physical pixel. The stock's
  1-texel strokes fall below that at 16 u.
- **Stock flaws.** These are not reproduced:
  - the grenade launcher's 2.4% black fill over the whole texture;
  - the machinegun's half-alpha tick;
  - the shotgun, gauntlet, nailgun and world-GUI cursors sitting a texel off
    the aim point;
  - the pixel stair-steps of the 32-texel cursors.

| Crosshair | Design | Used by |
| --- | --- | --- |
| Blaster | Four arcs of a white rim over a black inner band at 0.66, split by 2-unit axis gaps, around a haloed dot | Blaster, hyperblaster |
| Machinegun | A white ring over a black inner band at 0.79, four ticks crossing it, and a dot | Machinegun; the walker, hover-tank, flatbed, tram and E3 mech guns |
| Shotgun | A small white ring over a black ring, with a clear center | Shotgun |
| Grenade launcher | A bevelled ring cut by 6-unit axis gaps, with four horizontal wings | Grenade launcher, dark matter gun |
| Nailgun | Two mirrored crescent brackets with inward hooks, white faces and black inner edges | Nailgun, the walker's rocket pods, the air-defense reticle |
| Rocket launcher | A white rim over a black band with a dark outline and faint halo, four outer ticks and a dot | Rocket launcher |
| Railgun | Four bars with a black skeleton outline at 0.52 and a small plus | Railgun |
| Lightning gun | Four swallowtail darts on the diagonals pointing inward, under a soft black halo | Lightning gun, hover-tank cannon |
| Napalm launcher | A ring of white splatter shards at 0.71 over a faint black Quake emblem at 0.10 | Napalm launcher |
| Gauntlet | A black disc with a white core at 0.63 | Gauntlet (multiplayer) |

**Custom set.** The stock cycles twenty entries in this order:

1. the blaster, grenade launcher, lightning gun, machinegun, nailgun,
   railgun, rocket launcher and shotgun designs;
2. ten simple marks;
3. the gauntlet and napalm designs.

The simple marks are:

- 1: a thin circle and a dot;
- 2 and 8: the same plus;
- 3: four short ticks;
- 4: a flat disc at 0.11 with a dot;
- 5: a faint circle and a dot;
- 6: a dot;
- 7: the faint circle with the plus;
- 9: four dashes and a dot;
- 10: a gapped annulus at 0.24 with two diagonal slits and a dot.

**Context cursors.** Each keeps its stock color and motion. The friendly X
needs the far character-info option switched off, so in stock play only the
use, talk and locked cursors are certain to appear.

| Cursor | Design and color | Size and motion | Shown |
| --- | --- | --- | --- |
| Use | A yellow hand with a black outline, in its own colors | 24 u, pulsing to 28 u and back every 600 ms | Aiming at a usable object |
| Talk | Four frame pieces with 45-degree outer edges and arc inner edges, in #FF8000; for a medic, a red octagon badge with a winged staff; for a tech, an olive badge with TECH and hazard stripes | Closing from 34 u to 22 u while fading in over 200 ms, with the character's name and role | Aiming at a friendly character |
| World GUI | Three spokes, a small ring and three arcs, #FFCC00 | 24 u, two frames 50 ms apart | Not drawn while a world GUI has focus; the terminal highlight of section 13.5 takes over |
| Enter vehicle | An outlined triangle below a chevron, #FFFF00, with ENTER | The chevron rises 7 u and fades over 500 ms and the triangle dims to 0.40 over 750 ms, repeating | Aiming at a vehicle with a free seat |
| Locked vehicle | A rounded square with a diagonal slash, #FF0000 | Closing from 34 u to 22 u while fading in over 200 ms | A locked or full vehicle |
| Friendly | Four inward chevrons that fade outward, #00FF00 | Closing from 40 u to 34 u over 200 ms | Friendly AI within 512 units |

The machinegun's view-model sight and the air-defense cannon reuse these
designs at their own sizes and colors (sections 14.12 and 14.13).

**The Awakening.** The space cannon (section 14.12) aims with its own lock-on
reticule, 128-texel art in four designs that change with the lock.

- **Free.** Translucent corner brackets with bright edges, hollow side bars
  and a center dot, in white.
- **Acquiring and locked.** Two inward triangles join them, #FFFF00 while the
  rockets acquire and #40FF40 once locked.
- **Friendly.** An X over a faint ellipse, in #FF1A1A.
- **Out of range.** The brackets and dot alone, in white.
- **Guides.** Additive effects in the world turn 0.3 turn a second around
  targets: the stock nailgun lock ring in #F8C001 while the rockets acquire,
  the lock guide in #E60000 once locked (that ring with a thin inner ring and
  two inward triangles), and the stock ring in gray around the nearest enemy.
  Gray brackets frame a target out of range; their stock material adds
  nothing, so only Remastered shows them.

The reticule stays at the crosshair size, and only the guides scale with
distance. openQ4 applies each state's color at once; the stock cursor changes
color only on a weapon change or a hit. Its pulse cannon shows only the free,
out-of-range and friendly states. The
busy-talk cursor is a red X over a faint red ring (`npc_talkstate` 5) and
keeps its color. The new weapons borrow stock designs: the Core Cannon the
grenade launcher's, the napalm launcher the rocket launcher's, the Freeze Gun
the blaster's, and the grapple the rocket launcher's in yellow. The speeder's
cannon and the stock walker's cannon name a `vehicle_cannon` crosshair that
neither game ships, so openQ4 draws a heavy-shell reticle: four arcs of a
ring with ticks in their gaps, a center dot and two drop chevrons below it.

### 14.16 Item icons

Stock pickup and weapon-strip icons use vector paths. The side-on weapon
backdrops in the ammo gauge use their original installed bitmap materials,
including the weapon-specific `mtr_icon` for expansion weapons. The award
medals remain bitmaps. Quake 4 has no single-player inventory items: no keys,
keycards or data disks. The single-player pickups are health, armor and the
weapon and ammunition icons.

- **Master.** Each icon is authored on a master 24 units high. It keeps the
  stock texture's full frame and aspect, so it drops into the stock draw
  rectangle unchanged, and stock art that sits off-center stays off-center.
- **Tracing.** Traces of the stock masks are only a starting point. The icons
  are rebuilt as authored geometry: true circles and ellipses, straight runs,
  the stock's 45- and 33-degree slants, fitted curves, and one half mirrored
  for symmetric art.
- **Strokes and fills.** Line work uses deliberate strokes: 1.5 u rims, and
  the measured weights where the stock depends on them. Shapes that a stroke
  cannot hold are filled silhouettes. Holes and knockouts, such as the
  doubler's 2 and the chat bubble's dots, stay editable.
- **Color.** Tintable art is white. Baked colors and black drop shadows stay
  as drawn, so a tinted stopwatch keeps its black shadow. Paired states share
  one geometry: ready and not ready, speaker and muted, friend and not friend,
  the two flags, and each HUD item and its world sprite.
- **Legibility.** Every icon reads at 16 dp, at least as well as the stock
  downsampled to the same size.

| Family | Stock art | Reconstruction | Drawn |
| --- | --- | --- | --- |
| Weapon glyphs (`item_<weapon>`, `item_fire`) | Flat white glyphs | Filled silhouettes; the symmetric ones (saw blade, blaster sparks) are one fitted unit repeated by rotation | Strip, wheel and single-player pickups in the family tint; multiplayer strip and kill feed in the weapon color code |
| Weapon renders (`gun_*`) | Shaded gray side views with a light outline | Clean side views of 26–49 nodes: lit top facets at full ink, bodies at half, holes and cut lines cut out | Behind the single-player clip count, #C7BD78 at 0.40; never in multiplayer |
| Ammunition (`ammo_*`) | The weapon glyph inside a 1-texel frame, colliding with it | The weapon glyph at 80% inside a 1.5 u frame | Multiplayer pickup line |
| Health and armor | A cross in a ring; a vest and a shard with a solid rim around a 0.47 body | The same constructions with 2.03 u and 1.5 u rims | Gauges, pickup line, the boss bar's health mark, the aim text |
| Powerups and runes | Additive emblems in their own colors with a baked dark keyline and halo | Flat tonal facets, one tone per region: the Quake emblem, the haste bolt and orbit, the regeneration syringe and wings, the invisibility figure. The four Arena CTF runes share one six-toothed cog around a disc at 33–51%. Dead Zone is a gold cog with a Z. The halo becomes a separate soft layer (below) | The powerup column at 42 dp, the pickup line, world sprites |
| Scoreboard runes and flags (`sb_*`) | Colored 32-texel runes and 16-texel square flags | Filled silhouettes; the scout and doubler motifs are the powerup motifs at 0.46 and 0.43 scale | Scoreboard columns |
| Status (`icon_*`, `noammo`, `chat`) | Khaki and colored marks with baked black shadows | Ready check and not-ready cross in a 1.5 u box, speaker with a wave (muted adds a red slash), friend silhouette (0.125 when not a friend), stopwatch with two hands, infinity, prohibition sign, speech bubble | Scoreboard rows, timer, weapon strip, the typing bubble |
| Team and match (`marinelogo`, `strogglogo`, `quakelogo`, `ctf_flag`, `onectf_arc`) | White marks, the waving flag with a baked shadow | The hexagonal spearhead, the winged bolt, the Quake emblem, the flag with its slot as a hole, and two fitted arcs with a linear opacity gradient | Team panels, scoreboard, summary, flags |
| Kill feed marks (`kill_self`, `kill_telefrag`) | A white skull and crossbones; a teal mark of inward chevrons, arcs and an X | Filled silhouettes | Kill feed, #00FFA6 for the telefrag |
| Vehicle weapons (`veh_*`) | Shaded silhouettes | Side views as the weapon renders | The vehicle weapon panel |
| World sprites (`simpleicons`) | The HUD art recolored per variant, additive | The HUD geometry, white, recolored by the variant's color | Simple items in the world |
| *The Awakening* | The Core Cannon's three barbed spikes and the Freeze Gun's snowflake as 32-texel glyphs; weapon renders that copy the napalm launcher's; ammunition pickups showing the rocket and grenade icons; a purple Adrenaline injector; the quad icon for the team powerups and the flag carrier's armor regeneration; a red Dead Zone token | The glyphs in the glyph construction; side views of each weapon's own world model in the render construction; ammunition in the stock frame; Adrenaline in the powerup construction; for the rest, openQ4 emblems in a squad hexagon (three rounds, a cross, three claw strokes) and a flag on a shield over a returning arrow; Dead Zone's cog in red | As their stock families, in the color codes of Appendix B.4 |

**Glow.** The powerup emblems and the chat bubble carry one separate soft
layer instead of the stock's baked halo, capped at the family's light peak
(section 6):

- **Shape.** The emblem's silhouette blurred with a sigma of 1.3 u.
- **Gap.** Nothing within 0.6 u of the silhouette, where the stock drew its
  dark keyline.
- **Tone.** Added in the emblem's color at a gain of 1.67–1.96, so it peaks
  near a quarter of the emblem tone about 1 u out. Invisibility, with its thin
  strokes, uses 0.32.
- **Exceptions.** The Dead Zone token has no halo, and the Quad kill mark's is
  a normal-blend layer.

**Squad patches.** The marines' shoulder decals (`decal_<squad>`, 256 texels on
a flat gray ground) are drawn as flat-color layers in one construction: a light
gray rim, a black border, a two-tone ground and the squad's animal or emblem.
The nine stock patches are traced from their textures: Rhino, Badger, Cobra,
Eagle, Kodiak, Raven, Viper, Warthog and Wolf. *The Awakening* names five
squads that ship no patch, and openQ4 draws them in the same construction.

| Squad | Patch |
| --- | --- |
| Falcon, the player's squad; its marines wear the Viper patch in the content | A peregrine's head in profile, dark hood and malar stripe over a cream face, on a heater shield split by a stooping diagonal |
| Fox | A fox's head from the front, ears up and eyes narrowed, on a chamfered shield in two greens |
| Atlas, which drives the walkers | The titan holding the world overhead against a bronze sunburst, on an octagon |
| Hades | A skull with ember eyes over rising fire, on a downward banner |
| Razor | A shark's head in profile with its jaws wide, on a slanted plate |

The HUD shows only the player's squad stencil (section 14.8), and the
character skins keep their content's decals.

**Stock details kept and dropped.**

- **Pickup-line colors.** Multiplayer health and armor pickups ignore the
  window color and keep their own colors. The powerup emblems are multiplied
  by it, so the olive pickup line dims them.
- **Kill feed.** The Quad kill mark is drawn with straight alpha.
- **Not reproduced:**
  - the Dead Zone token's opaque black square: it becomes additive on a clear
    ground;
  - the collision between the stock ammunition glyph and its frame;
  - the pixel stair-steps of the 16- and 32-texel art.

### 14.17 Loading screens

The loading screen is the one full-screen picture between the menu and play.
The stock builds it from the level's levelshot and a little furniture. The
Remastered screen keeps every piece and fills the wait with what the player is
waiting for.

**Stock screens** (`guis/loading/*.gui`; section 9 gives the composition):

| Part | Single player (`generic`) | Multiplayer (`mplevel`) | Intro (`intro`) |
| --- | --- | --- | --- |
| Picture | The levelshot, full screen | As single player | The intro art (`e3_load`) |
| Bands | Top 0–119 u and bottom from 345 u, black 0.80, each with an additive #181D0A spill into the picture (Appendix B.2) | Bottom band raised to 227 u | As single player |
| Furniture | Corner brackets at 28 and 374 u, the dot matrix and the `+` grid at 0.04 | Lower brackets at 256 u | Dot matrix only |
| Identity | The level name trailing at the top in Marine 26 dp at 0.80, over a gradient that darkens toward the trailing edge | Adds a message line beneath it: the map path, "collision map", then each item as it spawns | None |
| Progress | The bar from 235 to 640 u at 431 u, #E06C00 track 0.30 and fill 0.50; LOADING trailing across it in Marine 29 dp with a shadow | Bar and LOADING at 313 u | As single player |
| Server | — | Name and address at 0.80 and game type and limit at 0.52, trailing, in Lowpixel 18 dp; a load line centered at 409 u; a row of 24 u item icons at 430 u, one for each kind of item as it spawns | — |
| Completion | LOADING becomes "- CLICK TO CONTINUE -", the full bar shows and `load_screen_ready` plays; the text pulses from white to 50% over 500 ms and back over 200 ms. A click closes the screen with `load_screen_click` | Never waits: the match starts | As single player |

The first mission, Air Defense Bunker, uses the intro screen. The stock centers
the multiplayer icon row 12 u left of center, a rounding slip that Classic
keeps.

**Remastered.**

- **Picture and bands.** The levelshot covers the whole view (openQ4's
  expanded levelshots fill the side screens) and drifts in by 3% over the load;
  reduced motion holds it still. Both bands keep their silhouettes and lengthen
  only their straight spans to the view's edges. The top band anchors to the top
  edge and the bottom band to the bottom, and the corner brackets and the dot
  matrix move out with the view's corners.
- **Identity.** The level name trails at the top. Beneath it, on the stock
  message line, run the mission and difficulty in single player, or the game
  type and map in multiplayer.
- **Objectives.** When the map lists objectives, they stand on the leading side
  in the objectives display's open plate (section 14.11): the orange >>MISSION
  OBJECTIVES title, then one line per objective behind its marker.
- **Progress.** The trailing part of the thick bottom band holds LOADING above a
  240 u bar. Under the bar sit the loader's current phase with its count and
  the percentage in the value color. The phases are the loader's own, such as
  the map, the collision map, area awareness and the asset queue, and the
  count is the phase's place or the queue's assets loaded of its total.
  Multiplayer names each item as it spawns ("SPAWNING · RAILGUN"). A long
  phase reads as work, never as a stall.
- **Tips.** Single player runs localized tips in the band's leading part: at
  most two lines of Lowpixel 14 dp under a TIP tag, changing every 6 s with a
  250 ms cross-fade.
- **Continue.** When the level is ready, LOADING becomes the continue prompt
  with the active device's glyph: CLICK TO CONTINUE with a mouse, the south
  button and CONTINUE on a controller, TAP TO CONTINUE on touch. It pulses with
  the stock timing, and the stage line reports the load time.
- **Multiplayer.** The bottom band stays low and the picture tall.
  - **Server card.** A section 6 card on the leading side, above the thin part
    of the band: the server's name in its header, then the address in Profont,
    the mode, the limits, the players by team and the server's message.
  - **Arsenal.** The band's leading part holds the map's items: each icon fades
    in over 150 ms in its color code as the item spawns.
  - **Hand-off.** When loading ends the prompt reads JOINING, and the screen
    gives way to the Welcome menu (section 14.18) without a click.
- **Classic** keeps the stock screens on the 4:3 canvas.

**Initializing screen.** While the engine starts, before any menu exists,
`PrintLoadingMessage` paints one frame for each step: the `gfx/splashScreen`
picture stretched over the canvas and one status line. The line reads LOADING...
twice, then INITIALIZING SOUND..., INITIALIZING NETWORK..., INITIALIZING RENDER
SYSTEM..., INITIALIZING USER INTERFACE..., LOADING GAME... and INITIALIZING
MENUS... (`#str_104343` to `#str_104351`). It is set in the console's 8x16 u
characters (`bigchars`) in #F09E0D, centered across the canvas with its top at
410 u. The screen repaints only when the step changes. openQ4's aspect
correction pillarboxes the picture in #181A08.

The picture (`splash.tga`, 512 texels square) is drawn pre-squashed, so its
rings are circles on the 640x480 canvas. Its parts, measured on the canvas:

| Layer | Stock construction |
| --- | --- |
| Field | #171A09, rising in soft steps to #1C1F0A by 141 u from the rings' center at 319,214.5 u, which is the center of the emblem's own ring |
| Rings | An inner ring at 169–175 u over a soft edge from 161 u; the plate band at 175–204 u, with two framed plates 90 degrees wide at the top and bottom and an 8 u frame; the rim at 204–210 u, fading into the teeth ring by 222 u; eight teeth at 211–247 u, 22.5 degrees wide on a 45-degree pitch; grooves out to 300 u. Tones step between #171A09 and #21270C |
| Clamps | Shade at 3 and 9 o'clock, 80 u tall from 183 u and darker again over the grooves from 246 u |
| Emblem | The Quake emblem in #0E1000, in its 260 u box centered on the canvas |
| Lettering | QUAKE 4 in the `q4text` letterforms, #F2F0EC, cap height 26 u: the texture's layout at 0.367 u per texel from 129.4,148.8 u, with an orange halo |

**Remastered initializing screen.**

- **Layers.** Each part is its own vector layer at native resolution: the
  rings from the table, the traced emblem (section 6) and the `q4text`
  letterforms traced from the texture's white core. The halo becomes vector
  light: the letters dilated 5.5 u, blurred with a 2.25 u sigma and added in
  #FF8340 at 0.10. The picture stays centered on the canvas, and the field and
  rings extend to any view instead of a pillarbox.
- **Motion.** The stock never moves. openQ4 turns the plate band
  counterclockwise once every 180 s and the teeth clockwise once every 120 s,
  beneath the fixed clamps, emblem and lettering. They turn on the presentation
  clock (section 8), so every repaint finds them where continuous motion would
  have. The screen repaints at each step and, between steps, whenever loading
  offers it a moment (every file read does), at most 60 times a second.
  Repaints may take half of the time while the entrance plays and a fifth after
  it, measured over the last few frames, so one slow frame does not stall the
  motion and a slow device still keeps most of the time for loading. Never
  sleep, catch up missed frames or wait for a full turn. Reduced motion holds
  the rings still and repaints only at each step. Reuse the dim rotating
  artwork's geometry: the machined bands are solid annuli without coverage, so
  they turn and scale as one compiled mesh.
- **Entrance.** The first frame, which builds the document, is black. Then,
  over 1.3 s, the mechanism (rings, plates, teeth, clamps and emblem) drops
  from 1.8 times its size to its seat about the rings' center, decelerating
  hard, passes 1.4 % under size and settles back by 1080 ms. The teeth wind
  in from 150 degrees counterclockwise and the plate band from 90 degrees
  clockwise, both at rest by 960 ms. The black lifts over the first 240 ms.
  The clamps wait 16 % wide and 20 degrees back, then close on the plates
  between 470 and 740 ms with a small overshoot, settled by 900 ms. The halo
  flares from nothing to full as they lock (560–760 ms) and eases to 0.40,
  where its breathing takes over; the light sweep fades in from 600 ms, and
  the status line and rule rise 6 u into place from 640 to 1000 ms. The
  lettering holds still in place throughout, appearing as the black lifts,
  and the glint's first pass crosses it as the clamps lock, 940 ms in.
  Reduced motion shows the screen at rest from the first frame.
- **Accents.** Remastered adds four quiet layers that keep close to the stock
  picture, on the presentation clock with the rings. Reduced motion holds the
  sweep still as a key light at the upper left, its leading edge 38 degrees
  counterclockwise from the top, keeps the halo at 0.80 of its strength and
  drops the embers, the glint and the step marker.
  - **Light sweep.** A warm light turns clockwise round the rings once every
    10 s, added in #5E6E24. It rises over its first 7.5 degrees to 0.26 from the
    inner ring through the teeth (161–250 u) and half that on the grooves out to
    300 u, then fades through a 60-degree tail. Short #C9D98A arcs on the ring
    edges catch its leading edge.
  - **Halo.** #FF8340 added behind the emblem at up to 0.10, fading out by 168 u
    and breathing between 0.40 and 1.00 of that strength over 6 s, so the dark
    emblem stands clear of the rings.
  - **Embers.** Fourteen sparks of 0.6–1.2 u in #FFB070 rise slowly through the
    rings, each fading in and out over 6–11 s, at most 0.50.
  - **Glint.** Every 9 s a narrow white light crosses QUAKE 4 from left to right
    in 900 ms, clipped to the letters.
- **Status.** The line is Marine 16 dp in #F09E0D, centered on the stock line's
  middle at 418 u. Each new step's line fades in from nothing over 280 ms
  while rising 5 u into place. A 168 u rule under it holds one segment per
  step: done at 0.55 and pending at 0.15. A marker pulsing between 0.62 and
  0.95 over 800 ms glides to the current segment over 440 ms; each segment a
  step reaches fades in to done over 300 ms behind it, 70 ms after the one to
  its left when a step reaches several. Reduced motion changes the line and
  the rule at once, without the marker.
- **Classic** keeps the stock picture and line, without the accents.

### 14.18 Multiplayer menus

The stock multiplayer menu takes the whole screen. A column of buttons runs down
the leading edge (Main Menu, Join Team, Players, Vote, Settings, Voice Config,
Server Info, Admin and Disconnect, to which openQ4 adds Match Control), with
the chosen page beside it. openQ4 also shows a compact join card when a player
connects. Classic keeps both. Remastered replaces them with one construction in
two variants.

**Construction.** A card in the center screen over the live view, never the
whole screen. It is the section 6 card: a 0.94 black body inside one rail, 12
and 3 dp cuts and a 40 dp header. The card holds a horizontal tab strip, the
page and a prompt bar.

- **Size.** 570x402 dp (380x268 u) for Welcome and 648x477 dp (432x318 u) for
  Escape, centered on the projection center, so at 16:9 the view shows on every
  side. Switching tabs never changes the card's size. In the 9:16 portrait view
  the card keeps these dp sizes, about 507x357 u and 576x424 u, and never comes
  closer than 16 dp to the sides.
- **Scene.** The view behind is softened by the scene effect (section 9, with
  the join card's values): blurred and slightly desaturated, never dimmed. The
  softening ramps in over 250 ms while the card rises 12 dp and fades in over
  150 ms, and it releases over 250 ms on every way out. Where softening is
  unavailable, the fallback is the darkening scrim and vignette of section
  13.6. The HUD hides while the card is up, and the match keeps running.
- **Header.** The 8 dp triangle, then the title in Marine: the server's welcome
  on Welcome, the map on Escape. The trailing end holds the player count on
  Welcome, and the mode, the clock in the value color and the score on Escape.
  The header wash is white 0.16 over `b3_light`.
- **Tab strip.** The stock tab strip, laid under the header at the card's scale:
  - **Active tab.** It rises 30 dp with a small leading flare and an 18 dp
    45-degree trailing shoulder, filled #5A652A from 1.00 at the top to 0.60 at
    the baseline. The baseline rail breaks under it, and a wash fades below the
    strip over one tab height.
  - **Labels.** Tab widths fit their Marine labels. Inactive labels sit at 0.55
    and rise to 0.85 on hover.
  - **Ends.** The previous and next glyphs sit at the strip's ends: Q and E on
    a keyboard, the bumpers on a controller.
- **Pages.** Content is inset 24 dp. Actions are plates in the in-game ladder
  of section 4: `b6_light` with its leading, cut and bottom rails, at 0.62 for
  the primary action and 0.28 for the others, rising to 1.00 with the orange
  marker when focused. Choices (steppers, No and Yes pairs, sliders and
  swatches) follow section 7, and player lists use the scoreboard's rows.
- **Prompt bar.** The leading prompts are Resume (Escape) or Spectate (Welcome)
  on the back button, Tabs on the previous and next glyphs, and Select on the
  accept button. Main Menu and Disconnect (Escape) or Leave Server (Welcome)
  trail it. The destructive action comes last and asks first in the stock
  modal, which soft-focuses the card and the view beneath it (section 6).
- **Unavailable actions.** They stay in place, dimmed to 0.38, with a lock after
  the label and the reason on the plate in the error color. Choosing one shakes
  the plate for 300 ms and announces the reason; nothing else happens.

**Welcome** opens when a player connects, after the loading screen (section
14.17), and replaces the join card. Esc spectates for now, and the menu key
reopens it.

| Tab | Content |
| --- | --- |
| Join | The mode and map; the match state with the time left in the value color, the limit and the player count. Team modes then show two team cards, each the plate that joins that team: the header band in the team color with the mark, name and score, then the player count and, in CTF, the flag's state. Auto join follows, focused, naming the team it will choose, then Spectate. Deathmatch shows the three leaders, Join game and Spectate. Tourney shows the arenas in play, Join the queue with the player's place, and Watch for each arena |
| Server | The name and address, the server's message in #FFFF8D, the rules in two columns (mode, map, limits, players, friendly fire, balance and next map) and the map rotation |
| Players | The team lists with the speaker and friend symbols, score and ping; spectators below |
| Settings | Name, clan tag, model, the rail color swatches and the crosshair, then All settings |

**Escape** opens with the menu key during a match, with Resume leading the
prompt bar.

| Tab | Content |
| --- | --- |
| Team | Your team in its header band with the score and the team sizes; Switch team, with the sizes it would leave, and Spectate. Ready is unavailable outside the warm-up and says so. The last three chat lines follow |
| Players | The team lists beside the selected player's statistics: kills, deaths and score, accuracy per weapon in the weapon color code, awards, then Mute and Friend |
| Vote | The running vote first: who called it, what it changes, the time left, the yes and no tally, and Yes and No (F1 and F2). The call-a-vote rows follow: map, game type, limits, balance, shuffle and kick. Call vote is unavailable while a vote runs |
| Match | Match Control's six sections as a second, smaller strip: Status, Teams, Proposals, Rules, Series and Evidence. Status shows readiness and the pause and referee actions; captain-only actions stay visible, unavailable, and say so |
| Settings | Player appearance: name, model, rail color, handicap, the forced models for self, enemy and teammate, and outline; then Controls, Game options and System |
| Voice | Send and receive voice, their volumes, voice echo and a live microphone meter beside the transmission trace (section 14.8); the push-to-talk keys and Test microphone |
| Server | As on Welcome |
| Admin | The remote console password first; then Players (kick, ban, force team switch and the ban list), Server (the rules and Apply changes) and Console |

**Input.** The previous and next glyphs, Q and E, the bumpers and the left and
right arrows switch tabs at once and cross-fade the page over 150 ms. Up and
down move focus through the page's plates, accept chooses, and back leaves.
Each page opens with its primary action focused (Auto join on Welcome's Join
tab) and keeps its focus while the player changes values on it. The tab set is
fixed; the strip scrolls only on compact widths (section 13.3).

### 14.19 Medical Labs implant activation

This is the temporary **medchange** overlay at the end of stroggification,
not the later `strogg_noise` body-table HUD or the world-space implant monitor.
The retail `vehicle_medbed` definition selects `guis/vehicles/medchange.gui`;
`nanocyteFade()` in `scripts/maps/medlabs.script` sends its `activate` event
as the white surgical flash fades away. Preserve that gameplay event and the
independent `hurt` and `saveMessage` events. The overlay starts hidden and
must never become a permanent HUD.

**Construction.** Reconstruct the gold hexagon glow and the four edge pieces
as editable vector geometry. The activation color is `(1, .815, .435)`
(approximately #FFD06F); black is the unlit additive state, not an opaque
black cover. Hexagons occupy 86×86 u boxes, with tips at approximately
`(1,43), (22,7), (64,7), (85,43), (64,79), (22,79)`. Their bright rim fades
inward. Preserve this stock hexagonal motif within the otherwise 45-degree
interface. Corner pieces occupy 256 u boxes, mirrored at each corner;
their sloping bright edges and inward glow are independent of the hex field.
No extracted retail art belongs in the repository.

**Sequence.** Keep the 38 authored center cells and their measured offsets.
Starting at 500 ms, illuminate one cell every 50 ms and fade that cell to
black over 500 ms. Repeat the same 37-cell order immediately a second time:
`10,24,14,32,30,4,8,15,9,34,28,21,1,38,20,2,12,26,18,33,16,23,6,13,35,27,11,22,7,31,3,17,25,36,19,5,29`.
Cell 37 is present but never flashed by the retail script. At 4150 ms the
final cell, corner light and readouts begin their 500 ms fade; hide the entire
activation overlay at **4650 ms**. The data/static ramps nominally last
5000 ms and the orange edge glow 6000 ms, but the parent hides them at 4650 ms.
Restart these clocks on every `activate`; do not inherit a half-finished fade.

**Readouts and noise.** Two 61×19 u processor readouts at `(54,437)` and
`(54,455)` use the Strogg face at 0.24, with 49×12 u segmented bars at x=3.
Three 92×22 u X/Y/Z readouts at `(542,6)`, `(542,27)`, `(542,50)` use scale
0.4. Their number cycles are 100, 150 and 200 ms; the processor cycles are
100 and 200 ms. Keep the intentional low alpha: readouts 0.2, bars 0.4.
Scrolling rune fields occupy `(574,70,58,72)` and `(2,350,67,81)` at 0.2.
Rebuild these glyph streams from the Strogg face; numbers are data, while
messages, including the quicksave notice `#str_200908`, use language tables.
Data, pink static and color static are separate full-view layers with initial
alpha 0.1, 0.6 and 0.2. Retain their character without a stretched 4:3 tile.

**Wide and tall screens.** Use `s = min(viewWidth/640, viewHeight/480)`
uniformly. Keep the original cell centers on the centered 640×480 u screen;
continue the same 70 u column / 80 u row lattice through every exposed side,
top and bottom band, with deterministic phases derived from the same sequence.
Anchor the processor pair and lower rune field to the **left screen's bottom
edge**, the X/Y/Z readouts and upper rune field to the **right screen's top edge**,
and the four edge pieces to the actual view corners. Extend noise, vignette
and hurt coverage to all four viewport boundaries. Never stretch hexagons,
text or corner slopes, and never leave a 4:3 rectangle visible in a wide or
portrait view. Keep the quicksave notice centered near the top.

**Hurt and reduced motion.** Hurt is a separate full-view red edge pulse
starting at `(0.6,0,0)` and falling to black over 1000 ms; it must work even
when activation is hidden. Reduced motion preserves the timed activation,
readouts and damage feedback using steady low-alpha cells and a gentle fade,
without travelling noise or repeated flashes. It must not alter the map script.

The editable [implant atlas sheet](ui/atlas/medlabs-implant.html) exposes a
timeline, Replay and Hurt, and 4:3, 16:9, 21:9, 32:9 and 9:16 previews.
`tools/ui/update_design_atlas.py` merges it into the full atlas. This specifies
the retained replacement; its vehicle HUD migration remains tracked in the
plan rather than being implied by the prototype.

### 14.20 Recovery, death and loading continuity

**Air Defense crash awakening.** Only `airdefense1`'s `cin_opening` camera
receives this treatment, driven by the three authored black fade-ins and two
blackouts. Each wake restores acuity over the original fade duration plus
2300 ms, starting at 16 dp Gaussian sigma and 0.62 saturation. A warm brown
periphery and asymmetric, feathered upper/lower eyelid shades open with it;
the middle remains unobstructed. A single subtle focus hesitation at 1.8–4.1 s
suggests difficulty staying conscious. Blackout closes the same treatment
under the original opaque fade, on its original deadline. Do not add white
flashes, camera motion, audio, or script delays. Reduced motion or reduced
flashing omits the hesitation. Camera exit, death, restore and disabling the
retained interface discard the transient state. Every wake clears fully,
including the final return to gameplay; the dropship sequence is unaffected.

**MCC treatment recovery.** The opening of `mcc_2` is Kane regaining
consciousness after medical treatment. Preserve the three authored fades,
doctor dialogue, bed camera, objectives and release timing. During each white
fade-in, recover acuity with a Gaussian blur falling from at most 20 dp to
zero and saturation rising from 0.70 to 1.00, with a quiet warm peripheral
shade. Recover smoothly through the fade and the following four seconds.
The last white fade after leaving `mcc2_playerBed` ends the sequence; unrelated
white fades later in the map do not trigger treatment recovery. The effect
sits below the readable HUD and subtitles and covers the full viewport.

**Death.** Preserve the original camera, respawn and fade-to-black deadlines.
Over 1800 ms, smoothly drain scene saturation to 0.06 and soften it to
3.4 dp sigma. One extra focus swell rises over 320 ms and settles by 1570 ms,
adding at most 4.5 dp to the same filter. A charcoal peripheral shade darkens
the corners while the center retains scene detail. A second, softer inward
shade arrives from 300 to 2400 ms, without moving or cropping the camera.
A restrained warm afterimage eases in over 120 ms and decays by 1400 ms;
reduced motion or reduced flashing omits both transient accents. The HUD remains
sharp, there is no camera shake or strobing, and revival, restore or changing
the interface preference clears transient effect state. Classic retains the
original view effects.

**Before loading.** Finish a 240 ms smooth fade of the outgoing view to
opaque black before unloading the level or showing the loading screen.
Reduced motion uses 80 ms. The rectangle covers the full physical view,
including side screens and tall bands. Present a terminal black frame even
when frame time skips across the deadline. Keep that frame until the loading
presentation takes over; retain the existing loading-screen reveal.

## Appendix A. Stock survey method

The 1.1 survey read the 30 installed `q4base` archives of the Steam 1.4.2
release in load order and took the effective copy of every path: 264 GUI files
(two of them includes), the `gfx/guis` interface art, the 12, 24 and 48 point
`.fontdat` atlases of six faces, and the material and table declarations. It
parsed every window's rectangle, background, colors, font, `textscale`,
`textspacing`, alignment, shear and rotation, and every event's transitions,
`onTime` keys and sound commands. It measured alpha and color profiles of each
furniture texture (visible bounds, cuts, rails, fades, baked rail colors),
traced the framing-band silhouettes column by column, took glyph ink from the
atlases, and read blend modes, `scroll`/`rotate` stages and `guitable_*` pulse
tables from the materials. Static composites of the stock settings page, home
screen and modal, built from the measured values, were compared against engine
captures of openQ4's current bitmap menus.

The 1.1 numbers came from one-off measurement scripts over those archives;
no extracted data is tracked. Re-measure from the installed archives when a
value is disputed, and record the method with the new value.

The 1.3 survey reread the same archives for every menu, HUD, cursor, loading
and objectives script. It resolved each screen change to absolute times,
measured the band, frame and HUD textures, and read the engine's transition,
list and window code and the 1.4.2 SDK game code for behavior the scripts do not
contain. An overlay of center-pivot gauge outlines on an openQ4 capture
confirmed the shear rule.

The 1.4 survey read the objectives, vehicle, scope and weapon-display GUIs and
the multiplayer HUD, scoreboard, summary and chat line, with the 1.4.2 SDK's
objective and multiplayer code. Static composites rendered from the retail art
and fonts checked the layouts; nothing was captured in play.

The 1.5 survey went deeper into the top of the multiplayer HUD, the chat box,
the chat input and the kill feed across every mode, match state and viewer
state, reading the retail engine's text clipping and wrapping rules from the
decompile. It inventoried and measured all 31 crosshair textures and every item
icon, with their materials, colors and uses in both HUD families. It also
resolved the credits, intro and medlabs translations, the rune face and the MCC
briefing transmission to absolute times. Every crosshair and icon was redrawn
as vector paths and compared with the stock texture at full size and at HUD
sizes. Composites rendered from the retail art checked the multiplayer layouts;
nothing was captured in play.

## Appendix B. Measured stock geometry

### B.1 Furniture textures

Rows are texel measurements. Divide by the texture size and multiply by the
drawn rectangle to obtain source units.

| Texture | Size | Visible rows | Cut | Rails | Fill and fade |
| --- | --- | --- | --- | --- | --- |
| `b3_light` | 512x32 | 3–27 | Rows 14–27, lower leading | Leading, cut, bottom; 1 texel, white | 0.49 to 50% width, then linear to 0 |
| `b4_light` | 128x32 | 3–27 | Rows 14–27, lower leading | As `b3_light` | 0.49 to 33% width, then linear to 0 at 95% |
| `b6_light` | 256x32 | 3–27 | Rows 14–27, lower leading | As `b3_light` | 0.49 to 33% width, then linear to 0 |
| `b1_dark` | 512x64 | 16–45 | Rows 32–45, lower leading | Leading, cut, bottom; `#CCCC51` | Opaque to 44% width, half at 62%, 0 at 88% |
| `b2_dark` | 256x64 | 16–45 | Rows 32–45, lower leading | As `b1_dark` | Opaque to 57% width, half at 72%, 0 at 94% |
| `b5_dark` | 256x64 | 16–45 | Rows 32–45, lower leading | As `b1_dark` | Opaque to 44% width, half at 55%, 0 at 78% |
| `header` | 512x32 | 11–28 | 8 texels, upper leading | Top and leading | 0.49 to half the width, 90% of that at 55%, half at 75%, 10% at 95% |
| `scoreheader` | 512x32 | 2–28 | 8 texels, upper leading | Top and leading | As `header` |
| `tooltip_edge` | 256x16 | From row 2 | 2 texels upper leading, 8 texels upper trailing | 1 texel `#A0A040`, top and sides | Opaque black |
| `popup_top` | 512x32 | From row 4, columns 7–504 | Tooth at 7–13, slot from 14 to 388 narrowing 1 texel per row to row 22, full width from row 23; raised trailing section 388–504 | None | White art, drawn black at 0.70 |
| `popup_btm` | 512x64 | 0–57 | Leading edge moves 1 texel per row from row 14 to row 57 | None | As `popup_top` |
| `ctrls_tab1` | 512x64 | Tab rows 1–26 | Leading fillet 3 texels; trailing shoulder 14 texels at 45 degrees | Outline and baseline at row 26 | Alpha 1.0 at top to 0.02 by row 52 |
| `corner` | 32x32 | Solid rows 8–24, columns 8–23 | Hypotenuse from upper leading to lower trailing | Halo to rows 1–29, columns 3–30 | White |
| `box`, `box_check` | 32x32 | Outline rows and columns 1–30 | None | 2-texel stroke | Inset square, rows and columns 5–26 |
| `bg_grid` | 512x512 | Crosses every 32 texels | None | 1-texel arms spanning 21 texels | White |
| `popup_bg` | 512x512 | Columns 1–510 | None | None | Additive; uniform across, graded down: 90% over rows 212–297, half at 141 and 369, zero outside 47–463; plateau `#454E1B` |
| `tooltip_edge2`, `tooltip_mid2` | 128x16, 128x2 | Columns 2–125 | 2 texels upper leading, 8 texels upper trailing | As `tooltip_edge` | As `tooltip_edge`; keeps the 8 u cut on 128 u cards |
| `chatbox_edge`, `chatbox_mid` | 512x16, 512x8 | Columns 1–511 | 2 texels upper leading | `#CCCC51`, top row and leading column | Top rail full to 41%, half at 65%, 10% at 90%; fill black 0.49 |
| `ctrls_tab2`–`4` | 512x64 | Columns 17–489 | 3-texel leading flare, 14-texel trailing shoulder | White outline, baseline at row 26 | Fill `#5A652A` 1.0 at row 1, 0.60 at row 26; wash 0.57 at row 27 to 0.02 at row 52 |
| `horiz_line2`, `vert_line2`, `vert_line` | 256x8, 8x256 | Two texels at the center | None | — | White; `vert_line` fades over its last 6% at each end |
| `bg_line`, `bg_hover`, `bg_focus` | 128x64 | Rows 3–60, rule at row 62 | Leading fade about 5%, trailing 13% | White 0.50 rule | `#FF9C00` at 0.01, 0.16 and 0.41 |
| `bg_line2`, `bg_hover2`, `bg_focus2`, `bg_grey2` | 512x64 | Rows 3–60 | Ends fade: 90% over 22–78%, half at 12% and 87% | No rule | `#9A9C6E` 0.08 and 0.29, `#FF9C00` 0.50, `#787E87` 0.41 |
| `scrollbar_thumb` | 16x16 | Columns 4–15 | 2-texel bevels at the trailing corners | Rows 1 and 14 and column 14 at 0.70 | Interior 0.13, `#B0B87A`; open leading side |
| `scrollbarv` | 16x16 | Columns 1–14 | None | Column 1 at 0.50 | 0.13 falling to 0.03 toward the trailing side |
| `bg2` | 512x32 | Columns 45–466, rows 3–27 | Lower leading 14 texels; parallel trailing end cut of 24 texels | None | Opaque white |
| `load_corner` | 64x64 | Legs in rows and columns 18–46 | L bracket, 29-texel legs | 1-texel white core | Additive warm halo `#502D14` spreading 9 texels |
| `load_dots` | 128x32 | Columns 12–115, rows 7–23 | Ten by two 5x5 squares on an 11 x 12 pitch | — | White |
| `scoregrad2` | 512x512 | Opaque 58–453 x 35–476 | About 7-texel rounded corners | None | White; 13-texel side feather |

### B.2 Framing band silhouettes

Band-local source units; both band rectangles are 1045x129 u. Diagonal
segments between listed vertices are 45-degree flanks; the coordinates carry
the stock texels' rounding.

Top band inner edge (x, y): (0, 89.7), (102.1, 89.7), (116.3, 103.8),
(430.7, 103.8), (440.9, 94.7), (476.6, 94.7), (485.8, 103.8), (527.6, 103.8),
(537.8, 94.7), (572.5, 94.7), (581.7, 103.8), (623.5, 103.8), (633.7, 94.7),
(668.4, 94.7), (677.6, 103.8), (715.4, 103.8), (752.1, 68.5), (1045, 68.5).

Bottom band inner edge (x, y): (0, 70.5), (166.3, 70.5), (176.5, 79.6),
(370.4, 79.6), (401.1, 49.4), (743.9, 49.4), (788.8, 92.7), (981.7, 92.7),
(990.9, 83.6), (1045, 83.6).

| State | Top band origin | Bottom band origin |
| --- | --- | --- |
| Home | -400, 0 | -399, 351 |
| Page | -77, -63 | -26, 386 |
| In-game multiplayer and buy menus, closed | -247, -129 | -198, 481 |
| In-game multiplayer and buy menus, docked | -75, -61 | -25, 386 |

Both bands are 1024x128 texel art drawn at 1045x129 u. The in-game states draw
the bands at 0.40.

The loading bands are separate art: `load_top_edge` (512x128 texels drawn at
640x119 u) and `load_btm_edge` (512x256 at 640x253 u). Top band inner edge
(x, y): (0, 28.8), (268.8, 28.8), (290, 9.3), (640, 9.3). Bottom band inner
edge, band-local: (0, 123.5), (165, 123.5), (207.5, 81), (640, 81), which is
468.5 and 426 u with the band at 345 u. Their `edgeadd` companions add
#181D0A from the inner edge into the picture, 0.98 at the edge to 0.55 at
35 u.

### B.3 Text size conversion

| Face | `textscale` | Em (dp) | Cap height (dp) |
| --- | --- | --- | --- |
| Marine | 0.22 / 0.25 / 0.26 / 0.28 / 0.31 / 0.33 / 0.36 / 0.40 | 15.8 / 18.0 / 18.7 / 20.2 / 22.3 / 23.8 / 25.9 / 28.8 | 7.9 / 9.0 / 9.4 / 10.1 / 11.2 / 11.9 / 13.0 / 14.4 |
| Lowpixel | 0.16 / 0.18 / 0.20 / 0.22 / 0.24 / 0.31 | 11.5 / 13.0 / 14.4 / 15.8 / 17.3 / 22.3 | 8.6 / 9.7 / 10.8 / 11.9 / 13.0 / 16.7 |
| Chain | 0.32 / 0.40 / 0.50 | 23.0 / 28.8 / 36.0 | 14.4 / 18.0 / 22.5 |
| Profont | 0.18 | 13.0 | 9.7 |
| R_Strogg | 0.40 / 0.50 | 28.8 / 36.0 | 19.2 / 24.0 |

### B.4 Weapon and item color code

| Item | Color | Item | Color |
| --- | --- | --- | --- |
| Machinegun | 1, 1, 0 | Rocket launcher | 1, 0.2, 0 |
| Shotgun | 1, 0.5, 0 | Railgun | 0, 1, 0 |
| Hyperblaster | 0, 0.45, 1 | Lightning gun | 1, 1, 0.73 |
| Grenade launcher | 0.2, 0.56, 0.07 | Dark Matter gun | 0.77, 0, 1 |
| Nailgun | 0.6, 0.8, 0.8 | Gauntlet | 0, 0.85, 1 |
| Health shard / small / large / mega | 0.5, 1, 0.5 / 1, 1, 0.2 / 1, 0.5, 0 / 0, 0.5, 1 | Armor shard / small / large | 0, 0.5, 1 / 1, 1, 0 / 1, 0, 0 |
| Napalm launcher | 1, 0.75, 0.25 | | |
| Core Cannon (*The Awakening*) | 1, 0.35, 0.78 | Freeze Gun (*The Awakening*) | 0.62, 0.92, 1 |

Values come from `hud.mtr`. The multiplayer ammo gauge uses its own tints for
the shotgun (1, 0.55, 0), rocket launcher (1, 0.25, 0), lightning gun
(1, 1, 0.6) and dark matter gun (0.77, 0.2, 1), and adds the napalm launcher
(1, 0.75, 0.25). *The Awakening* tints all three of its weapons' pickups yellow
(1, 1, 0); openQ4 gives the Core Cannon and the Freeze Gun their own codes,
which no stock weapon uses, and the napalm launcher its stock one.

### B.5 HUD textures

Gauge art is 256x128 texels drawn at 125x59 u (0.488 u per texel across,
0.461 down).

| Texture | Construction in the drawn rect |
| --- | --- |
| `backbar` | Body 123.0x40.1 u with 1–1.4 u radii; a trailing foot 6.9 u deep, 39.1 u wide at the top and 30.9 u at the bottom, with a 45-degree leading flank |
| `valbar` | The fill outline with its notch; luminance 0.58 inside and 0.93–0.97 at the edges over about 7 u |
| `arbar` | The `valbar` outline; interior luminance 0.69; nine dividers every 24 texels (11.7 u), 3 texels wide, alpha 0.61 |
| `backbar_add` | An inner-edge glow of the backing silhouette, full at the edge and gone about 17 u inside; drawn additively |
| `ekg` | Black with a white trace: baseline at 58% of the rect; two identical beats per width, each a P wave (+3.5 u), a Q dip (−2.5 u), an R spike (+17.5 u), an S trough (−6.2 u) and T and U waves (+6.2 u, +3.0 u), all with straight flanks; a 2-texel core and a halo from 0.24 to nothing over 8 texels |
| `waveform`, `waveform_mask` | A recorded voice trace: a 4-texel core on rows 64–67 and per-column spikes up to 41 texels above and 36 below, 71 of 256 columns flat. The mask is a right trapezoid: rows 2–29, the leading edge from column 21 at the top to 15 at the bottom, the trailing edge at 51. The Strogg mask leans both edges back (11 to 17 and 45 to 51) |
| `radio_rhino` | The Rhino squad emblem, alpha only: a rounded square frame and a rhino head with speed streaks |
| `aud_wavbg` | The voice bar: rows 1–14, alpha 0.85 to 0.14 across, a hard slanted end at 87% |
| `wsbar`, `wsbarglow` | A 24.4 u rounded square with a radius of about 1 u; the glow is a hard 1.6 u ring on the same outline |
| `noammo` | A red prohibition sign about 15 u across with a 2.3 u stroke, slashed from upper trailing to lower leading |
| `pickupbar`, `hud_rev3/radiobar` | Solid rounded rects, 226.4x21 u and 109.4x24.4 u, radius about 1 u |
| `powerup_bgbar` | A parallelogram 67.7 to 60.7 u wide and 30.2 u tall, alpha 1.0 at the square end to 0.44 at 80%, then zero at the slanted end |
| `directional_hit` | A soft dome of light at the top of the canvas; core 144x46 u, peak 151 u above the center |
| `s_bossbar` | A 185.8x43.7 u body with 14.2 u 45-degree chamfers at all four corners and a 35.8 u tab 3.9 u deep at the bottom center |
| `s_boss_healthbar` | A 146x18.7 u hexagon with 45-degree points 9.2 u long, bevelled from 0.67 inside to 1.0 at the edges |
| `s_boss_shieldbar` | A 6.9 u C bracket with a chevron point at its outer end; the two halves face each other around the health bar |
| `veh_backbar`, `veh_valbar`, `veh_arbar` | The Marine gauge art without its leading 35 u |
| `veh_backbar2` | A 92x73 u plate with a 26 u 45-degree chamfer at the upper-trailing corner and 1–2 u radii |
| `obj_btn1`–`obj_btn4` | Open plates: a 1-texel leading rail, a 14-texel lower-leading cut through the foot strip, a bottom rail, and fill at 0.49 fading from 27–46% of the width to nothing by 55–75% |
| `ffa_bgbar`, `ffa_bgbar2` | Score slabs: alpha 1.0 to 12% of the width, half at 44%, ending near 0.14 in a cut that leans with the 0.22 shear at 70–73%; the band adds 3.5-texel rims |
| `ctf_bgbar`, `ctf_bgbar2` | Team slabs: 0.87 at the screen edge, half at 34%, 0.15 at 81%, then a sheared cut; the color band's rims fall from 1.0 to 0.6 over 7 texels |
| `tourntop_msg` | An opaque band with a 45-degree trailing cut |
| `tourn_sep`, `tourney_box1`–`3` | An I-beam separator; closed 1-texel outlines with softened corners |
| `onectf_arc` | Two soft quarter arcs forming an open bracket |
| `ctf_flag` | A white flag silhouette with its pole on the trailing side and a baked shadow |

## Appendix C. Change record

### Version 1.1

| Area | 1.0 | 1.1 | Basis |
| --- | --- | --- | --- |
| Panels | Equal 12 dp upper and 8 dp lower cuts; 1 dp rail plus 3 dp inset rail; popovers 6 dp; bands 16–24 dp | Card: 12 dp and 3 dp cuts on diagonally opposite corners, one rail, no inset rail; modal silhouette; popovers keep 6 dp; bands follow measured silhouettes | `tooltip_edge`, `popup_*`, `topbar`, `btmbar` |
| Buttons | 40 dp high; 12 dp cut; 8 dp label gap | Visible plate 35–37 dp in a 45 dp target (29 dp in a 36 dp row); cut 47–56% of the plate; 4 dp label gap; 112 dp minimum width and 12 dp marker inset kept | `b*_light`, `b*_dark`, stock placements |
| Marker | 8 dp at 12 dp inset | 8 dp, 9 dp beside 24 dp navigation labels; inside the label's cap band | `corner` placements |
| Checkbox | Cut outline, vector tick | Square outline, inset filled square; settings use cycling values | `box`, `box_check`, settings pages |
| Slider | Recessed track, filled span, cut thumb | Tick ramp, bar thumb; ticks past the thumb at 40% | `slider_bg`, `slider_bar` |
| Type ramp | Title 28, panel 20, button 18, body 16, metadata and table 14 dp | Table in section 5; screen title 18 dp at 50% | Glyph ink, stock `textscale` |
| Tracking | Slightly tight everywhere | Face spacing by default; tightened for listed roles | Stock `textspacing` use |
| Opacity | Primary 0.62, secondary 0.28, rail 0.95, header wash 0.16 | Stock ladder; join-card values kept for in-game partial panels | Stock rest and hover values |
| Motion | `hover.leave` cubic; `modal.enter` 180 ms with scale; `modal.leave` 120 ms | Linear `hover.leave`; stock modal choreography; stock screen choreography tokens | Stock timelines |
| Added | — | Measured tokens and family values, composition modes, stock grid, band geometry, HUD geometry, symbol inventory, bitmap classification, stock screen reference | Survey |

[Register schema 2](ui/product-requirements.json) records the supersessions
this version requires: ART-003 (alpha ladder and tokens) by ART-018, ART-004
(panel cuts and inset rail) by ART-019, ART-006 (button anatomy) by ART-020,
MOT-003 (motion tokens) by MOT-009, TXT-006 (type ramp) by TXT-010, WID-002
(tick) by WID-017 and WID-004 (slider track and thumb) by WID-018. It also
appends REN-014 (additive and darkening composition), WID-023 (cycling values
and spinners), MOT-010 (screen-change choreography), MOT-011 (ambient loops
and alarms) and QUAL-010 (stock parity comparison). Quoted values that still
hold include ART-005 (panel header and 8 dp marker), WID-012 (tooltip timing)
and LAY-005 (margins).

### Version 1.2

Version 1.2 adds section 13. Section 3, section 7 and section 11 gain
cross-references and input qualification, and the touch minimum in section 3
rises from 44 dp to 48 dp to match the Android touch profiles.

| Addition | Summary |
| --- | --- |
| Viewing profiles | Desk, Couch, Handheld, Phone and Tablet defaults for scale, targets and presentation |
| Size classes | Compact height, compact width, regular and expanded structures; safe areas and gesture margins |
| Input model | Modality switching, focus memory, the prompt bar and the detail area |
| Controller | Positional button map, glyph families, value editing, on-screen keyboard, haptics, weapon and chat wheels, terminal assist |
| Touch and Android | 48 dp targets, gestures, lifecycle, first-run data setup, a first-party touch gameplay overlay and its editor, mobile rendering limits |
| Screen patterns | Title, pause, settings search and markers, lists, Arena, Match Control, demo playback, loading, notices |
| Visual refinements | Parallax depth, focus light, title continuity, extended symbols, tags, color-vision palettes, dynamic range |
| HUD presets and accessibility | Classic, Remastered, Minimal, Competitive and Touch presets; the accessibility baseline |
| Layouts | Remastered by default; Classic as the stock-parity reconstruction |

Register schema 2 supersedes LAY-007 with LAY-013 for the 48 dp touch minimum
and appends LAY-014 to LAY-017, INP-009 to INP-013, WID-019 to WID-022, ART-021,
ART-022, FLOW-019 to FLOW-028, PERF-009 and QUAL-011 for the section 13 scope.
The register validator's revision mode audits such appends and supersessions.

### Version 1.3

Version 1.3 measures the framing-band motion, the HUD and the pop-up frames in
a second survey of the effective retail GUIs, corrects what the first survey
got wrong, and adds modern components and a HUD section.

| Area | 1.2 | 1.3 | Basis |
| --- | --- | --- | --- |
| Motion profile | `trapezoid(150, 150)` with a constant-speed phase | `accel(250, 250)`: stock acceleration arguments are fractions of the duration | Engine transition code; 369 stock transitions |
| Screen change | Content out in 250 ms; a 150 ms title fade between settings categories | Per-element fades of 50, 100 and 250 ms, 640 u sweeps, per-destination backings, instant category switches, in-game menus without a home state | `mainmenu`, `mpmain` and `buymenu` timelines |
| Rim light | 36% over 18 u | 37% over 19 u, part of the band art | `topbar`, `btmbar` |
| Modal | 480 dp; actions fade in after the frame | Five widths; title, body and actions appear together at 200 ms; frame fade 200 or 250 ms | `popup_*` and modal timelines |
| Lists and scrollbar | Selection 42%; arrow buttons | Selection 41%, 50% in server lists; no arrow buttons; a fixed thumb | `bg_*`, list and slider code |
| Popovers | 6 dp major cuts | 12 dp, and 10 dp on one-line hints | `tooltip_edge2` |
| HUD | Gauge outline; shear about the GUI origin | Measured gauges, shear about each element's center, stock layout and behavior | `hud`, `hud_strogg`, `mphud`, `cursor` |
| Multiplayer icons | Health `#FA2B05`, armor `#F7C004` | Health `#F7C004`, armor `#FA2B05` | `hud.gui` |
| Added | — | Band motion rules, pop-up frame catalogue, detailing rules, modern components, section 14 with the weapon wheel, B.5 HUD textures | Survey and design |

Register rows ART-018 (list selection alpha), ART-019 (popover cuts), MOT-009
(the trapezoid band profile), WID-021 (the wheel's cut segments) and FLOW-027
(crosshair sizes and hit markers) quote values this version replaces, and the
pop-up catalogue, detailing rules, modern components and section 14 have no
rows yet. They need a register revision before these values serve as
acceptance criteria.

### Version 1.4

Version 1.4 surveys the rest of the HUD: the objectives display and its
notices, the boss, vehicle and scope displays, the weapon displays, the
multiplayer HUD, the scoreboard and the match summary.

| Area | 1.3 | 1.4 | Basis |
| --- | --- | --- | --- |
| Objectives | Panels over a darkened view with flicker-in steps; notices closing after 5 s or 64 units | Open plates beside a leading-edge bar, the three newest objectives, a 150 + 100 ms entry; notices timed from where the player stands at 2 s; the failure banner | `wristcomm`, SDK objective code |
| Mission failed | A 3000 ms fade and "Mission Failed" | No stock sender; failure is the objectives banner under a 12 s fade | SDK and stock scripts |
| Exit chevron | Rises and fades once a second | `accel(500, 500)` over 11 u | `hud.gui` |
| Kill feed | Text lines | Attacker, Quad mark, weapon icon and victim, with team-colored names | `hud.gui`, `MultiplayerGame.cpp` |
| Multiplayer family | Lists `spawn`; Tourney colors without roles | Lists `mpmsgmode`; Tourney roles, team text colors, header olive | `mphud`, `scoreboard` |
| Scoreboard rows | 22–24 dp | A pitch adapting to up to 16 entries | `scoreboard.gui` |
| Weapon displays | One palette | Per-weapon displays; five weapons without one | `guis/weapons` |
| Added | — | Sections 14.11–14.14, the Strogg weapon wheel, Remastered multiplayer and objectives additions, the rule keeping menu backdrop layers off the HUD | Survey and design |

Register schema 3 records the supersessions version 1.3 requires and appends
requirements for the scope of versions 1.3 and 1.4.

### Version 1.5

Version 1.5 lays out the HUD in screens, redesigns the multiplayer HUD's top,
chat and kill feed, and brings every crosshair and item icon into vector art. It
also gives the weapon wheels the stock's own translation and transmission
effects.

| Area | 1.4 | 1.5 | Basis |
| --- | --- | --- | --- |
| HUD layout | Groups anchored to safe corners; gauges bottom-leading | A 4:3 center screen with leading and trailing screens and top and bottom bands; nine anchors with stock offsets; the status bar centered; a HUD width limit at 21:9 | Product owner's review; stock offsets |
| Pickup lines | Up to three, over the gauges | Up to two, centered over the status bar, with the weapon strip above them | Layout |
| Radio chatter | Bar and waveform | Instant show and hide, the masked carrier's scroll and height steps, the Rhino emblem, the Strogg variant | `hud.gui`, `hud_strogg.gui`, `Misc.cpp` |
| Objective notices | Top corners with the stock back bars | Leading and trailing screens, the completion notice mirrored, 200 u plates with their own backing, no back bars | Product owner's review |
| Multiplayer top | The stock slabs and panels with Remastered additions | Compact plates: standing with gaps and limits, team panels with flag, carrier and zone states, one timer readout per state, the restyled Tourney strip, a stacking center column, the match-context card | `mphud.gui`, SDK multiplayer code |
| Kill feed | Stock lines, you on a band | Per-line plates, ellipses that keep the victim, team-kill tags, a fading Tourney feed, spectators included | `hud.gui`, retail text rules |
| Chat | Stock lines and input with a channel tag | A leading-side box beside or above the status bar, per-line plates, whole messages, eight lines while typing, a scrolling entry with a remaining-character count and prompts | `mphud.gui`, `mpmsgmode.gui` |
| Dead Zone flash | White to the team color | Pure team color relaxing to white | `mphud.gui` expressions |
| Weapon wheel | Plates, icons, an ammunition arc; `wheel.open` 90 ms | The Marine ammunition gauge, bezel and signal-screen hub; transmission (Marine) and translation (Strogg) transitions | MCC briefing, credits, stroggification monitor |
| Translation | One "decode reveal" | Credits translation, intro decode and changeover as separate effects, plus the Marine transmission | `mainmenu.gui`, `cinematic.gui`, medlabs and MCC GUIs |
| Crosshairs | Stock art; context cursors kept as art | A vector catalogue of all 31 textures; the stock cycle order; hit markers outside the art; colors applied at once | `cursor.gui`, `player.def`, crosshair textures |
| Item icons | Weapon renders and powerup emblems as bitmap exceptions | A vector catalogue of every item icon; medals remain bitmaps | `gfx/guis/hud/icons`, `simpleicons`, materials |
| Weapon strip | #FF8000 selection in both modes | Multiplayer keeps the weapon color code | `hud.mtr`, multiplayer weapon defs |

Register schema 4 supersedes LAY-018 with LAY-019, FLOW-029 with FLOW-044,
FLOW-033 with FLOW-043, FLOW-034 with FLOW-040, FLOW-035 with FLOW-041,
FLOW-036 with FLOW-042 and WID-024 with WID-032. It also appends FLOW-045 (chat),
ART-027 (crosshairs), ART-028 (item icons) and MOT-015 (translation and
transmission effects).

### Version 1.6

Version 1.6 adds the loading screens and the multiplayer menus, redraws the EKG
and the transmission waveform from their textures, and moves the scoreboard's
column headings out of the first row.

| Area | 1.5 | 1.6 | Basis |
| --- | --- | --- | --- |
| Loading screens | The stock composition (section 9) and one screen-pattern line | Section 14.17: the stock screens measured, and a Remastered screen with extended bands, the mission and objectives, named stages, tips, a device-aware continue prompt, and a multiplayer server card and arsenal | `guis/loading/*.gui`, `Session.cpp`, `mpgame/Game_local.cpp` |
| Multiplayer menus | A pause pattern and the join card | Section 14.18: a card with a horizontal tab strip over the softened view, in Welcome and Escape variants, with every page's content | `mpmain.gui`, openQ4's join card and Match Control |
| In-game panel ladder | The join card's values | The same values for every in-game partial panel, over the stock light plates, with focus at 1.00 | openQ4's `p_quickjoin` |
| EKG | Beats, speed and colors | The traced waveform and its halo; no smoothed curves | `ekg.tga`, `hud.gui` |
| Transmission waveform | A trapezoid mask and a canned table | The traced voice trace, the right-trapezoid mask, a Remastered height that follows the line, and the traced Rhino emblem | `waveform.tga`, `waveform_mask.tga`, `radio_rhino.tga`, `tables.mtr` |
| Voice indicator | Stock bars; Remastered bars above the chat | The stock bars measured; Remastered talker plates with team-colored traces | `mphud.gui`, `aud_wavbg.tga` |
| Scoreboard headings | Headings over a rule | Headings in their own strip under the header band, never inside a row | `scoreboard.gui` |

Register schema 5 appends FLOW-046 (multiplayer menus), FLOW-047 (loading
screens) and ART-029 (instrument traces). It supersedes no row.

### Version 1.7

Version 1.7 restores the Quake emblem to the title screen, sets the
single-player pause menu apart from it and adds a Strogg pause menu.

| Area | 1.6 | 1.7 | Basis |
| --- | --- | --- | --- |
| Title screen | Continue's detail in the emblem's place | The emblem watermark as vector art with a rim light and a glint; Continue's detail in the message line | `mainmenu.gui`, `q4logo`, the product owner's review |
| Single-player pause | The title screen with in-game actions | GAME PAUSED, seven actions from 172 u, and the current level block in the emblem's place | The product owner's review |
| Strogg pause | None | The Strogg family's bands, plates and labels, translating from runes | Strogg HUD, credits translation |
| Ammo gauge | The weapon's render | The selected weapon's own render, placed as the stock places it; none in multiplayer | `hud.gui`, weapon `mtr_icon` |

Register schema 6 appends FLOW-048 (title and pause menus) and ART-030 (the
emblem watermark). It supersedes no row.

### Version 1.8

Version 1.8 rebuilds the initializing screen from separate layers and turns its
rings slowly.

| Area | 1.7 | 1.8 | Basis |
| --- | --- | --- | --- |
| Initializing screen | Not specified | The stock picture measured and rebuilt as separate vector layers; the plate band and the teeth turn slowly in opposite directions; a Marine status line with a step rule | `splash.tga`, `PrintLoadingMessage`, the product owner's request |
| Quake 4 wordmark | Classified individually | Vector letterforms traced from `q4text`, with their halo as vector light | `q4text.tga` |

Register schema 7 appends FLOW-049 (the initializing screen) and ART-031 (its
layers and the traced lettering). It supersedes no row.

### Version 1.9

Version 1.9 replaces the modal scrim with a soft focus of the screen beneath.

| Area | 1.8 | 1.9 | Basis |
| --- | --- | --- | --- |
| Modal backdrop | The stock `marine.scrim`, 0.94 black over the whole screen | The screen beneath in soft focus (a 5 u blur and 0.80 saturation, never dimmed), ramping with `modal.enter` and `modal.leave`; the scrim only with the opaque-backing option or where soft focus is unavailable | The product owner's review |
| Modal glow | The stock column, 87 u above and 108 u below the dialog | Cut to the dialog over the soft focus: the 6 dp lit margin, half strength at the dialog's edges and gone 24 dp beyond | The column read as a strip of light across the softened screen |

Register schema 8 supersedes ART-023 and ART-024, which quoted the 0.94 scrim,
with ART-032 and ART-033, and appends REN-016 (backdrop soft focus).

### Version 1.10

Version 1.10 adds a sub-page level beneath the menu pages.

| Area | 1.9 | 1.10 | Basis |
| --- | --- | --- | --- |
| Menu levels | Home and pages; the stock went deeper only within the Multiplayer screen, with the bands docked | Home, pages and sub-pages: Single Player leads to Campaign and Arena, Multiplayer to Join Game, Create Server and Demos | The product owner's review |
| Band dock states | Home and page | A sub-page state in Remastered: only the top band moves on one notch pitch (`frame.step`); the bottom band and Back stay docked | The stock notch pitch, 97 u |
| Path title | The activated label carried into the title slot | The page title becomes the crumb in the band's thin span, and the band's step separates it from the sub-page's title (`title.crumb`) | Section 13.8's path title |

Register schema 9 supersedes MOT-013, whose band motion rules allowed exactly
two dock states, with MOT-016, and appends FLOW-050 (menu levels).

### Version 1.11

Version 1.11 follows the product owner's review of the weapon wheel, the
multiplayer chat, the scopes and the squad patches, and brings The Awakening
into the HUD sections.

| Area | 1.10 | 1.11 | Basis |
| --- | --- | --- | --- |
| Weapon wheel hub | The name, the ammunition and the radio's carrier waveform, swelling on each selection | The name and the ammunition alone; the waveform belongs to the transmission display | The product owner's review |
| Multiplayer chat | Beside the status bar from 16:9, its bottom near the bottom edge | Always clear of the status bar vertically, its bottom 8 u above the gauges' top; below 16:9 also clear of the weapon strip | The product owner's review |
| Scopes | Apertures and marks; the railgun "black at about 86% outside" | Every stock layer measured and rebuilt: the machinegun's smear, glow and darkening lens; the railgun's band, edge line, haze and glow; the nailgun's three-band surround, hooks, glow, rings and lock arrow. Round at every aspect, with the railgun's sweep at 8% in Remastered | The stock scope GUIs, textures, materials and the `SniperStretch2` shader |
| Squad patches | Only the Rhino stencil, on the radio | Nine stock patches traced and five Awakening squads designed; the radio shows the player's squad stencil, Falcon's under The Awakening | The stock decals; the expansion's squad names |
| The Awakening | Not covered | Its thirteen weapons in the strip and wheel, icons, crosshairs and lock-on reticule, squad patches, scopes and displays, and vehicle displays: the space cannon's ship panel and cockpit screens, the MCC readout, the speeder's race HUD and the GEV HUD, with openQ4 art where the expansion ships none | The expansion's content survey |

Register schema 10 supersedes WID-032 (the wheel's waveform), FLOW-040 (the
Rhino emblem alone) and FLOW-045 (the chat beside the status bar) with
WID-033, FLOW-052 and FLOW-051, and appends FLOW-053 (scope layers), ART-034
(squad patches), ART-035 (The Awakening's icons and crosshairs) and FLOW-054
and FLOW-055 (The Awakening's HUD and vehicle displays).

### Version 1.12

Version 1.12 follows the product owner's requests for a portrait view, a
multiplayer weapon wheel, a Competitive HUD and a spectator follow camera, and
the review of the pop-up titles and the initializing screen.

| Area | 1.11 | 1.12 | Basis |
| --- | --- | --- | --- |
| Portrait | Play landscape-only; portrait layouts a pattern to avoid | The 9:16 portrait view at a 720x1280 dp reference: the center screen spans the width between top and bottom bands, the touch deck takes the bottom band with the readouts above it, menus take the compact-width structure and the multiplayer card keeps its dp size | The product owner's request |
| HUD screens | The center screen always fills the view's height | It fills the height in views 4:3 and wider and the width in narrower ones | The vertical-expansion rule, which 5:4 already followed |
| Modal title and glow | A bare title; the glow as wide as the dialog plus the 6 dp lit margin, gone 24 dp above and below | A thin black outline 1 dp around the title's strokes at 0.85; the glow fading out over a further 16 dp at each side and gone 40 dp above and below | The product owner's review |
| Multiplayer wheel | Not specified | The Marine wheel for every team: the multiplayer roster, color-coded icons, the reserve alone and the team's text color on the ring, ticks, hub and pointer | The product owner's request; the multiplayer strip |
| Competitive HUD | A preset named in one sentence | The match bar's geometry, timer and states, network quality, team status at the leading edge, readouts at 115%, and the vote and Tourney placements | The product owner's request |
| Spectators | The stock breathing lines in the center column | A follow card above the status bar with the active device's prompts, free-camera name tags and both teams' lists in Competitive; Classic keeps the stock lines | The product owner's request; `mphud` Spectate0 and Spectate1 |
| Initializing screen | Turning rings and a status line replaced at once | Remastered accents: a light sweep every 10 s, a breathing halo, embers, a glint across the lettering every 9 s, a pulsing step segment and a flicker on each step; reduced motion keeps a fixed key light | The product owner's review |

Register schema 11 supersedes INP-013 (play landscape-only), LAY-019 (the center
screen at full view height), ART-033 (the modal glow), FLOW-041 (spectator lines
in the center column) and FLOW-049 (the status line replaced at once) with
INP-014, LAY-020, ART-036, FLOW-056 and FLOW-057, and appends LAY-021 (the
portrait view), WID-034 (the multiplayer wheel), FLOW-058 (the Competitive HUD)
and FLOW-059 (the spectator follow camera).

### Version 1.13

Version 1.13 follows the product owner's review of the vehicle displays.

| Area | 1.12 | 1.13 | Basis |
| --- | --- | --- | --- |
| Exit, lock and boost lines | The stock's bracketed lines, such as "[ Press JUMP to exit ]", which name an action rather than its control | HUD prompts in the menus' construction: the active device's glyph, then the action, on a HUD plate at the bottom-trailing corner; Vehicle locked under a lock in #FF8000; Classic at the stock lines' places without the plate | The product owner's review |
| Transmission display | Anchored top-trailing on foot; the vehicle displays drew it at the stock corner of the 4:3 canvas | The top-trailing corner of the right-hand screen on every HUD, vehicle panels stacked below it, and added where a vehicle's HUD has none (the air-defence cannon, the MCC side gun) | The product owner's review |
| MCC side gun | A hull readout, top-trailing | Confirmed without turret health: the gun cannot be damaged (`noDamage`, `health` 0, god mode for its gunner), so the readout is the MCC's hull, under the transmission display, with a 25% warning | The expansion's vehicle definitions |
| Speeder bike | The stock vehicle HUD and the race display | Remastered restores the gauges' warnings, fixes the swapped weapon glyphs, lowers the progress bar 18 u and restores Exit | The expansion's HUD survey |
| GEV | Bottom-leading | Bottom-leading in Classic; centered on the bottom edge in Remastered, as every status bar is | Section 14.4 |

Register schema 12 supersedes FLOW-037, which kept the stock exit and lock
lines, with FLOW-060, and appends WID-035 (HUD prompts) and FLOW-061 (The
Awakening's vehicle display refinements).

### Version 1.14

Version 1.14 corrects the modal against stock measurements made for the retained
confirmation dialogs: `popup_top` and an engine capture of the classic exit
dialog at 1280x720, whose capitals occupy rows 237 to 246 between the raised
line at 236 and the floor at 253.

| Area | 1.13 | 1.14 | Basis |
| --- | --- | --- | --- |
| Modal title | Its baseline on the raised sections' top line | Its capitals hang from that line into the lit slot; the baseline sits about 6 dp above the floor | The exit title's 149 u rectangle and 15 u line skip; the engine capture |
| Modal silhouette and glow | The silhouette filled its rectangle, with the lit margin beside it | The silhouette is inset in its rectangle (7/512 of the width at each side, 3.6 dp above, 5.3 dp below); the inset is the lit margin, and the soft-focus glow fades over 16 dp past the rectangle | `popup_top`, measured in Appendix B.1 |
| Modal body | Inset 33 dp, not placed vertically | Its first baseline 87.5 dp below the rectangle's top | The stock body rectangle at y 200 u |

Register schema 13 supersedes ART-036, whose modal title stood on the raised top
line, with ART-037.

### Version 1.15

Version 1.15 adds the measured Medical Labs implant activation as section
14.19, including its 4650 ms lifetime, independent hurt pulse and full wide/tall
composition. The atlas gains an editable sheet with five aspect ratios and
timeline controls. Register schema 14 appends `FLOW-062` for its remaining
retained vehicle-HUD migration. The retained title keeps pointer depth on its
scene image while stationary vector furniture avoids costly coverage rebuilds.

### Version 1.16

Version 1.16 adds a shared fourth menu level, campaign setup drafts and
matching forward/Back transitions; replaces the scope smear with a feathered
Gaussian surround for every optic; and specifies MCC treatment recovery,
death grading and a short black handover before loading (section 14.20).
Initialization now runs the layered artwork during real engine startup. Its
machined bands remain editable as radial color stops, the sweep uses conic
paint, and its full frame fits the Vulkan geometry budget through 4K.
Register schema 15 supersedes `FLOW-050` with `FLOW-063` and `FLOW-053` with
`FLOW-064`, and appends `FLOW-065` for the game-driven continuity effects.

### Version 1.17

Remastered incoming transmission and voice graphs plot actual audio sample
history, replacing fixed traces and synthetic level/phase animation. Silence
and unavailable audio stay flat; pause and seeking follow playback. The atlas
offers independent local audio playback for radio, received voice and your own
transmission, without recording a microphone or uploading files. Classic keeps
its measured texture and canned table. Register schema 16 supersedes `ART-029`
with `ART-038`, carrying the EKG, mask, squad emblem and talker layout contracts.
Other menu migrations and the complete-product acceptance gates stay open.

### Versions 1.18–1.19: wheel placement and scene focus

Version 1.18 keeps the original right-side wheel position and adds a saved,
signed horizontal offset with zero centered. Version 1.19 follows the product
owner’s request for quality soft focus on the scene: the wheel fades a full-view
Gaussian focus pass beneath the HUD and its own artwork. Register schemas 17
and 18 append `WID-036` (superseding `WID-033`) and `WID-037` (scene focus).
The broader wheel and quick-chat acceptance remains open.

### Version 1.20: the scoreboard and summary cards

At the product owner's request, the Remastered scoreboard and the match summary
drop the stock's soft backing (black fading to the sides) for the multiplayer
card of section 14.18, so every multiplayer screen shares one construction
(section 14.14).

| Area | 1.19 | 1.20 | Basis |
| --- | --- | --- | --- |
| Scoreboard | The stock board with Remastered rows | The card: header with your place and clock, match line, the Players page's bands and rows, ping bars, scrolling past its height | Product owner's review; section 14.18 |
| Tourney | The stock's closed outline tree | A bracket chart of open-plate arenas converging on the final, with lit links and your, followed and decided arenas | Product owner's review; `scoreboard.gui` events |
| Match summary | The stock summary with openQ4's fixes | The card with SUMMARY (result band, standings with best weapons, chat with a typed entry), STATISTICS (with match awards) and SCOREBOARD | Product owner's review; `summary.gui` |
| Your row | The marker in the Players page's olive | The orange ◥ in every list | Section 14.14 |

### Version 1.21: quick chat

Section 14.6's quick-chat wheel gains its control, its phrases and how its
channel switches: D-pad Down and V hold it open in multiplayer, alive, dead or
spectating; it opens on Team in a team game, where the zoom control switches
the channel and the hub shows that control's key; releasing inside the dead
zone sends nothing.

### Version 1.22: crash awakening and death

Section 14.20 adds the Air Defense crash's fade-driven focus recovery and
soft eyelid shading, plus one settling focus swell and delayed peripheral
darkening on death. Both preserve original scene timing, readable HUD and
subtitles, and the reduced-motion/flashing preferences. The presentation uses
editable gradients and the existing scene filter, without new bitmap assets.

### Version 1.23: the Arena pause menu

Section 13.7 gives the single-player Arena Campaign its own pause menu in the
single-player pause frame: Resume, Restart Match, Settings and Leave Match, with
the match in the level block's place, replacing the multiplayer Escape menu
during an Arena match. The register is unchanged.

### Version 1.24: Awakening single-player scope

The project guide excludes an Awakening multiplayer module and limits expansion
content/substitutions to active `q4xbase` single-player play. This supersedes older
clauses requiring expansion multiplayer powerup/pickup columns, thirteen-weapon
ammunition/accuracy/summary rows or multiplayer wheel captures. The applicable
single-player strip, thirteen-slot wheel, scopes, weapon displays, crosshairs,
vehicle displays and Falcon squad line remain required on both HUD families.
Ordinary Quake 4 multiplayer retains its complete eleven-slot wheel, team colors,
reserve-only ammunition, immediate equip and uninterrupted simulation contract.

Register schema 19 preserves `FLOW-054` and `WID-034` unchanged as historical rows
and replaces their active scope with `FLOW-066` and `WID-038`. Existing artwork
catalogue obligations, including `ART-035`, remain authoring/artwork requirements;
they do not authorize or require a new gameplay mode. This scope correction
accepts no GUI, artwork, platform or behavior. Hit-feedback defaults and the other
UI-05 qualification remain open.

### Version 1.39: the Awakening chaingun turret

Section 14.12 adds the HUD of The Awakening's mannable Strogg chaingun (m01, m04).
The expansion gave it the on-foot HUD with its ammunition gauge left empty and
nothing for the barrel heat openQ4 adds to the gun; it now reports the exposed
gunner's health and armor and the heat, on the gauges and under the crosshair,
with OVERHEATED while the gun vents. The Awakening's vehicle displays are
already required; no requirement changes, and nothing is accepted.

### Version 1.38: the black frame

Section 6's pop-ups gain an opaque black frame (the product owner's rule of 5
November 2026): modals trace their silhouette in black on top of the glow
without hiding it, with a black tab behind the title that steps down at 45
degrees past its end, and the DISPLAY page's dialogs and every dropdown list
gain a black edge outside their own outline. It adds to each construction;
nothing else changes. No requirement changes, and nothing is accepted.

### Version 1.37: the initializing screen's entrance and cadence

Section 14.17's Remastered initializing screen was meant to move, but startup
repainted it only about eight times a second, with pauses of up to a second.
It now repaints up to 60 times a second whenever loading offers a moment,
within half of the time during its entrance and a fifth after it. It opens
with an entrance: from black, the mechanism drops from 1.8 times its size to
its seat behind the still lettering, its gears winding in, the clamps close
on it and the halo flares as they lock, before the status line arrives. Each
step's line fades and rises into place instead of flickering, and a marker
glides along the rule while the segments behind it fill in turn. Register
revision 22 supersedes `FLOW-057` with `FLOW-069`; nothing is accepted.

### Version 1.36: the column in every portrait view that holds it

Section 13.7's Menu levels pattern no longer waits for 3:4: a menu level, whose
column scales to the room between the bands, stands in it in any portrait view
in which it reads at least at its landscape size, so a 4:5 window or an
unfolded foldable shows the column instead of the 4:3 composition between
empty bands. Each document finds that aspect from its own column (the pause
from about 0.83, the others from a square view); the pages that grow a fixed
amount take theirs from the room they need, and the list pages and the Arena
keep 3:4. `LAY-021` already holds this scope; no requirement changes, and
nothing is accepted.

### Version 1.35: the Arena page in tall views

Section 13.7's Arena Campaign pattern now says how the Arena page stands in a
view of 3:4 and taller. Its three columns stacked would not grow there, so
the page keeps two blocks of a column of plates with what they choose beside
it: the tier ladder with the briefing and progress, then the matches with the
difficulty and Start Match under them and the match's levelshot and details
beside them. The tiers stay a vertical ladder. `LAY-021` already holds this
scope; no requirement changes, and nothing is accepted.

### Version 1.34: list pages in tall views

Section 13.7's Lists pattern now says how a list page stands in a view of 3:4
and taller: one column, the filters, the selected item's picture, details and
actions, then the list in two-line rows down to the bottom band, the parts
under the list a line each, its confirmations grown with the rows and its
backing across the whole width; Create Server's map picture and list stand
over its options. `LAY-021` already holds this scope; no requirement changes,
and nothing is accepted.

### Version 1.33: menu levels in tall views

Section 13.7's Menu levels pattern now says how a menu level stands in a view
of 3:4 and taller: one column across the canvas, scaled together to the
height between the bands, the plates first and the description, message line
or level block under them. `LAY-021` already holds this scope; no
requirement changes, and nothing is accepted.

### Version 1.32: menu levels follow the text size

Section 13.7's Menu levels pattern now says how the home, pause and page
levels meet section 2's text-scale contract: plate labels, titles, plinth
links, prompt bars and card headings keep their size as furniture; content
columns, the message line, confirmation bodies and the level block's details
grow with the text in the room the frame leaves them. `LAY-004` already holds
this scope; no requirement changes, and nothing is accepted.

### Version 1.31: settings rows follow the text size

Section 13.7's Settings pattern now says how a category meets section 2's
text-scale contract: above 100 % its columns stack into one scrolling column
whose rows grow with the text, and the strip, prompt bar and titles keep
their size as furniture. `FLOW-068` already holds this scope; no requirement
changes, and nothing is accepted.

### Version 1.30: the DISPLAY category

Section 13.7's Settings pattern now covers DISPLAY, the SYSTEM page, which
was still the drafted full-screen panel: it is a sub-page like the other
categories, its settings on tabs of at most two columns. Because its rows edit
one draft that the settings service applies and confirms, it keeps APPLY
CHANGES on the page, under the columns beside the draft's status, as Create
Server keeps START, instead of in the prompt bar. `FLOW-068` already holds
this scope; no requirement changes, and nothing is accepted.

### Version 1.29: settings sub-pages

The user asked for the settings pages to fit the main menu's design rather
than the drafted full-screen panels, which filled a wide view with as many
columns of large cards as it held, and for long pages to be broken up by
tabs. Section 13.7's Settings pattern now makes each category a sub-page of
the SETTINGS page in Create Server's construction, its sections tabs in the
Escape card's strip, and allows at most two columns of setting rows. Register
revision 21 appends `FLOW-068` for this scope; no existing requirement
changes, and nothing is accepted.

### Version 1.28: objective points of interest

Section 14.9's Remastered additions gain objective points of interest for the
goal-oriented multiplayer modes: CTF and One Flag bases, including their Arena
variants, assault points and Dead Zone scoring volumes, each a symbol in a
45-degree diamond with its straight-line distance as the only text. The symbols
carry flag availability, zone control and artifact eligibility; colour never
does alone. Off-screen bearings end inside the safe frame. In stereo VR the
markers project head-relative bearings onto the compositor's HUD plane.
`hud_objectiveMarkers` is an archived client opt-out. Register revision 20
appends `FLOW-067` for this scope; no existing requirement changes, and nothing
is accepted.
