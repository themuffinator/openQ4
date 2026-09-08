# openQ4 UI Visual Design

Specification version: 1.0, 8 September 2026. Status: implementation target;
the replacement has not shipped. Applies to every player-facing GUI and to the
visual editor's Quake 4 preview. Implementation and evidence are tracked in
[the replacement plan](plans/idtech5-ui.md).

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

### Families

| Family | Identity | Required fidelity |
| --- | --- | --- |
| Marine menus | Olive/black plates, orange focus, Marine title face | Stepped rails, cut buttons, triangular markers, original scene imagery |
| Marine HUD | Tactical overlays, bright critical values, compact instruments | Weapon/ammo/health/armor hierarchy, feedback and original spatial relationships |
| Strogg HUD and terminals | Original alien glyphs, mechanical paths and family colors | Each source's geometry and animation; never generic Marine panels |
| World terminals | Local device panel and in-world illumination | Surface mapping, interaction targets, stateful scripts and intended viewing distance |
| Vehicles and turrets | Weapon-specific reticles, gauges and status | Cockpit alignment, targeting, damage and cooldown states |
| Scopes | Optical/technical marks in source shape and color | Exact aim center, FOV relationship, masks and tick cadence |
| Loading/cinematic overlays | Restrained frame, level identity and progress | Background composition, subtitle safe area, fades and cinematic bars |
| Multiplayer overlays | Tactical tables with team and spectator identity | Score sorting, timers, join choices and authoritative match state |

Family tokens extend shared geometry, motion and interaction tokens. Their
colors and motifs must be measured from their sources and recorded in their
migration entries before implementation. An invented palette applied to all
terminals does not satisfy full GUI replacement.

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
| Minimum interactive height | 36 dp desktop; 44 dp touch presentation |
| Compact presentation | Available width below 960 dp or height below 600 dp; keep navigation, scroll bodies |
| Smallest qualification viewport | 640x480 physical at 100% scale; larger scale settings remain recoverable |

At wide aspect ratios, expand tables, allow two columns, extend framing and
separate navigation/content. Preserve stroke width, corner angles, circles
and glyph proportions. At 32:9, contain forms and body text to a comfortable
center region while imagery/framing reach the edges. At 4:3 or large text
scale, stack columns and scroll bodies. Essential actions cannot leave the
visible safe area.

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

| Layer | Rest opacity | Interaction |
| --- | --- | --- |
| Primary action plate | 0.62 | 1.00 hover/focus |
| Secondary action plate | 0.28 | 0.62 hover, 1.00 active edge |
| Structural frame rail | 0.95 | Stable while contents animate |
| Header wash | 0.16 | Stable; no perpetual pulsing |
| Separators | 0.20–0.30 | Never compete with labels |
| Rest marker | 0.40 | 1.00 hover/focus |

Body text must remain readable over the brightest permitted scene. Target
4.5:1 composited contrast for essential normal text and 3:1 for large text and
essential control boundaries. If stock opacity fails, strengthen the local
plate or use the existing text-backing accessibility setting. Preserve the
original look where it meets the target. High-contrast mode uses opaque local
backing and independently strengthens rails, selection and focus.

## 5. Typography and language

Use shipped scalable equivalents of Marine for headings/actions and Lowpixel
for supporting text where available. Preserve the Marine small-cap character.
Resolve fonts from installed assets; do not copy proprietary faces into Git.
Scalable fallback faces require a compatible licence and explicit attribution.

| Role | Size / line height | Behavior |
| --- | --- | --- |
| Screen title | 28 / 34 dp | Marine; fixed hierarchy |
| Panel title | 20 / 26 dp | Marine; leading marker |
| Button | 18 / 24 dp | Marine; metric-based vertical centering |
| Body/form value | 16 / 23 dp | Lowpixel/source-equivalent face |
| Supporting metadata | 14 / 20 dp | Never an essential action |
| Table heading | 14 / 20 dp | Distinguished by tint and rule |
| HUD readout | Source-derived and recorded per group | Stable digit alignment |

Use slightly tight measured font-relative tracking as in stock menus; do not
import a fixed bitmap texel offset. Baselines and cap height determine label/
icon alignment. Rasterize glyphs for their output size; snapping cannot destroy
kerning. Scale animations use sufficient atlas density for their largest size.

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

### Panel anatomy

Standard panel: 12 dp 45-degree upper corner cuts, 8 dp lower cuts, 1 dp outer
olive rail, subdued inner rail offset 3 dp and translucent inset plate. Small
popovers use 6 dp cuts; major bands use 16–24 dp cuts. A 45-degree cut always
has equal horizontal/vertical extent after layout. Never stretch a corner to
a different angle.

