# UI product requirement register

28 October 2026: the [model-scene foundation](document-format.md#model-scenes)
adds bounded implementation evidence to `INV-013`: typed body/head/weapon
scenes, separate skins, native colour/depth targets and an internal coverage
resolve. Seven selected native UI suites and 36 actual Vulkan compiler cases
pass; GL/Vulkan gameplay pose captures pass, including Vulkan language/video
recreation. Failed fixture views, late precache warnings and unsupported/
unqualified native cases remain explicit. Player Setup and both production
previews remain outstanding; no requirement status, resource acceptance or
final gate changes.

25 October 2026: [tall views](retained-screens.md#tall-views-grow-the-lists-25-october-2026)
add bounded UI-02 evidence to `LAY-021`. Load and Save Game, Demos and Join Game
give a tall view's extra height to their list. The increment is audited against `137bd8d4bf2c08fabf5dc1e2ddd7ab44e36160fb`. No requirement status,
migration acceptance or final gate changes.

25 October 2026: [shared notices](retained-screens.md#shared-notices-and-a-lost-controller-25-october-2026)
add bounded UI-03 evidence to `WID-026`. The notices follow the player across the
retained menus, and a lost controller shows one until it returns. The increment is audited against `e28992e8ffbccde281e04b7f25722de47b903d79`. No requirement status,
migration acceptance or final gate changes.

25 October 2026: [full server names](retained-screens.md#full-server-names-25-october-2026)
add bounded UI-03 evidence to `WID-020`. Join Game's details card names its
server in full over the address. The increment is audited against `4d12fb9c86dcdced701b7ac2b60bac7d7f38220e`. No requirement status,
migration acceptance or final gate changes.

24 October 2026: [notices](retained-screens.md#notices-24-october-2026)
add bounded UI-03 evidence to `WID-026` and `MOT-009`. Retained pages show
section 13.13's notices; Join Game's favorites use them and a removal offers
Undo. The increment is audited against `563dec70115155ec478a37859b39718b5e3ed3aa`. No requirement status,
migration acceptance or final gate changes.

24 October 2026: [loading rows](retained-screens.md#loading-rows-24-october-2026)
add bounded UI-03 evidence to `WID-030` and `MOT-009`. Join Game shows its rows at
rest while a scan has found nothing, and the first servers replace them with
`row.reveal`. The increment is audited against `d8649f29d839060f27c27cc8f4ecd224bf006204`. No requirement status,
migration acceptance or final gate changes.

24 October 2026: [empty states](retained-screens.md#empty-states-24-october-2026)
add bounded UI-03 evidence to `WID-030`. Load and Save Game, Demos and Join Game
offer CLEAR FILTERS while a filter hides everything, and Join Game GET NEW LIST
when it found no server, under a heading. The increment is audited against `e8a56dd5663cf7f85e7f48ea4e3262150fdfe750`. No requirement status,
migration acceptance or final gate changes.

24 October 2026: [View glyphs](input-routing.md#view-glyphs-24-october-2026)
add bounded UI-02 evidence to `INP-011`. Each controller family draws the View
button, which Join Game's details card shows at its corner for a controller. The increment is audited against `87444682f370b0db50b18f943cd40e2299a67fe9`. No requirement status,
migration acceptance or final gate changes.

24 October 2026: the [menu pages](retained-screens.md#narrow-views-24-october-2026)
add bounded UI-02 evidence to `LAY-021`. Load and Save Game, Restart Level, Demos,
Credits, Create Server, Join Game and the Arena fit views narrower than 4:3 with
their prompt bars on the bottom band. The increment is audited against `e22da3be9914c88c36f7c18829f0cb96c1427f47`. No requirement status,
migration acceptance or final gate changes.

24 October 2026: [narrow views](retained-screens.md#narrow-views-24-october-2026)
add bounded UI-02 evidence to `LAY-021`. The home screens, the Multiplayer page,
the death screen and the multiplayer cards fit views narrower than 4:3 instead of
clipping. The increment is audited against `f8d3b30a153d12939457dd2ba07c101eec446535`. No requirement status,
migration acceptance or final gate changes.

24 October 2026: [map catalogs](multiplayer-menus.md#map-catalogs-24-october-2026)
add bounded UI-03 evidence to `FLOW-046` and `WID-005`. The Vote and Admin pages'
map choices hold 128 maps a game type, the game's cap read by the generators. The
increment is audited against `3132fa3be309a5c6f8af71207caedee0fc027366`. No requirement status, migration
acceptance or final gate changes.

24 October 2026: [the View button](input-routing.md#view-button-24-october-2026)
adds bounded UI-02 evidence to `INP-011`. View runs a document's `onViewButton`
(Back elsewhere and in dialogs or open lists), hides or shows Join Game's server
details, and by default shows the scores while held in play. The increment is
audited against `116b00bfd21a8c0ed101da46b3c84e94448ffa7d`. No requirement status, migration acceptance or final
gate changes.

24 October 2026: [Medical Labs implant captures](remastered-hud.md#medical-labs-implant-captures-24-october-2026)
add bounded UI-04 evidence to `FLOW-062`. The retained overlay is driven through the
level's own script and a real save at five aspects and in German. The increment is
audited against `53e31d1e90b2284dc6fe6b16472caf81bad16714`. No requirement status, migration acceptance or final
gate changes.

24 October 2026: [list focus memory](input-routing.md#wrapping-menus-24-october-2026)
adds bounded UI-02 evidence to `INP-010`. Scrolling lists remember their last
focused control, and a directional move back into one returns there. The
increment is audited against `623165435f858b70cfc2a0053fa8d240cc6696c9`. No requirement status, migration
acceptance or final gate changes.

24 October 2026: [dialog and Restart Level wraps](input-routing.md#wrapping-menus-24-october-2026)
add bounded UI-02 evidence to `INP-010`. Every retained confirmation's two actions
wrap Left and Right, and Restart Level's difficulties loop through RESTART LEVEL.
The increment is audited against `fb6547f03d0f2c8b70fa4c120cad8943253be23a`. No requirement status, migration
acceptance or final gate changes.

24 October 2026: [wrapping menus](input-routing.md#wrapping-menus-24-october-2026)
add bounded UI-02 evidence to `INP-010`. The home screens and menu pages wrap Up
and Down through their menus and plinth links, and authored navigation passes a
move through a target that cannot take focus. The increment is audited against
`d376ec693290957f26728185cdf287e81d3c5147`. No requirement status, migration acceptance or final gate changes.

24 October 2026: [Menu glyphs](input-routing.md#menu-glyphs-24-october-2026)
add bounded UI-02 evidence to `INP-011`. Each controller family's Menu button
glyph ends the Escape card's Admin Apply changes plate, which the button presses,
for a controller only. The art derives from section 13.5 and awaits review under
UI-05. The increment is audited against `b038777b2b1cec823b34b80256d0c3f7d50ccc64`. No requirement status,
migration acceptance or final gate changes.

24 October 2026: [joined lines and capitals](retained-screens.md#joined-lines-and-capitals-24-october-2026)
add bounded UI-02 evidence to `TXT-005`. List joins, the time in the mission, a
save's date and time and the load time's decimal mark and unit come from every
shipped table, and the Demos page's plates and headings are capitals in every
language. The increment is audited against `d610d54794ca41f13b6f8c9878023e4d8912e6c8`. No requirement status,
migration acceptance or final gate changes.

24 October 2026: [trigger glyphs](input-routing.md#trigger-glyphs-24-october-2026)
add bounded UI-02 evidence to `INP-011`. The Escape card's Match and Admin section
strips end with each controller family's trigger glyphs, a legend for a controller
only. The art derives from section 13.5 and awaits review under UI-05. The increment
is audited against `55ed6f210da77490d2389849e04d46faad92e9c0`. No requirement status, migration acceptance or
final gate changes.

24 October 2026: [the Menu button's Apply](input-routing.md#menu-button-24-october-2026)
adds bounded UI-02 evidence to `INP-011`. The opt-in SYSTEM page and the Escape
card's Admin page apply pending changes with the Menu button, and the adapter keeps
the button Back while a dialog or an open list holds the input. The increment is
audited against `2bfcb60cc42b51d700b36386d065c104c0d9daf5`. No requirement status, migration acceptance or final
gate changes.

24 October 2026: [the Menu button](input-routing.md#menu-button-24-october-2026)
adds bounded UI-02 evidence to `INP-011`. A document's `onMenuButton` runs once
per press where declared and the button is Back elsewhere; the pause's Save, Load
and Restart Level pages resume play through `menuResume`. The increment is audited
against `1ce1599f4357cdf28b0ceffec83c0b3d2fb51fe6`. No requirement status,
migration acceptance or final gate changes.

24 October 2026: [controller settings, list paging and Join Game focus](input-routing.md#settings-rows-24-october-2026)
add bounded UI-02/UI-03 evidence to `INP-011` and `WID-009`. Prompt Style and Swap
Confirm and Back are Settings rows, the triggers page retained lists and step a
focused slider coarsely, and Join Game's row focus follows its server. The increment
is audited against `35a1e919c828b8fd5326f46e1dda9a975c8549aa`. No requirement status,
migration acceptance or final gate changes.

23 October 2026: [save details](retained-screens.md#save-details) add bounded
UI-03 evidence to `FLOW-024`, `FLOW-048` and `FLOW-004`. Saves record the
run's play time and difficulty in their existing headers; CONTINUE and the
Load Game page name them, the pause's level block adds the run's total, and
completed objectives survive a save load. The increment is audited against
`8d821202d70ca746d58a2608b1aab3c69bcbf56f`. No requirement status, migration
acceptance or final gate changes.

23 October 2026: [focus-following prompt verbs](retained-screens.md#page-prompt-bars)
add bounded UI-02 evidence to `WID-019`, `FLOW-004` and `FLOW-013`. The pages'
Accept names the focused control's verb in the focus frame and leaves the bar
while nothing focused can act. The increment is audited against
`2f6c628a306b9dc42f752df505ebce5f26595996`. No requirement status, migration
acceptance or final gate changes.

23 October 2026: [Strogg confirmations](retained-screens.md#strogg-confirmations)
add bounded UI-03 evidence to `FLOW-048`, `WID-013`, `ART-037` and `MOT-015`.
The Strogg pause's and Strogg pages' dialogs take the Strogg family and
translate from runes as they open; their art derives from section 13.7 and
awaits review. The increment is audited against `e3cf8d037b1458c8ed49b9b423bf76201a194879`.
No requirement status, migration acceptance or final gate changes.

23 October 2026: the [acceptance transition](acceptance-transition.md) adds
UI-11 tooling evidence to `GOV-002` and `QUAL-006`. Only it may accept
requirements and resources, from current, separately reviewed records. It was
audited against `f74b48edb4b5cb3153749241f0b087df17e3eb0d`. Nothing has been
accepted with it, and no status changes.

23 October 2026: [save browser controls](save-browser.md) add bounded UI-03
evidence to `WID-009`, `FLOW-004` and `FLOW-026`: sorting, filter tags, explicit
refresh, filename-based selection/focus and stale catalog rejection in both
page families. The increment is audited against `c302254b865833786be9f1fd02041aef85f80b22`.
No requirement status, migration acceptance or final gate changes.

23 October 2026: [full multiplayer roster scrolling](multiplayer-menus.md#full-roster-scrolling-23-october-2026)
adds bounded UI-03 evidence to `WID-009`, `WID-010`, `FLOW-039` and `FLOW-046`.
Four production views keep fixed headings and readable rows, reach all 32
connection slots and preserve or clamp their scroll as appropriate; the kick
chooser includes every other client. Six native suites, the session gate,
four capture-oracle tests and 30 reviewed OpenGL/Vulkan engine screenshots pass.
The gameplay runs use three live players plus explicit 32-slot document fixtures;
the configured server limit remains 16. This increment is checked against
immutable Git baseline `f5bd60838ae25961fb9392ba71a9ab6c6a9c1437`. Existing evidence
bindings to the extended multiplayer document are refreshed without changing
their historical scope. No requirement status, migration or final gate changes.

23 October 2026: the [external settings refresh and queued value ownership
increment](external-settings-refresh.md) contributes bounded evidence to
`BEH-002`, `FLOW-002`, `WID-005`, `WID-017` and `WID-018`. Clean SYSTEM pages now
follow live settings; dirty drafts and local numeric text remain protected;
queued values check their starting readback and choice availability. Eleven
native suites, 110 production-service scenarios and SP/OpenGL/MP/Vulkan gameplay
captures cover this subset. No status, migration acceptance or final gate changes.

**Planning authority, 18 October 2026:** the [development roadmap](../roadmap.md#new-user-interface)
is the sole active plan; UI-01 through UI-11 coordinate this register's work.
The archived plans below preserve the original completion scope and section
provenance. This register and the [migration manifest](migration-manifest.json)
remain the detailed acceptance records: consolidation does not change any
requirement ID, dependency, status, evidence or migration acceptance. The
[visual specification](../ui-visual-design.md) remains normative.

9 September 2026, revised through 28 October 2026. The
[machine-readable register](product-requirements.json) contains **350
requirements** for the complete UI product: 311 that define acceptance and 39
superseded rows kept for history. It expands
the [archived completion scope](../archive/2026-10-18/plans/ui-product-completion.md), the
[archived original delivery scope](../archive/2026-10-18/plans/idtech5-ui.md) and the normative
[visual specification](../ui-visual-design.md) into implementation ownership,
dependencies, concrete evidence and explicit unfinished work.

Register schema 2 (26 September 2026) follows visual specification 1.1 and
1.2. It records eight supersessions: `ART-003`, `ART-004` and `ART-006` by
`ART-018` to `ART-020` for the measured alpha ladder, panel constructions and
open-plate anatomy; `MOT-003` by `MOT-009` for the stock motion tokens; `TXT-006`
by `TXT-010` for the stock-derived type ramp; `WID-002` and `WID-004` by `WID-017`
and `WID-018` for the stock check mark and tick-ramp slider; and `LAY-007` by
`LAY-013` for the 48 dp touch minimum. It appends 32 further requirements: the
additive and darkening composition, cycling values, screen-change choreography,
ambient loops and stock parity comparison of version 1.1, and the version 1.2
modern direction: viewing profiles, size classes, safe areas, the accessibility
baseline, input modality, focus memory, controller mapping and editing, touch
rules, the prompt bar, detail area, radial wheel and on-screen keyboard, glyph
artwork and visual refinements, Android lifecycle, first-run data setup, the
first-party touch gameplay controls and their editor, terminal interaction,
modern screen patterns, HUD presets, Remastered and Classic layouts, mobile
power limits and input/device qualification. `WID-017` and `WID-018` start
partial with the evidence their superseded rows carry; every other appended
requirement is pending. No existing row, evidence scope or migration entry
changed. The revision re-binds the visual specification and repairs eleven other
source bindings: an earlier increment recorded the completion plan, the original
delivery scope and nine evidence document hashes from working-copy bytes that
were never committed, and they now bind the current documents.

Register schema 3 (27 September 2026) follows visual specification 1.3 and
1.4. It records five supersessions: `ART-018` and `ART-019` by `ART-023` and
`ART-024` for the measured list bands, rim light, small-card cuts and modal
widths; `MOT-009` by `MOT-012` for the stock acceleration rule and per-element
screen changes; `WID-021` by `WID-024` for the weapon wheel's HUD plates and
Strogg construction; and `FLOW-027` by `FLOW-029` for the stock crosshair set
and sizes. It appends 24 further requirements: band motion, masked additive
composition, the pop-up frame catalogue, detailing, number formatting and text
shadow, the modern components, the HUD's composition, gauges, anchoring, weapon
strip, crosshair, messages, multiplayer HUD and alarms, and the version 1.4
objectives display, boss and vehicle displays, scopes and weapon displays, and
scoreboard. Every appended requirement is pending. The revision re-binds the
normative sources and the completion plan's evidence hash.

Register schema 4 (27 September 2026) follows visual specification 1.5. It
records seven supersessions: `LAY-018` by `LAY-019` for the HUD's screens,
anchors, centered status bar and HUD width; `FLOW-034` by `FLOW-040` for the
centered pickup lines and the measured radio chatter; `FLOW-035` by `FLOW-041`
for the Remastered multiplayer top of the screen and kill feed; `FLOW-036` by
`FLOW-042` for the side-screen objective notices; `FLOW-033` by `FLOW-043` for
the vector crosshairs and the hit markers outside the art; `WID-024` by
`WID-032` for the weapon wheel's gauge, hub and transitions; and `FLOW-029` by
`FLOW-044` for the HUD width setting and the stock crosshair swatches. It
appends four further requirements: the Remastered chat box and input
(`FLOW-045`), the crosshair and item icon catalogues (`ART-027`, `ART-028`) and
the translation and transmission effects (`MOT-015`). Every appended requirement
is pending.

Register schema 5 (28 September 2026) follows visual specification 1.6. It
supersedes no row and appends three: the Welcome and Escape multiplayer menus
with their tab strip over the softened view (`FLOW-046`), the stock and
Remastered loading screens (`FLOW-047`), and the EKG, transmission waveform,
Rhino emblem and voice traces drawn from their textures (`ART-029`). Every
appended requirement is pending.

Register schema 6 (29 September 2026) follows visual specification 1.7. It
supersedes no row and appends two: the title screen and the Marine and Strogg
single-player pause menus, which share one frame but never look alike
(`FLOW-048`), and the Quake emblem watermark with its rim light and glint
(`ART-030`). Every appended requirement is pending.

Register schema 7 (29 September 2026) follows visual specification 1.8. It
supersedes no row and appends two: the initializing screen, whose rings turn
slowly beneath the emblem and lettering while the engine starts (`FLOW-049`),
and its picture rebuilt as separate vector layers with the traced QUAKE 4
lettering (`ART-031`). Every appended requirement is pending.

Register schema 8 (29 September 2026) follows visual specification 1.9, which
replaces the modal scrim with a soft focus of the screen beneath. It supersedes
the two rows that quoted the 0.94 scrim: `ART-023` by `ART-032` for the tokens
and alpha ladder, and `ART-024` by `ART-033` for the modal construction, whose
glow is now cut to the dialog. It appends backdrop soft focus as a renderer
capability (`REN-016`). Every appended requirement is pending.

Register schema 9 (1 October 2026) follows visual specification 1.10, which adds
a sub-page level beneath the menu pages. It supersedes `MOT-013`, whose band
motion rules allowed exactly two dock states, by `MOT-016`, which adds the
Remastered sub-page state: only the top band moves on one notch pitch. It
appends the menu levels themselves (`FLOW-050`): Single Player leading to
Campaign and Arena, Multiplayer to Join Game, Create Server and Demos, and the
crumb that keeps the parent page in the path. Every appended requirement is
pending.

Register schema 10 (1 October 2026) follows visual specification 1.11. It
supersedes three rows that quoted changed values: `WID-032`, whose weapon wheel
hub carried the radio's carrier waveform, by `WID-033`; `FLOW-040`, which marked
a squad line with the Rhino emblem alone, by `FLOW-052`, which shows the
player's squad stencil; and `FLOW-045`, whose chat box sat beside the status
bar at the bottom edge, by `FLOW-051`, which keeps it clear of the status bar
at every aspect. It appends the scopes' measured layers (`FLOW-053`), the squad
patches (`ART-034`), The Awakening's icons and crosshairs (`ART-035`), its HUD
integration (`FLOW-054`) and its vehicle displays (`FLOW-055`). Every appended
requirement is pending.

Register schema 11 (1 October 2026) follows visual specification 1.12. It
supersedes five rows that quoted changed values: `INP-013`, which kept play
landscape-only, by `INP-014`, which adds portrait; `LAY-019`, whose center
screen always filled the view's height, by `LAY-020`, which fills the width in
narrower views; `ART-033`, whose modal glow stopped 24 dp beyond the dialog, by
`ART-036`, which also outlines the title; `FLOW-041`, whose center column held
the spectator lines, by `FLOW-056`; and `FLOW-049`, whose status line changed
at once over a fixed rule, by `FLOW-057`, which adds the Remastered accents. It
appends the portrait view (`LAY-021`), the multiplayer weapon wheel
(`WID-034`), the Competitive HUD (`FLOW-058`) and the spectator follow camera
(`FLOW-059`). Every appended requirement is pending.

Register schema 12 (1 October 2026) follows visual specification 1.13. It
supersedes `FLOW-037`, which kept the walker and hover tank's stock exit and
lock lines, by `FLOW-060`, which shows the HUD prompt and keeps the vehicle
HUD's transmission display at the trailing screen's corner. It appends the HUD
prompts (`WID-035`) and The Awakening's vehicle display refinements
(`FLOW-061`): the transmission display on every vehicle, the MCC side gun's
hull warning (the gun itself cannot be damaged, so it has no turret health),
and the speeder and GEV corrections. Every appended requirement is pending.

Register schema 13 (1 October 2026) follows visual specification 1.14, which
measures the modal against the stock. It supersedes `ART-036`, whose modal
title stood on the raised top line, by `ART-037`: the title's capitals hang
from that line into the lit slot, the silhouette is inset in its rectangle so
that the inset is the lit margin, and the first body baseline sits 87.5 dp
below the rectangle's top. `ART-037` is pending.

The [17 September implementation audit](masked-input-review.md) records current
delivery gaps and the shared vector-mask input repair. `INP-007` remains partial;
requirement counts, migration acceptance and final gates are unchanged.

The subsequent [independent text-scale increment](text-scale.md) implements
runtime typography and responsive SYSTEM layout. `LAY-004` is now partial;
the [interface preference increment](interface-size-preferences.md) adds normal
scale/reset controls and window fitting, making `LAY-003` partial as well.
The [output-size font increment](output-size-fonts.md) now adds bounded shared
physical-size glyph atlases, making `TXT-002` partial. Font style/weight,
largest animated transform density, complete text services and screen/editor
qualification remain required. All 271 migrations and seven final gates remain
unaccepted.

The [paired-field visibility repair](paired-field-focus.md) makes the SYSTEM
brightness and ambient-light sliders grow with their numeric readouts. Focus
keeps the complete pair visible at the qualified text sizes. It adds partial
`LAY-004` evidence without changing requirement counts or acceptance.

The [SYSTEM display-control increment](display-controls.md) adds four typed
display drafts and guards MSAA against the active strict display path's
capabilities. It contributes partial `BEH-002` and `FLOW-002` evidence; complete
display catalogs, effects, dependent controls, visible window behavior and the
screen/editor gate remain unaccepted.

The [SYSTEM dimension-field increment](dimension-controls.md) adds four exact
whole-pixel editors and dependent availability. Draft validation permits
intermediate width/height pairs; Apply still requires a supported complete
request. This adds partial `BEH-002` and `FLOW-002` evidence without changing
requirement counts, migration acceptance or final gates.
Its expanded-language matrix also repairs focus reveal for oversized numeric
fields, adding partial `LAY-004` evidence while preserving deliberate scrolling.

The [retained screen increment](retained-screens.md), based on engine `f5503259a5a2f618e5dd1cebab9adc55c2513f79`, builds the title, single-player pause and stock loading screens behind the single `ui_retained` gate. It adds image nodes, additive and multiply path blends, a view-height canvas and exact overflow clipping under translation and scale. Twelve windowed engine runs passed on OpenGL and Vulkan at 16:9, OpenGL at 4:3 and with the gate off; the gate-off runs and the gate test show that nothing retained loads. `FLOW-024`, `FLOW-047`, `FLOW-048`, `ART-030` and `REN-014` move from pending to partial, with acceptance evidence still empty. The Objectives action, the Strogg variant, soft focus, the Remastered loading details and the multiplayer menus remain. No migration acceptance or final gate changes. Evidence: `.tmp/ui/retained-screens/validation-evidence.json`, SHA-256 `e645a25b3daae42e1b4cc5a6301fc67cfc8c31c85895759f92fb4a179697d5b9`.

The retained loading screen's Remastered progress, based on engine `9417f8bbd1465dc54ed63f16e917cdac823e60fb`, adds the levelshot drift, the difficulty line, LOADING over a 240 u bar with the loader's phase and its place or asset count, the percentage, the load time and a continue prompt that follows the last input family. Real single-player loads traced the phases in order on OpenGL and Vulkan. `FLOW-024` and `FLOW-047` gain partial evidence without a status change; tips, touch prompts and the Remastered multiplayer screen remain. Evidence: `.tmp/ui/retained-loading/validation-evidence.json`, SHA-256 `9961bf8402d8897d4d95be20ab20f780ccd371c674c0b7371b676c4bdcce2fc8`.

The retained loading screen's Remastered multiplayer composition, based on engine `f1b696db3965e90792d5275718c94f4e64d1ef56`, keeps the band low under a server card with the server name, address, mode and limits, and reads JOINING when a multiplayer load ends. `FLOW-047` gains partial evidence without a status change; players by team, the server message, the arsenal and the Welcome hand-off remain. Evidence: `.tmp/ui/retained-loading-mp/validation-evidence.json`, SHA-256 `238cca71ae82b18ded323317822f1e770b3db55f4ebeea9afc58753cb73994db`.

The retained title carry, based on engine `2ad097f515aa39c3df6c3a0a55ce950964fb8342`, carries the chosen navigation label into the page title slot as the bands dock, on the title and single-player pause menus. With the focus glow and the emblem light already present, `ART-022` moves from pending to partial, with acceptance evidence still empty; the depth shift, high-contrast rail, symbols, tags and HDR rules remain. Evidence: `.tmp/ui/retained-title-carry/validation-evidence.json`, SHA-256 `36aa73558fb395d333351f52102d070871fc69352136a168137f07cd57785b69`.

The retained title's depth lean, based on engine `cb15ae91a93de5cd43f89487baf8ef3f85025e9a`, leans the backdrop, light and grid, and frame up to 6 dp away from the pointer while content stays fixed, and holds still under reduced motion. `ART-022` gains partial evidence without a status change; the stick and tilt inputs remain. Evidence: `.tmp/ui/retained-title-depth/validation-evidence.json`, SHA-256 `938294a3434fcd14513e9c5b54fa3efa7c53913a7544700e62a0f4d4d9f835ce`.

On engine `d491aa476c82c42e0828f06b5ff7b74caf22384b` the retained title's depth lean also follows a held look stick, which leads while deflected. `ART-022` gains partial evidence without a status change; device tilt remains. The session code is qualified by `ui_retained_gate.py`, not by an engine capture.

The retained confirmation modal, based on engine `e039c0ecad7c0010c8a52966235c791dc9dbd8b7`, rebuilds the title's exit and the pause menu's quit confirmations on the stock dialog's construction over the scrim: the stock art's silhouette sits inside the glow column so a 6 dp lit margin outlines it, and the title's capitals hang from the raised top line with a 1 dp black outline at 0.85. Engine captures on OpenGL and Vulkan at 16:9 and OpenGL at 4:3 were compared with a capture of the stock exit dialog. The same change makes retained layer composites sample GL row order on Vulkan, where every faded or masked element had vanished since 2026-09-25. `ART-037` moves from pending to partial, with acceptance evidence still empty; cards, the soft-focus glow, the other dialog widths, the chat box, tab strip and header band remain. Evidence: `.tmp/ui/retained-modal/validation-evidence.json`, SHA-256 `32b9bbf8b67cf36b59dca317274579f637bb2eda2bc7c3000b7122160b9f8945`.

Retained timeline completion programs, based on engine `9171699a34db3ebf4affb6c2880604eb3aee643b`, let a timeline name an event to run when it plays to its end: no actions, no completion cycles, and cancellation, replay or a takeover never complete it. The retained confirmations use them for the stock modal.enter and modal.leave. With the retained screens' authored band and content choreography, `MOT-012` moves from pending to partial, with acceptance evidence still empty; the links' 50 ms fade, page-side timing, band.slide, row.flash, list.unfold, light.up, destination backings and the sampled comparison with the stock timelines remain. Native tests only; no engine capture of the transitions. Evidence: `.tmp/ui/retained-modal-motion/validation-evidence.json`, SHA-256 `a23a96a22a4abd67ded93a9a00624ffa6dba93d4d4275170346aafc0c2ba5b70`.

On engine `694889557900c39e7940261dc61810cdffc52627` the retained title and pause screens fade the plinth and its secondary links over 50 ms on depart, give hover feedback at once, and dim the wordmark while the title's Exit modal is open. `MOT-012` gains partial evidence without a status change. Qualified by the native screens test; no engine capture. Evidence: `.tmp/ui/retained-motion-tokens/validation-evidence.json`, SHA-256 `0d139aa4bd795cece9cf6e7ebda88883b0b52bd53c1ce8ab9544a9ca3b082218`.

On engine `98fb185f2bd6051fd9acc1f213913ea84ae2c2d3` the retained title's modal and depart motion was captured in the engine on OpenGL and Vulkan, which exposed and now verifies a host fix: additive pictures no longer black out their rectangle inside a fading composition layer. `MOT-012` gains partial evidence without a status change. Evidence: `.tmp/ui/retained-motion-engine/validation-evidence.json`, SHA-256 `b915a8889da4535c7b4a19fc6c9af500ad8bc1dccd5608dafe6057221189cbf9`.

Retained backdrop soft focus, based on engine `813a8acb759a1b4178f282698bd67a3ad1888343`, softens the screen behind the title's exit and the pause menu's quit confirmations on OpenGL and Vulkan: a 5 u blur at 0.80 saturation, never dimmed, ramping with modal.enter and modal.leave, with the glow cut to the dialog. The opaque-backing option and renderers without GLSL keep the scrim and the stock column. `REN-016` moves from pending to partial, with acceptance evidence still empty; settings-page, in-game panel and multiplayer modals, nested layers, GLES and scale captures and the GPU cost remain. `ART-037` and `MOT-012` gain partial evidence without a status change. Evidence: `.tmp/ui/retained-softfocus/validation-evidence.json`, SHA-256 `ae57a44a9b1ddf3d582d1caa7d8565e82582b76a3c63f09717a7f8e5efec6eaf`.

On engine `f5c865df8bb499987bfe73b696a792136a8958cd` the retained single-player pause screen sits over the paused view softened with modal.softfocus's values, never dimmed, ramping in with menu.fade on every pause; the opaque-backing option and renderers that cannot soften keep a darkening scrim and vignette. Engine captures on OpenGL and Vulkan show the view blurred and the HUD crosshair blurred away. `BEH-007` moves from pending to partial, with acceptance evidence still empty; the release on resume, the multiplayer menus, gameplay traces and input-leak proof remain. `FLOW-048` and `REN-016` gain partial evidence without a status change. Evidence: `.tmp/ui/retained-pause-softfocus/validation-evidence.json`, SHA-256 `1443f7b6398db734e30c564d03a831f0aa401f3c3940d726a31bcb0ca9e4268d`.

On engine `eae2d39ed3cd59f6dd3e45cdda374de56a59a5e2` the developer command `ui_retainedProfile` also profiles the session-owned root views when no preview document is loaded, reporting per submitted frame their retained CPU time, the frame interval, and their layer composites and backdrop passes. It is a measurement interface only; no requirement status changes.

On engine `c5119c4193b19ab7524e1b2b08b6d44bdd19876b` the retained title screen's CPU per frame falls from about 9.6 ms to 3.6 ms on a debugoptimized build, with byte-identical engine captures on OpenGL and Vulkan: layers composited at zero opacity are dropped, sub-pixel jitter below 1/1024 px keeps a compiled vector mesh, the tessellator gives identical meshes with fewer allocations, unchanged retained meshes are reused with a stable identity (`Host::DrawMesh`), and motion and input eligibility skip unchanged work. See [runtime performance](runtime-performance.md#retained-title-screen-cpu). It is performance work only; no requirement status changes.

On engine `58ca62c695a54106fe6b20f7d01f67b0e951f585` the retained title screen submits about 117,000 vertices a frame at 1280x720 instead of 569,000, with byte-identical engine captures on OpenGL and Vulkan: plain solid paths triangulate their resolved boundary with shared vertices instead of every overlap of a stroke's segments and joins (the wordmark's halo), coverage runs that step one pixel a row become sheared quads when their colour matches, and paint under a mask skips the tiles outside the mask's hull. The line dropped this tessellation change when it merged into main on 14 October: main's coverage atlas and frame replay draw the stationary artwork instead, and the traced wordmark whose halo needed it is gone. It is performance work only; no requirement status changes.

On engine `cc7122143400555b15bcbba427235e3d01c116cd` the SYSTEM page's Apply/Keep/Revert confirmation and unapplied-changes dialogs soften the screen beneath them where the renderer can, keeping their 0.6 backing otherwise. `REN-016` gains partial evidence without a status change; native tests only. Evidence: `.tmp/ui/system-dialog-softfocus/validation-evidence.json`, SHA-256 `4e33e60981080475ab3a66f6098b885008ebb2dd657a7211c570cc8de342a6c5`.

On engine `31028407323741ded6b1a737f7f227d741aebba3` the retained screens are on by default (`ui_retained` 1, archived), and each one falls back to its stock GUI when it cannot stand in for it: a document that is not installed (quietly) or cannot load (reported once), a legacy main menu without a page the title and pause screens hand off to (a mod's own menu), or a view that fails after a renderer or language change. The SYSTEM page joins the default only once it offers every stock setting; it still lacks the display mode list, display device, multi-monitor, refresh rate, video quality and light-grid preload. The SYSTEM page and the campaign selectors fall back from the click that asked for them, CONTINUE shows a save's own picture only when its file exists, and `ui_retainedStatus` lists the fallbacks. Engine captures on OpenGL and Vulkan, in which a private mod hides or replaces content, show each stock screen taking over. `RUN-004` moves from pending to partial, with acceptance evidence still empty; a changed document's reload, stale hash checks, a manifest-driven mapping for every stock GUI and a mod's own stock-named loading GUIs remain. Evidence: `.tmp/ui/retained-default/validation-evidence.json`, SHA-256 `4aae5c75825a2036a9569bc19496bca79d567c3cf7ce682ca28dd576c19cf7f7`.

On engine `00436c476dc441697dcd339317cb5008f1ef4476` the retained pause screen's level block chooses its levelshot as the loading screen chooses its picture: the map's `loadimage`, the intro art for a map that loads through the intro screen, then the map's own levelshot, each only when it is installed, ending at the generic art. Engine captures on OpenGL and Vulkan show airdefense1's intro art where the generic loadscreen was. No requirement status changes. Evidence: `.tmp/ui/retained-pause-shot/validation-evidence.json`, SHA-256 `d3b289962528acd575ddf5fc17033f8ece7e2e159d89d3724d2e10d36ffa6a06`.

On engine `b6a7979bf2efe96fe8a2299ed342c3dbd85e8794` the retained pause screen's level block lists the objectives the player holds, newest first, and the time in the mission, which the session asks the game for with the `retainedPauseState` menu command; a game module that publishes neither leaves the map's objective summary. Engine captures on OpenGL and Vulkan show airdefense1's first two objectives and the time line. `FLOW-048` gains partial evidence without a status change; the Objectives action, completed objectives, the total time, the last save and the Strogg variant remain. Evidence: `.tmp/ui/retained-pause-objectives/validation-evidence.json`, SHA-256 `43f7a4867c20868c828296b1faf46a1bd64c1282538da0d113b0e58e7df2074b`.

On engine `9dc4eac7ba8dff03a429b7fbc54bd0a66c60fb23` the retained pause takes the Strogg family after Kane's stroggification (`guis/menu/pause_strogg.q4ui`): the bands' 30-degree shoulders, downward teeth and etched circuit traces, shouldered plates, R_Strogg labels and a chamfered level block, with every label arriving in runes and playing the short credits translation under the scan bar. The session asks the game once a level with the `retainedPauseFamily` menu command, and the Marine pause stands in when the Strogg pause cannot present. Engine captures on OpenGL and Vulkan at 1280x720 and on OpenGL at 1024x768 show game/recomp translating and at rest. `MOT-015` moves from pending to partial, with acceptance evidence still empty: the full credits translation, the intro decode, the changeover, the MCC transmission, the wheel's short forms and sampled timelines remain. `FLOW-048` gains partial evidence without a status change; the Objectives action, completed objectives, the total time and the last save remain. Evidence: `.tmp/ui/retained-pause-strogg/validation-evidence.json`, SHA-256 `2180c19336bea8e678a54440b5eacb20ab01d31a4a5121893dcbc9e725886894`.

On engine `a3a7eb5f4b7ead9cea89a886d40734b4f70e5a82` the retained pause lists OBJECTIVES among its seven actions and opens the objectives display as a page of the pause in both families: the bands dock and the label carries into the title slot, then the stock entry motion brings the back bar and a plate for every open objective, newest first, with its title, screenshot and wrapping description, then the objectives completed on the map, the serial and the stock empty state. The list scrolls under its bar, and Back returns with the bands. The level block lists the objectives with their state, and its time line adds the newest save's age. The game publishes descriptions, screenshots and completed objectives (kept per map, unsaved), and the session resolves every objective screenshot of the level inside the level load. Engine captures on OpenGL and Vulkan at 1280x720 and on OpenGL at 1024x768 show airdefense1 and game/recomp. `FLOW-042` moves from pending to partial, with acceptance evidence still empty: the display held on the scores button in play, its hold or toggle setting, the notices, Classic's construction, the failure banner and gameplay qualification remain. `FLOW-048` gains partial evidence without a status change; the total time, a failed objective's state, completed objectives surviving a save load and complete qualification remain. Evidence: `.tmp/ui/retained-pause-objectives-page/validation-evidence.json`, SHA-256 `f700445cf82cb8e6e7e01ba64b30bdbe8e55adcb22fbb0237e23d9976f0577c6`.

On engine `3c367e0fdbc4ca211da9ade81ba1a81701e95a93` RESUME closes the retained pause screen at once while the softened view, or the scrim standing in for it, releases over 250 ms over the running game, as the modal backdrop does: the session keeps drawing the closed screen, without input or cursor, until the release ends, and every activation after a release shows the screen and ramps the softening back in. Engine captures on OpenGL and Vulkan at 1280x720 show airdefense1 and game/recomp mid-release and resumed. `BEH-007` and `REN-016` gain partial evidence without a status change; a gradual release on the ways out that replace the view remains. Evidence: `.tmp/ui/retained-pause-release/validation-evidence.json`, SHA-256 `f7e0da538649ac0ccb7f21133ffd74133909e3bf25dc342bad0d57a48ef8f957`.

On engine `e4583498dba9e7e67b2b15f757880c75c68631ee` a mod's own copy of a stock loading screen presents instead of the retained loading screen: the session keeps a stock-named loading GUI that the mod's own game directory supplies, file by file, while openQ4's, the stock game's and the Awakening's stock screens are still replaced. Engine runs on OpenGL and Vulkan as a private mod load game/airdefense2 under the mod's screen. `RUN-004` gains partial evidence without a status change. Evidence: `.tmp/ui/retained-mod-loading/validation-evidence.json`, SHA-256 `a5bf27df44fef680527d2358f64eac706d3bbac6a0f2f1a97a9a1ddaa11bf874`.

On engine `779a2bc91ede808813f80c541daff9943abac840` a multiplayer load fills the retained loading screen's arsenal: each kind of item appears as it spawns, fading in over 150 ms in its color code, which the session reads from the item's colour-coded material. A listen server's mp/q4dm1 load publishes its eleven kinds with their Appendix B.4 tints, and captures on OpenGL and Vulkan at 1280x720 show the arsenal in the thick band. `FLOW-047` gains partial evidence without a status change. Evidence: `.tmp/ui/retained-loading-arsenal/validation-evidence.json`, SHA-256 `a2bd70225491d8ea1ff802bcb3b046b68ac7ca17136e49d46680faf92ceee094`.

On engine `5484be6253b5be7fe88723eb60e828ca700f46a7` the retained Single Player page opens its Campaign section as a sub-page (specification 1.10): the top band steps one notch pitch, the page title becomes the crumb, fitted per language to the band's thin span, and the CAMPAIGN label carries to the step's foot; the sub-page presents at 350 ms, Back climbs out over 300 ms with the focus returned to Campaign, Back while going deeper reverses the change from where it stands, and reduced motion places both levels with an 80 ms fade. Engine captures on OpenGL and Vulkan at 1280x720 and on OpenGL at 1024x768 show both changes. `MOT-016` and `FLOW-050` move from pending to partial, with acceptance evidence still empty: the other sub-pages, the section layout, Classic, a reversible Back and the sampled traces remain. Evidence: `.tmp/ui/retained-subpage/validation-evidence.json`, SHA-256 `eca04d36298751db3331776e99e4fde0e3c123a6bb06570d0eed62c1935b44b5`.

On engine `e59fdc340f1037f973a17b61cd10c96238794336` single player loads run localized tips on the retained loading screen: a TIP tag over one tip of at most two lines in the band's leading part while the level loads, changing every 6 s with a 250 ms cross-fade, 18 Quake 4 mechanics verified against the game code in all seven languages, each measured to fit. Engine captures on OpenGL and Vulkan and in German, Polish and Russian show the tips, and a real map load's trace shows them changing. `FLOW-047` gains partial evidence without a status change. Evidence: `.tmp/ui/retained-loading-tips/validation-evidence.json`, SHA-256 `4f0f69638a05f185f8018379dd746d6cd804d2eef97d7b66d3206dc090dea4b4`.

On engine `c1ed29af5f99218bf5bab284b1e50bece28475fd` the multiplayer loading screen's server card lists the players by team, with spectators and players still connecting, and shows the server's message: game_mp names the mode, limits and teams from the server info, and the session captures the players on a listen host's map change, on a client's map change and on a fresh connection, from an optional roster block after the connect response's server info that old clients and servers never read. Engine captures on OpenGL and Vulkan and in German, Polish and Russian show the card, and traces from a listen host and a second client show the players published. `FLOW-047` gains partial evidence without a status change. Evidence: `.tmp/ui/retained-server-card/validation-evidence.json`, SHA-256 `dc352d1c28dfe32f9057dcb7d8ee69d17ad11ba1712b42cb1472c236b617c113`.

An earlier recorded increment starts from engine `17b106daa1827d6e50c786f99ea5970b1e8b91cf` and companion
`1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. The register also identifies the current
[manager/snapshot foundation](instance-persistence.md) as partial
evidence with native and SP/OpenGL/MP/Vulkan validation. It does not treat that work as a
finished adapter, complete widget persistence or game-save compatibility.
The subsequent [application integration checkpoint](managed-application.md)
adds normal retained loading, two typed settings operations and coordinated
engine views. These remain partial evidence for the full application contract.
The [presentation alias checkpoint](presentation-aliases.md) adds explicit shared
metadata and rendered-property aliases, transient/explicit expression ownership,
snapshot version 2 and immediate ancestor visibility/input eligibility. The
checkpoint records native checks, five alias gameplay captures and a sixth
typed-settings regression across Windows SP/OpenGL and MP/Vulkan. These qualify
the authored subset and retain their stated display/platform limitations.
The [event-program checkpoint](event-programs.md) adds canonical ordered
state/presentation/motion programs, conditional and nested calls, immutable typed
action arguments, atomic pending-dictionary/host-state entry and normal GUI
lifecycle delivery. The Session pump delivers completed programs independently
of physical input; queued controls recheck document and input eligibility.
Five native suites, production-method checks and two source/binary-bound Windows
gameplay captures cover this subset: SP/OpenGL at 125% and MP/Vulkan at 200% passed
their event, ownership and outgoing-lifecycle checks and render-target visual
review. The captures include non-replaying save restore, language/video recovery,
independent peers and SP/MP pause behavior. That historical checkpoint's catalog
contains brightness, shadows and dismissal. Full legacy broadcast/timer/native-CVar
semantics, production GUI flows, complete widgets, world/game-save/demo contracts,
physical-device/platform/performance qualification and the native editor remain
open. The immutable local evidence is
`.tmp/ui/events-review/capture-evidence.json`; the separate register audit is
`.tmp/ui/events-review/register-validation.json`.

The [SYSTEM settings increment](system-settings-contract.md) adds the actual
source/control contract, complete 53-field host catalog, bounded transaction core
and normal typed service operations. Owned draft edits, immediate Apply/Cancel/
Defaults, conflict-safe rollback, read-only service state, lifecycle abandonment
and config-write protection have native and production-body test evidence.
That checkpoint's service test passes 15 scenarios; the separate host test covers
the full catalog. At that checkpoint, any changed non-immediate field rejected
the entire Apply batch before writes. Profile expansion, complete production
controls and the native editor remain open. Its gameplay evidence is
recorded at `.tmp/ui/settings-review/capture-evidence.json`:
hidden windowed Windows SP/OpenGL at density 125% and MP/Vulkan at density 200% each pass 168
ordered readbacks, 19 service results and two language/video recoveries. The
source-bound probes and reviewed render-target images qualify this subset only;
they do not perform real device changes or accept the production page. The
structural register audit is `.tmp/ui/settings-review/register-validation.json`.

The [display-device foundation](display-device-contract.md) supplies strict actual
window state, recoverable typed renderer requests, video-lifetime identity
preservation and backend presentation observations. Its diagnostic resizing and
restore probes preserve the open draft through resource reconstruction. The
Windows SP/OpenGL and MP/Vulkan captures each pass eight device observations,
32 ordered settings readbacks and four reviewed render-target images, including
gameplay after failure and restoration. Actual interval and GL sample changes
remain independent of stored preferences; an additional SP/OpenGL run verifies
gameplay with a four-sample default framebuffer and its restoration. Seven native UI suites, five production
method harnesses and the 23-test capture oracle qualify this subset. The immutable
record is `.tmp/ui/display-review/capture-policy-evidence.json`; its structural register
audit is `.tmp/ui/display-review/register-validation.json`. The
full production settings page, visible geometry behavior and platform
qualification remained open at that checkpoint.

The subsequent [display confirmation implementation](display-confirmation.md)
adds queued Apply/Keep/Revert/Retry, durable Pending/Confirmed recovery records,
checked atomic configuration writes, native settings/window-placement leases and
strict first-device startup recovery. Confirmation requires an eligible owning
view and successful presentation; request identities prevent stale actions from
affecting a later draft. The new native and gameplay qualification is recorded
separately from the immutable display-device checkpoint. This work remains
partial evidence for `BEH-002` and `FLOW-002`; it does not accept the production
SYSTEM screen, audio/resource effects, preset expansion, the native editor or any
of the 271 GUI migrations.

Its immutable local checkpoint is
`.tmp/ui/confirmation-review/capture-evidence.json`, SHA-256
`1f3e66f316a689ccaae2bf0c1e08c322deb296a0539852047a8a0c9ef0b00521`.
Two inspected display probes and four two-process recovery cases passed on
hidden windowed Windows SP/OpenGL and MP/Vulkan. The approved recovery cases
explicitly inject a Confirmed marker into a real Pending journal; they do not
qualify an actual interrupted Keep or power cut. The checkpoint binds the
tested source/binaries, native checks, configurations, journal bytes, logs and
engine screenshots. Its register audit is
`.tmp/ui/confirmation-review/register-validation.json`. Requirement statuses,
acceptance criteria and migration entries remain unchanged.

The historical [typed value-control increment](value-controls.md), based on engine
`b3bf279b4347329cd8a45a4533536adc0510e860`, added canonical toggles,
stepped sliders and constrained choices with authoritative typed readback,
immutable invocation-local proposals and authored vector parts. The packaged
`guis/menu/settings/system.q4ui` declares all 53 draft/baseline fields and
presents eight initial immediate image controls. Its default-off,
non-archived `ui_retainedSystem` option uses normal Session ownership and
preserves the exact parent on a clean return. Dirty Back opens a local prompt;
at that checkpoint, discard reset the draft and stayed on the page. The document also
provides the existing eligible Keep/Revert/Retry confirmation structure.

The final Windows build/package stage and 13 native UI suites passed. The
production Session boundary passed 173 checks; the capture runner passed
15 tests and rejected 444 altered-log cases. Two hidden windowed captures after
active gameplay, SP/OpenGL at 125% and MP/Vulkan at 200%, each passed 18 ordered
normal-route stages at 1280x720. All 14 engine render-target images were
reviewed, including the two-column/scrolling-column layouts, popup placement,
dirty dialog, distinct disabled Apply, resource resets, parent return and
reopen. SP had no warnings/errors; MP had 94 baseline warnings and no new warning
messages or errors. This evidence covers semantic integration and the recorded
static views; it does not qualify physical input, complete motion or all
display/platform/language cases.

The immutable record is `.tmp/ui/value-controls-review/capture-evidence.json`,
SHA-256 `2c5c14d4d0a9d0fb8cc8d45dab9af39fdc9e6c4ab017f93a1f6a3e49c3cf1a40`.
It binds 8,778 final build inputs, seven runtime binaries, packaged source and
supporting logs, images and tests. The strict register audit is
`.tmp/ui/value-controls-review/register-validation-final.json`.

That checkpoint did not establish full control, screen or artwork
acceptance. Precise numeric entry and IME, the full
setting inventory and dependent popups, remaining effects and presets,
Defaults/Cancel/discard-and-exit, accessibility, physical-device and display
qualification, component authoring and the native editor round trip remain
required. Historical display-confirmation captures cannot qualify these new
controls or normal entry/return behavior.

The subsequent [SYSTEM controls and transactional exit increment](system-exit.md)
adds seven immediate settings and VSync to the packaged page, bringing it to
16 value controls while preserving all 53 draft/baseline fields. Dirty Back now
offers Apply changes, Discard changes and continue editing. The ordinary Apply
button stays on the page. Apply-and-exit uses a typed service operation and a
one-use owner/request-bound receipt; asynchronous return requires successful
Keep and completed persistence. Revert, timeout, failure and recovery cancel
exit intent, and GUI dictionary values cannot authorize return. Repeated wheel
events no longer replay an unchanged pointer move; actual movement and button
input retain pointer-navigation reclamation. Authored modal scopes establish
safe default focus before drawing, restore prior focus and invalidate stale
queued input across scope replacement and instance reconstruction.

The final Windows build, 15 native suites and four staged normal Session runs
passed. All 34 engine images from 92 semantic stages were reviewed. The local
record `.tmp/ui/system-exit-review/capture-evidence.json` binds final sources,
packages, binaries, tools, logs and images. The
[qualification and limits](system-exit.md#qualification-and-remaining-work)
include localized-label CPU layout checks and outstanding native-resolution
composition/effect-parity findings. Earlier captures retain their original scope.
The complete settings inventory and dependent flows, numeric text entry/IME,
remaining effects and presets, Defaults, full modal/scrollbar behavior,
source-derived artwork/transitions, editor round trips and the complete
platform/language/display/physical-input matrix remain required. No requirement,
milestone, production screen or migration is accepted by this increment.

The subsequent [native-output OpenGL repair](native-output.md) removes the
UI-only swap-tail resolution filter while retaining existing world sizing and
resolves and the explicit legacy mode-0 crop. Its final integrated production-method
test passes 1,220 checks and rejects seven compiled mutations. Separately bound
Windows SP/OpenGL before/after engine images establish zero RGB differences
from the 100% UI reference across eight ordinary-scale cases after the repair.
The preserved baseline demonstrates the former filtering;
`24` engine images were reviewed. `SUR-006`
moves only from pending to partial, with acceptance evidence still empty.
Dynamic resolution, all UI surfaces and input/cinematic/subtitle geometry,
CRT parity, the existing GL mode-3 filtering mismatch and M6 remain open.
Historical evidence retains its original scope and bindings.

The subsequent [text-entry foundation](text-entry-foundation.md) integrates
validated Unicode commits/preedit, bounded local editing, exact numeric parsing
and checked Windows clipboard primitives. Pending event payloads are released
on queue clear/overflow. Native and compiled-method tests and a Windows
SP/OpenGL gameplay/menu smoke qualify this bounded integration. Live fields,
ordered native ownership, IME, shared shaped caret/grapheme/bidi editing and
the complete authoring application remain required. The final local record is
`.tmp/ui/text-input-foundation/validation-evidence.json`; the strict structural
audit is `.tmp/ui/text-input-foundation/register-validation-final.json`.

The [numeric-field increment](numeric-fields.md) adds a canonical exact-entry
model, authored selection/caret/preedit rendering, shared scalar font runs and
bounded inactive draft/history restoration. Fresh host values guard edits,
queued numeric dispatch and accepted acknowledgements. The final Windows build,
21 retained-UI suites, SP/OpenGL at 125% and MP/Vulkan at 200% managed fixture
captures after gameplay, and an ordinary SYSTEM rendering regression qualify
this subset. The journal repair bounds historical-layout payloads and lifetime
cleanup. Its local record is
`.tmp/ui/number-field-integration/validation-evidence.json`; its structural
audit is `.tmp/ui/number-field-integration/register-validation-final.json`.
Native keyboard/clipboard/IME routes, complete shaped/grapheme/bidi editing,
production precise-entry settings, editor round trips and full product/platform
qualification remain required. No requirement status, milestone, gate or GUI
migration is accepted by this increment.
No status, migration entry or final gate is accepted by this increment.

The native owner/store checkpoint adds exact managed editor ownership, engine
queue-continuity checks, a portable shadow document and an actual Windows SDK
text store. Application text/selection notifications allow reads and defer
writes until the full batch completes. The Windows build, 29 UI suites and the
updated text-store suite pass; counted SDK tests cover 729 checks and reject
18 compiled fault variants. Source-bound windowed SP/OpenGL at 125% and
MP/Vulkan at 200% captures pass 32 semantic operations with ten reviewed engine
images. The immutable evidence is
`.tmp/ui/native-owner-integration/validation-evidence.json`. The first SDL fence
remains disabled and does not prove earlier-event consumption; checked queue
delivery, live native editing, complete IME/shaping, production fields, the
editor and all migrations/gates remain open. No acceptance state changes.

The precise SYSTEM-field increment adds the two paired brightness fields,
localized validation, checked clipboard commands and explicit handling of
unfinished local edits during Apply/exit. A completed service cancellation and
an unchanged full editor inventory are required before local Discard. Keep
Editing synchronizes projected layout before resuming the field in the same
dispatch. Growing validation scrolls into view without overriding later user
scrolling. The Windows build and 32 UI suites pass. SP/OpenGL at 125% and
MP/Vulkan at 200% each pass 37 semantic operations with 17 reviewed engine
images after gameplay, recorded in
`.tmp/ui/native-editing-integration/validation-evidence.json`.
Checked queue consumption,
owned SDL event collections and a portable composition reconciler extend the
native-input foundation; field activation and complete IME delivery remain
unfinished. The new evidence supplements the existing scope and accepts no
requirement, GUI migration or final gate.

The subsequent [native field binding and precision checkpoint](numeric-fields.md)
starts from engine `7a8036ac87dde9d29eb279a9c6ecc31cf187df55`. It adds clean authored
slider ticks, fixed CVar writes for small and custom numbers, complete native
Number presentation, checked collection scopes and prepared stable settlement.
The Windows build and 36 UI suites pass. SP/OpenGL at 125% and MP/Vulkan at 200%
each pass 38 semantic operations and 12 reviewed engine images after gameplay.
The immutable evidence is `.tmp/ui/native-field-binding-integration/validation-evidence.json`.
At that checkpoint, 200% focused controls still met the scroll boundary; page layout
qualification remained open. Live managed/provider/Session integration, exact persistent
recovery, native IME, the complete editor and all production migrations remained unfinished.
No requirement status or acceptance gate changes in this increment.

The native managed-bridge increment, based on engine
`59dbfa9429056a85ead40fff5e8f4f5b38be4225`, extends the [numeric field contract](numeric-fields.md)
and [native text boundary](text-input-routing.md). Exact typed settings comparisons and FTZ/DAZ-independent journal number serialization extend through transaction, display validation and startup recovery. Managed native-owner endpoints re-resolve the registered allocation/backend/document after resource preparation; copied owner barriers and exact retirement preserve stable drafts. The Windows hook-to-store bridge binds one checked provider generation and native/editor lease, preserves the immutable closed receipt through FIFO acknowledgements and synchronization, and closes its own callback scope before fence publication. Explicit lifecycle reconciliation and a checked idle barrier query supplement the ordinary Pump path. Shared focus reveal uses exact projected border corners, a density-aware 4dp inset where authored scroll ranges permit it, nested transformed scroll planes and the owning view clock without repeated pointer-scroll takeover.

The Windows engine build and all 40 UI suites pass. Windowed SP/OpenGL at 125% and MP/Vulkan at 200% each reach gameplay, pass 38 semantic operations and produce 12 reviewed engine screenshots. The focused field retains the checked 4 dp body inset; at 200% its bottom is pixel 464 inside a body ending at 472. Raw screenshots and PNG previews have identical RGB pixels. SP has no warnings; MP retains the previous 93 warnings. Final captures use the verified E: installation, whose engine package checksums match the earlier capture.

The coordinator passes MSVC debug checks with 18 rejected mutations, Clang with 19 (including allocation failure), and Linux sanitizer checks. SDK bridge, managed-owner and exact recovery tests retain their own frozen source bindings and explicit limits. The immutable combined record is `.tmp/ui/native-managed-bridge-integration/validation-evidence.json`. Earlier timeout, harness-path and asset-discovery runs remain preserved; they are not substituted for the final qualification.

Native activation, the production Session route/probe, ordinary native character association, candidate geometry, complete composition/shaping, full page/editor/platform qualification, all 271 migrations and seven final gates remain open. Counted SDK callbacks, semantic controls and bounded focus geometry do not establish installed IME behavior, physical-input qualification or real interrupted-device/power-loss recovery. No requirement status or product acceptance changes.

The [preset and Windows text-session increment](performance-presets.md), based on engine `b542315efd4b724c458bc9cd0f6310c76afd71e8`, adds the six shared performance profiles and Auto-Detect to the opt-in SYSTEM draft. The generated 32-field change preserves all 21 unrelated settings, including custom brightness, and publishes only after validation and successful result construction. New controls reuse the existing editable vector artwork; labels wrap using all six existing language tables. Popup highlighting stays transient, and authoritative service readback precedes acknowledgement. Unfinished Number edits guard bulk actions and exit.

The [Windows text-session controller](text-input-routing.md) owns one exact native/editor/window/provider lease through activation and cleanup, including ordinary event disposition and a fresh ownership check before the final ACK. SDK tests use counted providers; production native activation and installed IME behavior remain unqualified.

The full engine build and all 44 UI suites passed, including 28,802 production Runtime assertions across 48 locale/density/wider-glyph/resize cases. The first gameplay capture exposed a missing local archive declaration: the protected emitter limit rejected lower presets. That failed evidence is preserved. Four preferences now declare their archive policy; actual production methods passed 22 checks on each of MSVC and Clang, followed by a fresh engine build and staging. Corrected windowed gameplay runs covered SP/OpenGL at 125%, MP/Vulkan at 200% and Russian SP/OpenGL at 200%, with 95 preset operations and 15 reviewed render-target images each, all 53 live settings unchanged. Separate SP/OpenGL at 125% and MP/Vulkan at 200% numeric regressions each passed 38 operations with 12 reviewed images, preserving exact values, Apply/reset and full focused borders. In total 361 operations and 69 images qualify only these states. SP runs had no warnings; each MP run retained 93 existing warnings. The source-bound record `.tmp/ui/preset-session-integration/validation-evidence.json` has SHA-256 `69ccd3f6872a47cf9fa66f632c969c426e9e95f61098ee7b93f9f7c317e526b8`.

Full effect execution and recovery follow the [unified settings direction](settings-effect-execution.md). Complete settings, native input/shaping, the editor, all 271 GUI migrations and every final gate remain required. No requirement status or acceptance changes.

The [settings effect foundations](settings-effect-execution.md) add automatic completion, checked audio finalization and a strict schema2 recovery envelope. Production startup and live settings still use schema1; complete mixed effects and portable reconstruction remain required. The [native disposition increment](text-input-routing.md) accounts for both fixed Session/poll schedules and preserves ownership through platform, pushed and scalar polled storage. Live native activation remains disconnected.

The [compact SYSTEM layout](performance-presets.md) keeps the real Marine-font Apply label on one line and the complete first brightness control visible at 200%. Full engine build, staging and all 47 UI suites passed, including 41,623 Runtime checks over 48 locale/wider-glyph/density/resize cases. Four engine render-target images were reviewed after windowed gameplay: English SP/OpenGL at 125% and MP/Vulkan at 200%. No input was injected or controlled and no mixed-effect Apply was performed. SP logged no warnings; MP retained 93 existing warnings. A visible authored scrollbar, full page interaction and all real-font/platform states remain required.

The source-bound checkpoint starts from engine `efcfdb0ffe6eaffa11dbf2ced026495539156478`; local evidence `.tmp/ui/native-effects-integration/validation-evidence.json` has SHA-256 `909df76b2aea07f6641eece8550cd90bd1d3d354e84acf1ffe5ad06c0dbd6457`. Earlier failed layout captures and the corrected pack-only staging failure remain recorded. No requirement, GUI migration or final gate is accepted by this checkpoint.

The [checked image/material restart](settings-effect-execution.md) preserves original recovery inventories and rejects stale instance or metadata ownership. The full engine and renderer modules build; all 49 UI suites pass. Windowed SP/OpenGL at 125% and MP/Vulkan at 200% return to SYSTEM after ordinary video restart, with four reviewed engine render-target images and no injected or controlled input. This qualifies ordinary compatibility; the checked image-policy Apply operation still needs live qualification and portable consumed-policy/content recovery. SP logs no warnings; MP logs the previous 93 plus a vertex-cache virtual-memory warning during restart.

The checkpoint begins at engine `d6abeabc7e2e348ea11ce50a7671b830e77a15b1`. Its local record `.tmp/ui/renderer-effects-integration/validation-evidence.json` has SHA-256 `38ec300cd73f59baca9761b21351f3daa8612d6dbc73b98b0f420f4089975ad2`. Full settings, native input, editor, every GUI migration and all final gates remain required.

The [native retirement foundations](native-input-retirement.md) preserve the original route and distinguish an absent original native model from one whose adapter generation has changed. The [settings recovery increment](settings-effect-execution.md) records portable requested/observed audio state and immutable renderer policy consumed by completed output. These are qualified internal contracts; actual storage retirement, production native publishers, complete resource reconstruction and mixed settings Apply remain underway. Scroll geometry and vector scrollbar composition are prepared, while Runtime interaction and production scrollbar content remain pending.

The full engine and renderer modules build, and all 56 UI suites pass. Four reviewed engine render-target images cover actual SP/OpenGL gameplay at 125% and MP/Vulkan gameplay at 200%, followed by ordinary video restart and SYSTEM. There was no injected or controlled input. SP logs no warnings; MP has the same 94 warnings as the preceding restart checkpoint. The audio test runner initially failed because its snapshot-specific default path was incorrect; the correction passes the targeted and full suites.

This checkpoint begins at engine `b2cb00dfb87cc8663cd8e895dcccce37d9c1faa1`. Its local record `.tmp/ui/native-retirement-integration/validation-evidence.json` has SHA-256 `fffcbfd8ec790ea00f764ce774044406ba1392aa13ed10e6958d140e5f14b372`. Requirement statuses and all seven final gate decisions are unchanged.

The current [authored scrollbar increment](scrollbars.md) integrates the SYSTEM vector trough/thumb with actual Rml scroll geometry, local ownership-aware commands and logical offset persistence. [Actual retirement storage](native-input-retirement.md) now transfers exact original heads and releases checked empty slices. [Renderer recovery foundations](settings-effect-execution.md) additionally check DDS/decoded image reduction and real GL stream allocation. Earlier statements that these pieces were pending describe their historical checkpoints. Terminal native disposition/activation, complete content reconstruction and mixed SYSTEM Apply remain underway.

The full engine and renderer modules build, and all 61 UI suites pass. Four reviewed render-target images cover actual windowed SP/OpenGL gameplay at125% and MP/Vulkan gameplay at200%, followed by ordinary video restart and SYSTEM. The scrollbar geometry is read from the actual runtime; no input was injected or controlled. This qualifies the recorded opening views, not full physical-device or transition acceptance.

This increment begins at engine `5e0d3ebf20234415815a72c914fe788953e4367b`. Its record `.tmp/ui/production-retirement-integration/validation-evidence.json` has SHA-256 `5a638d3df286ca14adf225ff16a99f3e6140dc25fbf13bfddbc7f7db8e7d5407`. Only `WID-010` advances from pending to partial; no acceptance evidence, migration acceptance or final gate changes.

The [native ownership publication increment](native-input-publication.md), based on engine `d16c5b9e7935a273e274ea6c5956729d0da62c85`, connects actual Session, managed-GUI and SDL-window ownership to the route and typed emission inventory. The same checkpoint adds [conditional native alpha semantics](legacy-import.md) without accepting either affected GUI. The full build and64 integrated suites pass, together with the17-case adapter mutation check and four reviewed windowed GL/SP and Vulkan/MP gameplay/restart/menu images. Native activation/disposal and live legacy-alpha observation remain required. Its record `.tmp/ui/native-publication-integration/validation-evidence.json` has SHA-256 `10dfc153acc3d0a6e26fe398cee80dc8098893e702656ca35df93e8b821bc7ae`. No status, acceptance, migration or gate changes.

The [initialized native alpha observation](legacy-import.md), based on engine `fae10f2d5bd610369b42f042780d9cd8f24aa19d`, establishes the two stock monitor alpha terms as zero through their original table lookup, completed variable fixup and exact mapped property evaluation in SP/OpenGL and MP/Vulkan. Separate material-preload receipts preserve late-load warnings; subsequent fresh parser intervals are clean. The full build,65 UI suites,15 integrated mutation checks and four reviewed gameplay/restart/menu images pass. Source-bound retained lowering and later behavior remain unfinished. Record `.tmp/ui/legacy-observation-integration/validation-evidence-final.json` has SHA-256 `b5dcac65a575d0b26816dd7fcdf72df4bdbcbc8e3ad26bb17969b8ada49f3a83`. No requirement, migration or gate acceptance changes.

The [image-content recovery](image-content-recovery.md) and [terminal input ownership](native-input-publication.md) checkpoint, based on engine `2e95a66bab30b1acd3f315f86f3570f142965b1d`, connects exact CPU image bytes and actual queue disposal. The full build,67 UI suites,21 integrated compiled mutants and four reviewed gameplay/restart/SYSTEM images pass; Linux sanitizers also cover the updated portable input runner. Loaded-image census records size the later recovery work without accepting any listed resource. Record `.tmp/ui/terminal-content-integration/validation-evidence.json` has SHA-256 `486bfaed7fdec7b9c295e8547b3151687fb8bf48696438fb7158aa95a1031112`. Complete native dispatch and heterogeneous settings recovery remain in progress; no requirement, migration or gate acceptance changes.

The [authored choice scrollbar](choice-scrollbars.md) checkpoint, based on engine `fc4443cb071022e29b429c9b74731dfa9b0c3b86`, integrates all four SYSTEM dropdowns. The full build,69 UI suites,compiled behavioral mutations,Linux sanitizers and20 reviewed SP/OpenGL125% and MP/Vulkan200% gameplay/SYSTEM/popup images pass. Fitting lists hide inactive bars and scrolling leaves settings unchanged. Popup safe-area placement and source-derived framing still need refinement; physical/native input, complete settings recovery, editor and full-corpus acceptance remain required. No requirement, migration or gate acceptance changes. Evidence: `.tmp/ui/choice-scrollbar-integration/validation-evidence.json`, SHA-256 `12d377f51a2cb3609e9975f14720ab587cddf9e64c3eb2422759a1d0bde5aefd`.

The [canonical document-editing foundation](document-editing.md), based on engine `1bcb4aec68fbaa3187ca5ffea30720f003f57f1f`, adds source-preserving structural/value batches and bounded undo/redo. The full build and70 UI suites pass; independent source review, integrated compiled mutations and frozen MSVC/GCC sanitizer evidence qualify this portable core. The existing strict parser is shared through narrow internal forwarding calls. The next editor increment must prepare and publish history, actual Runtime context and cached source/path metadata together, after exact native-owner retirement. Visible authoring, file-save recovery and full editor/product acceptance remain required. No requirement status or gate changes. Evidence: `.tmp/ui/document-edit-integration/validation-evidence.json`, SHA-256 `809d98410f6c2ca9572ad888ef4b0f3faababa1ce4ee99c0e4d10160aed6271e`.

The [owned image recovery](image-recovery-ownership.md) and [native sink driver](native-input-driver.md) increment, based on engine `b880d593e358d64276bccbb0ec58c3b83f5e5011`, retains prepared texture directions and connects actual engine input disposal to checked retirement. The full engine build and76 UI suites pass; the older metadata fixture needed declaration/link updates and then passed533 checks and14 compiled mutations. Windowed SP/OpenGL at125% and MP/Vulkan at200% passed gameplay, ordinary renderer restart and SYSTEM activation; all four engine images were reviewed. SP is warning-free; MP retains94 baseline warnings. These passive captures do not qualify prepared Apply or native entry. Built-in quiescent image disposal, complete retail reconstruction, native translator/bootstrap and lifecycle activation remain required. No requirement status, migration acceptance or final gate changes. Evidence: `.tmp/ui/runtime-ownership-integration/validation-evidence.json`, SHA-256 `d9c4fca9b14cdfad640b9ef35c5156e315f203f902e432918a4ff020a884eea9`.

The [full renderer shutdown increment](image-recovery-ownership.md), based on engine `7733297d306b944d4b16f9c147247712e228925c`, releases retained CPU recovery data only after the original renderer owner completes shutdown. Failed settings attempts and ordinary device restarts retain both recovery directions. The full engine build and 77 UI suites pass; Clang, MSVC debug and sanitized GCC each pass 569 checks and 12 compiled mutations. Windowed SP/OpenGL at 125% and MP/Vulkan at 200% passed gameplay, ordinary restart, SYSTEM activation and shutdown; all four engine images were reviewed. SP is warning-free; MP retains 94 baseline warnings. These passive captures do not qualify prepared Apply. General Common/fatal teardown, complete retail reconstruction, native activation and the editor remain required. No requirement status, migration acceptance or final gate changes. Evidence: `.tmp/ui/image-owner-shutdown/validation-evidence.json`, SHA-256 `c522301a10681b9630facb6beb3b4663dfe7d06371c90159655512094ea57f4e`.

The [editor file publication increment](editor-file-safety.md), based on engine `a11a00181b5997993bb9e225bad63a6422386311`, adds actual native creation of complete new files without replacing an existing or racing document. Windows Clang and MSVC each pass 340 checks, GCC with sanitizers passes 344, and all three reject seven compiled mutations. The full engine build and 78 UI suites pass. Windows symlink and DrvFS FIFO limitations are recorded. This file-only increment has no new game captures. Save As/autosave UI, live document association, overwrite/conflict handling and crash/relaunch recovery remain required. No requirement status, migration acceptance or final gate changes. Evidence: `.tmp/ui/editor-new-file/validation-evidence.json`, SHA-256 `846413e35eb34ed965a57005aa6eb724b5c8d3dbc2fb46f65ba30711a509cc63`.

On engine `353d8a55d33677b3bbdb6a0f602ac8f7634edb07` retained choices gain catalog lists: option labels from application state and a state-bound option count, with hidden options ineligible, unmeasured and unreachable, a hidden selection still named, and an open list closing when its labels or length change. The native schema, readback, interaction and runtime tests qualify them. `WID-005` gains partial evidence without a status change. Evidence: `.tmp/ui/runtime-choice-lists/validation-evidence.json`, SHA-256 `a7942e4f9e15c32e1ce694164cff1ff856b69ee5cd27fa99f9343910b7208cd3`.

On engine `2c1f988a7ac1c15453cd81159bcde95a17013390` render API 20 gives the settings service the renderer's own evidence: how it resolved `r_renderer` each time it selects a back end, and what each level load did with its light grids. A load pulls the committed preload policy, never a value an open attempt could still undo. A contract test, the settings service harness and SP map loads on OpenGL and Vulkan qualify them. `BEH-002` gains partial evidence without a status change. Evidence: `.tmp/ui/renderer-settings-reports/validation-evidence.json`, SHA-256 `240dffae83f06f6a459fdae2473ab1277718e2671ebf7a210a2fd05b3b9c7a74`.

On engine `fd4dd84355ee7f13acb7ff7a10711eefa76f5d6c` the light-grid preload gains its deferred executor: one executor per Apply, a schema-2 next-map journal that completes automatically after a later presented frame, the committed policy for level loads until it is saved, and cold-start replay in both directions. The page has no row for it yet. Production-body service, host, catalog and controller tests, a mutation pass and a cold-recovery probe qualify it. `BEH-002` gains partial evidence without a status change. Evidence: `.tmp/ui/deferred-light-grid-executor/validation-evidence.json`, SHA-256 `4909d049af40f5c7a8a8436bba2e85caffe6168b9ca676cb64ce213e90664598`.

On engine `218745151e4778026c53c7cb034de6b5da478635` the retained SYSTEM page gains Preload Light Grids: the toggle with its help and a status line that names what the loaded map does with its light grids, from the committed preload and the renderer's receipts. Apply completes it through the deferred executor, and the next map load uses it. The service harness, a native page test across seven locales, sizes and densities, and in-game OpenGL and Vulkan runs qualify it. `FLOW-002` and `BEH-002` gain partial evidence without a status change. Evidence: `.tmp/ui/system-render-options/validation-evidence.json`, SHA-256 `092fe0eb98ffc56f6dc38fc2141b6bbe302f9620c37c1798ed9395e753f6b237`.

On engine `f88d8c7cfdfc2c9c116b1a45d66c8b7a78f26d12` the renderer fallback gains its executor: `r_renderer` applies automatically through a checked device restart on the captured display, proved by the renderer's own selection report, with schema-2 recovery and startup replay. The host, service and exact-value harnesses, a mutation pass and the full Meson suite qualify it. `BEH-002` gains partial evidence without a status change. Evidence: `.tmp/ui/renderer-fallback-executor/validation-evidence.json`, SHA-256 `7213f4abf2a342f6665306862afa3a19139c09e32e835795985a14866cbea819`.

On engine `3219f4e5fb0e88ca64cc6b58841fb0703d8a1517` the retained SYSTEM page gains Renderer Fallback: the choice drafts `r_renderer` (Auto or ARB2) where the renderer executor admits it, shows a notice while the renderer reports a fallback from an unavailable or retired request, and applies through the executor's checked restart. The service and host harnesses, native page tests across seven locales, sizes and densities, a mutation pass and in-game OpenGL, cold-recovery and Vulkan runs qualify it. `FLOW-002` and `BEH-002` gain partial evidence without a status change. Evidence: `.tmp/ui/system-renderer-fallback/validation-evidence.json`, SHA-256 `abf8cfcf300430ff6f2026b8eef18dff78a8869644bee56c1c6044ab8cf796a7`.

On engine `1e457055bf2bda18b4c0a7477f0fd0727618b445` the settings service gains the SYSTEM display lists: the display device, resolution and refresh lists built from one cached topology capture with localized labels, published to the page owner, and picks checked against the list's token that become ordinary edits; a display picked from the list must still be that monitor at Apply. The builder, service, host and language harnesses, a mutation pass, the Meson suite and an in-game OpenGL run qualify it. `FLOW-002` and `BEH-002` gain partial evidence without a status change. Evidence: `.tmp/ui/system-display-catalog/validation-evidence.json`, SHA-256 `aaf2b08b88a32d8096460f8031f6b176f31974c5c67ce7ca548839f40f624e2f`.

On engine `d0d44aca6bd91ee2f006f1cffdd63bbb54530ddf` the retained SYSTEM page gains Display Device and Expand Across Displays over the published display lists. A device pick sends its slot and the list token, a display that is not connected cannot be picked and reads as unavailable, and spanning is offered only where it can apply, with a line saying why not; both rows hide on one display unless the draft names a display or a span the lists cannot offer or holds a change not yet applied. Dropdown options too wide for the safe area now wrap instead of refusing to open. The native page tests across seven languages, the Meson suite, a mutation pass and in-game OpenGL and Vulkan runs qualify it. `FLOW-002`, `BEH-002` and `WID-005` gain partial evidence without a status change. Evidence: `.tmp/ui/system-display-rows/validation-evidence.json`, SHA-256 `13a53dceb2a89b149ee7965bdfa1e43626f9049b8e78850cf2a2a0377bcaf0b4`.

On engine `8eba081a90b57454e5734e01474de17b120bcb3d` the SYSTEM page's routes are ready for the retained default. The multiplayer menu's SYSTEM route opens the same retained page over the main menu, which takes input again after Back, and falls back to its stock route; a refusal while the page is available, which no current state makes, opens the stock page for that request. The compiled session route harness, the gate test, a mutation pass and live captures, one through a listen server's in-game menu, qualify it. `FLOW-002` gains partial evidence without a status change. Evidence: `.tmp/ui/system-session-route/validation-evidence.json`, SHA-256 `f363fcb9d87b3a04aa4ba5617f2fa1982a9909d7d8618a99717eee18c3de2003`.

On engine `e242f927cc0fe252ab8438d2348c5b03d98ca727` the retained SYSTEM page gains Display Resolution and Refresh Rate over the published display lists and so has a control for every stock setting; it stays opt-in (`ui_retainedSystem 1`) while presets cannot apply from it and its size fields take no typed digits. One exclusive mode rule makes Auto refresh the desktop's rate on every display path, MSAA applies on Vulkan, and stale tuples, exclusive-mode monitor identity and native Wayland observation are fixed. The native page tests across seven languages, the Meson suite, the gate, route, service and builder tests, an SDL differential, a mutation pass and an in-game Vulkan run qualify it. `RUN-004`, `FLOW-002`, `BEH-002` and `RUN-006` gain partial evidence without a status change. Evidence: `.tmp/ui/retained-system-resolution/validation-evidence.json`, SHA-256 `d65f5e8eaac90ec10de47c348fb076ec23ef83e8c7316da1c07e19f9bd7adf32`.

On engine `9605f1dbbefc953b4c22af1650d5bf69e34e63cc` the session's controller menu pump completes each navigation step with its release on every retained input target, not only the home screen, so a stick push no longer leaves focus moving on the Single Player, Campaign and SYSTEM pages; legacy GUIs keep receiving presses alone. A compiled pump test, its mutants and the session tests qualify it. `INP-002` and `INP-003` gain partial evidence without a status change. Evidence: `.tmp/ui/controller-menu-pump/validation-evidence.json`, SHA-256 `90860f636950012e402d58132f0f8c0c2d4834633005a15d59bdca33bcb0c94c`.

On engine `34dba978638b3255d8d67e74a7eea374c151b07c` event programs gain a focus instruction, which moves focus as the program commits or after the next layout and yields to newer input, and Q, E and the gamepad shoulders run a document's tab events. The behavior, document and runtime suites, the adapter test and a mutation pass qualify it; no document uses them yet. `INP-010` and `INP-011` move from pending to partial. Evidence: `.tmp/ui/event-focus-tabs/validation-evidence.json`, SHA-256 `a46e0a80f07dade3ec51c8fe9a363e4deb53d3aff60860f86ccc522793c86df0`.

On engine `8ad1fcac762283b29e76fb36f6fa7a251e73effb` the multiplayer Escape card covers the game's in-match menu behind ui_retainedMultiplayer: the card, its header, the eight-tab strip, the prompt bar, the Disconnect confirmation and the softening, with every page still handing off to its stock page and the card 890 dp wide to fit the strip. The gate and native screens tests, a mutation pass and a live probe on OpenGL and Vulkan qualify it. `FLOW-046` and `WID-008` move from pending to partial; `BEH-007` and `REN-016` gain partial evidence. Evidence: `.tmp/ui/retained-mp-escape/validation-evidence.json`, SHA-256 `39b8f759d2e6445b321c64507f125dcc015334dbbcb6ffaf0bb363df6e3255cf`.

On engine `a7db82f34de864059cf66441b2ad63dfce5cec14` the multiplayer Escape card gains its Team page, with the game's join, spectate and ready actions, their refusals and reasons, the team band and recent chat, and its Server page, with the server's message, rules and map rotation. The gate and native screens tests, a mutation pass and a live probe qualify it. `FLOW-007` moves from pending to partial and `FLOW-046` gains partial evidence. Evidence: `.tmp/ui/retained-mp-team-server/validation-evidence.json`, SHA-256 `470d27af66c563373136d5db16f862eb0d3f1c935e3b1dfe6609896f18b56fb3`.

On engine `27a3cf97551dbb4d0caead0904985e702631e0ca` the multiplayer Escape card gains its Players page: the team lists and spectators in the scoreboard's rows beside the selected player's kills, deaths, score, accuracy per weapon and awards, with Mute and Friend. The gate and native screens tests, a mutation pass and live probes, one from a remote client, qualify it. `FLOW-046` gains partial evidence without a status change. Evidence: `.tmp/ui/retained-mp-players/validation-evidence.json`, SHA-256 `6ef9ecdea0cd0d453a3485185bdffa0562cd091d703969aa96244f2febccefb6`.

On engine `e76d4956f40fa15321add0e7e47bd97dfc4ffd99` the multiplayer card gains its Welcome variant over the connect-time join offer: the game's join choices for each mode with their refusals, the server and the players, Spectate on Back and Leave Server asking first; the game names the card for each opening. The gate and native screens tests, a mutation pass and live probes qualify it. `FLOW-046` and `FLOW-007` gain partial evidence without a status change. Evidence: `.tmp/ui/retained-mp-welcome/validation-evidence.json`, SHA-256 `1620a52f64b2971dabf170c00bbe91d50b1041ccdbad74a66e67feaa503d0887`.

On engine `8ea6ec75de6db7b55cd58028f1c905e69e3522c6` the multiplayer Escape card gains its Vote page: the running vote with its caller, lines, time left, tally and ballots, and a call drafted from rows for the fields the drafted game type uses, sending only what differs from the server's settings. The gate, adapter and native screens tests, a mutation pass and live probes with a remote client qualify it. `FLOW-046` gains partial evidence without a status change. Evidence: `.tmp/ui/retained-mp-vote/validation-evidence.json`, SHA-256 `c43517ab3160ddb9ff865380dc2a83374597aa5b0bb5d4f0daf575d3b0d08aa2`.

On engine `399beec21e893594a35ec9d8848bccc5c79d311b` the multiplayer cards gain their Settings pages (the player's model, rail color, handicap or crosshair, and how opponents and teammates look) and the Escape card its Voice page; with all its pages built, the Welcome card presents by default. The gate, adapter and native screens tests, a mutation pass and live probes qualify them. `FLOW-046` gains partial evidence without a status change. Evidence: `.tmp/ui/retained-mp-settings/validation-evidence.json`, SHA-256 `ef740505998f2b64c757928e2c62e08fd34dda5a735a1645c369e9d817e83f8a`.

On engine `f7173a4fc81ab9b4a33575e2c9d0e148c8bb2507` the retained loading screen hands a join over to the Welcome card: it stays up, reading JOINING, until the card presents, then fades out over it in place of the load's wipe. The gate's compiled hold case, the native screens test, a mutation pass and live probes on a listen server and a remote client qualify it. `FLOW-047` gains partial evidence without a status change. Evidence: `.tmp/ui/retained-loading-handoff/validation-evidence.json`, SHA-256 `ed845b87064b11dea753eab7d7f27b70ad79d07e649d400d0c43325619e02c1e`.

On engine `09c9d97018b26933eaf5806947df02aa9c379f08` the multiplayer Escape card gains its Match page: Match Control's six sections in an inner strip the triggers page, and its Status section (the match's state, the action target, the readiness, pause, forfeit and abort actions with their reasons and the stock confirmations, the referee's sign-out and a spectator's camera) mirrored from the game's own projection. The gate, adapter and native screens tests, a mutation pass and live probes qualify it. `FLOW-012` and `FLOW-026` move from pending to partial and `FLOW-046` gains partial evidence. Evidence: `.tmp/ui/retained-mp-match/validation-evidence.json`, SHA-256 `861b55c1226ee7c57e37954faefacfa38824f5f33b7f2b85fc422f38e6bc89b7`.

On engine `c93ecbad3d8c502bda29bd41d2d9230fff4b9142` the multiplayer Escape card's Match page gains its Teams section: Match Control's team and roster rows and the participants a substitution can bring in, chosen by index as the stock list's own selection, the role an invitation or an assignment gives, and the team, queue and roster actions with their reasons and the stock confirmations. The gate and native screens tests, a mutation pass and live probes qualify it; `FLOW-012` and `FLOW-046` gain partial evidence. Evidence: `.tmp/ui/retained-mp-match-teams/validation-evidence.json`, SHA-256 `39a117a008411b46d33444719522c720b0a498a66cc15e190feaa9add95e76df`.

On engine `ded46e92e916a794bb26f028d5059c848ef79de1` the multiplayer Escape card's Match page gains its Proposals and Rules sections: the running proposals, the ballot target, the ballots and the proposals to make; the committed and staged rules, the profiles and rule fields, a whole-number value to stage and the rule actions with the stock confirmation. The gate and native screens tests, a mutation pass and live probes qualify it; `FLOW-012` and `FLOW-046` gain partial evidence. Evidence: `.tmp/ui/retained-mp-match-proposals-rules/validation-evidence.json`, SHA-256 `830d701016bab145731404a0e018a3619ff5a24694f3737b140f13406e0d619c`.

On engine `7e428ca62d07ec7097215112eaf030545a3916ba` the multiplayer Escape card's Match page gains its Series and Evidence sections, completing the page: the series, the map pool, the veto and map history, the format and the series and veto actions with the stock confirmations, and the evidence with Refresh; only the referee credential opens the stock page. The gate and native screens tests, a mutation pass and live probes qualify it; `FLOW-012` and `FLOW-046` gain partial evidence. Evidence: `.tmp/ui/retained-mp-match-series-evidence/validation-evidence.json`, SHA-256 `6ee747ef06a2c9d6d90984021912bb53e22b4c354be2c6380d5bb9aedbeddf23`.

On engine `a2548a5e83749ff8a4579f35eade5dca59c4fa14` LOAD GAME opens a retained Load Game page over the home that handed off to it. `FLOW-004` and `WID-009` become partial. Evidence: `.tmp/ui/retained-loadgame-evidence/validation-evidence.json`, SHA-256 `0ca1295e613bac04ccb9625f73af37b3bafe0311b1dc7d0b75f4f3eda9ffed49`.

On engine `5d5522343eedef52f0bd34638b43cbf51e1a067f` a text control role edits one line of free text, committed characters type into a focused retained field, and SAVE GAME opens a retained Save Game page. `WID-006` becomes partial; `FLOW-004`, `WID-009`, `RUN-004` and `FLOW-002` gain evidence. Evidence: `.tmp/ui/retained-savegame-evidence/validation-evidence.json`, SHA-256 `a19668e3ec74f32813923274778912d3dadfe6f682bc0867a75f259f61966b9a`.

On engine `46052f8a0371c1b717decd1c7f999d4afdfc40f5` RESTART LEVEL on the Marine pause opens a retained Restart Level page with the stock New Game composition: the difficulty plates beside the chosen one's description and the enlarged primary plate. Back returns the pause unchanged, and RESTART LEVEL restarts the level at the choice. `FLOW-001` becomes partial; `FLOW-048` gains evidence. Evidence: `.tmp/ui/retained-restartlevel-evidence/validation-evidence.json`, SHA-256 `c59bf89c0bccdfa9615b95eacd3b215bd181b71c5c755769df3d530b9f21d3d9`.

On engine `5e8a70555e43c9f86d984956a457a901db495f4d` DEMOS on the title opens a retained Demos page: the demo library under filter tags, the selected demo's picture and details, and PLAY, DELETE and REFRESH. `FLOW-013` becomes partial; `WID-009` gains evidence. Evidence: `.tmp/ui/retained-demos-evidence/validation-evidence.json`, SHA-256 `6af050d9337ba49f1906fc18bfc799af9adde4b8c2aa4b70dee997d98747ae9f`.

On engine `c118aba6c2453b6f5123c6101d3a47130b432687` MODS on the title opens a retained list dialog, the modal construction grown to 280 u, with the installed mods, their details, LOAD MOD and CANCEL. `WID-013` becomes partial. Evidence: `.tmp/ui/retained-mods-evidence/validation-evidence.json`, SHA-256 `6be45e12dadc8a14882242127fc431e54e54504b23ceb43a71a91b6cb329339d`.

On engine `580cd561215b223d6653e22a1e7aae9a2db1280e` the restart screen after a death presents retained: RESTART, LOAD GAME, MAIN MENU and EXIT GAME over the stock popup art, each explaining itself. `FLOW-005` becomes partial. Evidence: `.tmp/ui/retained-restartmenu-evidence/validation-evidence.json`, SHA-256 `c0efdf55a7abe4f8ac14bd3239fd2fdbae555b9cded737973c565211f2eb8fdf`.

On engine `acca732c88269a3619a207086fa52d59c5855029` CREDITS on the title opens a retained Credits page that plays the stock credits translation compiled from the stock scripts, its words read from the stock menu at runtime. `MOT-015` gains evidence. Evidence: `.tmp/ui/retained-credits-evidence/validation-evidence.json`, SHA-256 `d6ac5ccd40691bbd7b368514a72dbc9278374db007fab76b14be34e98efc1045`.

On engine `9f25242bde0265eb52afccadb7206fa830af63ca` ARENA on the Single Player page opens a retained Arena page, its second sub-page: the Arena campaign's tier ladder, matches, detail column, entrance and result ledger, the campaign publishing into it and keeping its authority. `FLOW-011` becomes partial; `FLOW-026` and `FLOW-050` gain evidence. Evidence: `.tmp/ui/retained-arena-evidence/validation-evidence.json`, SHA-256 `d47a2a28292827e6f85d91cbfef10f21fe7c8aa8c2f010c0883756cc3254360f`.

On engine `473749c63833198215d4d6ef9d5ee3a579e45550` MULTIPLAYER on the title opens a retained Multiplayer page at the page level: JOIN GAME, CREATE SERVER and PLAYER SETUP open the stock browser, Create Server and Settings pages and return to it, and BACK returns the title. `FLOW-050` gains evidence. Evidence: `.tmp/ui/retained-multiplayer-evidence/validation-evidence.json`, SHA-256 `f07ddc497858aca4c9c94abb64cb3bd966a53f7078c018c51f882c0f63c40393`.

On engine `6a8bdb0c4026053ac2599824a0a2263668261297`, with `ui_retainedMultiplayer`, CREATE SERVER on the Multiplayer page goes deeper to a retained Create Server sub-page: the stock page's map selection, options and advanced options, the session owning each setting and starting the server as the stock page does. `FLOW-050` gains evidence. Evidence: `.tmp/ui/retained-createserver-evidence/validation-evidence.json`, SHA-256 `a307268c17d9e94836bbd26d8f4684825f8787e11c35d2cf89f5a48ded4b0b33`.

On engine `3d8ab71457492b6d88f69cab9c1b580585bfcab8`, a text field can be masked (a password: one * a character, its carets mapped onto the mask, Copy and Cut refused, never a native text candidate) and may allow empty text; Create Server's advanced options gain Set password and Remote console password on them, leaving the ban list before the page joins the default. `WID-006` and `FLOW-050` gain evidence. Evidence: `.tmp/ui/retained-createserver-passwords-evidence/validation-evidence.json`, SHA-256 `608164c9a749f0cd13f931057fb9bc6c3ebe406bd2d89c89c9f470285c7cdbd0`.

On engine `156694227f16c666cfabefbfbac0f1c28c1adb06`, Create Server's advanced options gain PLAYER BAN LIST, a list dialog nested over them that lists and lifts the local server's bans, and the retained page becomes the default; the stock page's ban list and player-count guards work again as retail's session ran them. `FLOW-050` gains evidence. Evidence: `.tmp/ui/retained-createserver-banlist-evidence/validation-evidence.json`, SHA-256 `aaaeed1f5512c93c181d1fb2895122bc30dcea5437f35b660bd4a2bf057951d8`.

On engine `03688d4387b0661ef86de7359c0bcd1306cca12b`, Multiplayer's JOIN GAME goes deeper (opted into with ui_retainedMultiplayer) to a retained server browser that lists the scan's servers with their sources and column sorts and joins the chosen one, and favorites of a server on the same machine now list. `FLOW-050` gains evidence. Evidence: `.tmp/ui/retained-joingame-evidence/validation-evidence.json`, SHA-256 `79aa239d2f10627940727d4a0c5c7306dee0a571ddee62d5dc149ad95b5c7e86`.

On engine `c09c2195de958d704bbeb53f5cc7010824caf4d4`, the retained Join Game gains the stock list's symbols on their tiles, the mod filter, a favorite toggle and the chosen server's levelshot, leaving JOIN IP ADDRESS and FIND PLAYER/CLAN to keep it opt-in. `FLOW-050` gains evidence. Evidence: `.tmp/ui/retained-joingame-symbols-evidence/validation-evidence.json`, SHA-256 `fc9c671fb6fe43184528d0613b8bdbe4c44794efca2598295c47d0e5a8e538bc`.

On engine `5f62d8a6df8510260df59f46990949aaf234e4a5`, the retained Join Game becomes the default: JOIN IP ADDRESS and FIND PLAYER/CLAN open entry dialogs, the stock popups' "your ip address:" and player search work again, and the Multiplayer page goes deeper to both of its sub-pages without an opt-in. `FLOW-050` gains evidence. Evidence: `.tmp/ui/retained-joingame-default-evidence/validation-evidence.json`, SHA-256 `4d2c6a2f86f0f1c3fa141acba00768cdbe5783344740d6d0071d74163d51ba4c`.

On engine `e61425f8ea23e48916a40b66ae6775d7258880b3`, the server scan publishes its progress again, so the classic browser's load bar fills and the retained Join Game shows the same bar and notice under its list. `FLOW-050` gains evidence. Evidence: `.tmp/ui/retained-joingame-progress-evidence/validation-evidence.json`, SHA-256 `86ad58e9f0cb2e487b06fa2e253b38711cb750ecee629ff6b72f3acb43b5cf7f`.

On engine `45b72594b52f378d24a62e3da7149ccc951a82a8`, the server scan describes a server's address, players and settings, which the classic browser's server tooltip shows again for the hovered row. `FLOW-050` gains evidence. Evidence: `.tmp/ui/retained-joingame-details-evidence/validation-evidence.json`, SHA-256 `5b7857f09bfd8f65fd8cfa22264375b890b55387090450c5ca5010fd6b034e46`.

On engine `a2b3d602526ebabe92b7e9ca6bc384d56827f7f4`, the retained runtime gains a detail card, the hover card anchored to the pointer's control or the focused one, and Join Game shows a server's address, players and settings in it. `WID-025` moves from pending to partial and `FLOW-050` gains evidence. Evidence: `.tmp/ui/retained-detail-card-evidence/validation-evidence.json`, SHA-256 `f756cd836ed5469f16f0f60658c9568d31f35bff637eb694493c464dc0f58854`.

On engine `562ffb3597e8e263b0379081baea781666511784`, a controller's west and north face buttons run a document's secondary and tertiary events, and Join Game refreshes its list on west and sorts it by the next column on north. `INP-011` and `FLOW-050` gain evidence. Evidence: `.tmp/ui/retained-face-buttons-evidence/validation-evidence.json`, SHA-256 `fc90b152d4822bab61ea49b047678f0e5b035bd6932284ff83c2a7fe25134753`.

On engine `fd814b464b8d117856d55eace2fa94bd9863ff9a`, prompts follow the active device: the keyboard's keycaps or the controller family's face buttons, from the label SDL reports, with ui_promptStyle to force one. `INP-009` and `WID-019` move from pending to partial and `INP-011` gains evidence. Evidence: `.tmp/ui/retained-prompt-glyphs-evidence/validation-evidence.json`, SHA-256 `8246e1584cd199da491094441ac48f320d14fe796122ecb7a7e2991214ff004c`.

On engine `58ca62c695a54106fe6b20f7d01f67b0e951f585`, the multiplayer cards' prompt bars and the Escape card's tab keys show the active device's glyphs, the shoulders for Q and E. `INP-011`, `WID-019` and `FLOW-046` gain evidence. Evidence: `.tmp/ui/retained-card-glyphs-evidence/validation-evidence.json`, SHA-256 `a8373b906eb04c74ddcd5de1d8d3e1dc6edcead176b5ccdcd2769b16f8a4cd75`.

On engine `f137d9408cacfbdef208176299bbf1b5a7aca01c`, Nintendo layouts confirm on the east button by default and in_swapConfirmBack forces either convention in every menu, with the prompts following. `INP-011` gains evidence. Evidence: `.tmp/ui/retained-confirm-swap-evidence/validation-evidence.json`, SHA-256 `3f3d402b39d24c8fe5926b1bfd16fe7ab791975460ef3493bbc8a35c60342b67`.

On engine `35311b0e5d880a428b1311cdb925608d8d6c4e3a`, the server browser shows each server's network quality as four bars beside its ping. `WID-029` moves from pending to partial. Evidence: `.tmp/ui/retained-network-quality-evidence/validation-evidence.json`, SHA-256 `05402c1366af7555381f7142104d346e7e019382670c6b9801977120db398e28`.

On engine `f15a41f03ef7e918989e008883e70be760c98367` the retained-UI line that built the Load Game, Save Game, Restart Level, Demos,
MODS, restart screen, Credits, Arena, Multiplayer, Create Server and Join Game screens merges into main, with
its prompt glyphs, detail card and menu fixes. Main's initializing screen, weapon wheels and campaign
difficulty page replace the line's own, so `ART-031`, `FLOW-057`, `WID-033`, `WID-034` and `MOT-009` keep
main's state and the line's initializing, weapon wheel and New Campaign evidence is not carried.
`WID-006`, `WID-009`, `WID-013`, `FLOW-001`, `FLOW-004`, `FLOW-005`, `FLOW-011`, `FLOW-013`, `INP-009`,
`WID-019`, `WID-025` and `WID-029` move from pending to partial with the line's evidence.

On engine `84daa5a702a0e17236641993c1cc25f840068a96` the register catches up with work recorded in the
[Remastered HUD](remastered-hud.md) notes: `FLOW-054`, `FLOW-055` and `FLOW-061`
move from pending to partial with its checkpoint (the Awakening's on-foot and
vehicle documents), and `FLOW-042` and `FLOW-024` record the objectives display
held in play, its toggle, the notices, single-player loading tips and the
multiplayer Escape card. Each row keeps its unbuilt parts and qualification
matrices as remaining.

The [AUDIO page](retained-screens.md#the-audio-page-25-october-2026) moves `FLOW-003`
from pending to partial: the stock AUDIO page's every row is reconstructed,
applied through the session and read back. GAME, CONTROLS, Load Defaults,
Player Setup, dependency explanations, bindings and the cancellation and
failure scenarios remain.

The [GAME OPTIONS page](retained-screens.md#the-game-options-page-25-october-2026) adds
`FLOW-003` evidence: the stock page's every row is reconstructed, its CVar rows
set through the adapter's allowlist and its computed rows through the stock
choices. CONTROLS, Load Defaults, Player Setup and the rest of `FLOW-003`
remain.

The [CONTROLS page](retained-screens.md#the-controls-page-25-october-2026) adds
`FLOW-003` evidence for the bindings: every stock binding, its key capture
and clear. The settings screen itself, Load Defaults, Player Setup and the
rest of `FLOW-003` remain.

The [SETTINGS page](retained-screens.md#the-settings-page-26-october-2026)
adds `FLOW-003` evidence for the settings screen's level: its buttons, the
return onto each, Load Defaults' question and the stock hand-off. Player
Setup and the rest of `FLOW-003` remain.

On 28 October the SETTINGS hub was recovered into the main checkout, including
the correction that keeps its Strogg-pause route out of the Load Game slot.
The [acceptance-tool review](acceptance-transition.md) also adds tooling
evidence to `QUAL-006`: committed records, actual image evidence and earlier
binary/capture bindings are enforced. The checkpoint preserves all requirement
statuses, every migration record and every gate. Earlier Vulkan front-end runs
with unexpected early exits remain failed evidence; both cases subsequently
pass in a coordinated runtime window without establishing the earlier cause.

[Touch in the menus](retained-screens.md#touch-in-the-menus-1-november-2026)
moves `INP-014` from pending to partial: a touch that moves past 8 dp is no
longer a tap, one moved up or down in a scrolling container drags its
content, and a finger leaves no cursor or hover behind. `WID-010` gains the
touch drag. The rest of section 13.6's menu rules then narrow `INP-014`
further: the pressed treatment shows at touch-down, a released drag keeps
going as inertia, a list's rim light brightens at either end instead of the
content stretching, a swipe across a tab strip changes tab and a 500 ms
long-press opens a button's detail; the real menus' adapter now receives the
finger as it moves. `WID-010` gains the inertia and the rim light. Haptics
and device acceptance remain.

[The window's safe area](retained-screens.md#the-windows-safe-area-1-november-2026)
moves `LAY-016` from pending to partial: the retained menus keep their text
and controls inside the platform window's safe area (a view-height canvas
fits it, four read-only view sources give its insets, the generated menus
make their root the safe area and reach their backdrops back out to the
view's edges, and detail cards and opened lists stay inside it), with
native tests at phone shapes. The HUD, the stock GUIs, swipe controls' 24 dp
edge margin, fold and multi-window resizes and device acceptance remain.

[The HUD's safe area](remastered-hud.md#the-windows-safe-area-2-november-2026)
narrows `LAY-016`: each Remastered HUD edge anchor takes the larger of the
player's Safe Area Inset and the window's inset, measured on the canvas
ratio the HUD documents' runtime uses, and the center-left and center-right
anchors center on the safe area's height, while the root and the layers that
fill it stay full-bleed and a window without insets lays out as before.
Native tests check the anchors and every Remastered document at portrait and
landscape phone shapes, binding no evidence record. The stock GUIs (Classic
included), multiplayer objective markers, swipe controls' 24 dp edge margin,
fold and multi-window resizes and device acceptance remain.

There are **123 partial, 229 pending and 0 verified requirements**, counting the
40 superseded rows at their recorded states. `BEH-002`
remains partial for the implemented settings transaction/service boundary.
`BEH-005` remains partial for committed action delivery and cancellation; stock
sounds and the complete device/modal/widget behavior still require implementation
and evidence. `WID-002`, `WID-004`, `WID-005` and `FLOW-002` moved from pending to
partial at the historical value-control checkpoint; every remaining conjunct and acceptance
criterion is preserved.
The SYSTEM exit increment adds bounded qualification evidence for `BEH-001`,
`BEH-002`, `SAVE-003`, `INP-004`, `INP-005`, `INP-006` and `FLOW-002` without
changing any status; `INP-005` remains
pending for the complete wheel, touch, text, composition and device contract.
The 18 October inventory revision withdraws the stale `INV-001` verification and
preserves its previous evidence as history. The reconciled retail/base/Awakening
inventory contains **358 migration variants**, all unaccepted. All seven final
product gates remain open. See the [inventory revision](inventory-report.md#18-october-2026-reconciliation)
for source identities, scope decisions and the audit command. Earlier counts in
the checkpoint narrative below describe those checkpoints, not the current corpus.
These counts describe evidence state, not percentage of implementation effort
or product quality.

## Using and maintaining the register

Each requirement has a permanent `GROUP-NNN` ID, normative section references,
an accountable milestone, a responsible implementation workstream, dependencies,
acceptance evidence needed, current evidence and remaining work. Workstream names
assign engineering responsibility; they do not invent individual assignees.

Keep IDs stable. Append requirements when new source behavior or an omitted
normative detail is discovered. If a requirement is split or superseded, preserve
its ID and explicitly link its successors. The specification remains normative;
an omission in this register cannot authorize reducing scope.

The `supersessions` list records each replacement. A superseded row keeps its ID,
text, status and evidence for history but no longer defines acceptance; its
successors carry the replacement scope, and dependencies or audit findings that
name it resolve to them. `carried_evidence` lists the superseded row's evidence
that still applies to a successor. Only a register revision appends requirements
or supersessions; an implementation increment cannot.

Dependencies identify capabilities needed at the stated scope. For example,
the first settings screen needs the applicable controls and source references
before the entire widget or family library is finished. Its narrow evidence may
pass the intermediate screen gate; it cannot mark the broader shared requirement
verified. **Final product acceptance requires every register requirement**, not
only the dependencies reachable from a selected gate.

`current_evidence` can describe limited implementation, historical checks or a
source audit. It is not automatically acceptance. `acceptance_evidence` is empty
until direct, current evidence proves every part of the individual requirement.
An evidence record names scope, revisions, source/asset hashes, exact operations,
builds/binaries where applicable, host/backend, logs/images, outcome, review and
limitations. Relevant changes require revalidation; a stale hash or unrelated
green test cannot close a requirement.

Only the [acceptance transition](acceptance-transition.md) records acceptance.
It names each requirement and resource it accepts and audits their committed
acceptance records: every conjunct, constraint and dependency, a gate's bounded
milestone scope, and per-resource behavior and artwork proof. All of it must be
current against the present files and reviewed by someone other than the
author. The checkpoint audit keeps earlier acceptance only while its records
stay current. Otherwise the acceptance must be withdrawn, keeping its records
as history.

The [migration manifest](migration-manifest.json) remains the per-resource source
of replacement, behavior and visual acceptance. This register covers shared
capabilities and product gates; it neither duplicates nor overwrites acceptance
for an individual GUI/include. Includes may become components, but still need
complete source-bound mappings and evidence.

## Requirement index

| IDs | Required area | Main accountable milestones |
| --- | --- | --- |
| `GOV-001`–`008` | Complete scope, source authority, requirement maintenance, canonical ownership, dependencies and publication decisions | M0–M6 |
| `INV-001`–`013` | Fresh effective corpus, native provenance, expression/name/time semantics, full lowering, assets and special widgets | M0, M1, M5 |
| `DOC-001`–`008` | Canonical versioned model, components/variants, structural transactions, behavior and binding ownership | M1–M3 |
| `RUN-001`–`008` | Public retained adapter, neutral manager, source routing, independent views, application frame order and restart | M1, M6 |
| `BEH-001`–`007` | Typed operations, settings transactions, aliases/events, exactly-once actions/sounds, pause and MP authority | M1, M2, M4 |
| `SUR-001`–`006` | Explicit output surfaces, GPU lifetime, world density/rays, HUD/aim projection and output-resolution UI | M1, M5, M6 |
| `SAVE-001`–`004` | Versioned instance/game-save policy, full durable state and live/level restoration | M1, M5 |
| `LAY-001`–`021` | Exact scale/spacing/target-size values, aspect expansion, compact recovery, HUD safe areas, contrast, viewing profiles, size classes, safe-area gestures, the accessibility baseline, HUD anchoring, the HUD's screens and the portrait view | M1–M5 |
| `TXT-001`–`011` | Scalable typography, the stock-derived type ramp, shaped runs, Unicode, localization, caret/selection/clipboard/IME, platform accessibility, number formatting and text shadow | M2, M3 |
| `INP-001`–`014` | Input transforms, paired activation, repeat/handoff, wheel/touch/text, modal focus, authored mask-aware hits, modality switching, focus memory, controller mapping/editing and touch rules | M1, M3 |
| `WID-001`–`035` | Every named functional widget: actions, checkbox, radio, slider, choice, text, binding, tabs, lists, scrolling, progress, tooltip, modal, status, tree, path/color controls, prompt bar, detail area, radial wheel, on-screen keyboard, cycling values, the weapon wheels and their transitions, the pop-up frame catalogue, the HUD prompts and the modern components | M2, M3, M5 |
| `REN-001`–`016` | Editable paths/paints/strokes/SVG, coverage/color, isolated opacity/masks/clips, additive, darkening and masked additive composition, backdrop soft focus and recovery | M3, M6 |
| `RES-001`–`005` | Full material/movie/model operations, generated images and actual renderer resource accounting | M3, M4 |
| `ART-001`–`037` | Measured component/icon libraries, exact visual tokens, panel/plate anatomy, all eight distinct families, complex-image exceptions, device glyphs, modern refinements, detailing, HUD gauge constructions, the crosshair and item icon catalogues, the traced HUD instruments, the title screen's emblem, the initializing screen's layers, the squad patches and The Awakening's icons and crosshairs | M2–M5 |
| `MOT-001`–`016` | Continuous clocks, normative timing/easing, reversal/ownership, page/modal orchestration, stock choreography, sound, ambient loops/alarms, reduced motion, band motion with the sub-page step, HUD alarms and the translation and transmission effects | M1–M6 |
| `FLOW-001`–`067` | Every menu/MP flow, HUD, communications, scope/vehicle, cinematic and scripted/world GUI; lifecycle, first-run setup, touch controls and editor, terminal input, modern screen patterns, HUD presets and layouts; the HUD's composition, gauges, strip, crosshair, messages and multiplayer HUD; objectives, boss, vehicle, scope and weapon displays; the scoreboard; the Remastered multiplayer top and chat; the multiplayer menus, loading, title and pause, initialization, four menu levels, Competitive HUD, spectator follow camera, expansion vehicle displays, Medical Labs implant activation, scope focus, recovery and death continuity, the Awakening single-player HUD and multiplayer objective points of interest | M2, M4, M5 |
| `ED-001`–`030` | Native workspace, canvas/constraints, components, vectors, motion/behavior, persistence/recovery, diagnostics and complete SP/MP delivery | M2, M3 |
| `PERF-001`–`009` | Named optimized baselines, separate CPU/GPU telemetry, warm/cold runs, budgets, long-run plateau, measured optimization and mobile power limits | M3, M6 |
| `QUAL-001`–`011` | Display/text/input/refresh/language/world extremes, independent visual review, stock parity, input/profile/device matrix, actual platforms and evidence provenance | M6 |
| `SHIP-001`–`006` | Complete retained routing, external-mod policy, retail-asset staging, artifact hygiene, dedicated builds and documentation | M1, M6 |
| `GATE-001`–`007` | The seven milestone exit conditions | M0–M6 |
| `FINAL-001`–`007` | Each numbered final product audit item | M6 |

Numeric `constraints` retain the specification's UI/text scales, dimensions,
spacing, typography, palette/alpha, panel/button/icon geometry, input repeat,
motion tokens and proposed performance limits. Family/source exceptions require
documented rationale and review; numerical tokens do not replace composition
or visual fidelity review.

The reusable [register validator](https://github.com/themuffinator/openQ4/blob/e3e65887480368191df154a9b52cce69d8e72140/tools/ui/validate_product_requirements.py)
compares an increment with its immutable Git baseline. It preserves requirement
scope, constraints, owners, dependencies and acceptance fields, checks explicit
pending-to-partial transitions, historical evidence, source hashes and unchanged
migration records. A hash binding matches its file in either line-ending form,
so LF and CRLF checkouts of one commit audit alike. It never interprets a linked
test or image as product acceptance. Run the final audit after evidence
documents and hashes are frozen:

```powershell
python tools/ui/validate_product_requirements.py `
  --baseline a8bad0f5bf69b08493714ad4faaeae7585d5abf2 `
  --partial SUR-006 `
  --output .tmp/ui/native-output-review/register-validation-final.json
```

A register revision is audited separately against its starting commit. The
`--revision` mode accepts only appended requirements that continue their group's
ID sequence, use their group's owner and references and start pending or partial
with existing evidence, plus new supersession records whose successors were
appended in the same revision and appended audit-finding references. Existing
rows, evidence scope and results and the migration manifest must be unchanged;
as in an increment, document hashes may be re-bound:

```powershell
python tools/ui/validate_product_requirements.py `
  --baseline 63aa36fd6571cc84d4f68104607822ffd0faae07 `
  --revision `
  --output .tmp/ui/register-revision-13/register-validation-final.json
```

Use a new output path for each audit. The optional `--defer-hashes` preparation
mode explicitly reports incomplete source bindings and cannot serve as the
final audit. The schema 13 revision audit is recorded at
`.tmp/ui/register-revision-13/register-validation-final.json`, the schema 12
audit at `.tmp/ui/register-revision-12/register-validation-final.json`, the
schema 11 audit at `.tmp/ui/register-revision-11/register-validation-final.json`, the
schema 10 audit at `.tmp/ui/register-revision-10/register-validation-final.json`, the
schema 9 audit at `.tmp/ui/register-revision-9/register-validation-final.json`, the
schema 8 audit at `.tmp/ui/register-revision-8/register-validation-final.json`, the schema 7
audit at `.tmp/ui/register-revision-7/register-validation-final.json`, the schema 6
audit at `.tmp/ui/register-revision-6/register-validation-final.json`, the schema 5
audit at `.tmp/ui/register-revision-5/register-validation-final.json`, the schema 4
audit at `.tmp/ui/register-revision-4/register-validation-final.json`, and the schema 3
audit at `.tmp/ui/register-revision-3/register-validation-final.json`. The schema 2 revision audit is recorded at
`.tmp/ui/register-revision-2/register-validation-final.json`, and the rejected
candidates of its negative cases are under `.tmp/ui/register-revision-2/negative/`.
The native-output strict structural/source audit is recorded at
`.tmp/ui/native-output-review/register-validation-final.json`. The historical
SYSTEM-exit strict structural/source audit is recorded at
`.tmp/ui/system-exit-review/register-validation-final.json`. The preparation
audit remains at
`.tmp/ui/system-exit-review/register-preparation-validation-final.json`. Historical
preparation, negative-case checks and the previous strict audit remain under
`.tmp/ui/value-controls-review/`. Three pre-existing historical summary
pointers have no recorded capture hash; the audit reports them as unbound and
does not turn their currently observed hashes into historical qualification.

The JSON `audit_findings` map explicitly connects all twelve implementation
findings (`UI-01` through `UI-12`) to required resolutions. `FINAL-001` through
`FINAL-007` preserve the complete corpus/behavior, visual product, editor,
display/accessibility/lifetime, performance/platform, clean installation and
documentation/publication gates respectively. None is satisfied by a prototype
or conversion count.

## Historical effective inventory refresh

The read-only refresh used the same explicit low-to-high retail/openQ4 mounts as
the [inventory method](inventory-report.md), reading installed PK4s in place.
It did not launch the engine, extract retail artwork or modify migration status.

| Comparison against the tracked manifest | Result |
| --- | ---: |
| Effective GUI roots/includes | 271 |
| Added / removed resources | 0 / 0 |
| Changed source content hashes | 0 |
| Changed selected source locations | 0 |
| Mount order | Identical |
| Pending migration entries | 271 |
| Lexical window / event declarations | 14,517 / 8,037 |
| Missing/cyclic include errors | 0 |

Reproduce with a fresh output location:

```powershell
python tools/ui/legacy_inventory.py `
  --mount 'retail=C:\Program Files (x86)\Steam\steamapps\common\Quake 4\q4base' `
  --mount 'openq4-pak0=content/baseoq4/pak0' `
  --mount 'openq4-pak1=content/baseoq4/pak1' `
  --output .tmp/ui/requirements/legacy-inventory-2026-09-09.json `
  --export-requests .tmp/ui/requirements/legacy-export-requests-2026-09-09.json
```

Local evidence includes the full inventory, source/hash export requests,
`inventory-refresh-summary.json` with comparison results and tool/manifest hashes,
and `register-validation.json` under `.tmp/ui/requirements/`. The inventory SHA-256
is `d48708c473d52e140c11143083d8bbf9d2de8b30086c8cbe1221d9a4622caae0`;
the unchanged migration-manifest SHA-256 is
`82120ffe3e07964828b5a05e538c797c99f4de6b4e43f9d58c6121377d45f04a`.

The alias checkpoint's refreshed structural/source-hash check is recorded in
`.tmp/ui/aliases-review/register-validation.json`. It preserves the original
inventory evidence and all requirement acceptance states.

Register checks confirmed unique IDs, valid milestone/source/evidence references,
resolved dependencies with no cycles, all twelve audit-finding mappings and all
seven final audit items. This checks traceability structure, not completeness of
runtime behavior, visual quality or the product. Native preprocessing was not
rerun; its existing [source-bound checkpoint](legacy-import.md) remains historical
evidence with its stated limits.

The same 17 unresolved lexical artwork references and one lexical brace
diagnostic remain. The latter source is consumed by the native importer, so it
is not newly established as a gameplay failure. The two previously recorded
bare-backslash alpha expressions remain explicit semantic work in `INV-010`.
No new unrelated defect was established by this read-only inventory/register
work. Historical MP content warnings remain a separate qualification backlog;
that inventory-only refresh did not run a game. The later renderer probes above
record their own gameplay and warning comparisons.

The source consolidation also preserves ART-036's earlier partial record from `e3e7237a`, carrying the confirmation evidence retained by ART-037 after supersession. No additional requirement or production GUI is accepted.

## 7 October 2026: implant design coverage

Register schema 14 appends `FLOW-062` for the Medical Labs temporary implant
overlay, now measured in visual specification 1.15 section 14.19 and illustrated
in the editable atlas. It preserves the 4650 ms activation, separate hurt pulse,
wide/tall layout and reduced-motion requirements. The row remains pending until
its retained vehicle-HUD migration and engine acceptance captures exist. Existing
requirement statuses and acceptance gates remain unchanged.

## 7 October 2026: initialization and view continuity

Register schema 15 follows visual specification 1.16. It supersedes `FLOW-050`
with `FLOW-063` for four menu levels and campaign setup drafts, and `FLOW-053`
with `FLOW-064` for soft focus outside all three scopes. It appends `FLOW-065`
for treatment recovery, death grading and the black loading handover. Existing
rows remain historical and unchanged; the fourth-level row remains partial and
carries the earlier sub-page evidence. The two other rows start pending; this
scope revision preserves the evidence registry. No row gains acceptance evidence.

The separate implementation checkpoint records early vector initialization,
shared forward/Back transitions, menu drafts, Gaussian optics and view effects.
Native tests and engine screenshots cover the implemented Windows OpenGL/Vulkan
paths, portrait layout, reduced motion, localized menus, actual campaign
progression and terminal loading pixels. See the
[implementation record](retained-screens.md) and
`.tmp/ui-continuity/validation-summary.json`. The revision audit is recorded in
`.tmp/ui-continuity/requirement-audit-ready.json`. Other platforms, physical input,
future screen migrations and full product acceptance remain outside this
checkpoint; no migration or final gate is accepted.

## 7 October 2026: playback-driven communication graphs

Register schema 16 follows visual specification 1.17 and supersedes `ART-029`
with pending `ART-038`. It preserves EKG, Classic, masks, squad emblems and
talker layouts while replacing synthetic Remastered traces with independent
decoded audio histories. No existing requirement changes state or gains
acceptance. [Implementation and evidence](audio-graphs.md) explicitly record
the unfinished live microphone voice backend and remaining HUD/platform work.

## Revision 17: weapon-wheel placement

Visual specification 1.18 preserves the original 77% horizontal wheel position
through archived `ui_weaponWheelOffset 27`, with zero centered, signed offsets,
and safety fitting that does not rewrite the preference. `WID-036` supersedes
`WID-033`; the complete wheel/quick-chat and motion scope is carried forward.
The appended row remains pending. [Native weapon-wheel evidence](weapon-wheels.md)
does not accept the broader product requirement. There are 346 requirements:
95 partial, 250 pending and one verified; 37 rows are superseded.

## Revision 18: weapon-wheel scene focus

Visual specification 1.19 replaces the former no-blur wheel backing with
quality Gaussian scene focus below the HUD. `WID-037` appends this requirement
and depends on the existing full wheel scope in `WID-036`; no existing row or
supersession is changed. The new row remains pending. There are 347 requirements:
95 partial, 251 pending and one verified; 37 rows are superseded.

## 9 October 2026: HUD continuation checkpoint

The recovered HUD work now has bounded evidence for the Classic gauges and canvas, individual ammo gains, notice separation, MP edge states and the Marine/Strogg wheels. `ART-026`, `FLOW-031`, `FLOW-051`, `LAY-020`, `FLOW-056`, `WID-034` and `WID-036` move from pending to partial; `MOT-015` gains wheel-motion evidence. All normative requirements, constraints, dependencies, historical rows and acceptance gates remain unchanged. There are 102 partial, 244 pending and one verified requirement.

The source-bound local record is `.tmp/claude-continuation/hud/validation-summary.json`; [HUD](remastered-hud.md) and [wheel](weapon-wheels.md) notes retain the open work. This checkout already contained register revisions 14 through 18 beyond its committed schema-13 baseline. The continuation therefore records a structural/source-binding check against its preserved working checkpoint, not a successful immutable-Git increment audit or product acceptance.

## Revision 19: Awakening single-player scope

Visual specification 1.24 applies the project guide's existing single-player
expansion policy. `FLOW-066` supersedes `FLOW-054` while preserving the thirteen
weapons, both HUD families, scopes/displays/crosshairs and Falcon squad line in
`q4xbase` single player. `WID-038` supersedes `WID-034` while preserving the complete
eleven-slot ordinary Quake 4 multiplayer wheel, team treatments, reserve-only
readout and equip/timing behavior. The historical rows are unchanged. Expansion
multiplayer powerup/ammunition/accuracy/summary and wheel clauses no longer define
acceptance; the artwork catalogue remains required without adding a game mode.

Both successors remain partial and carry their predecessors' bounded evidence.
No requirement, migration variant, artwork or platform is accepted. There are
349 rows, 310 active requirements and 39 historical superseded rows; the current
358 migration variants and seven final gates remain unaccepted.

This revision follows the already validated Android inventory update in this
uncommitted checkpoint. Its audit compares a frozen working-register snapshot,
checks preserved historical rows/evidence, supersession/dependency structure,
source hashes and unchanged migration bytes. It is a structural/source-binding
check, not an immutable-Git increment audit or implementation acceptance.

The scoped audit is `.tmp/stage-two/ui-awakening-scope/scope-audit-r3.json`, SHA-256
`7a8cf674e111a7efa515e730fa4c1e854d4595b49e261e8d21e0ba52be9e6960`.
Current document bindings pass. Historical local capture references remain
unchanged and are explicitly not requalified; the unavailable older inventory
capture is not treated as new evidence.

## 23 October 2026: portrait cinematic subtitles

Visual specification 1.25 lets cinematic subtitles hang 12 u below the 640x480
cinematic artwork on screens narrower than 4:3 where two lines fit there. The
picture is too short there for enlarged captions, which covered title text that
cinematics draw in it, such as the Awakening introduction's. The active subtitle
clause of `FLOW-052`, which superseded `FLOW-034` through `FLOW-040`, reads "at most
two lines, never over the crosshair" and already admits this placement. No
requirement, status or supersession changes; the increment re-binds the visual
specification and the audio evidence document. The
[portrait caption captures](../audio-voice-evidence.md#authored-scenes-centred-reverb-and-portrait-captions-23-october-2026)
record the implementation, not acceptance. The increment audit
(`validate_product_requirements.py`) passes against baseline `c12d36cda`; its report
is `.tmp/audio/round2/register-increment.json` in the audio worktree.

## Revision 20: objective points of interest

Visual specification 1.28 adds objective points of interest to the multiplayer
HUD's Remastered additions: flag bases, assault points and Dead Zone zones marked
in the world with distance as their only text, their status carried by the
symbols, off-screen bearings inside the safe frame, and the stereo VR HUD-plane
projection. No active row admitted them: `FLOW-035` lists the Remastered
multiplayer additions one by one and `FLOW-056` covers only the Remastered top.
`FLOW-067` appends the scope and depends on both. It is pending; the
implementation (`src/mpgame/mp/ObjectiveMarkers.cpp`) is recorded in
[the HUD notes](remastered-hud.md), and its captures are not yet bound as
evidence. No existing requirement, evidence record or supersession changes; the
revision re-binds the visual specification and the three evidence records that
bind `remastered-hud.md`. There are 350 rows, 311 active requirements and 39
historical superseded rows; the 358 migration variants and seven final gates
remain unaccepted.

## Revision 22: the initializing screen's entrance and cadence

Visual specification 1.37 gives the Remastered initializing screen an entrance
and a smoother cadence. Startup repainted it only about eight times a second,
with pauses of up to a second, so its turning rings stepped and stalled. It now
repaints up to 60 times a second whenever loading offers a moment, its repaints
taking at most half of the time while the entrance plays and a fifth after it.
The 1.3 s entrance drops the mechanism from 1.8 times its size to its seat as
its gears wind in and the clamps close, and each step's line rises into place
while a marker glides along a cascading rule. `FLOW-057` quoted the 30 Hz
ceiling and the step flicker, so `FLOW-069` supersedes it, carrying every other
initializing rule. It is pending; engine traces and captures of the entrance
bind no evidence record yet. The revision re-binds the visual specification.
There are 352 rows, 312 active requirements and 40 historical superseded rows;
the 358 migration variants and seven final gates remain unaccepted.
