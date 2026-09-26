# openQ4 UI Visual Design

Specification version 1.2, 26 September 2026 (1.1 and 1.0: 26 and 8 September
2026). Status:
implementation target; the replacement has not shipped. Applies to every
player-facing GUI and to the visual editor's Quake 4 preview. Implementation and
evidence are tracked in [the replacement plan](plans/idtech5-ui.md).

Version 1.1 added measured references from a survey of the effective retail
GUI scripts, interface art, fonts and materials, and corrected anatomy that did
not match them. Version 1.2 adds the [modern interface
direction](#13-modern-interface-direction): viewing profiles, adaptive layout,
controller, touch and Android behavior, and modern screen patterns.
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
- Record motion from `transition` durations (optional accel/decel times give a
  trapezoidal velocity profile), absolute and `+N` relative `onTime` keys,
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
| Objectives (PDA) | Pale-green objective panels over the darkened view | Objective imagery, flicker-in cadence, Marine and Strogg variants |
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
| Marine HUD (`hud`) | Readouts `#DDEBC3`; gauge fill `#B3D06E` at 20–50% over black 50%; icons `#A8A360` at 60%; weapon name `#B0C891`; selected weapon `#FF8000` | Chain 0.50 numerals, trailing-aligned; Marine labels | Rounded gauge plates with a stepped foot, notched fills, 10-cell armor gauge, 0.22 forward shear; scrolling EKG; 2 Hz alarm |
| Strogg HUD (`hud_strogg`) | Readouts `#FCFFC8`; gauge fill `#FF9000` at 20–35%; icons `#FFCC00` at 60%; unselected weapon `#F59512` | R_Strogg 0.40–0.50 numerals | Irregular 28–35° shoulders and small notches; 0.22 backward shear |
| Multiplayer (`mphud`, `scoreboard`, `summary`, `spawn`) | Marine team `#6AA42B`; Strogg team `#FF7B04`; spectators `#999999`; notices `#FFFF8D`; Tourney `#3E57B7`, `#5EB987`; MP health icon `#FA2B05`, armor icon `#F7C004` | Lowpixel 0.16–0.36 | Team-tinted header bands, faction emblems, award medals |
| World terminals (`guis/maps`, `monitors`, `common`, `movers`) | Per source. Common Strogg values: teal `#59AD87`, orange `#E6540F`, amber `#F0960D`, cyan `#2ECCCC`. Marine devices: `#7A9957`, `#BFE375` | R_Strogg and Strogg on Strogg devices; Marine on Marine devices | CRT surface stack (section 6); heavy shear, rotation and mirroring; `guisound_beep2` |
| Weapon displays (`guis/weapons/*_ammo`) | Normal `#73A640`; low `#CA8F15`, 3 Hz shimmer 80–100%; empty `#C72C1B`, 2 Hz pulse 100–50%; idle flicker 92–100% at 3 Hz | Marine digits at very large size, tight tracking | Per-weapon background art |
| Scopes (`guis/weapons/*_scope`) | Railgun marks `#00FFFF` at 50%; ammo cells `#99FFFF` | — | Mirrored quadrant glows, diamond zoom-in, slow ring rotation |
| Objectives (`wristcomm`, `wristcomm_strogg`) | Frames `#B0CD6B`; text `#D0DEB6`, `#D9E7BF`; accent `#FF8000` | Marine 0.14–0.25; Lowpixel 0.20 | Objective shots; 20–150 ms flicker-in steps |
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
| `marine.rim` | `#626934`, alpha 0.36 falling to 0 across 18 u | Rim light outside framing band edges |
| `marine.glow` | `#616F26` peak, applied at 70% | Additive backdrop light band |
| `marine.glow.modal` | `#46501B` peak | Additive glow field behind modals |
| `marine.list` | `#FF9C00` | List hover band 16%, selection band 42%, row rule 50% |
| `marine.list.alt` | `#9A9C6E`, alpha 0.29 | Server-browser hover band |
| `marine.check` | `#586A08` | Filled square of a set check or radio box |
| `marine.sort` | `#9BA545` | Sortable column hover highlight |
| `marine.plinth` | `#3E4A21`, alpha 0.30 | Plinth under the secondary links |
| `marine.progress` | `#E06C00`; track 30%, fill 50% | Loading and refresh progress |
| `marine.scrim` | `#000000`, alpha 0.94 | Front-end modal scrim |
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
| Separators | 0.26–0.32 olive in menus; 0.10–0.20 white in MP tables | Never compete with labels |
| Card frame | 0.94 black body inside its rail | Stable |
| Modal frame | 0.70 black silhouette over the glow field | Stable |
| Modal scrim | 0.94 black | Fades in over 200 ms, out over 250 ms |
| Framing bands | 1.00 in the front end; 0.40 in the in-game MP menu | Move; never fade |
| Table header band | 0.60 olive | Stable; no perpetual pulsing |

The stock marks a primary action by size, not opacity: START GAME is a
356x43 u light plate with a 24 dp label. openQ4's in-game join card rests its
primary action at 0.62, secondary actions at 0.28 and its header wash at 0.16
so the live scene stays readable; that variant is limited to in-game partial
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
| R_Strogg | Angular all-capital Latin | Strogg HUD numerals and Strogg terminals |
| Strogg | Alien runes mapped onto Latin | Strogg devices; credit and intro decode reveals |

Measured ink cap height per em: Marine 0.50, Lowpixel 0.75, Chain 0.63,
Profont 0.75, R_Strogg 0.67. Size tokens are ems; compare faces by cap height.

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
weapon name and cinematic credits. Never center an action label on a plate.

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
| Header band (`header`, `scoreheader`) | Upper-leading cut of about 8 u; top and leading rails; fill 49%, fading from 60% of the width; olive at 0.60 in menus, team colors on scoreboards |
| Link plinth (`bg2`) | Solid band with a lower-leading cut and a trailing end cut at 45 degrees, parallel to it; `marine.plinth` |
| Marker (`corner`) | Right triangle filling the upper-trailing half of its square (◥), legs half the tile, halo about 20% of the tile beyond the solid; mirrored vertically (◢) as the sort marker |

Frames and bands:

| Element (stock art) | Measured construction |
| --- | --- |
| Card frame (`tooltip_edge`, `tooltip_mid`) | Black body at 0.94; 1 u `marine.rail.card` rail on every edge; 2 u top-leading cut and 8 u top-trailing cut; the bottom cap is the top cap rotated 180 degrees |
| Modal frame (`popup_top`, `popup_mid`, `popup_btm`, `popup_bg`) | Black silhouette at 0.70 over an additive glow column; leading tooth, recessed title slot and raised trailing section along the top; deep lower-leading chamfer; square trailing corners; no rails |
| Tab strip (`ctrls_tab*`, `admin_tab*`, `summ_tab*`) | Baseline rail; the active tab rises about 22 u with a small leading fillet and a 12 u 45-degree trailing shoulder; olive fill fading downward over about two tab heights |
| Chat box (`chatbox_edge`, `chatbox_mid`) | Rail on the top and leading edges in `marine.rail.dark`, a 2 u chamfer at the top-leading corner, translucent black fill |
| Framing bands (`topbar`, `btmbar`) | Opaque black silhouettes (Appendix B.2) with `marine.rim` light |
| Loading edges (`load_top_edge`, `load_btm_edge`, `*_edgeadd`, `load_corner`) | Black 0.80 bands with one 45-degree riser each and additive olive light; 52 u corner brackets at the picture's corners |
| Separators (`horiz_line2`, `vert_line`, `vert_line2`) | One- or two-texel line centered in a 5–6 u strip |

Fields, lists and backdrop:

| Element (stock art) | Measured construction |
| --- | --- |
| Slider (`slider_bg`, `slider_bar`) | Tick ramp: alternating tall and short ticks rising from 2 to 9 u across the 71 u range above a baseline; 4x8 u bar thumb with a bright border and 40% interior; `marine.value` |
| Check or radio box (`box`, `box_check`) | Square outline, stroke 1/16 of the box (18 dp box in stock); set state is an inset filled square 69% of the box in `marine.check` |
| Scrollbar (`scrollbar_*`) | 16 u gutter; beveled gray arrow buttons with black triangles; bracket thumb with top, bottom and trailing rails and an open leading side; trough of one light line with a faint fill |
| List bands (`bg_hover`, `bg_focus`, `bg_line`, `*2`) | `marine.list` band at 16% (hover) or 42% (selection) with soft ends and a 50% hairline rule under every row; server-list variants fade across 20% at both ends |
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

**Card** (hover cards, tooltips, in-game partial panels such as the join card):
a black body at 0.94 inside a single `marine.rail.card` rail; 12 dp 45-degree
cuts at the top-trailing and bottom-leading corners and 3 dp cuts at the
top-leading and bottom-trailing corners. Small popovers keep this construction
with 6 dp major cuts. Content inset is 24 dp, 16 dp in compact mode and
10 dp on hover cards (stock 6–7 u). A panel header is 40 dp, with a 1 dp
rule below and an 8 dp triangle before its title; the header wash fades
toward the trailing edge. Body height follows content; scroll regions clip
inside the rail. Footer actions have 16 dp separation and align to the
action edge.

**Modal** (front-end confirmations, warnings and advanced-settings dialogs):
the `marine.scrim` covers the screen; an additive glow column peaking in
`marine.glow.modal` stands behind the dialog; the dialog is a black 0.70
silhouette. Along its top edge a 6 dp leading tooth, a title slot recessed
17 dp across about 73% of the width with 45-degree flanks, and a raised
trailing section; a 38 dp lower-leading chamfer; square trailing corners; no
rail lines, because the glow outlines the silhouette. The title (Marine 20 dp,
tracking -0.075 em) sits in the slot; body text is inset 33 dp; the
affirmative action leads and the negative action trails. The stock dialog is
480 dp wide.

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

### Framing bands

The home and page states use the same two band paths in different positions
(Appendix B.2). The top band's inner edge runs straight, rises through three
9 u notches with 45-degree flanks (36 u across the top, 97 u pitch), and
steps up by 35 u at a 45-degree riser. The bottom band's inner edge rises
30 u, later drops 43 u, and carries two 9 u steps near its ends, all at
45 degrees. `marine.rim` light falls from 36% to zero across 18 u outside
every inner edge.

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

- **Marine HUD gauges**: a backing plate at black 0.50 with corner radii of
  about 1 u and a foot along the trailing quarter of its bottom edge, reached
  by a short diagonal step; a fill plate inset about 4 u with a soft inner
  glow and a concave quarter-round notch cut from its lower-trailing corner,
  where the item icon sits; the armor fill is divided into ten equal cells;
  the health gauge masks a scrolling EKG trace. All three gauges are sheared
  by 0.22, about 12 degrees, so their tops lean toward the trailing edge.
- **Strogg HUD gauges**: irregular silhouettes with 28–35 degree shoulders and
  small notches, sheared the opposite way.
- **Weapon-select strip**: 26 u slots with about 1 u corner radii on a 30 u
  pitch, centered, with the weapon name centered below.
- **World terminals**: a layered CRT surface of content, additive scanlines
  drifting 0.2 texture heights per second, dirt and scratches, static, a bezel
  vignette with a thin bright edge, and an additive glass reflection.

Shear and rotation apply about the GUI origin in virtual space. Reproduce the
drawn geometry and verify it against captures instead of re-deriving it from
authored rectangles.

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
| HUD and items (`gfx/guis/hud/icons`, `simpleicons`) | Silhouettes | Health, armor, ammo, weapons, powerups, flags |

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
| Quake 4 wordmark (`q4text`) | Classify individually: textured glowing letterforms; its glow may become vector light |
| Quake emblem, faction marks, simple HUD silhouettes | Vector |
| CRT dirt, scratches, glass reflections | Bitmap exception |
| Static grain | Procedural noise; a bitmap only where the look depends on the stock grain |
| Light bands, vignettes, page backing, list bands, progress shading | Vector gradients |
| Scanlines, reticle grid, tick ramps, EKG and waveform traces | Vector or procedural patterns |
| Frames, plates, markers, boxes, arrows, scrollbars, tabs | Vector |

Record each exception with purpose and source in the migration inventory.
Generated glyph coverage atlases are a text-rendering detail, not permission
to reuse low-resolution bitmap type. Native paths must not become a fixed-size
atlas. If an SVG feature is unsupported, diagnose it; do not silently rasterize
furniture to hide the limitation.

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
| Choice/dropdown | Value in the value column that cycles in place; openQ4 dropdowns add a chevron and a constrained scrollable popup; selected value visible |
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
teams/spectators/local player with text or geometry as well as color. Empty,
offline, disconnected and failed states cannot be unstyled debug output.
Section 13 defines each component's controller and touch behavior, the
prompt bar and the detail area.

### Stock control conventions

| Control | Stock construction |
| --- | --- |
| Setting row | Light plate; Lowpixel label 21 dp in; value in `marine.value` at 64% of the row |
| Boolean or enumeration | A value that cycles on activation and steps back on the secondary action; no arrows, no checkbox |
| Filter spinner | Value between ◀ and ▶ arrows (`arrow6`, orange 0.80) on 0.40 black squares |
| Exclusive pair (Internet/LAN, Player/Clan) | Square boxes; the set option fills its box |
| Slider | Tick ramp at the value column, numeric value after it at 85% of the row |
| Text field | Dark plate without an outline; save names in white 0.80, setting values in `marine.value` |
| Key binding | Key names in `marine.value` in the value column |
| List | Lowpixel column headings at 40% above an olive rule; 30 dp rows with `marine.list` hover and selection bands and a rule under every row |
| Server list | Olive 0.60 header band holding 28 dp column symbols; 27 dp rows; sortable columns highlight in `marine.sort`; flipped marker beside the sort notice; progress bar under the list |
| Hover card | Card frame at 0.94 that appears at once and follows the pointer 6 dp away, moving to the pointer's leading side near the trailing edge; Lowpixel 14 dp and Profont 13 dp content |
| Tooltip | Stock sort hints sit 12 dp right of and 15 dp below the pointer, appear after 1000 ms and fade in over 250 ms; the replacement shortens the delay (table above) |

Stock hit targets were invisible windows separate from the art. The replacement
makes each control one target spanning its full pitch. The stock drew the
current page's navigation plate exactly like hover; keep that for selection and
add the focus rail for keyboard and gamepad focus.

## 8. Motion, transitions and sound

Use a continuous monotonic presentation clock sampled each rendered frame.
Motion continues while gameplay is paused and cannot inherit simulation tick
rate. Game-state events retain authoritative time. Keys specify time, property,
value and easing; interpolate continuously at 144/240 Hz as well as 60 Hz.

`trapezoid(a, d)` is the stock accel/decel profile: constant acceleration for
`a` ms, constant speed, then constant deceleration for the final `d` ms.
Unmarked stock transitions are linear.

| Motion token | Timing | Curve / use |
| --- | --- | --- |
| `hover.enter` | 0 ms | Plate, label and marker respond at once (stock) |
| `hover.leave` | 300 ms | Linear return (stock) |
| `press` | 60 ms | Fast ease-out, 1 dp inset movement (openQ4) |
| `focus` | 80 ms | Ease-out rail/marker; never delay navigation (openQ4) |
| `frame.dock` | 500 ms | `trapezoid(150, 150)`; framing bands move between home and page positions (stock) |
| `screen.depart` | 500 ms | `trapezoid(150, 150)`; home content sweeps toward the trailing edge (stock) |
| `screen.return` | 300 ms | Linear; a page sweeps toward the leading edge on Back (stock) |
| `content.out` | 250 ms | Linear fade of departing plates, labels and markers (stock) |
| `content.in` | 150 ms | Linear fade of arriving title, backing, actions and home content (stock) |
| `page.enter` | 220 ms | Cubic (0.16, 1, 0.3, 1); 12 dp slide and opacity, between sibling pages of one screen (openQ4) |
| `page.leave` | 140 ms | Cubic (0.4, 0, 1, 1); 8 dp departure and opacity (openQ4) |
| `modal.enter` | 200 ms | Scrim to 0.94, glow field brightens from black, frame to 0.70, actions fade in over 150 ms; no scale (stock) |
| `modal.leave` | 50 + 250 ms | Contents hide at once; after 50 ms scrim and glow fade over 250 ms, frame 200 ms, actions 150 ms (stock) |
| `light.up` | 150 ms in, 250 ms out | Logos and glow fields fade from and to black rather than transparency (stock) |
| `row.reveal` | 120 ms | Opacity, optional 15 ms stagger capped at 90 ms total (openQ4) |
| `value.change` | 100 ms | Local emphasis; no perpetual blinking |
| `tooltip` | 100 ms | Opacity after initial delay |

These are authored defaults. Story-driven timing, terminal sequences, weapon
reticles and scripted effects remain faithful to sources. Marine menu screen
changes follow the stock choreography below; other major transitions may
choreograph frame/content groups, normally totaling less than 300 ms. Stock
hover growth (+0.01 `textscale`, about 3%) uses a local transform without
reflowing the button or moving neighbors.

### Stock choreography

**Home to page.** At 0 ms the home content fades out (`content.out`) and the
wordmark and emblem fade to black (`light.up`). At 50 ms the home content
sweeps toward the trailing edge while both framing bands move to their page
positions (`frame.dock`). At 550 ms the page is placed, and its title (to
0.50), backing (to 0.60) and Back action fade in (`content.in`). **Back**
reverses it: the page sweeps toward the leading edge (`screen.return`) while
the bands return (`frame.dock`); when they settle at 500 ms the home content
fades in (`content.in`) and the wordmark and emblem light up from black.
Sibling pages inside a screen, such as settings categories, switched instantly
in stock with a 150 ms title fade; `page.enter` and `page.leave` now cover them.

The stock blocked all input for the whole choreography and re-enabled it
only once the new content had faded in. The replacement stays responsive:
input during a transition retargets it instead of being dropped, and a
control that is not yet, or no longer, presented never activates.
Reduced motion places bands and pages at their destinations and cross-fades
content within 80 ms.

**Decode reveal** (credits and intros): the rune line fades in over 500 ms;
the Latin line then wipes open from the center over 1000 ms while settling
from white to its color, and the rune line fades out over the same 1000 ms.

### Ambient loops and alarms

| Stock loop | Measured | Treatment |
| --- | --- | --- |
| Backdrop levelshots | Cross-fade over 2000 ms every 5000 ms, four images | Decorative |
| Static grain | Scrolls 1 texture width and 5 texture heights per second at 2.4% | Decorative |
| Animated gear | One turn per 10 s | Decorative |
| Terminal scanlines | Drift 0.2 texture heights per second | Decorative |
| EKG trace | Scrolls 0.2 texture widths per second | Instrument; its information stays under reduced motion |
| Idle display flicker | 92–100% at 3 Hz | Decorative |
| Warning shimmer (low ammo) | 80–100% at 3 Hz | Alarm |
| Alarm pulse (no ammo, low health, boss shield) | 100–50% at 2 Hz | Alarm |
| Settings notice | 100–50% at 1 Hz | Alarm |

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
World terminals play `guisound_beep2` on actions. The front end plays
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
rather than an indiscriminate full-screen dimmer. Front-end confirmation
modals keep the stock `marine.scrim`. Effect ownership survives stacked panels
and releases on every exit/shutdown. Offer opaque local backing as an
accessibility alternative.

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

**Modal.** 320 u wide and centered, body text at +22 u, actions of 120x30 u
at the leading and trailing ends.

**Loading.** Full-bleed levelshot; top band thick on the leading side with its
riser near the middle, bottom band thick on the trailing side; the level name
trails at the top (Marine 26 dp, tracking -0.1 em) over a gradient that fades
toward the leading side; progress bar from 235 to 640 u at 431 u with
`LOADING` trailing-aligned across it; corner brackets at the four corners of
the picture; the `+` grid over everything.

**Cinematic.** Black bars of 60 u top and bottom frame a 16:9 picture on the
4:3 canvas. Preserve that picture aspect rather than the bar height.

**Marine HUD.** Ammo (13 u), health (190 u) and armor (326 u) gauges, each
125x59 u at 420 u, with trailing-aligned numerals and 16 u icons beneath
them; the weapon-select strip centered at 370 u with the weapon name centered
below at 392 u.

**In-game multiplayer menu.** The Marine menu vocabulary with framing bands at
0.40, so the match stays visible.

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
| Phone | Android phones in landscape | 25–35 cm | Touch, controller | 100% / 100% at native density | 48 dp |
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
| Compact width | Width below 960 dp | One content column. Navigation becomes a tab strip in the top band; details expand in place |
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
plates for the mouse, and 48 dp tappable plates for touch.

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

**In game.** Holding a bound button opens a weapon wheel: a ring of cut
segments in the HUD family showing the owned weapons' icons and ammunition,
with unavailable weapons dimmed, the selected segment filled and marked in
orange, and the weapon name at the center. Either stick selects and release
equips; a tap swaps to the previous weapon; the game keeps running. The same
component provides a quick-chat wheel of localized phrases in multiplayer.
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
- Play is landscape-only in both orientations; no portrait layouts are provided.

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

**World terminals.** A tap on a terminal's projected surface within use range
activates the region under the finger through the projected ray (SUR-004). A
region within 16 dp of the finger counts as hit when no other region is closer.

**Other input on Android.** A connected controller hides the touch overlay and
switches to the controller presentation; touching the screen brings the
overlay back. A pointer device, as on ChromeOS or a tablet with a mouse,
enables hover.

**Mobile rendering.** The GLES renderer omits some desktop post effects, so no
interface element depends on them. Where scene softening is unavailable or too
costly, partial panels use a darkening scrim and vignette instead. Keep to two
full-screen blended layers on tile-based GPUs by merging the backdrop's light
band and vignette. A static menu redraws only on change, at most 30 times per
second. The Phone profile starts with ambient loops and parallax off.

### 13.7 Screen patterns

- **Title.** When a save exists, Continue leads the navigation, and its detail
  shows the levelshot, mission, difficulty and play time. SINGLE PLAYER (Mission
  and Arena), LOAD GAME, MULTIPLAYER and SETTINGS follow, and the secondary links
  stay on the plinth.
- **Pause.** Resume leads. Single player offers Save, Load, Settings, Restart
  Level, Objectives and Quit to Menu. Multiplayer offers Team or Spectate,
  Match Control, Settings and Disconnect, and never pauses the server. The
  panel sits over the softened scene (section 9).
- **Settings.** Categories are tabs switched with the bumpers or a swipe, and
  sections keep their picker. Search runs across every category when the player
  types, presses north, or taps the search symbol, and each result shows its
  category path. A modified row's leading rail turns `marine.value`; the west
  button resets the focused row to its default. Rows that need a restart carry
  a warning tag, and rows with a performance cost show a three-tick impact
  gauge drawn from the slider's tick ramp. Crosshair, HUD scale, text size,
  brightness and color-vision rows preview live. Apply and Discard live in the
  prompt bar, and display changes keep the 15-second Keep or Revert countdown.
- **Lists** (servers, saves, demos). Filters are small cut tags built like the
  header band. Sort from the column header or with north. The detail column
  shows the levelshot, players and rules; Join, Spectate and Favorite sit in
  the prompt bar; west refreshes. Empty, loading and failed states follow
  section 7.
- **Arena Campaign.** Tiers form a vertical ladder of plates. Locked tiers are
  dimmed and carry the lock symbol, boss matches use a larger plate, and the
  bumpers switch tier.
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
  continue" on touch.
- **Notices.** Short notices appear in the top band's trailing section: a
  controller disconnect, a low battery, match events. They never cover the
  crosshair or touch controls, and unresolved errors persist.

### 13.8 Visual refinements

- **Depth.** The backdrop, light, grid and frame layers may shift by up to 6 dp
  with the pointer, the right stick or device tilt. Content never moves.
  Reduced motion disables the shift, and the Phone profile starts with it off.
- **Focus light.** The focused plate gains a soft additive glow in
  `marine.glow`, about 24 dp across, beside its focus rail. High-contrast mode
  replaces the glow with a solid rail.
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
| Competitive | Multiplayer: enlarged readouts, match timer and team status |
| Touch | Readouts moved clear of the touch controls and enlarged |

HUD settings cover an independent HUD scale, the safe inset, opacity, color
vision, the crosshair editor (the stock set, sizes 16–48, eight colors, live
preview), hit markers in multiplayer, damage direction indicators, pickup
messages and the weapon strip. Couch and Handheld raise the default HUD scale.

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
[product completion plan](plans/ui-product-completion.md): 10% of the target
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

### 13.13 Patterns to avoid

- Storefront, advertising or engagement patterns; nagging dialogs.
- Generic platform components: rounded cards, floating action buttons, pill
  chips, toggle switches or bottom sheets with rounded corners.
- Emoji or font glyphs standing in for symbols.
- Gestures without a visible alternative.
- A controller-driven cursor in menus.
- Portrait layouts.

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
| `header` | 512x32 | 11–28 | 7 texels, upper leading | Top and leading | 0.49 to 60% width, then linear to 0 |
| `scoreheader` | 512x32 | 2–28 | 7 texels, upper leading | Top and leading | As `header` |
| `tooltip_edge` | 256x16 | From row 2 | 2 texels upper leading, 8 texels upper trailing | 1 texel `#A0A040`, top and sides | Opaque black |
| `popup_top` | 512x32 | From row 4 | Tooth at 7–13, slot from 14 to 388 narrowing 1 texel per row to row 22, full width from row 23 | None | White art, drawn black at 0.70 |
| `popup_btm` | 512x64 | 0–57 | Leading edge moves 1 texel per row from row 14 to row 57 | None | As `popup_top` |
| `ctrls_tab1` | 512x64 | Tab rows 1–26 | Leading fillet 3 texels; trailing shoulder 14 texels at 45 degrees | Outline and baseline at row 26 | Alpha 1.0 at top to 0.02 by row 52 |
| `corner` | 32x32 | Solid rows 8–24, columns 8–23 | Hypotenuse from upper leading to lower trailing | Halo to rows 1–29, columns 3–30 | White |
| `box`, `box_check` | 32x32 | Outline rows and columns 1–30 | None | 2-texel stroke | Inset square, rows and columns 5–26 |
| `bg_grid` | 512x512 | Crosses every 32 texels | None | 1-texel arms spanning 21 texels | White |

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