Content inset: 24 dp normal, 16 dp compact. Header height: 40 dp, with a 1 dp
rule below and an 8 dp triangle before its title. Header wash fades toward
the trailing edge. Body height follows content; scroll regions clip inside
the rail. Footer actions have 16 dp separation and align to the action edge.

Main top/bottom framing follows the shipped stepped/notched silhouette, long
restrained rails and asymmetric technical detailing. Reconstruct measured
silhouettes as paths; a generic beveled rectangle is not a substitute for every
frame. Keep ornaments as named paths so expansion lengthens straight spans
while preserving corner/notch dimensions.

### Button anatomy

Default height 40 dp, minimum width 112 dp, 12 dp lower-left cut. Leading and
bottom rails fade toward the trailing edge. An 8 dp marker sits at a 12 dp
leading inset; label follows with 8 dp gap and 16 dp trailing padding. Plate,
marker and label form one semantic control and one target. Widen the straight
span without deforming the cut.

Focus adds a fine orange inset rail and solid marker. Hover strengthens the
plate and warms label/marker. Press briefly emphasizes the inner edge and moves
contents 1 dp inward; release restores current focus/hover state. Movement
never changes layout or the hit target.

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

### Bitmap exceptions

Only complex pictorial content uses bitmaps/video: levelshots, scene backdrops,
portraits, photographic/painted imagery, cinematics and detailed illustrations
whose character depends on textured content. Resolve installed assets and
sample at appropriate quality. Logos/diagrams require individual classification;
complexity, not convenience, justifies an exception. Simple logo outlines,
frames, reticles and checkboxes require vector work.

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

| Component | Visual and behavior contract |
| --- | --- |
| Action | Cut plate, marker, clear verb; one activation per action; disabled reason where useful |
| Checkbox | Cut outline, vector tick; shared label/box target; mixed state is a dash |
| Radio | Source-appropriate angular enclosure, filled center; one selected, directional group navigation |
| Slider | Recessed track, filled span, cut thumb, numeric value; keyboard increments and precise entry |
| Choice/dropdown | Recessed field, chevron; constrained scrollable popup; selected value visible |
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

## 8. Motion, transitions and sound

Use a continuous monotonic presentation clock sampled each rendered frame.
Motion continues while gameplay is paused and cannot inherit simulation tick
rate. Game-state events retain authoritative time. Keys specify time, property,
value and easing; interpolate continuously at 144/240 Hz as well as 60 Hz.

| Motion token | Timing | Curve / use |
| --- | --- | --- |
| `hover.enter` | 0–60 ms | Immediate marker/label response; optional brief plate fade |
| `hover.leave` | 300 ms | Cubic ease-out; stock asymmetric release |
| `press` | 60 ms | Fast ease-out, 1 dp inset movement |
| `focus` | 80 ms | Ease-out rail/marker; never delay navigation |
| `page.enter` | 220 ms | Cubic (0.16, 1, 0.3, 1); 12 dp slide and opacity |
| `page.leave` | 140 ms | Cubic (0.4, 0, 1, 1); 8 dp departure and opacity |
| `modal.enter` | 180 ms | Ease-out opacity and 0.985→1 local scale |
| `modal.leave` | 120 ms | Ease-in opacity; deliberate focus ownership transfer |
| `row.reveal` | 120 ms | Opacity, optional 15 ms stagger capped at 90 ms total |
| `value.change` | 100 ms | Local emphasis; no perpetual blinking |
| `tooltip` | 100 ms | Opacity after initial delay |

These are authored defaults. Story-driven timing, terminal sequences, weapon
reticles and scripted effects remain faithful to sources. Major transitions
may choreograph frame/content groups, normally totaling less than 300 ms.
Stock hover growth (0.26→0.27) can use a local transform without reflowing the
button or moving neighbors.

Retarget interrupted animations from current sampled values. Reversal cannot
jump to an endpoint. Different properties can animate concurrently; the most
recent explicit owner wins per property. Define activation, cancellation,
completion and teardown semantics. Modal input ownership starts before its
first visible frame. Handle rapid back/forward, repeated clicks, resizing,
theme changes and map shutdown.

Use `main_menu_mouseover` and `main_menu_selection` through existing sound
declarations where appropriate. Play once per semantic event. Gamepad focus
gets equivalent feedback without duplicate hover sounds. Never sound each
animation frame or server-list refresh.

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
rather than an indiscriminate full-screen dimmer. Effect ownership survives
stacked panels and releases on every exit/shutdown. Offer opaque local backing
as an accessibility alternative.

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

Stages can contain unfinished work but cannot mark it visually accepted or
rewrite this specification around its current limitations.
