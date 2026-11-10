# Retained menus, initialization, loading and view effects

This increment extends the retained (RmlUi) interface beyond the SYSTEM page to
the title screen, the single-player pause menu and the stock loading screens,
built from [visual specification 1.9](../ui-visual-design.md) and the design
atlas. It starts from engine `f5503259a5a2f618e5dd1cebab9adc55c2513f79` and
advances screen migration in the [idtech5-ui plan](../archive/2026-10-18/plans/idtech5-ui.md). It
adds partial evidence for `FLOW-024`, `FLOW-047`, `FLOW-048`, `ART-030` and
`REN-014` in the [requirement register](product-requirements.md); the
remaining screen migrations and the final gates stay open.

## 11 October 2026: death and crash awakening

The shared SP/MP view effect now layers death's existing 1800 ms colour drain
with one focus swell (320 ms rise, gone by 1570 ms) and an inward charcoal
shade that arrives from 300–2400 ms. The extra sigma joins the existing death
filter, rather than blurring the scene twice. The red afterimage uses eased
ends. Reduced Motion or Reduced Flashing suppresses the swell and afterimage;
the quiet colour loss and peripheral shade remain.

Air Defense Trenches receives its own warmer treatment. Only the active
`cin_opening` camera on `airdefense1`, in single player, qualifies. The
installed `airdefense1::cinFadeBackIn` black fades open a 16 dp / 0.62
saturation focus treatment, warm edge wash and asymmetric soft eyelid shade.
Each clears over the original fade plus 2300 ms, with one small focus
hesitation omitted by either accessibility preference. The two authored
100 ms blackouts close it under the original screen fade. No scripts,
dialogue, audio, cameras, fade deadlines or retail assets are replaced.
Camera departure or a skipped cinematic clears the state; death takes
precedence, restore clears transient state, and Classic never submits it.

The presentation remains below the HUD and uses the existing editable
`view_effects.q4ui` gradients and backdrop filter. The generator is
`tools/ui/build_retained_effects.py`. The production-method fixture
`tools/tests/retained_view_effects.py` exercises the actual shared implementation
with a controlled clock: death/revival, clock rollback, both accessibility
preferences, wheel submission isolation, map/camera/MP exclusions, three
wakes/two blackouts, camera exit, restore/reset, spectators and MCC recovery.
The native screen suite checks filter composition and viewport coverage.
`tools/ui/capture_ui_continuity.py` adds death focus/tunnel checkpoints,
reduced-settings and MP cases, and full original crash playback using engine
screenshots with host input disabled. Qualification evidence belongs under
`.tmp/view-effects-refinement/`.

The Windows x64 build and staging pass. The focused native suite passes 956
checks, its registered `openq4-ui-view-effects` Meson test passes, and the
production-method fixture and generated-document check pass. OpenGL 1280x720
captures qualify normal/reduced death and the complete three-wake/two-blackout
crash sequence; Vulkan 720x960 captures qualify SP/MP death and the reduced
crash sequence through clear gameplay and clean exit (39 engine screenshots).
The latter needs the cinematic-specific 300-second harness allowance; the
first attempt reached the final awakening but exceeded the old 240-second
limit, so it is excluded from complete-run acceptance. The first GL MP
probe was rejected because the listen-server `kill` command needs a client
index; the corrected Vulkan `kill 0` capture shows the dead player and sharp
respawn/match overlays. Source/runtime hashes and the exact staged-document
comparison are in `identities.json`.

Limits and unrelated findings: the broad retained-screen suite timed out at
240 seconds in the debug build, before completing its full UI coverage.
Meson reports an RmlUi wrap-revision warning and include-directory sandbox
warnings; MP warmup reports non-pre-cached material/table warnings. The
effect captures have no retained-UI diagnostics or geometry-ring overflow.
Other platforms and VR remain untested. Automatic approval review rejected
deletion of verified task-created caches with “blocked by policy”; those
caches remain beside the useful screenshots and logs. See
`.tmp/view-effects-refinement/validation-summary.json` for accepted and
rejected runs.

## 7 October 2026: initialization, fourth level and view continuity

`PrintLoadingMessage` now draws `initialization.q4ui` through an early retained
view, before the ordinary GUI manager starts. The session fallback uses the
same document. Stage text remains localized; the view retires when a menu or
gameplay takes over, and Classic or an unavailable document uses the original
splash. The plate band, teeth, sweep, halo, embers, glint and progress accents
follow the presentation clock. Reduced motion fixes the light and omits the
decorative accents. Radial and conic paints consolidate the ring/sweep meshes:
native 3840×2160 validation counts 457,764 vertices, below 30 MiB including
room for the final black overlay. Round-stroke
coverage clamps sub-quantization residue within 0.0001 pixel area while still
rejecting larger overlap errors.

`retained_menu_levels.py` supplies the shared next-level band step, fitted
breadcrumbs, label carry and reversible descent. Both campaign branches use
`campaign_skill.q4ui`, including the existing campaign reload continuation.
The five localized difficulties and Turbo remain drafts until Start Game;
Back discards them. Each arrival focuses the chosen difficulty, as Restart
Level's page does. CHAPTERS (`chapter_view`) shows the campaign's levels in
the difficulty's place, under the Load Game page's LEVEL and STATUS columns
and rows: the session publishes every chapter (`chapter.row<n>.*`,
`PublishCampaignChapters`), a row not reached yet is disabled, a row's first
Accept chooses it (its levelshot, name and how it starts follow) and the
second, or START CHAPTER, starts it. Back and BACK return to the difficulty.
The stock `campaign_menu.gui` holds the same two levels when the retained
pages are off: CAMPAIGN (`campaign_view` 0) and the campaign page (1) with
its difficulty, Turbo, START GAME and CHAPTERS list. The top and bottom frame stay at the physical view edges,
and the isotropic content centers vertically in portrait windows. The route
audit found no other currently implemented fourth-level main-menu page:
Settings tabs and Arena's tier/match selection edit in place, multiplayer
browser/create/demos are third-level routes, and dialogs remain modals.
The planned Chapters route and other future children must use the same helper.

All three scopes now use a 13.5 dp Gaussian backdrop with a 38 u aperture
feather and 0.86 saturation outside the sight. The crisp center retains its
isotropic circle or diamond. A single full-view gradient supplies each feather
mask without redundant circular coverage edges. The nailgun uses one graded
glow throughout acquisition; its resting tint stays within one 8-bit channel
value of the stock measurement, and its locked tint is exact. Native 4K counts
are 318,360 / 350,176 / 651,816 vertices for machinegun / railgun / nailgun,
and 690,300 for a locked nailgun. Vulkan's shared vertex ring grows from 32 to
96 MiB per frame slot to leave room for the world and stencil shadows beneath these high-resolution
optics; index and uniform ring allocations are unchanged. The capture harness
now rejects geometry-ring overflow explicitly.

`RetainedViewEffects.inl`, shared by SP/MP, draws
the MCC recovery and death presentation below the readable HUD. It follows
the original `mcc_2` treatment fades, then consumes the sequence after the
white fade following bed exit. Death smoothly drains color over 1800 ms, with
soft peripheral shading and a brief restrained afterimage; original camera,
respawn and black-fade timing are unchanged. Transient UI state resets on
restore, respawn and disabling the retained interface.

Map transitions finish a wall-clock 240 ms smooth fade (80 ms with reduced
motion) before loading work starts. The outgoing menu stays alive through its
last black frame. `ui_retainedCaptureLoadFade 1` is an unarchived diagnostic
that calls the registered engine screenshot command at the middle and end;
the capture harness checks that every terminal pixel is zero.

Evidence is under `.tmp/ui-continuity/`:

- `optics-native-final.log`, `geometry-budget-final.txt`, `initialization-paints.log`,
  `gate-geometry-final.log` and `adapter.log`: native screen/paint/view tests,
  4K geometry budget, 360 session checks, server/loading contracts and compiled
  adapter mutation checks. Every generated document also passes `--check`.
- `verified-gl` and `final-recovery-vulkan`: real `mcc_2` treatment captures,
  preserving the three recovery events and final bed exit.
  `qualified-scopes-vulkan` and `qualified-scopes-portrait`: each scope, rail ammo and
  nailgun lock, plus live Classic fallback over real gameplay.
  `qualified-scopes-offscreen4k` exercises 3840×2160 rendering of all optics,
  including the locked nailgun, without mesh or frame-ring overflow. Its images
  are rejected for visual acceptance: the oversized screenshot path tiles
  retained render-target effects incorrectly. Normal window-sized captures
  render correctly. This separate screenshot-export defect remains open.
- `final-4k-vulkan`, `final-portrait`, `final-german` and `final-progression`:
  final menu/death captures, corrected portrait framing, German labels,
  reduced motion, repeated entry and a real campaign start with difficulty 2
  and Turbo enabled. Windows resized the large requested window to
  2538×1312; `offscreen4k` separately exercises the engine's explicit
  3840×2160 screenshot command as a geometry stress check, subject to the
  oversized-capture limitation above. Terminal loading frames are verified black.
- `validation-summary.json`: source/binary identities, captured image hashes,
  checked scope and remaining platform/input limits. These are development
  checks, not complete UI migration or platform-release acceptance.

## 7 October 2026: title motion, loading anchors and scopes

The title's image retains pointer parallax, while its vector light/grid and
framing groups stay stationary. Fractional pointer movement previously rebuilt
their analytic pixel coverage. The native screen test now proves that pointer
motion leaves these paths cached; the independent animated emblem glint remains.
The screen generator also caches font metrics instead of reparsing a font for
every localized label measurement.

Loading uses `canvas: {height: 720, minWidth: 960}`. The optional bounded
`minWidth` makes the scale `min(viewHeight/height, viewWidth/minWidth)`; omitted,
the existing height-based contract remains. Objectives/tips/server details use
the leading edge, identity/progress use the trailing edge, lower furniture uses
the actual bottom edge, and corner brackets follow all four view corners.
The background still covers the complete view. Native bounds tests cover 32:9
and 9:16; engine captures cover 1920×810 GL and 720×1280 Vulkan.

`tools/ui/build_retained_scopes.py` builds the machinegun, railgun and nailgun
overlays from the atlas's authored paths and visual specification 14.13. Optical
centers use the same fit-to-width-or-height canvas, preserving circular and
diamond geometry; outer veils/haze fill every exposed band. Continuous radial
and diamond paints replace stepped concentric bands. The initial machinegun
radial smear is superseded by the Gaussian treatment above. No retail bitmap
mask is added. Rail ammo cells and motion, yaw compasses and nailgun target lock
events remain driven by game state and presentation timelines.

The shared `src/game/gamesys/RetainedScope.h` selects only the three shipped
scope paths behind `ui_retained`, for SP, MP and vehicle weapons. Differently
named scopes and mods' replacements keep their own GUI. Missing/invalid retained
documents keep the stock instance. A gate change transfers state into a unique
replacement and releases the old unique instance. Save restore consumes its
serialized GUI first, then resets only the transient selection cache. Named lock
state is restored; both player and vehicle presentation publish state before draw.

Validation is retained locally under `.tmp/ui-fixes/`:

- `native-verified.log`: all five screen/vector/three-scope paint tests pass;
  gradient budgets include 4K. `scope-routing-test.log`, `gate-test2.log`,
  `adapter-test.log` and `shader-test.log` cover routing/fallback/restore,
  session behavior, compiled adapter mutations and the Vulkan shader header.
- `accepted-gl` and `accepted-vk`: hidden windowed `game/airdefense1` captures,
  `in_mouse 0`, no host input, using the engine's `screenshot`. Scope compass,
  rail ammo and nail lock states render over the actual map. The live machinegun
  reports `.q4ui → .gui → .q4ui` as the gate changes. No retained rendering
  errors remain; the existing non-precache declarations and the generated scope
  material's first-use non-precache notice are not rendering failures.
- `scopes-4x3/report.json`: the reusable scope capture harness also passes at
  1280×960 OpenGL, including all three optics and live gate fallback.
- `performance-before` and `performance-after`: the same staged debugoptimized
  executable, 1280×720 GL, and 90 moving-pointer samples, with the previous title
  document supplied through a private test mod for the baseline. Median retained
  CPU time falls **46.7568 → 7.4827 ms** (84%); p95 **71.4682 → 19.3318 ms**.
  These are retained CPU timings, not an FPS measurement: the hidden console
  sequence and logging affect frame intervals.
- `monster-materials/report.json`: stock Berserker, Strogg Marine and Gladiator
  traces select flesh and monster-metal FX separately. The installed flesh FX
  names `bullet_impact_flesh`. `ModelTrace` now requests the existing posed
  collision snapshot; visible MD5/MD5R snapshots omit the collision-only meshes
  carrying these maps. The pre-fix Berserker probe selected default impacts for
  all 832 sampled hits. No monster-wide flesh override is used.
- `openq4-atlas.html` and `atlas-validation.json`: full atlas with the tracked
  Medical Labs implant sheet, five aspect ratios, in-bounds readouts and no
  JavaScript errors. The design and timeline prototype are complete; the broader
  vehicle HUD migration remains tracked separately as `FLOW-062`.

For repeatable monster checks run `tools/tests/monster_hit_materials.py` with
`--assets`, a fresh project-local `--output`, and optional `--renderer vulkan`.
`tools/ui/capture_retained_scopes.py` takes the same arguments plus `--size WxH`
for the five scope-state captures. The register revision and source-binding
audit passes in `requirement-audit-final.json`; historical acceptance states stay intact.
For overlay captures, `ui_testGuiInGame 1` draws the loaded scene behind
`testGUI`; its default remains the black inspection background.

Unrelated findings: the font generator's repeated font parsing was corrected
alongside this work. Existing non-precache declarations remain in gameplay logs,
and several immutable historical register descriptions already contain replacement
characters; this revision preserves those descriptions as the register requires.

## One gate (session screens)

`ui_retained` is the only switch for these screens. It defaults to 1 in the
current source ([CVar declaration](https://github.com/themuffinator/openQ4/blob/main/src/framework/Session_menu.cpp))
and is archived, so an existing player preference is preserved.
`seta ui_retained 0` selects the stock screens; `seta ui_retained 1` selects
the retained interface. The 0.14.000 release shipped with a default of 0;
commit `1aa2332d9` restored the default to 1 afterward. This does not close
the outstanding [UI performance gates](runtime-performance.md).
While it is 0,
no menu, pause or loading route creates a retained view, and the stock GUIs
present every screen exactly as before. Developer commands that name a
document explicitly (`testGUI`, `ui_retainedPreview`) remain the runtime's
test and preview tools and load it regardless of the gate.

`ui_retained` includes the SYSTEM page (SETTINGS > DISPLAY) while every
setting of the stock SYSTEM page works there, so the default never loses one.
The session lists the stock settings the page has no control for in
`RETAINED_SYSTEM_MISSING_SETTINGS`, and those it has a control for that cannot
do what the stock page did in `RETAINED_SYSTEM_INCOMPLETE_SETTINGS`. Both are
empty since 10 November 2026, when a Performance Preset began to apply there
([SYSTEM resolution rows](system-resolution-rows.md#the-gate)), so the gate
opens the page. The gate test derives both lists from the code, and a reason
that returns closes the gate again. `ui_retainedSystem` opens the page
regardless, also with `ui_retained 0`.

`ui_retainedMultiplayer` likewise opts into the multiplayer menu card, which
covers the game's in-match menu; `ui_retained` includes it only once none of
its pages hands off to a stock page any more
([multiplayer menus](multiplayer-menus.md#the-gate)). Neither opt-in is
archived, so each lasts until the player quits. Both are `CVAR_NOCHEAT`, so
joining or hosting a game without cheats, which resets every unarchived cheat
CVar, keeps them, and either can change during a match.

`tools/tests/ui_retained_gate.py` holds the gate: it checks the CVar defaults,
that every retained document path in the session is reached only behind the
gate, that the documents are current with their generator and request only
allowlisted session verbs, that no legacy content names a retained document and
only the shared scope router selects one in game code, and it compiles the
production session code against counted doubles
to show that nothing retained loads while the gate is 0 and that every
fallback below presents the stock screen.

## Falling back to the stock screens

A retained screen stands in for a stock GUI only while it can. Otherwise that
screen presents its stock GUI, for the rest of the session, and every other
screen keeps its retained form:

- **Not installed.** No search path provides the document. This is quiet: a
  developer message only.
- **Cannot load.** The document is installed but fails to parse, validate or
  load. It is reported once as a warning.
- **The main menu lacks a page.** The title and pause screens cover the legacy
  main menu and hand its pages off to it, so they present only over a menu
  that defines every window they hand off to (`main_b_newgame`,
  `main_b_loadgame`, `main_b_savegame`, `main_b_multiplayer`,
  `main_b_settings`, `main_b_credits`, `main_b_demos`, `main_b_difficulty`,
  `main_b_mods` and `main_b_updates`). A mod's own main menu without them
  presents itself, as the mod made it. The loading screens hand nothing off and
  still present. Under The Awakening the menus come from openQ4's own content,
  so the retained screens present there too.
- **The view stops drawing.** A view that fails to come back after a renderer
  or language change can no longer draw; the stock home screen takes over, with
  one warning.

The SYSTEM page and the campaign selectors follow the same rules. When the
opted-in SYSTEM page cannot load, the click that asked for it opens the stock
SYSTEM page, and the SYSTEM button opens the stock page directly from then on.
Should the session ever refuse an open while the page is still available (no
current state does: a message box or test GUI takes the menu's clicks first),
that request opens the stock page with a developer warning, and the button keeps
the retained page. When the Single Player or campaign selector cannot load, the
stock selector opens.

The multiplayer menu reaches the SYSTEM page through the main menu: the game
returns `main fromMp_toSystem`, and the main menu's `fromMp_toSystem` event
shows the stock SYSTEM sub-page (`desktop::dest` 22). With the retained page
available, the event instead starts `anim_retainedSystemFromMp`, whose
timeline asks the session on the next frame to open the retained page
(`openRetainedSystemFromMp`) over the main menu as it comes up from the match.
The request goes through a timeline because a command set in a named event the
session fires is never collected. The branch runs no page transition, so it
leaves `desktop::active` alone; the stock route raises it for its transition,
which lowers it again. Both routes show the same SYSTEM page. Back returns to
the main menu as the multiplayer menu's Main Menu choice leaves it: its main
page, or the server browser when the match was joined from there. An open the
session refuses sends `fromMp_toSystemStock`, the stock route.
`tools/tests/ui_system_session_route.py` compiles the route against counted
doubles and pins the menu script. The `system-mp-game` capture scenario takes
the real route from a listen server's in-game menu, and `system-mp-route` and
`fallback-system-mp` fire the event at the title; each checks that the main
menu is usable again after Back.

SYSTEM route evidence: `.tmp/ui/system-session-route/validation-evidence.json`,
SHA-256 `f363fcb9d87b3a04aa4ba5617f2fa1982a9909d7d8618a99717eee18c3de2003`.

Pictures fall back as well. CONTINUE shows the newest save's picture only when
its file exists: an autosave names its level's loadscreen, and any other save
keeps its own screenshot beside it (`savegames/<slot>.tga`), as the stock Load
Game page shows it. That screenshot is reread when the title opens, so a newer
save's picture replaces an older one. The pause screen's level block chooses
its levelshot as the loading screen chooses its picture: the map's
`loadimage`, the intro art for a map that loads through the intro screen, then
the map's own levelshot. A candidate that is not installed falls through to
the next, ending at the generic art.

## Screens

The documents are generated by `tools/ui/build_retained_screens.py`, which
cites the specification section behind each measured value. Regenerate them
after editing the script; `--check` fails when an output is stale.

- **Title** (`guis/menu/title.q4ui`, section 9 and 13.7). Framing bands with
  their rim light, the lit field (vignette, additive light band, reticle grid),
  the backdrop levelshot montage, the additive wordmark and the multiplied
  Quake emblem with its 0.20 rim light. A glint runs around the emblem every
  9 s: the rim drawn bright under a wedge mask 12% of a turn wide that turns
  about the ring's center, while the rim inside it turns back so the outline
  stays put. Reduced motion (`ui_retainedReducedMotion`) hides the glint and
  keeps the rim. The backdrop, the light and grid, and the framing bands lean
  up to 6 dp away from the pointer, deeper layers further, while the content
  stays fixed (section 13.8 depth). The session publishes the pointer's
  place, or a held look stick's deflection while it leads (menu focus moves
  with the other stick), and reduced motion holds the layers still.
  CONTINUE leads the navigation when a save exists and its
  message line shows the save's levelshot and name, then its mission,
  difficulty and the run's play time ([save details](#save-details)); the same line explains
  whichever item has focus. SINGLE PLAYER, LOAD GAME, MULTIPLAYER and SETTINGS
  follow, and the secondary links and EXIT stay on the plinth. EXIT asks before
  leaving. Opening the menu focuses its primary action (section 13.4),
  CONTINUE or, without a save, SINGLE PLAYER, so a controller's first Accept
  acts; coming back from a page keeps the focus on the item that led there.
  While the mouse leads, only the item under the pointer is lit
  ([interaction](interaction.md)), so a mouse player never sees two. The prompt bar sits in the bottom band's raised section, its
  glyphs following the active device ([prompt glyphs](#prompt-glyphs)).
- **Pause** (section 13.7, single player). GAME PAUSED replaces the wordmark,
  RESUME leads, followed by SAVE GAME, LOAD GAME, RESTART LEVEL, OBJECTIVES,
  SETTINGS and QUIT TO MENU, the seven actions from 172 u (the plates' art is
  drawn from 172 u, as the title's from 202 u); only EXIT stays on the
  plinth, and both quits ask first. The current level block takes the
  emblem's place: a card with the levelshot, the mission, the difficulty, the
  objectives with their state and the time line. The session asks the game
  with the `retainedPauseState` menu command when the screen opens. Three
  rows list the open objectives, newest first as the stock objective screen
  stacks them, each behind the atlas marker, and then the ones completed on
  this map, newest first, behind a check at 0.5; the completed ones survive
  a save load ([save details](#save-details)). Two time lines under a faint
  rule give the mission's game time (it restarts with each map and is saved
  with it) with the run's play time, then how long ago the newest save was
  written: "0:42:10 in mission · 3:05:06 total" over "saved 4 min ago". A
  game module that publishes neither leaves the map's own objective summary
  and no time lines, and the heading hides when there is nothing to list. The paused view
  stays behind the screen, softened and never
  dimmed: the specification gives scene softening no values of its own, so
  it takes `modal.softfocus` (a 7.5 dp blur at 0.80 saturation), ramping in
  over 250 ms with the screen's fade from black. With the opaque-backing
  option, or on a renderer that cannot soften the view, a darkening scrim and
  vignette stand in. Back resumes the game. RESUME closes the screen at once
  (section 8), but the softened view, or the scrim standing in for it,
  releases over 250 ms as the modal backdrop does (section 4): the session
  keeps drawing the closed screen over the running game for those 250 ms,
  with no input and no cursor. Pausing again during the release reopens the
  screen at once, and a disconnect or level load ends the release. Every
  activation after a release shows the screen again and ramps the softened
  view back in, including a return through a page's Back. Every opening, and
  every return after a release, focuses RESUME; a page's Back returns the
  focus to the item that led there.
- **Objectives page** (sections 13.7 and 14.11). OBJECTIVES opens the
  objectives display in its Remastered form as a page of the pause:
  - **Hand-off.** Choosing it docks the bands as a page hand-off does and
    carries OBJECTIVES into the title slot. At 550 ms, when a stock page
    would appear, the stock entry motion plays: the back bar (black 0.80 over
    the leading 45%, under the framing bands so their rim light keeps its
    strength) slides 220 u in over 150 ms, then the heading, plates, text and
    screenshots fade in over 100 ms. The stock frames lead the text at 0.50;
    here they fade in with it, because their 0.40 is baked into their paint.
  - **Plates.** Every open objective is reachable, newest first, as a plate of
    the stock construction on the 143 u pitch: the marker at 0.40, the title
    in Lowpixel 0.20 wrapping to two lines at 308 u, the 134x100 u screenshot in its
    black border and the description in #D0DEB6 wrapping without a limit,
    the plate growing with it. The frames rest at 0.40, baked into their
    paint.
  - **Completed.** Under COMPLETED and its count, the objectives completed on
    this map follow, newest first: the title only, at 0.40 behind a check.
    The stock serial closes the list, at the 13 dp type floor. With no open
    objective one plate shows the stock static screenshot and its line beside
    it, in Lowpixel 0.31 and #D0DEB6.
  - **Scrolling and Back.** The list scrolls under its bar with the wheel, the
    stick, the keys or a drag; the bar holds focus. The pause's prompt bar
    gives way to the page's, Back alone at 532,441 u in the docked bottom
    band ([page prompt bars](#page-prompt-bars)), and Back (or the back input) fades
    the page while the bands come home. The page closes when they arrive; a
    second Back during that leave changes nothing. In the Strogg family
    Back's glyph takes #FFCC00 and its verb R_Strogg. A reopened list starts at its first entry, and a pause that closed
    without Back, as a console load closes it, opens at home without the
    page.
  - **Data.** The game publishes each objective's title, description and
    screenshot, and the completed ones, with `retainedPauseState`. The
    session shows a screenshot only when it is a plain, installed image
    name. Every level load asks the game for all the level's objective
    screenshots (`retainedLevelImages`) after it spawns the map and resolves
    them inside the load, so the page reads no picture mid-frame; they include
    the screenshots of the objectives the player holds, whose entities removed
    themselves when given, so a savegame load resolves them too. The game
    keeps the completed objectives per map, unsaved, so the save format is
    unchanged. The newest save's time is read once in the level load and
    restamped by each save, so opening the pause lists no files.
  - **Strogg.** The Strogg page keeps the construction with #FF9000 frames,
    #FF9900 markers, an R_Strogg heading and the STROGG NET serial. Its
    return plays the translation again.
- **Strogg pause** (`guis/menu/pause_strogg.q4ui`, section 13.7 and the atlas
  Strogg pause). After Kane's stroggification the pause takes the Strogg
  family; it offers the same actions, level block and confirmations.
  - **Frame.** The bands keep their frame but trade the 45-degree notches for
    30-degree shoulders and downward teeth. Their rim light, and the circuit
    traces etched in them, are #F59512, over a warm light band and #FFB060
    crosses.
  - **Plates and labels.** The plates end in a 30-degree shoulder at the end
    of their target (357 u), outlined by their rail. Markers are slanted, as
    the atlas draws them. Labels are R_Strogg in #FCFFC8, turning #FFCC00 on
    focus.
  - **Level block.** The block takes a card chamfered 10 u at every corner,
    with the stock Strogg HUD static added faintly inside it. The levelshot
    sits in a chamfered orange frame.
  - **Translation.** Every label arrives in runes and plays a short form of
    the credits translation (section 8). The rune copy sits on the label's
    baseline at 1.2 times its cap height. It cross-fades into R_Strogg while
    both copies pop 6% larger and settle.
  - **Timing.** GAME PAUSED translates from 60 ms over 500 ms under the scan
    bar, arriving white and settling. The actions follow, 120 ms plus 45 ms
    per row, over 200 ms each, then EXIT. The level block's lines come after
    them, and a map's own objectives summary fades in with its heading. The
    prompt bar and the time line are readable from the start. Returning from
    a page plays the same once the home content shows.
  - **Focus.** A focused plate runs the scan bar under its label. The bar's
    wrapper fits the label, and the bar slides by its right inset, so its
    head sweeps in from the leading edge and rests 12 u past the label in any
    language.
  - **Input and reduced motion.** Input works from the first frame. Reduced
    motion shows the translated labels at once.
  - **Confirmations.** QUIT TO MENU and EXIT ask in the Strogg family too
    ([Strogg confirmations](#strogg-confirmations)).
  - **Choosing it.** The session asks the game once a level, with the
    `retainedPauseFamily` menu command, whether the player is Strogg (the
    `player_strogg` definition, from game/medlabs on). Every single-player
    level load resolves both pause documents. When the Strogg pause cannot
    present (not installed, unable to load, or stopped drawing), the Marine
    pause stands in for the rest of the session. A game module that does not
    answer keeps the Marine pause.
- **Loading** (section 14.17). Replaces the stock `generic`, `splevel`,
  `mplevel` and `intro` loading GUIs; a map's own loading GUI stays, and so
  does a mod's own copy of a stock one: a stock-named GUI that the mod's own
  game directory (`fs_game` or `fs_game_base`, other than openQ4's, the stock
  game's or the Awakening's) supplies. The
  levelshot, reticle grid, framing bands, brackets and dot matrix frame the
  level name, its message line (the difficulty in single player, the game type
  in multiplayer) and the objectives plate. The levelshot drifts in by 3% over
  the load unless motion is reduced. The Remastered progress sits in the thick
  part of the bottom band: LOADING above a 240 u bar and, under the bar, the
  loader's phase with its place among the four phases (map, world, assets,
  finishing) or the asset queue's count, and the percentage in the value
  color. When the load finishes, the phase line reports the load time and
  LOADING becomes the continue prompt for the last-used device: the desktop
  prompt, or the controller family's south button and CONTINUE
  ([prompt glyphs](#prompt-glyphs)).
  Multiplayer keeps the band low: a card on the leading side holds the
  server's name in its header, then its address in Profont, the mode and the
  limits, the players by team, and the server's message (`si_motd`), and a
  finished multiplayer load reads JOINING, since multiplayer never waits for
  a click; joining by the Welcome card, the screen stays up until the card
  presents ([hand-off](#hand-off-to-the-welcome-card)). The card stands on 384 u, clear of the lower corner bracket, and
  its parts flow, so it grows upward with what it holds. The game module names
  the mode, the limits it enforces (the scoreboard's rule:
  `MPResolveMatchLimitFor`) and whether it has teams from the server info the
  session passes (`retainedLoadingServer`); a game that does not answer leaves
  the session's own strings and one list. In team modes the players stand in
  two columns under MARINES and STROGG in their team colors, otherwise in one
  under PLAYERS, six names a column and then "+N more", over a line of
  spectators and players still connecting. The players are those known as
  the load starts: a listen host's own (bots included) before its map change
  wipes their user info, a client's on a map change before `InitGame` clears
  them, and on a fresh connection those the server names in an optional block
  after its connect response's server info (`ConnectRoster`: a tag, a length,
  then each player's slot, flags, team and name), with this client joining
  them; a server that sends none, or a malformed one, leaves the card without
  players and the connection goes on. Names and the message lose Quake 4's
  color and icon escapes, control bytes and malformed UTF-8, keep whole
  characters within their limits (48 and 256 bytes), and never read as a
  `#str_` key. The message wraps to at most three lines in the notices color;
  the console turns backslashes into slashes, so a cvar can carry no typed
  line break. The arsenal fills the thick band's leading part, from its riser to
  the bar: two rows of ten 16 u icons on a 17 u pitch, one for each kind of
  item as it spawns (the stock row's twenty), each fading in over 150 ms in
  its color code (Appendix B.4). The stock icons are colour-coded materials,
  one stage tinting the icon image; the session reads the image and the tint
  from the material's definition and publishes them with the icon
  (`load_icon_src_N` and `load_icon_r/g/b_N`), then plays its fade
  (`arsenalN`). In single player the same part of the band runs tips while
  the level loads: a TIP tag over one tip of at most two lines of Lowpixel
  14 dp, changing every 6 s with a 250 ms cross-fade between two slots
  (`loading_tip_a`/`_b`, `tipA`/`tipB`, by text alpha). The session starts
  each load at a different tip and moves to the next from the load's redraws
  (`PublishRetainedLoadingTip`); once the level is ready the continue
  prompt, which runs across that part in most languages, takes the band.
  The 18 tips state Quake 4 single-player mechanics checked against the game
  code and stock data (the blaster's charge, the flashlight, medics and
  techs, firing at people and screens instead of a use key, objectives on
  TAB and the pause page, quick save, the machinegun's zoom, headshots,
  armor, barrels, grenades, self damage, pickups that wait, dropped guns,
  vehicle repair, the shotgun's reload and leaving a vehicle), and in every
  shipped language each fits its two lines, which the gate test measures
  from `lowpixel.ttf`. The name strip darkens the message line so it reads
  over any levelshot.
- **Single Player and Campaign** (`guis/menu/singleplayer.q4ui` and
  `campaigns.q4ui`, sections 8 and 9, the sub-page level of 1.10). The Single
  Player page titles itself in the top band's slot at 39,19 u, where the
  title's carry lands. Its Campaign and Arena sections each open a
  sub-page, Arena the [Arena page](#arena-page). The page arrives with the
  focus on CAMPAIGN and the Campaign sub-page on the campaign being played,
  QUAKE 4 unless it is the expansion:
  - **Section summaries.** Each section summarizes itself in the content
    column while it is the subject (section 13.7): the plate under the
    pointer, or the focused one after keys, a controller or arrival. The
    column shows that section's sentence in the description's place, and the
    page's description for BACK or with no subject. A control's feedback
    reaches only its own subtree, so each plate names its section as a
    `detail` and the document's cardless detail definition keeps the runtime's
    subject in `summary.section`, which the column binds ([interaction
    contract](interaction.md#detail-cards)). CAMPAIGN stands over the newest
    save's levelshot, the one CONTINUE shows, and ARENA over the match its
    page would open on; the session publishes both as the page opens
    (`summary.campaign.shot`, `summary.arena.shot`), read as precaches, and a
    levelshot still waiting for the front end's idle frames is read there
    instead. The picture stands 140x105 u at 395,77 u, over the column under
    the docked top band, and shows only while the session names one, while
    the column's text stays clear of it, and never in a tall view, where the
    column stands under the plates. On Campaign, NEW GAME stands over the
    active campaign's first level and CHAPTERS over the furthest it has
    reached (`summary.newgame.shot`, `summary.chapters.shot`). The pictures
    show only while the campaign chosen there is the active one, since the
    other's levels are not mounted. QUAKE 4, THE AWAKENING and BACK keep the
    column's word on the campaigns.
  - **Sub-page state.** The top band stands one notch pitch (97 u) further
    toward the trailing edge while the bottom band stays docked. The parent
    title is the crumb in the band's thin span at 39,12 u and 0.40, cut at
    the step, and the sub-page title starts at the step's foot, 136,19 u.
  - **Crumb fit.** The crumb takes 80 % of the title's size, or less where the
    title would overrun the 83 u span, but never less than the 13 dp type
    floor; past that it is cut. The generator measures each shipped
    language's title in `marine.ttf` as the host lays it out (advances at
    pixels per em, no kerning) and plays that language's crumb timelines,
    chosen by `sys_lang`. English and German shrink to the floor and fit;
    Italian and Polish are cut.
  - **Going deeper.** Accept plays `main_menu_selection` once, and Campaign
    plays the page's half: its plates fade over
    150 ms while it sweeps 640 u toward the trailing edge over 300 ms. Its
    title becomes the crumb over 150 ms. From 50 ms the top band steps
    (`frame.step`, accel(150, 150) over 300 ms) and the CAMPAIGN label
    carries from its row into the slot at the step's foot, scaling to the
    title size. At 350 ms the session presents the sub-page: its plates and
    title show at once and its backing fades in over 150 ms. Back while the
    page goes deeper reverses the change from where it stands, over 150 to
    300 ms by the distance the band has stepped.
  - **Back.** The sub-page title drops at once and the sub-page sweeps 640 u
    toward the leading edge over 300 ms (`screen.return`, linear, as the
    page's sweep going deeper) while its backing fades over 250 ms
    (`content.out`) and the band steps back from the start. The crumb returns
    to the title slot over the last 150 ms. At 300 ms the page returns,
    fading its plates and content in over 150 ms with the focus on Campaign.
  - **Session.** The two levels are two documents. The session
    (`BeginRetainedSubpage`, `UpdateRetainedSubpage`, `RetainedSubpageEvent`)
    plays the leaving document's half and presents the other at the
    hand-over: 350 ms going deeper and 300 ms back. Under reduced motion it
    hands over at once and the arrival's 80 ms fade is the change. Mid-change
    a key's release still reaches the leaving document, so a key held into
    the change retires normally; Back going deeper reverses it, and other
    input and requests wait for the hand-over. Anything else taking the
    screen abandons the change. Leaving the screen places a document at rest
    while it is hidden, so it never presents moved, and every arrival shows
    its plates at once and fades its backing in (`content.in`). When the
    other level cannot present, its stock selector opens at once, as before.
  - **A campaign.** Accepting QUAKE 4 or THE AWAKENING continues to its
    difficulty choice.
  - **Arena.** ARENA goes deeper the same way, its own label carrying from
    its row, and Back from Arena returns with the focus on ARENA.
- **MODS** (section 6, the list dialog). MODS on the title's plinth opens the
  title's own dialog instead of the stock pop-up: the modal construction grown
  from 136 u to 280 u, 320 u wide with its centre 17 u above the view's, as the
  modal's is, titled MODIFICATIONS. Its body holds the installed mods (the
  stock list's mods, at most 32) under NAME and VERSION headings and the olive
  rule, on 20 u rows lit as the save lists' are, with a scrollbar, and under
  the list the selected mod's author, release date, required engine and
  directory. LOAD MOD and CANCEL stand where YES and NO do. The session
  publishes the mods as the dialog opens, the current game's selected; the
  dialog opens on CANCEL and moves to the selected mod once its contents show.
  A row's first Accept selects its mod; Accept on the selected mod, or LOAD
  MOD, restarts the engine into it as the stock LOAD MOD does. The wordmark
  dims to 40% gray, as for EXIT. The pause has no MODS.
- **Confirmations** (section 6, the 480 dp confirmation width). EXIT and both
  pause quits ask in the stock confirmation dialog. The screen beneath stays
  in view in soft focus (`modal.softfocus`: a 5 u, 7.5 dp blur at 0.80
  saturation, never dimmed). The glow is cut to the dialog's 320 u
  rectangle: graded only vertically, at half strength at the rectangle's top
  and bottom edges, gone 40 dp beyond them, and fading out over a further
  16 dp past each side. With the opaque-backing option
  (`ui_retainedOpaqueBacking 1`), or on a renderer that cannot soften the
  screen, the stock 0.94 scrim covers it instead, and the glow keeps the stock
  column, exactly the rectangle's width. The stock art's black
  0.70 silhouette sits inside it. The silhouette starts 6.5625 dp in from
  each side, as `popup_top`, `popup_mid` and `popup_btm` do, so a 6 dp lit
  margin and the lit slot outline it. The title starts at the foot of the
  slot's leading flank. Its capitals hang from the raised sections' top line
  into the lit slot, as the stock title's do, with a black outline at 0.85
  extruded 1 dp around the strokes behind the fill. The outline is eight
  opaque copies composited once as a group, so their overlaps never darken it
  past 0.85. The body and the actions sit where the stock ones do: the body's
  first baseline is 87.5 dp down, and the actions start 96 u down. They open
  and close with the stock motion (section 8, `modal.enter` and
  `modal.leave`).
  - Opening brings the soft focus (or the scrim), the glow and the frame in
    over 200 ms, and the title, body and actions appear together at its end,
    when NO takes focus.
  - Closing hides the contents at once. After 50 ms the soft focus (or the
    scrim) and the glow release over 250 ms, and the frame over 200 ms for
    Exit or 250 ms for the pause quit; then the modal closes.
  - The title's Exit modal also dims the wordmark to 40% gray over 200 ms,
    and restores it from 50 ms over 150 ms as it closes.
  - Timeline [completion programs](event-programs.md#completion-programs)
    show the contents and close the modal. A reopen during the leave takes it
    over, so the leave never completes and the modal stays open.

Band motion follows the stock choreography (section 8): opening reveals the
content, choosing a page sends the bands to the page dock (`depart`), a
pop-up only dims the content (`departPopup`), and returning from a page
brings the bands back (`returnHome`). On depart the home plates fade over
250 ms, but the plinth and its secondary links go in 50 ms (`content.out`);
they return together with `content.in`. Control feedback follows the same
section: hover responds at once, focus over 80 ms, a press over 60 ms, and
every return to rest is linear over 300 ms. As the bands dock, the chosen
navigation label travels from its row into the page's title slot at 39,19 u
and settles at the 18 dp screen-title size (`title.carry`, section 13.8),
where the stock page then shows its own title; the session plays it only for
a real page hand-off, and the home state starts without it.

## Hand-off to the stock pages

The title and pause screens cover the legacy main menu while it rests at its
home state; the legacy menu stays active underneath and keeps every page.
Choosing SINGLE PLAYER or SETTINGS runs the legacy home button's own action
(MODS opens the title's own dialog), and the retained screen plays its
departure for the 550 ms (200 ms for a pop-up) the stock page takes to appear.
Back on a legacy page returns to the retained screen as soon as the page starts
to leave. Back on the retained Single Player page runs the legacy menu's own
return from that hand-off (`returnFromSinglePlayer`, as the stock selector
does) unseen, and the menu rests at home at once, so the title comes back
with its bands returning from the page dock
(`idSessionLocal::ReturnRetainedSinglePlayerHome`). UPDATES runs the legacy
button's update check in place: no pop-up opens, so the title keeps its
content and its input until the check's answer arrives as a stock message
box. A stock message box over the home (that answer, a failed connection)
keeps the retained home drawn behind it, still and without input, where the
legacy menu stood, and the home takes the screen back as it was when the box
closes. LOAD GAME, SAVE GAME, RESTART LEVEL, DEMOS, CREDITS and MULTIPLAYER
hand off the same way to their retained pages ([Load Game](#load-game-page),
[Save Game](#save-game-page), [Restart Level](#restart-level-page),
[Demos](#demos-page), [Credits](#credits-page) and
[Multiplayer](#multiplayer-page)); the Strogg pause hands off to the
[Strogg family's pages](#strogg-pause-pages), and the stock page opens when a
page cannot present. The departure fades the plinth
and the prompt bar with it, since a page brings its own
([page prompt bars](#page-prompt-bars)). CONTINUE, RESUME, QUIT TO MENU and EXIT are handled by the session
directly.

Documents request these through a typed `session.menu` operation whose
`command` argument must be in the adapter's allowlist; the session drains the
queued requests in `DispatchCommand` and ignores requests from any view other
than the presented home screen.

## Runtime additions

- **Strogg faces.** Documents may name the `r_strogg` and `strogg` (rune)
  faces besides `marine` and `lowpixel`; any other name still draws in
  Chain. The rune face maps the Latin alphabet and leaves most punctuation
  blank, so the host folds what it lacks for the rune copies. Punctuation it
  has no rune for, combining marks and general punctuation become a space.
  Of the letters past ASCII it keeps Latin-1's, with ß, Æ, Ø, Ð, Þ and Ł
  folded to S, E, O, D, P and L; every other letter, in any script, folds
  onto the alphabet. A translated label therefore never shows the face's `?`
  in its runes, which the gate test checks against every shipped string.
- **Image nodes.** An `image` node draws a relative VFS image or material,
  with `image-fit` (fill, contain, cover), `image-align-x/y`, `image-color` and
  `image-blend` (normal or additive light). Sources are validated, can be bound
  to application strings, and are never animated.
- **Path blends.** Vector paths take `blend: additive` (premultiplied light)
  or `blend: multiply`, drawn through the engine's `_retainedMultiply`
  material. Inside a composition layer, or on a host without the material, a
  multiply path darkens by its luminance instead and is counted.
- **View-height canvas.** A document can declare `canvas: {height: 720}`: one
  canvas height of dp spans the view height at every resolution, replacing
  display density and user scale, so stock 640x480 measurements map to 1.5 dp
  per unit and the 4:3 composition stays centered.
- **Overflow under a transform.** RmlUi clips an overflow container inside a
  transformed group with the container's box geometry instead of a scissor.
  The renderer now applies that clip as an exact rectangle for translation and
  scale; a rotated or rounded clip is limited to its bounds and counted in
  `clipMaskFallbacks`. Before this, such content was never clipped (the loading
  bar lit its whole length at any progress).
- **Additive pictures in layers.** An add-blended picture used to write its
  texture alpha. Inside a composition layer (any ancestor fading through
  `opacity`), the layer then composited the picture's whole rectangle as an
  opaque box: the wordmark darkened its rectangle as the title departed.
  Additive pictures now draw with zero coverage, which leaves their light
  unchanged; `ui_retained_layer_pool.py` checks the conversion.
- **Layer row order on Vulkan.** Opacity below 1 and masks composite through
  engine render textures, which keep GL row order on every backend. Since
  2026-09-25 the Vulkan executor has recorded render textures lower-origin and
  marked 2D writes canonical, but the host still sampled Vulkan layers
  top-origin. Every composite therefore read mirrored, empty rows: on these
  screens the navigation and action plates, the markers and the modal
  title's outline vanished on Vulkan, and the SYSTEM page's faded and masked
  parts share the same path. No release shipped it. Composites now sample GL
  row order everywhere. `ui_retained_layer_pool.py` checks the composite
  coordinates and pins the executor's side of the contract.
- **Backdrop soft focus** (REN-016). A node's `backdrop-blur` and
  `backdrop-saturate` soften the frame beneath it (see the
  [document format](document-format.md#typed-values-and-layout)). RmlUi reads
  the backdrop into a pushed layer through its backdrop filters, and the
  runtime hands that layer to `Host::SoftenBackdrop`. The engine binds the
  base surface and copies the window into `_retainedBackdrop`, then runs the
  `glsl/retained_blur.glsl` program twice. The first pass blurs
  horizontally into a scratch target, over every row the second reads (3
  sigma beyond the region). The second blurs vertically and desaturates into
  the backdrop layer. Each pass is opaque and takes up to 49 bilinear taps,
  spaced out for a wide sigma. OpenGL needs GLSL support; Vulkan compiles the
  program at run time; GLES keeps the fallback. The capture and both targets
  keep GL row order, as the layer composites do.
  `ui_retainedSoftFocus` reports whether a frame can be softened, and the
  documents show the scrim when it cannot. `ui_retainedProfile` counts the
  backdrop passes and fallbacks. The SYSTEM page's Apply/Keep/Revert
  confirmation and unapplied-changes dialogs soften the screen beneath them
  too, keeping their 0.6 backing as the fallback
  (`tools/ui/retained_system.py`).

## Pictures and precaching

A document's own pictures resolve when it loads. With the gate on at startup,
the title and the pages it reaches load with the stock menus, the Load Game
page with the generic picture a save without one shows, and each
single-player level load resolves the pause documents, the pages only they
reach and the restart screen (each once a session), and the level's
levelshot, so opening a screen reads no file mid-frame
([startup preload](runtime-performance.md#startup-preload)). Every
multiplayer map's levelshot and every Arena match's precache on the front
end's idle frames, and Create Server and the Arena page each read their own
still waiting when they open ([Create Server](#create-server-sub-page),
[Arena page](#arena-page)). `_retainedMultiply` is a generated material and
needs no precache. These preloads and precaches run as a load, so they report
no late loads, and an engine reload (a campaign's game directory, an Arena
match's game module) resolves them again, the levelshots at once. A picture
first drawn outside a load, such as a document opened with `testGUI`, the
gate switched on in the menu, or a save's own screenshot (as on the stock
page), still loads on demand and reports it once.

## Diagnostics

`ui_retainedStatus` prints one `OPENQ4_RETAINED` line: the gate, the SYSTEM
and multiplayer opt-ins, the presented home screen, which home documents are loaded, whether a
hand-off is in progress, whether a closed pause screen or multiplayer card
is releasing the softened view, the multiplayer card covering the game's menu
(`mp=escape`), the number of live retained views (0 while the gate is off)
and, after `stock=`, the documents that fell back to their stock
screens this session (`-` when none). `ui_retainedTrace 1` also logs each
session request with whether the adapter accepted it, and each loading phase as
`RETAINED_LOADING_PHASE`. With `developer 1` the log also names the reason for
each quiet fallback.

## Load Game page

LOAD GAME on the title and on the Marine pause opens
`guis/menu/loadgame.q4ui` (FLOW-004), generated by `tools/ui/retained_saves.py`.
The Strogg pause opens the same page in the Strogg family
([Strogg pause pages](#strogg-pause-pages)).

- **Hand-off.** The home hands off as to a stock page: the bands dock and the
  LOAD GAME label carries into the title slot while the content, the plinth
  and the prompt bar leave. At 550 ms, when the stock page would appear, the
  session presents the page (`BeginRetainedPage`, `UpdateRetainedPage`). A
  stock page lives in the main menu over its home's backdrop and docked bands,
  so this page is the stock page's content alone, and the home keeps drawing
  behind it as the departure left it (`DrawRetainedPageHome`): the title's
  levelshot montage, or the pause over the softened game. The carried label
  is the page's title. The page's plates show at once and its panel and
  backing fade in over 150 ms (content.in). Back returns the home at once with
  the bands, as from a stock page, and the carried title leaves at once, as
  `anim_loadOut` drops the stock page's. The page loads with the stock menus
  at startup, so the click never waits on its 0.1 to 0.2 s load; with the gate
  switched on later, it loads at the first click.
- **Composition** (section 9). The selected save's picture, 183x137 u at
  25,78 u, fitted inside a 1 u #414624 frame on black 0.60, with its date and
  time on one line at 37,215 and 128,215 u, then the difficulty at 37,230 u and
  the run's play time at 37,245 u, the Demos page's three detail lines
  (Lowpixel 18 dp; [save details](#save-details)). LOAD GAME and DELETE GAME are
  section navigation plates: `b2_dark` at 21,256 and 20,293 u (Appendix B.1),
  208x52 u, with the band rows 13-37.4 u, the lower-leading cut from row 32,
  the #CCCC51 rail and the fill opaque to 57 %, half at 72 % and clear by
  94 %. A section plate's target is its 30 u pitch around the band (45 dp,
  section 3), from the leading rail to where the plate fades out or its
  widest label ends. Its art takes no hits, so the target stops short of the
  list, and the plates follow the page's full-size panel in the document,
  whose box would otherwise take their hits. The list is at 217,78 u,
  410x325 u, under SAVE NAME and LOCATION (Lowpixel 13 dp at 0.40) and an
  olive rule at 0.26. Its 20 u rows put the
  name and the level at the stock tabstops, 0 and 180 u, in Lowpixel 15.8 dp
  on `bg_line`'s light: #FF9C00 at 0.01, 0.16 under the pointer or focus and
  0.41 for the selected save, with the white 0.50 rule. The focused row takes
  the home plates' orange inset, and the scrollbar sits in the list's last
  16 u. The prompt bar is in the bottom band, Back at 532,441 u
  ([page prompt bars](#page-prompt-bars)). The stock page's backing (`grad2`: `bg_darkgrad2`
  black at 0.60 across the view, clear to 18.4 % of its width and opaque from
  34 % to 91.8 %) darkens the trailing side.
- **Saves.** The session lists the saves newest first, as LoadGame accepts
  their headers. Each is named from the label it wrote (`^:name\t^0level`,
  colors removed), or by its slot. An autosave shows its level's loadscreen;
  any other save shows its own screenshot, reread when chosen; one with
  neither shows the stock page's generic picture. The list holds 64 rows.
  With more saves, NEWER SAVES and OLDER SAVES rows page by 64, selecting and
  focusing the first save of the older page or the last of the newer one.
- **Choosing.** The page opens with the newest save selected and focused. A
  row's first Accept selects its save; Accept on the selected save, or LOAD
  GAME, loads it. DELETE GAME asks with the stock confirmation, focused on NO;
  YES deletes the save with its picture and label, and the save that takes
  its place is selected. LoadGame refuses a save it cannot use before it
  leaves the current game, and records why (`loadGameFailure`). The page then
  stays, and the reason shows over the foot of the picture in #E46D56 until
  another save is chosen: the level is not installed, the save is damaged or
  comes from an incompatible version, or it cannot be read. A save still
  there after DELETE GAME stays selected and says it could not be deleted.
- **No saves.** The list's place shows the empty state of section 13.13: NO
  SAVED GAMES, one sentence saying where saves come from and, at the title,
  NEW GAME, which opens the Single Player page. The plates rest disabled.
- **CONTINUE.** The title's CONTINUE line showed the save's raw label, color
  codes and tab included. It now shows the save's name, then its level, the
  difficulty and the run's play time, or when a save from an earlier build
  was written ([save details](#save-details)). A newest save that LoadGame
  refuses opens this page on it with the reason, as LOAD GAME on it would,
  instead of nothing.
- **Text.** The page adds `#str_231100` to `#str_231107` to all twelve tables.
  The generator measures every language's text in its box and fails
  otherwise: the plates, headings and pager rows on one line, the empty
  sentence on two and a reason on the strip's three. NEW GAME's plate widens
  to its longest translation.

`ui_retainedTrace` logs `RETAINED_PAGE present` when the page presents. The
capture scenarios `loadgame` (three saves made in the level, then the title:
select, delete, Back, load, and the page over the pause),
`loadgame-empty`, `loadgame-failure` (a seeded save the list accepts and
LoadGame refuses) and `loadgame-pages` (70 saves) drive it by console
command. Hyphenated ids such as `row-1` must be quoted there, since the
console tokenizer splits an unquoted `-`. On a shared machine,
`--set win_allowMultipleInstances=1` lets a capture start beside another
openQ4 client; without it, a second client exits at once, leaving no log.

## Save details

A save names its difficulty and the run's play time (UI-03; FLOW-024 and
FLOW-048), and the objectives completed on its map survive loading it. None
of this changes the save format.

- **Recording.** `idPlayer::SavePersistantInfo` adds `openq4_playSeconds` and
  `openq4_skill` to the persistent player info, which carries the inventory
  from one map to the next and which every save header already holds for
  each client. The play time is the time the persistent info carried into
  the map plus the map's game time, so pauses and menus never count. A new
  game (empty persistent info) starts it at zero; a player who re-enters the map (`playerModel`) keeps the value it
  arrived with. A run that a save from an earlier build began has no play
  time, and keeps none until a new game: the menus show nothing rather than
  a partial total. Loading an earlier save, or restarting the level from its
  start autosave, rewinds the play time with the rest of the run.
- **Showing.** The menus read the first client's persistent info from the
  save's header (`Session_MenuReadSaveGameHeader`), as the list already
  parses each header to accept a save. The title's CONTINUE detail is the
  mission, the difficulty and "1:02:05 played" (`#str_231525`, a format
  each table orders), joined as the language joins a list ([joined
  lines](#joined-lines-and-capitals-24-october-2026)); a save that records
  neither says when it was written instead, which also drops the old
  unlocalized hyphen. The Load Game page puts the date and time on one line under the
  picture, then the difficulty and the play time; both stay empty for a save
  from an earlier build. The pause's level block gives the mission time and
  the total (`#str_231524`, "3:05:06 total") on one line and the newest
  save's age on the next; the levelshot gives up 12 dp so the third
  objective row keeps its place.
- **Objectives.** `idPlayer::CompleteObjective` records each completed
  objective's title among the player's spawn arguments
  (`openq4_objectiveDone<n>`), which every save writes, and `idPlayer::Restore`
  lists them again. The inventory still does not save them, so older saves
  restore none.
- **Checks.** The generator measures the difficulty names and the played
  phrase with a 999-hour time in every language, and the date and time in
  their shared line. The session gate covers CONTINUE, the Load Game page
  and the pause with and without the details; the screens test lays out the
  Load Game page's detail lines clear of LOAD GAME's band and the pause's
  two time lines under the third objective row. The `save-details` capture
  completes airdefense1's first objective, saves, loads that save and opens
  the pause, takes the level change to airdefense2 and pauses again, saves
  there, and shows the title and the Load Game page on both saves. On a
  machine busy with other builds the waits can run out before the title
  and the page arrive; the German Vulkan run took those two screenshots
  early, and the English OpenGL run covers them.

## Save Game page

SAVE GAME on the Marine pause opens `guis/menu/savegame.q4ui` (FLOW-004), also
generated by `tools/ui/retained_saves.py`. The title has no game to save, and
the Strogg pause opens the Strogg family's page
([Strogg pause pages](#strogg-pause-pages)).

- **Hand-off.** As the Load Game page's: the pause docks its bands and carries
  SAVE GAME into the title slot, the page presents at 550 ms over the pause,
  which keeps drawing behind it, and Back returns the pause with the bands.
  The page loads with the first single-player level. Each arrival starts with no
  name and no save selected, as the stock page's field did, and focuses the
  name.
- **Composition** (section 9, the stock `p_savegame`). The saves fill the
  content column from 35,78 u, 570x285 u, under SAVE NAME and LOCATION and the
  olive rule, on the Load Game page's 20 u rows, here 550 u wide with the
  scrollbar in the last 20 u. Under them the name plate (`b1_dark` at 4,370 u,
  484x52 u, at 0.40: the band rows 13-37.4 u starting 33 of its 512 texels in,
  the rail up the leading edge, down the cut and along the foot, the fill
  opaque to 44 %, half at 62 % and clear by 88 %) holds the name field at
  49,382 u, 344x20 u, in Lowpixel 20 dp (textscale 0.28). SAVE GAME is the
  section plate at 397,370 u; its label may run past the plate's faded end
  toward the canvas edge, as the stock one did in French. The prompt bar is
  in the bottom band, Back at 532,441 u ([page prompt bars](#page-prompt-bars)), over
  the stock page's backing.
- **The name.** A [text field](numeric-fields.md#text-fields) of up to 32
  bytes. Typing into it opens it, as Accept does; an empty field shows "Name
  this save" in white 0.35, and while it is edited empty the line above the
  plate asks for a name in #E46D56. Enter writes the name into the page's
  `save.name` (`ui.state.set`) and saves under it, as the stock field's
  onEnter did.
- **Choosing.** A row's first Accept selects its save and puts the save's name
  in the field, unless a name is being typed there, which a selection never
  replaces. Accept on the selected save, or SAVE GAME, saves under the name.
  With a name typed but not yet entered, SAVE GAME returns the focus to the
  field, where Enter saves. SAVE GAME rests disabled without a name.
- **Saving.** A name no save holds saves at once; the new save becomes the
  newest, selected, and the name stays. A name a save already holds asks
  first with the stock OVERWRITE SAVE? dialog, focused on NO, its body naming
  the save in orange; YES saves over that save even if the field has changed
  since. An autosave is refused with the stock WARNING notice, "You cannot
  save over an autosave.", and CLOSE alone. A save the session refuses says
  why in the same notice instead of the stock message box: the player must be
  alive, the drive must have space, a failed mission cannot be saved, and
  anything else reads "The game could not be saved." The stock page's own
  overwrite check now shares the page's (`Session_MenuSaveSlotState`).
- **Text.** The page adds `#str_231108` to `#str_231113` to all twelve tables;
  the last three carry the stock code table's reasons, which the Polish and
  Russian tables lack. The generator checks every language's text in its box.
  The overwrite title runs on over the dialog's raised trailing section, as the
  stock title ran past its rectangle, because Italian and Spanish overrun the
  lit slot; the notice's lone CLOSE is 210 dp wide for German.

## Restart Level page

RESTART LEVEL on the Marine pause opens `guis/menu/restartlevel.q4ui`,
generated by `tools/ui/retained_difficulty.py`. The Strogg pause opens the
Strogg family's page ([Strogg pause pages](#strogg-pause-pages)), and the
title has no level to restart.

- **Hand-off.** As the Load Game page's: the pause docks its bands and carries
  RESTART LEVEL into the title slot, and the page presents at 550 ms over the
  pause. It loads with the first single-player level and opens at the level's
  difficulty (`g_skill`), focused.
- **Composition** (section 9 "New game", the stock `p_newgame`, which openQ4's
  in-game Difficulty page retitled). Five section plates (`b2_dark`) on a
  30 u pitch from 21,172 u: PRIVATE, CORPORAL, LIEUTENANT, GENERAL and
  WARLORD. The chosen one is lit at full strength with an orange label,
  whatever its feedback, as the stock `skillCheck` lit it. Its description
  stands at 239,181 u in Lowpixel 18 dp (textscale 0.25) at 0.60. RESTART
  LEVEL is the enlarged primary plate (`b6_light` at 237,222 u, 356x43 u: the
  band rows 4-27 of 32, a 45-degree cut from row 14, the olive rail and fill
  at 0.49 fading out by 95 %, at 0.40 and full strength under the pointer or
  focus) with its 18 u corner marker and the Marine label (textscale 0.33).
  The prompt bar is in the bottom band, Back at 532,441 u
  ([page prompt bars](#page-prompt-bars)), over the stock page's backing.
- **Choosing.** A plate chooses its difficulty on the page; nothing reaches
  the session. RESTART LEVEL restarts the level at the choice
  (`RestartLevelAtSkill`, with the autosave's loadout), and Back leaves the
  level's difficulty as it was. The in-game page offers no turbo mode, as the
  stock one hid it.

## Strogg pause pages

After Kane's stroggification the pause's SAVE GAME, LOAD GAME and RESTART
LEVEL open the same three pages in the Strogg family (section 13.7):
`guis/menu/savegame_strogg.q4ui`, `loadgame_strogg.q4ui` and
`restartlevel_strogg.q4ui`, generated with the Marine pages by
`tools/ui/retained_saves.py` and `retained_difficulty.py` (their `STROGG`
family).

- **Construction.** The pages keep the Marine pages' layout, controls and
  verbs; only the family changes. The section plates, the name plate and
  RESTART LEVEL's primary plate trade the 45-degree lower-leading cut for a
  30-degree shoulder 7 u tall, and the corner markers for the Strogg pause's
  slanted ones. Rails, the list's rule and scrollbar and the picture's border
  take #F59512 and #FF9000; the rows light #FF9000, ruled in #FCFFC8.
  Labels, headings and the pager rows are R_Strogg in #FCFFC8 (headings at
  0.45), turning #FFCC00 with the plate's focus inset and the row's first
  column. The saves' names, the date, the descriptions and the typed name
  stay Lowpixel, tinted #FCFFC8. The prompt bar takes the Strogg pause's
  #FFCC00 glyphs and R_Strogg verbs. DELETE GAME, the overwrite question and
  the save notice ask in the family's confirmations, as the Strogg pause's
  own do; the overwritten save's name stays Lowpixel, in #FFCC00.
- **Hand-off.** The Strogg pause carries the chosen label in R_Strogg as for
  any page, and the page presents at 550 ms. The first Strogg answer of a
  level (`RetainedPauseIsStrogg`) queues the three documents on the job
  workers, so the page the player chooses is usually parsed before it is
  needed; each loads on its first use and stays for the session. Back
  returns the Strogg pause, which plays its translation again.
- **Falling back.** A Strogg page that is not installed, cannot load or
  stopped drawing opens the stock page, never the Marine page, whose family
  would break.
- **Captures.** `pages-strogg` saves twice in game/recomp, then drives SAVE
  GAME (typing a name and saving), LOAD GAME (selecting a save, asking to
  delete it and declining) and RESTART LEVEL (choosing a difficulty) from the
  Strogg pause, returning each time.

### Strogg confirmations

The Strogg pause and its pages ask in the Strogg family: QUIT TO MENU and
EXIT on the pause, and DELETE GAME, the overwrite question and the save notice
on the pages. `confirmation(..., strogg=True)` in
`tools/ui/build_retained_screens.py` applies section 13.7's Strogg rules to
the stock modal construction of section 6. The visual specification defines
no Strogg dialog of its own, so the construction below derives from those
rules.

- **Silhouette.** The dialog keeps the stock rectangle, inset and title slot.
  The slot keeps its 17 dp depth, but its flanks fall at 30 degrees (29.4 dp
  along). The lower-leading chamfer becomes a 30-degree shoulder over the same
  38 dp run. It is filled with the Strogg level card's #0A0503 at 0.85 inside
  a #F59512 rail at 0.75. The glow column, cut to the dialog over soft focus
  as in the stock modal, takes the Strogg backdrop's #6B2A0E light.
- **Text.** The title is R_Strogg 20 dp in #FCFFC8, with the stock title's
  tracking and 1 dp black outline, hanging from the raised top line into the
  lit slot. A title too long for the slot runs on over the raised trailing
  section, as on the stock dialog. The body is R_Strogg 15 dp in #FCFFC8 at
  0.85 on the stock body's 22 dp lines, so every shipped language still fits
  two lines above the actions.
- **Actions.** The actions are the stock 180 dp action plates in the Strogg
  family. The 20 dp lower-leading cut becomes a 30-degree shoulder over the
  same run, with a black fade fill, a #F59512 rail and a slanted marker. Their
  R_Strogg labels turn #FFCC00 with the focus inset.
- **Translation.** While the dialog's contents are hidden, every line returns
  to runes. When modal.enter completes and the contents show, they play the
  short credits translation. The title, outline and fill as one, translates
  over 300 ms. The body fades in from 60 ms. YES and NO, or CLOSE, follow
  45 ms apart from 100 ms, over 200 ms each. Reduced motion shows the
  translated dialog at once.
- **Behavior.** Ids, events, focus, the soft focus, modal.enter and
  modal.leave are the stock dialog's, so the session and its tests treat both
  families alike. Every Marine document is unchanged.

The native screens test checks the faces, the rune copies, the order of the
translation, NO's focus color, reopening in runes and reduced motion on the
Strogg pause, and each page's dialogs in their family. The `confirm-strogg`
capture saves in game/recomp and then, from the Strogg pause, asks QUIT TO
MENU mid-translation and settled (NO, then YES focused) and EXIT. It then
types an existing save's name to raise the overwrite question, declines,
and resumes.

## Arena page

ARENA on the Single Player page opens `guis/menu/arena.q4ui`, generated by
`tools/ui/retained_arena.py`: the Arena campaign's browser as Single Player's
second sub-page (section 9 "Arena Campaign", FLOW-011), in the sub-page state
Campaign holds, titled ARENA under the SINGLE PLAYER crumb, over the stock
page's backing.

- **Change.** ARENA plays Campaign's change with the ARENA label carrying
  from its row, and the browser presents at 350 ms; Back plays the page's
  half and Single Player returns at 300 ms with the focus on ARENA (the
  session sets `from_arena` first). Back while the page goes deeper reverses
  the change as for Campaign. Where the campaign must first reload the engine
  into its game directory or game module (after an Arena match, say), ARENA
  selects it as before and the page presents once the engine is back.
- **Authority.** `idArenaCampaign` publishes the browser's state into the
  stock browser and, while it is loaded, this page (`PublishGui`), its words
  already in the player's language, and opens this page where it can
  (`OpenBrowser`, through `idSessionLocal::RetainedArenaPage`). The page's
  verbs reach the campaign's own commands (`RETAINED_ARENA_VERBS`), which
  check each again, and the campaign's events (the entrance, the result, the
  reset question) reach whichever browser presents. With the gate off, or
  when the page is not installed, cannot load or stops drawing, the stock
  browser presents as before.
- **Ladder.** The five tiers form a vertical ladder of plates in the section
  navigation at 16,72 u (170x40 u on a 44 u pitch), and the chosen tier's
  matches a column beside it at 196,72 u: three qualifiers (36 u), then the
  boss match on a larger 50 u plate whose orange edge shows while it waits.
  Each plate is the section plate's dark band, rail, corner marker and Marine
  label (QUALIFIER 18 dp, TIER 20 dp, the boss 22 dp across the plate) with
  the published status under it: LOCKED, AVAILABLE, BOSS READY or COMPLETE. A
  locked one is dimmed with the lock and cannot be chosen; a completed one
  carries the check and a green edge; the chosen one is lit over an orange
  wash. A plate's first Accept chooses it; Accept on the chosen tier moves to
  its chosen match, and Accept on the chosen match starts it. The bumpers (and
  Q and E) step to the next open tier either way, unless a modal or the
  entrance holds the page (`arenaStepTier`).
- **Briefing and detail.** Under the matches the chosen tier's briefing: its
  name, description, the path to the next tier and the rule. The detail column
  at 404 u shows the match's levelshot, name, description, map, game type,
  roster, limits and skill, then DIFFICULTY between its - and + steps (each
  out at its end of the range) and START MATCH, the enlarged primary plate,
  live only while the campaign can start the match. Under the ladder,
  CAMPAIGN PROGRESS with its count and a bar filling by a twentieth a match,
  CAMPAIGN COMPLETE once the last boss falls, and RESET PROGRESS.
- **Best result.** An unlocked match's best result crosses the foot of its
  levelshot inside the border, as the stock browser's band does: BEST RESULT
  over the record the campaign publishes (`arena_selectedBest`), in amber
  where there is one and dim while it reads No result yet. The column is
  narrower than the stock browser's, so the label stands over the record
  instead of beside it; generation checks both, and the widest record
  (result, two-digit score, difficulty), in every language. A replay that
  beat the record shows NEW BEST on the ledger's score row.
- **Reset.** RESET PROGRESS, live while there is progress or a best result
  to clear (`arena_canReset`), asks the
  campaign, which raises `arenaResetReveal`; the stock modal, grown 14 u and
  titled RESET PROGRESS, says what it clears and asks on CANCEL. RESET clears
  the progress; CANCEL or Back keeps it.
- **Entrance.** START MATCH (the campaign's `arenaMatchLaunch`) plays the
  stock entrance on the page: the screen darkens to 0.72 over 320 ms as a
  section 6 card (12 and 3 dp cuts, the stock panel's orange rail) scales in
  from 94 % with the match's levelshot dimmed under it, ENTERING THE ARENA, the
  match, an orange rule, its game type and map and PREPARE FOR COMBAT; from
  760 ms the card fades and the screen goes to black for the campaign's
  hand-over at 1250 ms. While the campaign holds the entrance
  (`arena_transitioning`) every control is out and the session takes no
  request, Back included.
- **Result.** Back from a match the campaign opens the page and raises
  `arenaResultReveal`: the stock ledger as the modal grown to the stock
  report's 396 u, titled MATCH REVIEW and revealed in the stock phases from
  the black the match ended on, which lifts over 720 ms. The result shows over
  its green, red or amber accent with the match from 260 ms, the message and
  score from 610 ms, what it unlocked and the awards (or CAMPAIGN COMPLETE,
  on the win that completed the campaign: `arena_resultUnlockedCampaign`)
  from 930 ms, an award line taking a second line where it needs one, and at
  1260 ms the actions, with the focus on the second: RETRY after a defeat or
  an aborted match (the opponents left, or the player abandoned it) or REPLAY
  after a win or a draw, where the match can be played again, then NEXT
  after a win short of the last, RETURN, or CONTINUE after an aborted match
  that cannot be played again. Back dismisses it. The campaign holds every
  other browser command while the report or the reset prompt is open.
- **Pictures.** As it loads with the menus, the campaign queues every
  match's levelshot for the front end's idle frames, which precache a few a
  frame once a menu has presented for a second, and the page reads any still
  waiting when it opens, so choosing a match never loads one. The session's
  retained preloads and those precaches run as a load
  (`declManager->SetInsideLoad`), so they report no late loads, and an engine
  reload (an Arena match's game module, a campaign's game directory)
  precaches the levelshots at once, inside its load.
- **Captures.** `arena` drives the page from Single Player with progress
  set, `arenamatch` starts a real match and disconnects from it, and
  `arenafail` holds the server's port (on the loopback interface, which
  `net_ip` names for it) so the launch fails and the page reports MATCH
  ABORTED. The campaign appends its launch behind the capture script, so a
  match stage continues through the after-load hook (`@aftermap`) or through
  appended `vstr` steps (`exec` would insert ahead of the launch), and the
  reloaded game module logs to `logs/openq4-match.log`.

## Demos page

DEMOS on the Multiplayer page goes one level deeper to
`guis/menu/demos.q4ui` (FLOW-013, FLOW-050), generated by
`tools/ui/retained_demos.py`: the Load Game page's construction for the demo
library that openQ4's legacy browser (`guis/demo_menu.gui`) shows. During
playback, or when the sub-page cannot present, the legacy browser opens over
the Multiplayer page and returns to it; playback keeps its legacy controls.
Until 2 November 2026 the title's plinth carried DEMOS and the page stood at
the page level ([Demos as a Multiplayer sub-page](#demos-as-a-multiplayer-sub-page-2-november-2026)).

- **Hand-off.** As Create Server's, from the Multiplayer page: the page's
  plates fade while it sweeps away, MULTIPLAYER becomes the crumb, the band
  steps and DEMOS carries to the step's foot, and at 350 ms the sub-page
  presents with its panel and backing fading in (content.in). Back climbs
  out onto DEMOS. The page loads with the stock menus at startup.
- **Composition** (sections 9 and 13.13). The selected demo's map in the stock
  preview, 183x137 u at 25,78 u (its levelshot as the pause's level block
  finds it; render demos have no map and show the generic picture), with its
  kind, status and details below it in Lowpixel 18 dp; a status this build
  cannot play is in #E46D56. PLAY, DELETE and REFRESH are section plates on
  the stock 37 u pitch from 256 u. The list is at 217,78 u under NAME, MAP,
  DATE and DURATION and the olive rule, its rows on the stock 20 u pitch.
  Above the headings the filter tags (section 13.13) stand at 43 u: small
  header-band plates with a 45-degree cut and an olive rail, 0.28 unset and
  0.60 with a close cross when set, lit under the pointer or focus. The
  prompt bar is in the bottom band, Back at 532,441 u
  ([page prompt bars](#page-prompt-bars)), over the stock page's backing.
- **Library.** The session rescans `demos/` for every publication and lists
  the recordings newest first, filtered: MULTI-VIEW, RENDER, LEGACY or
  INCOMPLETE, the legacy browser's filters, or every recording with no tag
  set. A tag sets its filter, and Accept on the set tag clears it. The list
  holds 64 rows and pages through more with NEWER DEMOS and OLDER DEMOS rows,
  as the Load Game page does. Each row and detail line is capped as the legacy
  browser caps it.
- **Choosing.** The page opens with the newest demo selected and focused. A
  row's first Accept selects its demo; Accept on the selected demo, or PLAY,
  plays it where it can play, through the legacy browser's own player. A demo
  that could not start leaves the page here, saying so over the foot of the
  picture. DELETE asks with the stock confirmation, which names the demo;
  YES deletes the recording, which must lie in the save path's demo
  directory, and the demo that takes its place is selected; one that could not
  be deleted stays selected and says so. REFRESH rescans and keeps the
  selected demo wherever it now stands.
- **No demos.** The list's place shows the empty state: NO DEMOS and one
  sentence saying where demos come from, or that the filter hides them all.
  The plates rest disabled, and REFRESH takes the focus.
- **Text.** The page adds `#str_231114` to `#str_231120` to all twelve tables;
  the generator measures every language's text in its box and fails
  otherwise.

## Restart screen

When the player dies in single player and the respawn wait runs out, the game
reports the death (`died`), the level unloads, and `guis/menu/restartmenu.q4ui`
(FLOW-005, generated by `tools/ui/retained_death.py`) presents in place of the
stock `guis/restart.gui`.

- **Composition.** The stock screen's: black at 0.94; the QUAKE 4 wordmark as
  the title draws it, the additive q4text in its 258x63 u box at 145,91 u;
  the popup art 360 u wide at 140,129 u to 341 u, lit
  by its glow column and with no title in its slot; and four enlarged primary
  plates (`b6_light`, 324x38 u from 159,162 u on a 40 u pitch, their Marine
  labels at textscale 0.33 and 14 u corner markers): RESTART, LOAD GAME, MAIN
  MENU and EXIT GAME. The plate under the pointer or focus explains itself in
  the line at 362 u (Lowpixel textscale 0.20, centred), each with the stock
  screen's sentence; the line belongs to its plate, since a control's feedback
  reaches only its own subtree.
- **Choosing.** RESTART has the focus on arrival. RESTART, LOAD GAME and MAIN
  MENU run the stock screen's own commands (`HandleRestartMenuCommands`):
  restart from the last checkpoint or autosave, else the map; the main menu at
  its Load Game page; or the main menu. EXIT GAME asks with the stock exit
  confirmation, focused on NO.
- **Session.** `OpenRestartMenu` presents it, loaded with the pause so a death
  never waits on its load. With the gate off, without the file, or after its
  view stopped drawing (then for good), the stock screen presents instead.

## Credits page

CREDITS on the title opens `guis/menu/credits.q4ui`, generated by
`tools/ui/retained_credits.py`, as LOAD GAME opens its page.

- **Source.** The generator reads openQ4's stock `p_credits` for its layout and
  choreography, never its words: the Raven logo's light-up (`anim_credits`),
  then thirteen screens of headings and names, each line twice, in the rune
  face and in Lowpixel, with an orange scan bar behind each heading. It
  compiles each screen's `onTime` script (its sets, and its colour,
  `textscale` and rect transitions, a later transition retargeting an earlier
  one from where it stands, the stock rule) into tracks, and lays the logo and
  the screens end to end in the stock order (1-8, 11, 12, 10, 9, 13) in one
  timeline of about 111 s that repeats. A chain of completion programs back to
  the logo would be a cycle, which documents may not hold.
- **Words.** Every line shows `credits.<window>`, which the session fills from
  the legacy main menu's credits window of the same name when the page opens
  (`PublishRetainedCredits`, which finds the lines through
  `UI_RetainedStateNames`), so the generated page carries none of the names.
- **The credits translation** (section 8). The runes fade in over 2000 ms; then
  each heading translates from runes to Latin over 1000 ms as its scan bar
  slides in from the leading edge and dims, each name follows 50 ms later over
  500 ms, settling from white to #D5FFA7, both copies popping 0.02 textscale and
  settling, and the screen leaves 640 u toward the trailing edge over 500 ms.
  The scan bar is an additive #FF8000 ramp brightening to its head, with soft
  edges. The logo is the stock picture, tinted from black to #D6FFA8 and back
  as additive light.
- **Page.** The page stands on the stock backing at full strength (section 8:
  opaque for Credits) under its own CREDITS title, which fades in with the
  panel (content.in) since nothing carried it from the plinth; the prompt
  bar's Back, alone, is at 532,441 u ([page prompt bars](#page-prompt-bars)). Every arrival starts the loop over from the logo, and leaving
  stops it with every screen out. In game, or when the page cannot present,
  the stock page opens as before.

## Multiplayer page

MULTIPLAYER on the title opens `guis/menu/multiplayer.q4ui`, generated by
`tools/ui/retained_mp_page.py`, as LOAD GAME opens its page: the stock
Multiplayer page's level (section 9 "Menu levels", FLOW-050).

- **Page.** It holds the page state Single Player holds: the bands docked,
  MULTIPLAYER in the top band's title slot at 39,19 u, where the title's carry
  lands, the sections on the home navigation plates from 181 u (the stock
  page's first button) with BACK under them, and the page's description in
  the content column at 395,190 u. The sections are the stock page's GAME
  BROWSER as JOIN GAME, CREATE SERVER, DEMOS (the demo library, which
  section 9 puts here) and SETTINGS as PLAYER SETUP (section 13.7's name,
  which keeps it apart from the title's SETTINGS). The stock
  FRIENDS LIST, which the stock menu hides and nothing handles, is gone. The
  generator fails when a language's label or description would not fit.
  Arriving from the title focuses JOIN GAME, the primary action.
- **Summaries.** As on the Single Player page, each section summarizes
  itself in the content column while it is the subject, and BACK, or no
  subject, keeps the page's description. CREATE SERVER stands over the
  levelshot of the map it would host (`si_map`), which the session publishes
  as the page opens (`summary.create.shot`) and takes out of the idle-frame
  queue; JOIN GAME, DEMOS and PLAYER SETUP have no levelshot that fits.
  Player Setup's editor hides the column and its picture.
- **Sections.** CREATE SERVER goes one level deeper to the retained
  [Create Server sub-page](#create-server-sub-page), JOIN GAME to the
  retained [Join Game sub-page](#join-game-sub-page) and DEMOS to the
  retained [Demos page](#demos-page) where the session offers them. PLAYER
  SETUP edits in place: the sections give way to its editor
  ([Player Setup edits in place](#player-setup-edits-in-place-2-november-2026)).
  Where a sub-page cannot present, JOIN GAME and CREATE SERVER open their
  stock pages, DEMOS opens the legacy demo browser over the page, and a
  Player Setup whose preview cannot present opens the stock Settings page.
  The page leaves for a stock page as the stock Multiplayer page leaves for
  one: its plates fade over 150 ms while it sweeps 640 u toward the trailing
  edge over 300 ms, the title and prompts going with them. At 500 ms the legacy menu takes the screen in its
  Multiplayer page state. The session first runs the menu's timelines up to
  now, then writes `desktop::curr` 3 (which keeps the menu's `onActivate` from
  resetting it home), `desktop::active` 1 and the page (`desktop::dest` 14,
  17 or 18); the menu's `openMultiplayerPage` then places that page and plays
  its arrival with the bands docked. The session writes these as the menu's
  own `set` does, taking each variable off its declared expression: a value
  written without that is restored to the declared 0 by the menu's next
  evaluation, and the title would come back.
- **Back.** A stock page's BACK, Escape or video-warning path leaves as
  before. Where its out-animation used to bring the stock Multiplayer page
  back, it asks the session (`multiplayerOpen`), which presents the retained
  page at once with the focus on the section that led there, or, where the
  page cannot present, sends `openMultiplayerStock` for the stock page. BACK on
  the page returns the title: as from any page before a stock page has opened,
  and after one the legacy menu leaves its Multiplayer page for home
  (`leaveMultiplayerPage`) while the title returns from the page dock.
- **Fallback.** In game the stock menu keeps its pages. Where the page is not
  installed, cannot load or stops drawing, MULTIPLAYER runs the stock button
  and the stock pages return to the stock Multiplayer page. A main menu
  without the three events the page's routes run (a mod's copy of an older
  one, say) keeps its own Multiplayer page the same way, quietly, as a home
  screen keeps a main menu that lacks one of its pages.
- **Captures.** `multiplayer` opens the page, CREATE SERVER and its BACK, JOIN
  GAME's retained browser on the LAN source (`openq4_browser source lan`, so
  no master server is asked) and its BACK, PLAYER SETUP and its CLOSE, then
  BACK to the title. `playersetup` edits a player in place, and `demos` and
  `demosback` go down to the Demos page and climb back out.

## Create Server sub-page

CREATE SERVER on the Multiplayer page goes one level deeper to
`guis/menu/createserver.q4ui`, generated by `tools/ui/retained_create_server.py`,
as Single Player's CAMPAIGN goes to Campaign (section 9 "Menu levels",
FLOW-050). It offers everything the stock page does, so it is the default
under `ui_retained`: the session's list of what it lacks
(`RETAINED_CREATE_SERVER_MISSING`) is empty, and while anything is listed
there only `ui_retainedMultiplayer 1` offers the page.

- **Change.** The Multiplayer page plays `subpageEnterCreate`: its plates fade
  over 150 ms while it sweeps 640 u toward the trailing edge, MULTIPLAYER
  becomes the crumb, and from 50 ms the top band steps one notch while the
  CREATE SERVER label carries from its row to the step's foot. The sub-page
  presents at 350 ms, its content and backing fading in over 150 ms with the
  focus on CREATE SERVER. BACK plays the sub-page's `subpageLeave` and the
  Multiplayer page returns at 300 ms onto CREATE SERVER (`from_create`); Back
  while going deeper reverses the change from where it stands, and reduced
  motion changes at once. The session's change of level now takes a sub-page
  kind (`retainedSubpage_t`) where it took Arena's flag.
- **Content.** The stock `p_mp_create` over the stock page backing: MAP
  SELECTION over the chosen map's levelshot (183x137 u at 45,75 u) and the
  maps the chosen game type plays (42,219 u, 20 u rows, 64 a page with pager
  rows past them); SERVER OPTIONS from 228,73 u as section 7 setting rows on
  the stock 24 u pitch, the value column 180 u in; ADVANCED OPTIONS under
  them; the enlarged CREATE SERVER plate at 244,397 u; the prompt bar in the
  bottom band, Back at 532,441 u ([page prompt bars](#page-prompt-bars)). The rows are the stock page's: Server Name (a text field),
  Server Type, Dedicated, Gametype (the stock twelve), Auto Balance, the limit
  (Frag Limit, Capture Limit for the CTF families or Control Time for
  DeadZone), Time Limit, Max Players, Min Players, Allow Voting, and Team
  Damage, or Tourney Round Limit for Tourney. Auto Balance and Team Damage
  lock outside team modes, as the stock rows dimmed. ADVANCED OPTIONS opens the
  stock popup's options in the modal construction grown to the popup's size:
  Use Password, Set Password, Remote Console Password, Allow Server Side
  Mods, Weapon Stay, Pure Server, Server Upstream Rate, Ready Mode, Allow
  Spectators, Do Warmup, Allow Voice Chat, Allow Buying, Fill With Bots, Bot
  Skill, the map cycle's text field and VIEW LOCAL SERVER BAN LIST. The two
  passwords are masked text fields
  ([text fields](numeric-fields.md#text-fields)) that may be empty; Set
  Password dims and hides its text while Use Password is off, as the stock
  popup hid its field. Its value column starts 220 u in, where every
  language's label and choice fit; the stock one ran past its rows. The
  dialog's height follows its rows, and it rises from 23 u to stay centered
  once it outgrows that.
- **Ban list.** VIEW LOCAL SERVER BAN LIST, a row with a chevron at the
  dialog's foot as the stock link sat on its pitch, asks the session for the
  list and opens PLAYER BAN LIST over the dialog, nested in its modal scope as
  the stock popup opened over its own: the MODS list dialog's construction,
  as wide as ADVANCED OPTIONS or wider where a language's REMOVE BAN or CLOSE
  needs it, the bans under NAME and GUID on 20 u rows (the first 32; removing
  one brings up the next), or a line saying nobody is banned. A row's first
  Accept chooses its ban; REMOVE BAN, enabled once one is chosen, removes it
  and the list stays open; CLOSE and Back return the focus to VIEW LOCAL
  SERVER BAN LIST. The session reads the bans as the stock popup does (the
  game's `PopulateBanList` rows, `N:  name<tab>guid`), keeps their GUIDs, and
  removes the chosen one through `RemoveGuidFromBanList`; the game writes
  `banlist.txt` at its shutdown.
- **Settings.** The session owns them. It publishes each one as its CVar holds
  it (`create.*`), and each control asks for a change through its own verb
  (`RETAINED_CREATE_SETTINGS`), which the session checks against the CVar's
  range before it writes it. The player counts keep their order: a lower
  maximum lowers the minimum, a higher minimum raises the maximum, as
  retail's session kept the stock page's `checkMinPlayers` and
  `checkMaxPlayers`; openQ4's session now handles those again, and the stock
  popup's `populateBanList` and `handleServerAdminRemoveBan`, so the stock
  page's ban list lists and removes bans as retail's did.
  The game type lists its maps again as the stock MAPScan does: the maps whose
  decls play it, else every multiplayer map, `si_map`'s own chosen where the
  list has it, each named without the color codes the stock list drew.
  Choosing a map writes `si_map`. The text fields hand over their text when
  entered (`RETAINED_CREATE_TEXTS`), at most the stock fields' 25 bytes for
  the name, 200 for the map cycle and 16 for each password, never cut inside
  a character; the map cycle and the passwords may be empty. Every
  multiplayer map's levelshot precaches on the front end's idle frames, a
  few a frame once a menu has presented for a second, and the page reads
  any still waiting when it opens, so browsing the maps never loads one.
- **CREATE SERVER** starts the server through `StartMenuServer`, which the
  stock page's `startMultiplayer` now calls too: the upstream rate's client
  limit and the listen-server warning ask first, then the menu closes and the
  server spawns.
- **Fallback.** In game, or where the page is not installed, cannot load or
  stops drawing, CREATE SERVER opens the stock page.
- **Captures.** `createserver` goes deeper, chooses CTF, opens ADVANCED
  OPTIONS, turns Use Password on and types a password, which shows as its
  mask while editing and once the session has taken it, opens the seeded
  ban list, removes the second ban and closes it, closes the dialog and
  climbs back; `createserverstart` chooses Team DM and starts a listen server
  on the loopback, following the reloaded module.

## Spectate on Join Game (3 November 2026)

Section 13.7's list pattern puts Join, Spectate and Favorite in the prompt
bar. The bar's raised section leaves 214 dp before BACK, while the three
prompts need about 335 dp in English and 390 in German and Spanish, so, as
the Menu and View glyphs already do, the actions stay on the plates and the
glyphs sit on what the buttons press (FLOW-050, INP-011):

- **SPECTATE** (`join_spectate`, the stock `#str_200195` in every language)
  stands between JOIN SERVER and the favorite toggle, ending where they end,
  in the 44.5 dp between them, and is enabled while a server is chosen. Its
  Accept verb is SPECTATE (`check_page_prompts` fits it in every language).
  `joinSpectate` connects to the chosen server as JOIN SERVER does, with the
  player's `ui_spectate` userinfo set to Spectate; JOIN SERVER sets it back
  to Play, so one watched join does not make the next one watch.
- **The stick presses.** A press of the left stick runs a document's
  `onLeftStick` and of the right stick its `onRightStick` (the adapter's
  `TabKey`, with the face buttons' rules: once per press, never with Ctrl or
  Alt or while a field is edited). Join Game spectates on the left stick and
  toggles the favorite on the right, while a server is chosen and no dialog
  is open. Each plate shows its stick 8 dp past its trailing end for a
  controller (`stick_glyph`: LS and RS for Xbox and Nintendo, L3 and R3 for
  PlayStation, Steam Deck and generic controllers, a cap seen from above
  with a doubled rim); the keyboard has no key for them and shows none.
- **Tall views** stand SPECTATE in a row of its own over the favorite toggle,
  aligned with it, and the list's actions move up by that row.

`ui_retained_adapter.py` presses both sticks (declared or not, repeated,
with Alt, while editing), `ui_retained_gate.py` follows SPECTATE's connect
and `ui_spectate` beside JOIN SERVER's, the mp-pages native test checks the
plate's place, the glyphs per family and the events, and the
`join-stick-glyphs` capture forces each family through Prompt Style.

## Chapters as a level of its own (3 November 2026)

Section 13.7's menu levels give Campaign a campaign choice and then two
children, New Campaign (the difficulty) and Chapters (the reached missions),
and its fourth level says every child route beneath a sub-page uses the same
helper. The retained menus now follow it (FLOW-050):

- **Campaign** (`campaigns.q4ui`) holds QUAKE 4 and THE AWAKENING as a
  choice, the chosen one marked with the orange rail the difficulty page
  uses (`campaign.awakening`, the campaign being played when the page
  opens; THE AWAKENING only where its content is ready). Accept on either
  chooses it and moves the focus to NEW GAME (`#str_200000`, the stock New
  Game label). NEW GAME and CHAPTERS (`campaignNew`, `campaignChapters`) go
  one level deeper through `child_transition`: NEW GAME carries the chosen
  campaign's label from its row into the title slot, as Accept on the
  campaign did before, and CHAPTERS carries its own (`detailEnterChapters`).
  Every arrival focuses NEW GAME, or CHAPTERS coming back from it
  (`from_chapters`).
- **The difficulty page** (`campaign_skill.q4ui`) keeps its difficulty,
  Turbo, START GAME and BACK and loses its CHAPTERS plate and the in-place
  view. With the chapters gone it grows a fixed amount, so in tall views it
  takes its column in every portrait view that holds it (visual
  specification 1.36), at 0.906 at 200 % text.
- **CHAPTERS** (`chapters.q4ui`, `retained_menu_levels.chapters_document`) is
  the levels list in the fourth level's frame under the SINGLE PLAYER and
  CAMPAIGN crumbs, titled CHAPTERS. Its rows, picture, name, explanation,
  START CHAPTER, text size and tall view are the ones the difficulty page
  held. Each arrival focuses the chosen level, or the first. BACK and Back
  (`campaignChaptersBack`) climb to Campaign onto CHAPTERS. A chapter started
  fresh, without its autosave, takes the current difficulty, since the page
  has none of its own.
- **The session** routes both children through `BeginRetainedCampaignSkill`
  (detail targets 3 and 4 for CHAPTERS and the climb back onto it), and
  `SelectCampaign`'s engine reload into The Awakening continues with
  `campaignSelect awakening chapters` onto CHAPTERS. Where the CHAPTERS page
  cannot present, or under reduced motion, the change hands over at once, and
  the stock campaign page with its own CHAPTERS stands in. The classic menus
  keep that page.

`ui_retained_gate.py` follows both routes (the choice, cancellation,
drafts, publication, a pick and a start at the current difficulty, the climb
back onto CHAPTERS, reduced motion and the missing documents),
`campaign_chapters.py` pins the documents and verbs, and the native screens'
continuity, page prompts and text-scale checks cover the new page. The
`campaignskill` capture walks Campaign, NEW GAME, Back, CHAPTERS and Back.

## Section summaries (3 November 2026)

Section 13.7 says a section that leads deeper summarizes itself in the
content column, with a levelshot where one fits, until Accept opens it.
Single Player, Campaign, Multiplayer and SETTINGS now do (FLOW-050): each
section's sentence (`#str_231552` to `#str_231557`, `#str_231571` to
`#str_231576`) takes the description's place while it is the subject.
CAMPAIGN, ARENA, NEW GAME, CHAPTERS and CREATE SERVER stand over a levelshot
the session publishes as the page opens. BACK, Campaign's QUAKE 4 and THE
AWAKENING, and SETTINGS' LOAD DEFAULTS keep the description.
[Single Player and Campaign](#screens), the
[Multiplayer page](#multiplayer-page) and the
[SETTINGS page](#the-settings-page-26-october-2026) describe the pages.

- **Why a detail state.** A control's feedback reaches only its own subtree,
  so a plate cannot light a node in the content column. The document format's
  detail definition now takes an optional card: without one, the runtime only
  writes the subject's detail to its state (`summary.section`), and the
  column's bindings show that section's summary. The subject is the plate
  under the pointer while the pointer moved last, else the focused one,
  so the pointer and the keys reach the same summary
  ([interaction contract](interaction.md#detail-cards)).
- **Pictures.** The levelshot stands 140x105 u at 395,77 u, over the column
  under the docked top band. It shows only while the session names one and
  the column's text, grown, stays clear of it, and never in a tall view. The
  session reads it as a precache and takes it out of the idle-frame queue
  (`Session_PublishSummaryShot`). ARENA's comes from
  `idArenaCampaign::SelectedLevelshot`, the match the Arena page would open
  on. NEW GAME's and CHAPTERS' come from `CampaignSummaryShots`: the active
  campaign's first level and the furthest one reached. Only the active
  campaign's levels are mounted, so they show only while the campaign chosen
  on the page is the active one.

`UiDocumentTest` accepts the cardless definition, `ui_retained_gate.py`
checks the published pictures and their precaches, the menus, pages and
mp-pages native groups walk the subjects and the column's text scale, and
the `summaries` capture walks the four pages at 16:9 and 9:16.

## Demos as a Multiplayer sub-page (2 November 2026)

Section 9 makes the demo library a section of the Multiplayer page that
opens a sub-page, as Join Game and Create Server do, and section 13.7 keeps
the title's plinth to MODS, UPDATES, CREDITS and EXIT. The retained menus now
follow it (FLOW-050):

- **The Multiplayer page** holds JOIN GAME, CREATE SERVER, DEMOS and PLAYER
  SETUP on the home navigation plates from 181 u, BACK under them, and its
  description names all four (`#str_231550`, in every shipped language).
- **DEMOS** goes one level deeper through the shared change of level
  (`subpageEnterDemos`, `RETAINED_SUBPAGE_DEMOS`): the library is published
  as the page leaves, and at 350 ms the Demos page presents in the sub-page
  frame (`retained_subpage.subpage_document`) under the MULTIPLAYER crumb with
  DEMOS at the step's foot. Back climbs out over 300 ms onto DEMOS. Its list,
  filters, details, PLAY, DELETE and REFRESH are unchanged.
- **The title** loses its DEMOS link: the plinth carries MODS, UPDATES,
  CREDITS and EXIT, and the session no longer takes a `demos` request from
  the home. The legacy main menu, the `demoMenu` command and Escape during
  playback keep the legacy browser.
- **Fallback.** During playback, or where the sub-page is not installed,
  cannot load or stops drawing, DEMOS opens the legacy demo browser over the
  Multiplayer page, which its close returns to.

`ui_retained_gate.py` follows the route (the library published while the page
leaves, nothing else taken meanwhile, Back onto DEMOS, paging, the legacy
browser in its three cases), and the native screens' pages and mp-pages
groups check the five plates, the new description and the sub-page frame.
Engine captures on OpenGL at 16:9 (`demos`, `demosback`, `multiplayer`) show
the page, the descent and the return with the focus on DEMOS.

## Player Setup edits in place (2 November 2026)

PLAYER SETUP on the Multiplayer page has edited in place since 28 October
2026 (`tools/ui/retained_player_setup.py`): the sections and description give
way to the editor's NAME and CLAN TAG fields, the MODEL choice with PREVIOUS
and NEXT pages through the installed catalog, the preview's rotation, the
rail colors and BACK, beside the animated player model the
[model scene](document-format.md#model-scenes) draws. Each change applies at
once and reads back from its CVar; a choice made against a catalog or value
that has since changed is refused with a notice. BACK, or Escape, closes the
editor onto PLAYER SETUP. Where the preview cannot present, the stock
Settings page opens instead.

The `playersetup` capture (OpenGL, 16:9) typed a name, chose the next model,
set a rail colour and turned the preview, each read back (`ui_name`,
`ui_model`, `ui_hitscanTint`), and closed onto PLAYER SETUP. The editor keeps
its 100 % rows at larger text sizes and the 4:3 composition in a tall view.

## Join Game sub-page

JOIN GAME on the Multiplayer page goes one level deeper to
`guis/menu/joingame.q4ui`, generated by `tools/ui/retained_join_game.py`, as
CREATE SERVER goes to Create Server (section 9 "Menu levels" and "Lists",
FLOW-050). It is the stock browser (`p_mp_browse`) on the sub-page frame the
two share (`tools/ui/retained_subpage.py`). It offers every control the
stock page does, so it is the default under `ui_retained`: the session's
list of what it lacks (`RETAINED_JOIN_GAME_MISSING`) is empty, and while
anything is listed there only `ui_retainedMultiplayer 1` offers the page.
`ui_retained_gate.py` maps each of the stock browser's buttons to the page's
verb or node, or to that list; the stock PunkBuster column's sort is exempt
(section 9's symbol table: a discontinued service, no replacement column).

- **Change.** The Multiplayer page plays `subpageEnterJoin`, CREATE SERVER's
  change with the JOIN GAME label carrying from its row to the step's foot;
  the sub-page presents at 350 ms with the focus on GET NEW LIST. BACK plays
  its `subpageLeave` and the Multiplayer page returns at 300 ms onto JOIN GAME
  (`from_join`); Back while going deeper reverses the change, and reduced
  motion changes at once.
- **Content.** MOD (the mod filter) with its spinner at 27,46 u and SOURCE
  with its INTERNET and LAN tags (section 7's cut tags, the list's source lit
  at 0.60 and the other at 0.28, each as wide as its longest translation) at
  324,46 u; the server list at 23,83 u under its column headings, ten of the
  stock 18 u rows high (64 a page, with pager rows past them): the favorite, the
  password and the dedicated server's symbols at the stock symbol stops, then
  SERVER NAME from the stop the stock PunkBuster column held, PING, the
  repeater's symbol, PLAYERS, GAMETYPE and MAP. The symbols sit on their
  16 dp tiles with the cut upper-leading corner as section 9's symbol table
  draws them: a gold star, a padlock, a server case with its green power
  light, a television with its antennas and color bars. Each ping follows
  its network quality (section 13.13), drawn as four bars rising like slider
  ticks from PING's stop (the filter row under the list is described under
  **Filters** below):
  - four bars to 60 ms, three to 120 ms, two to 200 ms and one above
    (`Session_NetworkQualityBars`, which takes a bar off per 2% of packet loss
    where loss is known; the browser hears none);
  - lit in `status.success` at three and four bars, `status.warning` at two
    and `status.error` at one, the rest faint;
  - 1.6 u wide, 0.9 u apart, rising from 2.4 to 7.2 u on the row text's
    baseline, with the ping's text following at 292 u.

  The columns after the
  name are wider than the stock ones, so every language's heading fits beside
  the sort's chevron, which follows the heading's longest translation. The
  scan's status line at 41,291 u (175 u, which every language's status fits),
  a player search's tag after it and CLEAR SORTING; the scan's progress bar
  across the list's width at 21,312 u with its notice over it from 41 u (the
  stock load bar); REFRESH LIST, GET NEW
  LIST, JOIN IP ADDRESS and FIND PLAYER/CLAN down the leading column from
  19,330 u on a 26 u pitch, so the fourth clears the bottom band, each as
  wide as the longest label of any (Italian's FIND PLAYER/CLAN); the chosen
  server's levelshot in the stock frame, at 240,337 u so the favorite toggle
  clears it; JOIN SERVER, enabled once a server is chosen, at 385,330 u, and
  under it the favorite toggle (ADD TO FAVORITES, or REMOVE FAVORITE while
  the chosen server is one); the prompt bar in the bottom band
  ([page prompt bars](#page-prompt-bars)). An empty list says so,
  unless a scan is still running: that no server was found, or, when the
  scan holds servers that the filters or a search all hide (`join.listed`,
  how many it holds), that none matches the current filters. A join from the
  page, by JOIN SERVER or JOIN IP ADDRESS, that fails or is aborted comes
  back to this list, as the stock browser kept it: when the menu next rests
  at home without a level, Join Game presents instead of the title, once
  (`retainedJoinReturn`); a join that loads its map forgets it.
- **List.** The session owns it. While the page presents it lists the
  servers the scan found and the filter passes, in the sort's order, a page
  at a time (`join.row<i>.*`, the ping's bars as `join.row<i>.quality`),
  with their count and how many the scan holds,
  the scan's status line (the stock page's `sortNotice`), whether a scan
  runs, its progress (`join.progress` and `join.notice`, the stock page's
  `loadPct` and `loadNotice`), the source, the mod filter (`filterMod`,
  which the scan writes on the legacy menu), the player search and the sort,
  and publishes again whenever the scan's revision moves (a server answered,
  a sort, filter or search changed, the status or the progress changed). The
  scan's read view
  (`idServerScan::VisibleServers`, `IsFavoriteServer`, `GetSorting`,
  `GetRevision`) lists exactly what the stock list shows. The servers' names,
  game types and maps are read as players' text (`Session_RetainedPlayerText`):
  escapes and controls dropped, never read as a `#str_` key; a nameless
  server shows its address. The chosen server's levelshot is the loading
  screen's choice for its map (`RetainedPauseShot`), looked up again only
  when the map changes and precached; a map path the browser may not look up
  (`idServerScan::ValidMapPath`: no parent references or absolute paths)
  shows the generic art.
- **Verbs.** INTERNET and LAN choose the source on the legacy menu
  (`gui::lanSet`, which the stock browser's scans read) and get a new list
  from it; GET NEW LIST gets a new list (the master server and the favorites
  on the Internet, the LAN otherwise) and REFRESH LIST asks again
  (`Session_ScanServers`, the stock `UpdateServers` and `RefreshServers`). A
  heading sorts by its column, and again the other way; CLEAR SORTING lists
  the servers as they answered. On a controller, west asks for the list
  again, as REFRESH LIST does, and north sorts it by the next text column
  (`joinSortNext`: PING, SERVER NAME, PLAYERS, GAMETYPE, MAP and round again,
  ascending, from PING while the list is unsorted or sorted by a symbol):
  section 13.5's secondary and tertiary actions, through the document's
  `onSecondary` and `onTertiary`, neither of which acts while a dialog is
  open. MOD's spinner shows the previous or next mod
  the list holds (`UpdateFilterByMod`, the stock arrows'). A row's first
  Accept chooses its server and Accept on the chosen one moves on to JOIN
  SERVER, which connects to the chosen server by the address the session
  kept. The favorite toggle adds the chosen server to the favorites or takes
  it off (`idServerScan::ToggleFavoriteServer`, the stock toggle's work for a
  server the page names). The choice follows its server through new orders
  and stays while a scan may list it again. So does a focused row: the
  session keeps the address each page row last showed
  (`retainedJoinRowAddresses`) and, when a new order, a refresh or a filter
  moves that server, turns to its page and focuses the row that now shows it
  (the document's `joinFocus<row>` events). A server that leaves the list
  leaves the focus where it was.
- **Progress.** The bar is the stock `load_bar3` tube in `marine.progress`:
  its alpha profile down the 14 u tube (a bright top edge, dim under the
  notice, a lighter foot), the track at 0.30 and the fill at 0.50, the fill's
  width the scan's fraction. Retail's scan published that fraction and its
  notice to the stock browser (`loadPct`, `loadNotice`); openQ4's had
  dropped both, so the stock bar stayed empty, and publishes them again
  (`idServerScan::GetLoadNotice` and `GetLoadProgress` for a menu that draws
  its own): "Getting list of servers..." while a new list waits on the master
  server, "Scanning for LAN servers..." as the LAN sweep starts, then the
  server that answered last with the count of listed servers heard from or
  given up on ("Scanning %d/%d: %s"; on the LAN "Scanning - Found: %s",
  across its one-second reply window), and once the scan completes the
  count it covered ("Scanned %d openQ4 Servers") on a full bar, which stays
  until the next scan or a clear. A refresh says nothing until a server
  answers, as retail's did. The fraction moves in whole percents, so a LAN
  sweep republishes the list at most a hundred times. The notice can name a
  server, so the page reads it as a player's text. Polish and Russian gained
  the three scan strings their tables lacked.
- **Server details.** The scan describes a server as the stock browser's
  tooltip shows it (`idServerScan::GetServerDetails`): its address, its
  players' names, and its settings as "label: value" lines (the map, the time
  limit, the limit its game type ends on, team damage, team autobalance, pure
  and spectators), its text read as the browser reads a server's. The limit
  follows the game's descriptor table (`mp/GameTypes.cpp`): the capture limit
  for the flag modes, the round limit for Clan Arena, Freeze Tag and Red
  Rover, DeadZone's control time, and the frag limit otherwise;
  `server_browser_contract.py` holds the scan's table to the game's. Retail's
  scan kept the stock tooltip for the row the pointer was over each frame the
  classic menu ran; openQ4's had published the selected server's players
  there and kept the tooltip hidden. The session keeps it again
  (`UpdateBrowserTooltip`, `idServerScan::GUIUpdateHover`, reading the
  list's `serverList_hover`). Polish and Russian gained the tooltip's labels,
  which their tables lacked, and French and Italian DeadZone's control time.
- **Details card.** The retained page shows those details in the stock
  tooltip's card as section 7's hover card, through the runtime's detail
  card ([interaction](interaction.md#detail-cards)). Each row names its row as
  the subject (`join.detail`). The pointer's row is the subject while the
  pointer moved last, and otherwise the focused row. The session publishes the
  row's server (`PublishRetainedJoinDetail`: `join.detail.address`,
  `join.detail.player0`-`10`, `join.detail.setting0`-`6`, read as players'
  text) and the row it describes (`join.detail.row`), and the card shows only
  while that matches the subject, so it never shows another server's details
  for a frame. The card is the stock 270x186 u, 30 u taller for the server's
  name: a black body at 0.9 with an olive border, cut at its lower-leading
  corner and bevelled at its upper-trailing one; the server's name in full
  (`join.detail.name`), which its row cuts with an ellipsis, on up to two
  Lowpixel 14 dp lines (section 13.13's detail area shows what a row
  truncates); the address under it at 0.7; PLAYERS and SERVER
  SETTINGS at 0.4 over 11 player and 7 setting lines in Profont 13 dp on the
  stock 12 u pitch; and olive dividers at 0.32 as the stock draws them. It
  sits under the focused row (over it near the view's edge), or beside the
  pointer.
- **JOIN IP ADDRESS** opens section 6's entry dialog on the stock popup's
  pieces: the address field (99 bytes, as the stock field took), which Enter
  commits into `join.address` and connects with; CONNECT, while there is an
  address; CANCEL; and "your ip address:" with this machine's
  (`join.clientip`). The session connects through
  `Session_ConnectToTypedAddress`, which the stock popup's `inetConnect` now
  shares: no control, quote, semicolon or backslash, resolved, IPv4, IPv6 or
  loopback, the default port added. An address it cannot join keeps the
  dialog with the reason (`join.addressError`, the stock popup's message
  box text); one it can closes the dialog as the connect begins. Retail's
  session published "your ip address:" for the stock popup too
  (`getClientIP`, the first interface's address), which openQ4 had dropped:
  it is back, on `Sys_GetLocalIPAddress` (the first non-loopback IPv4
  interface, on both net backends).
- **FIND PLAYER/CLAN** opens an entry dialog with SEARCH FOR: and a name
  field (31 bytes, a player name's), which Enter or SEARCH applies:
  the scan hides every server without a player whose name holds the text,
  colors removed and in any case (`idServerScan::SetPlayerSearch`), and the
  search's tag shows it until its Accept ends it. Retail asked the master
  server for a player's or a clan's servers, which no master answers now, and
  openQ4's stock FIND PLAYER/CLAN did nothing; both pages search the list
  instead, and a clan tag is part of a player's name, so the stock PLAYER and
  CLAN choice is gone. GET NEW LIST ends a search, as retail's search was a
  list of its own.
- **Filters.** Under the list, at 265 u, the filter row (section 7's filter
  tags, section 13.7's lists): the game type's spinner from the list's
  leading edge, built as MOD's, its value ALL GAME TYPES while it shows every
  type and otherwise the type's name (`join.gameType`, `join.gameTypeName`),
  stepping round the browser's twelve public types (`joinGameTypePrevious`,
  `joinGameTypeNext`); then NOT FULL, NOT EMPTY and NO PASSWORD, ending where
  the source tags do, each as wide as its longest translation with room for
  its close cross. A tag's Accept toggles its filter (`joinFilterFull`,
  `joinFilterEmpty`, `joinFilterPassword`); a set tag lights at 0.60 with its
  cross, an unset one rests at 0.28 (`join.filterFull`, `join.filterEmpty`,
  `join.filterPassword`). The list gave up a row and a little for the row,
  so ten rows show. The scan already filtered by the inherited
  `gui_filter_*` CVars, which no stock control set; it now offers them as
  `idServerScan::ToggleFilter`, `StepGameTypeFilter` and
  `GameTypeFilterName`. The players filter's two bits hide full and empty
  servers in the order of the stock choices left in the language tables
  (`#str_200306`: Show All; Hide Full; Hide Empty; Hide Full and Empty), so 2
  now hides only empty servers where the inherited code hid full ones too.
  The list filters every server itself, so the master server is now asked for
  every server: a filter changed after a scan never needs a new list, and
  the request's legacy two-bit fields, which cannot name openQ4's added game
  types, never withhold one. A filter that hides the chosen server keeps it
  chosen while the scan holds it, as a search does. The classic browser has
  no filter controls, so filters set here also narrow its list, which its
  status line counts ("Showing 1 out of 2 servers").
- **Favorites.** A favorite on this machine (a loopback endpoint) now lists
  from the Internet source: the scan kept it as `localhost:port`, which only a
  DNS lookup parses, so it was never asked. Favorites are kept in numeric form,
  and a `localhost:` line saved before reads as `127.0.0.1`.
- **Fallback.** In game, or where the page is not offered, not installed,
  cannot load or stops drawing, JOIN GAME opens the stock browser.
- **Checks.** `ui_retained_gate.py` compiles the session's Join Game code
  against a scan double: the opt-in, the change and its reverse, the rows
  and their text, the choice following its server, the sorts and north's
  next column, each source's scans, the paging, the symbols, MOD's spinner, the filter row's tags and game
  type, the favorite toggle, the
  levelshot (and the generic art for an unsafe map path), JOIN SERVER's
  address and the fallback; it also maps each stock browser button to the
  page or to `RETAINED_JOIN_GAME_MISSING`. The native screens check lays the
  page out at 1280x720 and drives its states, west and north (neither under
  a dialog), the two dialogs, the search's tag, the filter row and the progress bar, and `server_browser_contract.py` runs the
  scan's read view, the per-server favorite toggle, the map-path check, the
  player search, the filter row's players bits, password filter and game type steps, the scan's progress (a LAN sweep, a new Internet list, a
  refresh, a clear, and what the stock browser receives) and the loopback
  favorites against the production `ServerScan.cpp`.
- **Captures.** `joingame` (opted in) goes deeper, gets a new Internet list in
  which the seeded favorite, a dedicated server the capture starts on the
  loopback (`SCENARIO_SERVERS`), answers (the master server cannot be reached
  from the loopback the client binds), sorts by name and clears the sort,
  sorts by PING and asks for the list again as north and west do
  (`openq4_retainedGui event onTertiary` and `onSecondary`), climbs back and
  returns, chooses the server (its levelshot and dedicated
  symbol shown), takes it off the favorites and puts it back, steps MOD to
  the next mod and back, searches for a player nobody is (the list empties
  under the search's tag until its Accept), and joins it, the game module
  reloading into game_mp; the progress bar is full once the scan completes,
  and the focused first row's details card shows under it.
  `joinfilters` lists the same server (Deathmatch, nobody playing, no
  password): NOT EMPTY hides it and the empty line says no server matches,
  and again brings it back; the game type's spinner shows Deathmatch, which
  keeps it, then Tourney, which hides it, and back to every type; NOT FULL
  and NO PASSWORD keep it and light their tags, and again clear them.
  `joingameaddress` opens JOIN IP ADDRESS, enters an address it cannot join
  (the reason shows), then the server's, which joins it. `stockbrowser` runs
  the classic menu (`ui_retained 0`): its browser's GET NEW LIST fills the
  stock load bar again and names the count (`openq4_browser report` prints
  `loadNotice` and `loadPct`), and the server's row, treated as hovered
  without a pointer (`openq4_browser hover`), shows the stock tooltip with
  its address and settings, which no row hides again (the report prints the
  tooltip's state and items).

## Loading phase line

The session publishes the loader's phases to the retained loading screen
only while that screen presents the load: MAP before the render world loads,
WORLD before the game builds the level, ASSETS while the renderer works
through its model and image queues (their own loaded and total counts replace
the phase's place), FINISHING after the renderer's level load, and the load
time once the level is ready, in tenths of a second as the language writes
them ([joined lines](#joined-lines-and-capitals-24-october-2026)). The continue prompt follows `prompt.glyphs`
like every prompt ([prompt glyphs](#prompt-glyphs)). The wait for continue
redraws the screen each frame, so touching a controller or the mouse switches
it. The session no longer publishes its own `loading_controller`.

## Empty states (24 October 2026)

Section 13.13 gives an empty list a heading, one sentence saying why and an
action row that fixes it. The lists now carry the action:

- Load and Save Game: while a filter hides every save, the empty state says
  none matches and offers CLEAR FILTERS under its sentence, in NEW GAME's
  place on the Load page (`load-empty-clear`, `save-empty-clear`, the
  family's filter plate and the session's `saveListClear`). Unfiltered, the
  Load page keeps NEW GAME at the title.
- Demos: while a filter hides every demo, CLEAR FILTERS (`demo_emptyclear`)
  clears it and asks for the list again.
- Join Game: the empty line gains a heading, NO SERVERS or NO MATCHING SERVERS
  (`#str_231537`, `#str_231538`, in every shipped language), and the fix:
  GET NEW LIST when the scan holds no server, CLEAR FILTERS when the filters
  or a search hide them all. CLEAR FILTERS is the session's new
  `joinClearFilters`, which clears the NOT FULL, NOT EMPTY and NO PASSWORD
  tags, the game type, the mod and the player search at once
  (`idServerScan::ClearFilters`, one re-filter). A running scan shows its
  status instead.

The rows show only while their condition holds, so a list with entries and a
running scan are unchanged. The native page tests check each row's condition,
place and request, and the gate's session harness checks that one request
clears every Join Game filter. Captures at 16:9 in English and Russian
show the rows and the lists they bring back.

Evidence: `.tmp/ui/empty-actions/validation-evidence.json`, SHA-256
`b6ac9bfa909d37afecfa59d210f880116dcbb3db8cb8810af29596ee6744e47f`.

## The CONTROLS page (25 October 2026)

The settings screen's CONTROLS button, and the multiplayer menu's Controls
hand-off (`fromMp_toControls`, routed as GAME OPTIONS' is), open a retained
CONTROLS page built like the other settings pages
(`tools/ui/retained_controls.py`, `guis/menu/settings/controls.q4ui`). It
carries the stock page's 49 bindings in its order, its four tabs as the
page's tabs (MOVEMENT, WEAPONS, ATTACK/LOOK and OTHER); see
[the settings sub-pages](#the-settings-sub-pages-31-october-2026) for its
layout. Each row names the action over
the keys bound to it, which the session publishes as the language names the
keys and joins a list (`Session_RetainedList`), or the stock page's word for
none. A hint on the heading line (`#str_231545`, in every language) says
how to change one.

Accept on a row waits for a key, the row saying so (`#str_07000`), and the
session takes the next key before the page sees it
(`idSessionLocal::RetainedControlsKey`, in `MenuEvent`), as the stock binding
field did: Escape, Start or Back cancel; Backspace or Delete clear the row;
any other key binds, an action keeping at most two keys as the stock
`bindunbindtwo` did. Delete or Backspace on a row, or the west face button
(`onSecondary`), clears it. Each change also refreshes the stock page's key
names. `testControlsKey <key>` stands in for a key pressed on the page, so
captures drive the wait without touching a device.

The stock settings screen opened on its CONTROLS panel, where the CONTROLS
button hides. While the retained CONTROLS page is available it now arrives
on no panel, titled SETTINGS, with every button showing and the stock rows'
hit targets hidden (`anim_settingsIn`), so its buttons choose a page as a
menu's do: CONTROLS, GAME OPTIONS and AUDIO their retained pages, DISPLAY
the stock panel (the retained SYSTEM page where opted into) and LOAD
DEFAULTS the stock dialog. Back from a page returns to the screen as it
was. Captured from the title in English and German, through CONTROLS and
through the stock DISPLAY panel. The
[SETTINGS page](#the-settings-page-26-october-2026) now stands in for the
screen and its LOAD DEFAULTS dialog; the stock screen still shows a stock
panel the page sends it to, and serves where the page cannot.

`openq4-ui-retained-controls` checks the 49 rows, the keys shown, the wait,
the clear and Back; the gate pins the bindings to the stock page and the
session's table, the two-key rule, the cancel keys and the routes. Captured
at 16:9 in English and Turkish and at 9:16, with the multiplayer route and
the fallback.

## The GAME OPTIONS page (25 October 2026)

The settings screen's GAME OPTIONS button opens a retained page while the
retained screens are on, built as the AUDIO page is:
`tools/ui/retained_game.py` builds `guis/menu/settings/game.q4ui` in the
settings sub-page that `tools/ui/retained_settings_rows.py` shares between
the settings pages. It carries every row of the stock page, in the stock
order and text, in eight sections: GENERAL, MOUSE INPUT, CONTROLLER,
CROSSHAIR, HUD, GAMEPLAY, VIEW WEAPON and VIRTUAL REALITY. The three headings
the stock page lacked (`#str_231542` to `#str_231544`) are each language's
own section names from its Section list, in capitals. The sections stand on
five tabs, which took the place of the Section row on 31 October; see
[the settings sub-pages](#the-settings-sub-pages-31-october-2026).

The 73 rows that name a CVar read it back through the page's CVar state and
set it with `settings.player.set`, which the adapter allows for exactly what
the stock control offered (`FindPlayerSetting`): a slider's range, a check
box's Boolean, or a list's values. A list whose values have a fraction
(Invert Mouse, the VR haptics and vignette) compares the CVar's text, as the
stock list does, since a float CVar reads back inexactly; for the same reason a
slider with a fraction shows its CVar rounded to its places. The rows the stock
page computes go through the session (`HandleRetainedGameRequest`), applied
through the stock main menu's own choices so the two pages agree: Gun
Position, Force Pro Skins, Set Crosshair (a slider over the custom crosshairs
with the chosen one's picture, usable while Crosshair is Custom), Corpse Time
(Custom shows when the delay matches no preset and is never offered) and
Language. Language reloads the language and the menus as the stock list
does; the page stays, translated, with the focus on Language, and BACK then
returns to the main menu's home, where the reload restarts the stock menu.

BACK and the back input (`gameBack`) return to the settings screen and its
handler; a page that is missing or stops drawing opens the stock page from
then on, as AUDIO does. The multiplayer menu's Game Options hand-off
(`fromMp_toGameoptions`, which moves the main menu straight to that panel
rather than pressing the button) has its own route, as SYSTEM's does: with
the page available the main menu comes up at its home and the session opens
the page over it (`openRetainedGameFromMp`), so BACK returns there; a page
that cannot open sends `fromMp_toGameoptionsStock`, the stock route, and the
button keeps the retained page while it is available. Captured from the menu
event, on a listen server through the in-game menu's own GAME OPTIONS button,
and with a broken page.

`openq4-ui-retained-game` checks the document's rows and verbs, the tabs and
their columns, CVar rows setting their CVars, a colour list's stock value, the custom
crosshair's lock, Custom never offered, the focus after a language reload,
Back and a portrait fit. The gate compiles the route against its doubles and
pins each computed row to its stock choice, the language list to the stock
one and every CVar row to a stock control and an adapter rule. Captured at
16:9 in English and Russian and at 9:16, with the fallback; each run switches
the language to German on the way.

## The AUDIO page (25 October 2026)

The settings screen's AUDIO button opens a retained AUDIO page while the
retained screens are on. `tools/ui/retained_audio.py` builds
`guis/menu/settings/audio.q4ui` in the settings sub-page (see
[the settings sub-pages](#the-settings-sub-pages-31-october-2026)), so the
page adds no art and no strings. It carries every row of the stock page, in
the stock order and with the stock text:

| Section | Rows |
| --- | --- |
| LEVELS | Volume, Music Volume and Dialogue Volume, 0 to 100% |
| DEVICES | Sound System (Default, OpenAL) and Sound Device, the sound system's devices (up to 16) |
| EFFECTS | EAX ADVANCED HD; Surround Speakers and Headphone HRTF (Auto, Off, On); Dialogue Subtitles; Subtitle Size, 100 to 200%; Subtitle Background, 0 to 100%; Speaker Names |

Each row asks the session with its own value verb (`audioVolume` through
`audioSubtitleSpeakers`, allowed by the adapter's `SessionMenuValueCommand`),
and the session applies the value at once, as the stock controls do. The
levels and subtitle rows set their CVars. Sound System, Sound Device and
Headphone HRTF run the stock page's `sound drivar`, EAX `sound eax` and
Surround Speakers `sound speakers`, which restart the sound system and may
answer with the stock message box. The session then publishes every value
back (`PublishRetainedAudio`), so the page shows what the engine holds: EAX
that the device refuses reads back off. The device list is the stock choice's
own (`idSoundHardware_OpenAL::BuildDeviceChoiceStrings`), its quoted items
read with their escapes; a chosen device the system no longer lists stays in
the list, marked unavailable, as on the stock page.

BACK and the back input (`audioBack`) return to the settings screen and the
handler that opened the page. A page that is missing or stops drawing opens
the stock page instead, from then on in the session (`retainedStock`), as the
SYSTEM page falls back; with `ui_retained 0` the stock page opens as before.

`openq4-ui-retained-audio` checks the document's verbs, the readback, a
slider step, the check boxes, the device list unfolding and folding without a
request, Back and a portrait fit. The gate compiles the session code against
its doubles: the route, the readback from a quoted device list, each verb's
CVar and command, malformed and stale requests, Back and both fallbacks.
Captured at 16:9 in English and German and at 9:16, with the fallback.

## The SETTINGS page (26 October 2026)

SETTINGS on the title or the single-player pause now hands off to a retained
page, `guis/menu/settings.q4ui` (`tools/ui/retained_settings_page.py`), as
MULTIPLAYER hands off to its page (`BeginRetainedPage`): the home docks its
bands and carries SETTINGS into the title slot, and at 550 ms the page
presents. It is the stock settings screen's level of the menu: CONTROLS, GAME
OPTIONS, DISPLAY, AUDIO and LOAD DEFAULTS on the home's navigation plates in
the stock order and words, BACK under them, and one line in the content
column saying what the page is for (`#str_231546`, in every language).
CONTROLS, GAME OPTIONS, DISPLAY and AUDIO summarize themselves there while
each is the subject ([section summaries](#section-summaries-3-november-2026));
LOAD DEFAULTS and BACK keep that line, which names the defaults.

| Button | Opens |
| --- | --- |
| CONTROLS, GAME OPTIONS, AUDIO | Their retained sub-pages, one level deeper; Back climbs out onto the button |
| DISPLAY | The retained SYSTEM page where it is on, otherwise the stock screen on its DISPLAY panel |
| LOAD DEFAULTS | The stock dialog's question in its words; YES restores the defaults as the stock YES did, NO closes it |
| BACK, the back input | The title or the pause, as it was |

Each button is a session verb (`settingsControls` through `settingsDefaults`
and `settingsBack`, in the adapter's `SessionMenuCommand` allowlist). A page
that cannot open, and DISPLAY without the SYSTEM page, send the page away as
the Multiplayer page leaves for a stock page: its plates fade over 150 ms as
it sweeps toward the trailing edge, and at 500 ms the stock settings screen
takes over on that panel (`RETAINED_SETTINGSPAGE stock dest=<n>`). Back from
the stock screen returns here (`settingsOpen`) onto the button that left. A
page that is missing or stops drawing leaves the stock screen to SETTINGS,
which then opens as before, as with `ui_retained 0`.

LOAD DEFAULTS' YES runs the stock dialog's commands (`cvar_restart`, the
selection sound, `resetdefaults` and `s_restart`). `cvar_restart` resets
every CVar, the log's included, so the capture sets the log again after it.

`openq4-ui-retained-settings` checks the buttons, their verbs, the question,
YES and NO, the return onto each button and a portrait fit. The gate compiles
the session code against its doubles: the section table against the
generator, each route, the stock hand-off and its return, the defaults
commands against the stock dialog's, and Back to the title before and after
a stock panel. Captured at 16:9 in English and German and at 9:16 from the
title, with the defaults, and from the pause and back to it.

### Main-checkout verification, 28 October 2026

The uncommitted SETTINGS work was recovered from the earlier UI worktree into
the main checkout. Its Strogg-pause route incorrectly selected the Strogg Load
Game slot before it looked up the hub; SETTINGS now uses its own document and
returns to the same pause family. The gate covers that route and missing-page
fallback. The portrait native check uses the document's actual canvas height
and minimum width, as the engine does, and checks label bounds: section 9's
navigation plates deliberately bleed off the leading edge.

The optimized main build and six native settings/menu/pointer suites pass.
Current OpenGL captures cover the title route, the defaults confirmation and
reset, and both Marine and Strogg gameplay pauses. Vulkan captures cover both
gameplay pauses. German portrait captures cover the hub, its confirmation,
stock DISPLAY return and a broken-document fallback. The reset reads mouse
sensitivity back as 5, its declared default, after setting it to 9.

Two Vulkan front-end runs and their two retries exited with code 4294967295,
without an engine error or new crash report; they are retained as failures.
The retry's engine, renderer and SP/MP module hashes did not change. After
coordinating exclusive runtime windows with the preset and multiplayer chats,
both front-end cases pass on Vulkan too: six title-route captures and the
defaults reset capture, with unchanged before/after module hashes. This does
not establish the cause of the earlier exits or erase their failed evidence. A lazy
`player_marine_mp_ui` declaration warning remains in the gameplay logs.
All output comes from registered engine `screenshot` commands in hidden,
windowed processes with mouse/controller input disabled. No host input or OS
screen capture was used. Evidence is under `.tmp/retained-ui-completion/`;
this is a bounded working-copy checkpoint, with no product acceptance.

## The pop-up black frame (5 November 2026)

Every pop-up now carries an opaque black frame (visual specification 1.38, the
product owner's rule). It adds to each construction and changes nothing else:

- **Modal confirmations** (`confirmation()` in `build_retained_screens.py`:
  every confirmation, notice, list and entry dialog) trace the silhouette's
  outline in black at 1.00, 3 dp wide, on top of the glow; the 0.70
  silhouette, the glow and the lit margin around it are unchanged. In the
  Strogg family the frame is 4.5 dp beneath the orange rail.
- **The title's tab.** The frame fills the title slot in black from the raised
  top line behind the title and steps down to the slot's floor 10 dp past the
  title's end, at 45 degrees (Strogg 30). The tab's width follows the running
  language's title (`per_language` on `sys_lang`), capped where the slot's
  trailing flank begins. It is a child of the frame layer, so it enters and
  leaves with it; the Strogg tab carries the rail along its top and step.
- **The DISPLAY page's dialogs** (`retained_system.py`) and **every dropdown
  list** (`choice_popup` in `retained_mp_menus.py`) gain a 4 dp black stroke
  beneath their plate or body, so 2 dp of black shows outside their own
  outline or border.

All 65 retained native suites and the retained gate pass. Engine captures
show the Admin page's Kick confirmation on Vulkan, and the Strogg Exit
confirmation, the DISPLAY page's unapplied-changes dialog and an open Game
Options list on OpenGL (`.tmp/ui-roadmap/frame2-vk/`, `.tmp/ui-roadmap/frame2-gl/`,
`.tmp/ui-roadmap/frame2-list/`). The UI register's binding of the visual
specification was already stale before this change (it matches neither form
of the earlier text), so the specification's new hash waits for a register
increment with the other stale bindings.

## Opaque Menu Backing (5 November 2026)

The opaque-backing option, which covers the screen behind the retained
menus' dialogs and pause with the stock 0.94 scrim instead of the softened
view, was a CVar only (`ui_retainedOpaqueBacking`). It is now a row in Game
Options > HUD, after Reduced Flashing, on both the retained page and the
stock one (`set_game_opaquebacking`). The label is `#str_231592` in all
twelve languages. The stock page's later rows move down one 24 px step,
with its scroll range (77), content height (2162) and section jumps. The
adapter allows the CVar as a player setting. The stock page's
Cinematic Shake row, added without a settings-registry entry, now has one,
and `settings_menu_coverage.py`'s scroll pins follow both rows.

An engine capture on OpenGL opens Game Options > HUD from the title and
toggles the row: the CVar reads 0, then 1, then 0 again
(`.tmp/ui-roadmap/opaque-gl/`). The native Game Options, Settings,
safe-area and pages suites pass.

## The settings sub-pages (31 October 2026)

CONTROLS, GAME OPTIONS and AUDIO were drafted as full-screen panels that
filled the view with as many columns of large cards as it held. They now
follow the main menu's design: each is a sub-page of the SETTINGS page, built
as Create Server is, and no page shows more than two columns of rows.

- **Frame.** `tools/ui/retained_settings_rows.py` puts each page in the
  sub-page frame (`retained_subpage.py`): the top band one notch on, SETTINGS
  as the crumb in the band's thin span, the page's title at the step's foot,
  and the bottom band docked with the prompt bar's Select and Back. A backing
  at the stock page backing's 0.6 darkens both columns, faded in over the
  leading margin. AUDIO's title is now the SETTINGS page's own AUDIO label,
  so the label that carries matches it.
- **Tabs.** A page's sections are tabs (section 13.7) in the Escape card's
  strip (section 14.18) across the content's top, from 44 u and 46 u down,
  clear of the stepped band: the active
  tab's rise, flare and shoulder, the broken baseline and its wash, and the
  Q and E keycaps at its ends, which show the shoulder buttons for a
  controller. Q, E, the shoulder buttons or a click switch tabs, wrapping at
  the ends; the pages cross-fade over 150 ms and the new tab's first row
  takes the focus. Rows on other tabs take no input. A page short enough for
  one screen has no strip, only Create Server's header band with its
  columns' headings.
- **Columns.** Each tab holds at most two columns of 287 u, 13 u apart, of
  section 7 setting rows (`retained_mp_menus.value_row`, as Create Server's)
  on the stock 24 u pitch from 87 u, at most 14 to a column. The values line
  up in one value column, placed so that every label fits in two lines in
  every shipped language: one for all of GAME OPTIONS, one per tab on
  CONTROLS (the keys bound to an action get the room its labels leave) and
  one per column on AUDIO (the device names get theirs). A label that a
  language needs two lines for takes the 14 dp face. The generator fails when
  a tab label, heading or row label would not fit, or a column would
  overflow.
- **Navigation.** Up and Down read down the first column and on down the
  second, then wrap; Up from the first row reaches the tab. Left and Right
  cross to the row beside on a row with no value to change (sliders and lists
  keep them).

| Page | Tabs and columns |
| --- | --- |
| CONTROLS | MOVEMENT, WEAPONS, ATTACK/LOOK, OTHER; each tab's bindings split over two columns, the key hint on the heading line |
| GAME OPTIONS | GENERAL (general / gameplay and view weapon), MOUSE INPUT, CONTROLLER (sticks and look / rumble, gyro, touch and prompts), HUD (HUD / crosshair), VIRTUAL REALITY |
| AUDIO | No tabs: LEVELS and DEVICES / EFFECTS |

GAME OPTIONS lost its Section row: the tabs do its job. After a language
reload the page returns to GENERAL, on Language, from whichever tab was
showing. The sliders show their value as Create Server's and the in-game
Settings page's do; the fine ones also take a typed number (below).

The SETTINGS page goes one level deeper to each, as Multiplayer goes to
Create Server (`BeginRetainedSubpage`, with the new
`RETAINED_SUBPAGE_SETTINGS_*` kinds): its plates fade and sweep, SETTINGS
becomes the crumb, the band steps one notch and the section's label carries
to the step's foot, where the sub-page presents at 350 ms. Back plays the
sub-page's leave and the SETTINGS page returns onto the section at 300 ms;
Back while it goes deeper reverses the change. Reduced motion places each
level at once. A sub-page that cannot open at the hand-over leaves the stock
settings screen to show its panel, as before. From the stock settings screen
and the multiplayer menu's routes the pages still open and close at once.

`openq4-ui-retained-controls`, `-game` and `-audio` check the canvas, the
tabs, the columns, the column-major navigation and Up to the tab, a tab's
rows refusing focus while another is current, and the language reload's
return; `openq4-ui-retained-settings` checks the SETTINGS page's deeper and
reverse events. The gate drives the descent, the Back and the stock
fallback at the hand-over against its doubles. The `settings-subpages`
capture scenario walks the SETTINGS page into each sub-page and every tab.

### Portrait views and typed numbers (31 October 2026)

- **Portrait.** A view of 3:4 or taller rearranges each tab instead of
  shrinking its two columns into the width (section 13.3's compact width: one
  content column; section 13.14 rules out a portrait layout that only shrinks
  the landscape one). The second column stands under the first in one column
  across the content, its rows, headings and the page's note at 587/287 (about
  2.05) times their landscape size, and the column scrolls between the tab
  strip and the bottom band: a 9:16 view shows about twenty rows, with the
  wheel or the focus revealing the rest. The runtime's new view source
  (`"view": "aspect"`, [bindings](bindings.md)) makes the switch below 0.76;
  bindings on every length in a tab's rows carry it, and the rows' vector art
  is drawn in fractions of its box so it scales with them. The frame, the tab
  strip and the prompt bar keep the narrow-view fit; AUDIO's header band gives
  way and its headings stand in the column like the others. Up and Down read
  the same order as before; Left and Right on a CONTROLS row still cross to the
  row beside it, now in the other half of the column. Views taller than 4:3
  also give the rows' area their extra height (`fit_narrow`'s stretch), the
  strip and headings staying at the top.
- **Typed numbers.** A GAME OPTIONS slider with more than a hundred steps has a
  typed number after it (`retained_settings_rows.entry_row`): mouse
  sensitivity, CPI, acceleration, its offset, curve and cap, the gyro and
  touchpad sensitivities and the gun's field of view and X, Y and Z offsets. It
  takes any value in the slider's range to the slider's places (the number
  control's new `decimals`, or whole numbers) through the same
  `settings.player.set` action, and the adapter acknowledges it once the CVar
  reads it back. Text it cannot take shows the field's own range over the row's
  label in the error colour: `#str_231560` ("Enter a number from %s to %s.") in
  every shipped language, the runtime filling in the bounds to the field's
  places ([numeric fields](numeric-fields.md)); generation fails if a
  language's message would not fit the label's two lines. The number follows
  its slider in Up and Down, and Left returns to the slider.
</content>
</invoke>

`openq4-ui-retained-controls`, `-game` and `-audio` also check each page at
9:16 (one column across the content, its rows taller than at 16:9, the
focused last row scrolled into the rows' area, two columns again at 16:9),
and the game test a typed sensitivity: a third decimal place refused, 7.25
committed and settling once the CVar reads it back.
`openq4-ui-retained-screens-menus` checks the view source and
`openq4-ui-number-runtime` the decimals policy and the range message. The
`settings-subpages` capture scenario adds the refused value and the
controller tab's scrolled last row.

## The SYSTEM page as a settings sub-page (31 October 2026)

The opt-in SYSTEM page (`ui_retainedSystem`) was the last drafted full-screen
panel: a hand-authored 8.4 MB document that twelve `update_system_*.py`
scripts patched region by region, its bands and columns of large cards
filling a wide view. It is now the SETTINGS page's DISPLAY sub-page in the
construction CONTROLS, GAME OPTIONS and AUDIO use, generated by
`tools/ui/retained_system.py` with the other retained screens
(`build_retained_screens.py`, 2.2 MB). The twelve scripts and
`system_rendering_controls.py` are retired, and CI checks the page with
`build_retained_screens.py --check` like the rest.

| Tab | First column | Second column |
| --- | --- | --- |
| GRAPHICS | Performance Preset, Auto-Detect, Rendering backend, Rendering device, Renderer Fallback and its fallback line, Restart renderer (while a change waits), the active renderer and the device notice | INTERFACE: UI scale, Text size, UI Aspect, Reset UI sizes and the fit note |
| SCREEN | DISPLAY: Display Device and Expand Across Displays (with several displays), the span's reason line, Fullscreen, Borderless Window, Fullscreen Policy, MSAA, V-Sync | DISPLAY SIZING: Window Width and Height, Display Resolution, the custom-size hint, Custom FS Width and Height, Refresh Rate |
| IMAGE QUALITY | IMAGE QUALITY: Brightness, the five Enable rows, Bloom, SSAO, HDR Tonemap, Post AA Mode, Resolution Scale | RENDERING: Force ambient light, Ambient brightness, CRT Emulation, Irradiance Volumes, Preload Light Grids with its help and the map's light-grid status |

- **What it keeps.** Everything the page did is unchanged: the settings
  service's `settings.system.*` operations and its `settings.*` state, the
  draft every row edits, APPLY CHANGES, the 15-second Keep or Revert
  confirmation and the unapplied-changes question with their scrolling bodies
  at large text sizes, the Menu button's Apply, Performance Preset and
  Auto-Detect, the renderer back end and device lists, the display,
  resolution and refresh lists, the typed window and custom sizes, and the
  typed numbers paired with Brightness and Ambient brightness. Every control
  keeps its id, value, action, options and availability; a one-off
  comparison against the hand-authored document matched every state, action,
  program, presentation variable and binding, apart from the tab condition
  each row's availability now carries.
- **Layout.** Section 7 rows on the 24 u pitch from 87 u in at most two
  columns, 12 slots to a column so that APPLY CHANGES (a primary plate, as
  Create Server's START) and the service's message (or the typed numbers'
  message) stand under them. Status and help lines are notes under their
  row, one to three lines, and the generator fails when one would not fit in
  any shipped language. Slider rows end in their typed number; size rows are
  the field. Every list unfolds within the page (`settings-body`).
- **Dialogs.** The Keep or Revert and unapplied-changes dialogs soften the
  page (or, with soft focus off, scrim it at 0.6) and hold it still: no tab
  changes, no pointer on the page, until they close.
- **Navigation.** As on the other sub-pages: Up and Down read down the first
  column and on down the second, Up from the first row reaches the tab, Q, E
  or the shoulder buttons switch tabs. The page opens on GRAPHICS with the
  focus on Performance Preset.

DISPLAY on the SETTINGS page now goes one level deeper to the page where it
is on (`RETAINED_SUBPAGE_SETTINGS_SYSTEM` in `RETAINED_SETTINGS_SUBPAGES`,
`subpageEnterDisplay`), and its Back, once the draft is settled
(`UI_RetainedSettingsCanReturn`), climbs out onto DISPLAY
(`ReturnSystemSettings` calls `BeginRetainedSubpage`). The page is still the
unique document the session loads at the hand-over and frees on leaving; one
that cannot open leaves the stock settings screen its panel. From the stock
SYSTEM button and the multiplayer route it opens and closes at once, as
before. It stays opt-in: Performance Preset's effects still have no apply
path (`RETAINED_SYSTEM_INCOMPLETE_SETTINGS`).

The SYSTEM native page tests, the static SYSTEM checks (presets, number
fields, the capture contracts), the session gate (descent, Back over a
settled and an unapplied draft, the reversal and the stock fallback) and the
SYSTEM route harness cover it. The `system-optin` (every tab and a list
unfolded), `system-menu-apply` and new `settings-system` (the descent and
Back) capture scenarios pass at 16:9 (OpenGL, English), 4:3 (Vulkan, German)
and 9:16 (OpenGL). A portrait view first scaled the page's two columns down
(`portrait=False`); see the next section for the single column it now takes.

When this landed the rows kept the 24 u pitch whatever the text size and the
prompt bar's Back left a 4:3 screen at 200 %; the next section closes both.

## Settings rows follow the text size (31 October 2026)

The settings sub-pages kept their rows on the 24 u pitch whatever the text
size, so above 100 % the text the runtime enlarged outgrew its rows, and the
prompt bar's BACK ran off a 4:3 screen at 200 %. Section 2's text-scale
contract (100-200 % independently of furniture, labels and rows reflowing)
now holds on CONTROLS, GAME OPTIONS, AUDIO and DISPLAY.

- **Rows.** A new read-only view source, `{"view": "text"}`, publishes the
  text size the runtime applies to font lengths (`Viewport::TextRatio()`,
  1 to 2). Above 100 % a tab takes the portrait layout's single scrolling
  column, and every length in its rows grows by the text size
  (`retained_settings_rows.grown()`), so each row is the 100 % row scaled:
  labels keep their two lines, values their box, sliders, check boxes,
  lists and typed numbers their proportions. The column scrolls between the
  strip and the bottom band; DISPLAY's message and APPLY CHANGES stay under
  it. A tall view keeps its own size, which already exceeds 200 %.
- **Furniture.** Text in fixed slots keeps its authored size at every text
  size (`build_retained_screens.fixed_text()`): the prompt bar on every page
  (so BACK stays on the screen), the settings tab strip, APPLY CHANGES' label
  and every sub-page's crumb and title. The dialogs, the footer message and
  the lists keep following the text size.
- **DISPLAY in portrait.** The SYSTEM page dropped `portrait=False`: a
  portrait view now stands each of its tabs in the single column too.
- **Lists on scaled rows.** A list whose rows the scaling leaves a fraction
  of a pixel tall (25.3105 px at 9:16) never settled once it scrolled: the
  wanted offset alternated between two values that round to different
  pixel strings, so each pass moved the content and the scroll stayed
  unmeasured. `ValueControlView` now keeps the offset it applied for the
  opening while a new one lies within 0.01 px of it.

Captured at 200 % on OpenGL English 16:9 and Vulkan German 4:3, 150 % on
OpenGL 9:16 and 16:9, and 100 % (every tab of the four pages, the descent
and Back). The native page tests check every row, label, value, heading and
note of every tab at 100, 150 and 200 %, the stacked column, the strip, the
prompt bar and APPLY CHANGES.

## Menu levels follow the text size (31 October 2026)

The page-level menus still overflowed above 100 % text: at 200 % the home
navigation plates' labels outgrew the plates, the content column's
description ran past the docked bottom band, page titles clipped in the top
band's slot, the title's plinth links ran into one another and the
confirmations' bodies ran under their actions (the 4:3 German SETTINGS page
showed most of these at once). Section 2's text-scale contract now holds on
the title, both pauses, SETTINGS, Single Player, Campaign and Multiplayer
(visual specification 1.32).

- **Furniture keeps its size.** Text that names a slot of the frame keeps its
  authored size at every text size (`build_retained_screens.fixed_text()`):
  the navigation plates' labels on the stock 30 u pitch (Marine and Strogg),
  which also carry into the title slot; every page title, crumb and carried
  label; the plinth's links; every prompt bar, the title's and the pauses'
  too; a confirmation's title and its action plates; the level block's
  header, level name and objectives heading; and the session's notice plates.
- **Content reflows.** Above 100 % a content column's description widens to
  the 4:3 view's 32 dp margin (224 u from 395 u) and its lines grow with the
  text; the column keeps its place beside the first plate while the running
  language's lines fit above the docked bottom band (420 u) and otherwise
  rises as far as they need, never past the docked top band (60 u). The
  home's message line runs on to the same margin; where the running
  language's sentence needs a second line, or CONTINUE's save title and
  detail (on lines tightened from 24 to 20 and 17 dp of the text size, a
  long one ending in an ellipsis at the margin) need the room, the plinth
  steps down to clear them, short of the prompt bar. A
  confirmation's body of known text grows the dialog by what its lines need
  past the stock box: the frame, the glows and the actions move with it and
  the dialog stays centred. In the level block the picture gives its room to
  the lines (it shrinks to nothing at 200 % and is hidden from 190 %), the
  difficulty, objectives and time lines grow, and the time in the mission
  wraps onto a second line over the save's age; the card keeps its size.
- **Generation guards it.** Every binding that reads the text size must give
  its node's authored value at 100 % in every shipped language
  (`check_text_bindings`), so the 100 % documents present as before; it
  caught the Arena's RESET PROGRESS link, whose label was resized after its
  plate was built (`link()` now takes the size). Generation also fails where
  a shipped language's description, message line, dialog body or time lines
  would outgrow its room at 200 %.

The native page tests (`CheckPageTextScale`) check, at 100, 150 and 200 % on
16:9, 4:3 and 9:16 views, that every shown text of each of the seven
documents stays on the screen, that furniture keeps its authored size, the
content column's box and descriptions, the title's message lines above the
plinth and the plinth above the prompt bar, the level block's lines in order
inside the card, and the exit, quit and LOAD DEFAULTS questions with their
bodies above their actions; and that every node lies where it did once the
text returns to 100 %.

Captured from a candidate runtime (114 scenario runs, 658 engine
screenshots, every run complete): the title, SETTINGS, Multiplayer and
Single Player scenarios at 200 % on 4:3 in all twelve shipped languages; the
pause at 200 % 4:3 in English, French, German, Hungarian and Russian; all
five at 200 % on 16:9 with Vulkan (English, German, Russian) and on 9:16
(English, French, Ukrainian); 150 % in Hungarian on 4:3, 16:9 and 9:16 and in
English on 16:9; 100 % in English on 16:9 and German on 4:3; and CONTINUE at
200 % 4:3 in English and French, where a long save description ends in an
ellipsis at the margin and the French plinth steps down for its two-line
Multiplayer sentence.

Evidence: `.tmp/page-text-scale/validation-evidence.json`, SHA-256
`3f5d2df857c0b4b7511ca509bbe68a4ede7bbff0c151b83579ff9ece2ea7faed`.

Not covered: the Multiplayer page's PLAYER SETUP editor, the Mods dialog's
list, the pause's Objectives page and the list and form pages (Load and Save
Game, Restart Level, Demos, Credits, Arena, Create Server, Join Game and the
Campaign skill page) keep their 100 % rows at larger text sizes. Their
confirmations' bodies grow with the shared dialog, and their titles, plinth
links and prompt bars keep their size.

## List pages follow the text size (2 November 2026)

At a text size above 100 % the list pages' rows kept their 20 u pitch while
the runtime enlarged their text, so a save's or a server's name sat half
outside its row and was cut off, and the selected save's or demo's details
ran into one another and into the plates under them. Section 2's text-scale
contract now holds for the lists of Load and Save Game (Marine and Strogg),
Demos, Join Game and Create Server (`LAY-004`):

- **Rows.** Each row, pager row included, is its 100 % height times the text
  size, with its cells, lights and rule (`retained_saves.grow_rows`), so the
  enlarged text keeps its line and the list scrolls through fewer rows at a
  time. A pager row's marker keeps its size, centred. Above 100 % a name,
  location, map or date past its column ends in an ellipsis (section 2:
  metadata may truncate) instead of being cut.
- **Details.** On Load Game and Demos the selected item's lines under the
  picture stand each on a line of its own above 100 % (Load's time moves
  under its date) at the lines' pitch times the text size, their foot kept
  over the section plates, and end in an ellipsis where a value runs past
  the column. The picture gives them its room, shrinking about its top
  leading corner with its aspect kept, and the error strip stays on its foot
  and grows upward (`grow_details`), as the pause's level block does.
- **Empty states.** The sentence takes the lines the running language's
  longest sentence needs at the running size, never less than its box, and
  NEW GAME or CLEAR FILTERS moves down by what it grew (`grow_empty`).
- **Furniture.** The list headings, the filter tags and the sort and refresh
  buttons, the section plates (LOAD GAME, DELETE GAME, PLAY, DELETE, REFRESH,
  SAVE GAME), the empty states' headings and actions, and Save Game's name
  field keep their size (`fixed_text`). On Join Game and Create Server
  everything but the list's rows keeps its size for now: Join Game's
  sources, mod and game type spinners, filters, status, actions and details
  card, and Create Server's headings, options, ADVANCED OPTIONS and CREATE
  SERVER.
- **Tall views.** The rows keep growing with the text in a view of 3:4 and
  taller, two lines each; the selected item's lines beside the picture keep
  the tall view's own size there, as the settings rows do, since the plates
  under them leave no room to grow.
- **At 100 %** every property of the seven documents evaluates exactly as
  before in landscape and tall views (English and German checked), and the
  Demos captures compare pixel-identical with the previous build's.

The native pages group checks Load Game and Demos at 100, 150 and 200 % on
16:9 and 4:3 (`CheckListTextScale`): rows and their cells at the 100 % row
times the text size, the cells' ellipsis, the detail lines in order under
the picture and above the first plate's band, the picture's aspect,
furniture at its authored size, every shown text on the screen, and every
node back in place at 100 %. Engine captures on OpenGL at 200 %: 16:9 for
Load Game (with its filtered empty state), Save Game, Demos, Join Game and
Create Server, and 9:16 for Load Game.

Not covered: Player Setup, the Mods dialog, the pause's Objectives page,
Create Server's options and Join Game's other controls (which keep their
size), Restart Level, Credits, the Arena page and the campaign skill page
keep their 100 % rows; the scrollbar's wheel step stays one 100 % row.

## The remaining pages follow the text size (3 November 2026)

Section 2's text-scale contract (`LAY-004`) now holds on the retained pages
the list increment left: furniture keeps its size (`fixed_text`), content
grows with the text in the room its frame leaves, and no essential action
leaves the screen.

- **Setting rows** (Create Server's options and its advanced dialog, Player
  Setup, Join Game's FIND PLAYER/CLAN): a column that cannot widen stacks
  each row above 100 % (`retained_mp_menus.stack_row`): the row grows with
  the text, its label runs across the row on the lines the running language
  needs (two where a long translation does), and the value, check box,
  slider or field stands under it from the label's edge, a choice's value
  ending in an ellipsis past the row (its list shows it whole). Check boxes,
  chevrons and slider tracks keep their size; a choice's options grow with
  the text. Create Server's options and advanced rows scroll in their
  column, as Player Setup's already did; a lone entry field (JOIN IP
  ADDRESS) grows in its place. The entry dialogs, the Arena's reset question
  and its ledger grow with what they hold, as a confirmation grows with its
  body (`confirmation`'s `contents_grow`).
- **Player Setup's rail colour** keeps its swatches' size, under its label;
  PREVIOUS, NEXT, CLOSE and the heading keep theirs. The model's name and the
  notice grow.
- **Restart Level** (both families): the chosen difficulty's description
  runs on to the 4:3 view's margin and takes the lines the running language
  needs, and RESTART LEVEL moves down under it; the plates keep their size.
- **The campaign's difficulty** grows its description as a page's content
  column does (`reflow_details`, which now takes a lone label whose column
  starts elsewhere than 395 u), and TURBO's value keeps its size. In
  CHAPTERS the levels' rows grow (`grow_rows`), the chosen level's name and
  explanation grow under the picture, which gives them its room down to the
  docked bottom band (and gives way where too little is left), and START
  CHAPTER and BACK stand side by side under the levels instead.
- **Objectives**: an objective's title takes the lines it needs to hold what
  its two lines held at 100 %, and its description wraps on in its column,
  so the plate grows; both stay over the back bar, which darkens no further
  (text widened past it washed out over a bright view). The empty state's
  line and plate grow; each completed objective's row grows, its title
  ending in an ellipsis. The heading, COMPLETED and the serial keep their
  size.
- **MODS**: the mods' rows grow, and the selected mod's details stand on a
  pitch that grows from the same foot over the actions, the list scrolling
  through fewer rows; NAME, VERSION and the details' labels keep their size.
- **Credits**: a screen is a timed composition that cannot reflow into more
  screens, so each grows as one, lines, pitch and letters, by the text size
  or as far as its lines stand between the title and the prompt bar and end
  before the 4:3 view's margin, whichever is less (from about 1.05 times on
  the fullest screens to 2.1 on the shortest).
- **Join Game**: the scan's status grows upward from its place, the filter
  row and the list's foot moving up with it, and runs on to CLEAR SORTING
  while no player search's tag follows it; the empty state's sentence
  wraps onto the lines it needs, its action moving down; the details card
  grows as one with the text, its headings keeping their size. The sources,
  spinners, filter tags, headings and actions keep their size.
- **Arena**: the chosen match's name, description and info grow under its
  levelshot, which gives them its room from its foot (and gives way, with
  its best-result badge, where too little is left); DIFFICULTY, its steps
  and START MATCH move down only as far as the lines still need. The
  match's and the tier's descriptions keep the three lines the stock boxes
  give them, so at 200 % a long one shows less of itself than at 100 %
  (a known limit: the column has no room for more). The briefing grows down
  the matches' column, its static rule giving way where it would cross the
  bottom band (the path above it says the same); the progress line wraps
  onto a second line under the ladder so that its count stays whole. The
  entrance card grows as one, up to 1.25 times, which the canvas holds.
- **Tall views.** Restart Level, the campaign's difficulty and its chapters
  follow the text in their tall column too, and take it only in a portrait
  view that holds it at the running size (`TallLayout.fits` now takes the
  heights at 150 and 200 %); in the views between, their landscape
  arrangement stands. Their chapters' list shows two levels at least at
  150 % and one at 200 % in the shortest such view. Player Setup gains a
  tall arrangement in the Multiplayer page's column: the heading and rows
  across the column up to 1.6 times their size, the notice under them, and
  the model preview filling the rest of the height with the figure centred
  and its name under it. Credits' names and the Arena's content keep the
  tall view's own size there, as the list pages' details do.
- **At 100 %** every property evaluates as before in landscape and tall
  views (the bindings diffed against the previous documents at view.text 1
  over twelve aspects and every language), but for property defaults made
  explicit (an `overflow: auto` on two rows columns that their rows exactly
  fill, `text-overflow: clip`, `display: block`), vector art converted to
  fractions of the same box, Player Setup's new tall arrangement, and tall
  font sizes of newly fixed furniture that differ from the old rounded
  constants by less than 0.0001 dp. `fit_narrow` now carries a binding on a
  part's `top` over to its centred offset.

The native groups check each page at 100, 150 and 200 % on 16:9, 4:3 and
the 0.78 and 9:16 portrait views (`CheckTextScaleCase`): chains of parts that
must stand one under another without overlapping, parts that must stay
inside their row or dialog, essential controls and every shown text outside
a scrolling body on the screen, furniture at one size (its authored size in
landscape), and every node back in place at 100 %. Credits' lines also stay
between the title and the prompt bar and grow by no more than the text.
Engine captures on OpenGL at 200 %, 16:9 and 9:16, are listed in the
release-completion record.

## Menu levels in tall views (31 October 2026)

In a portrait or rotated window the menu levels shrank the 4:3 composition
into a narrow band, leaving most of the height empty. In a view of 3:4 and
taller (the runtime's `view.aspect` source below 0.76) the title, both
pauses, SETTINGS, Single Player, Campaign and Multiplayer now stand their
parts in one column across the 960 dp canvas (visual specification 1.33):

- **The column.** The parts stand one under the other between the bands
  (`tall_column`): the home's wordmark, plates, message line and plinth; a
  pause's GAME PAUSED, actions, EXIT and level block; a page's plates and its
  description. The column is scaled as one, as large as the view's height
  allows up to 1.6 times (the plates' labels and the wordmark across the
  canvas), and centred in that room. The scale follows the view alone: it is
  the largest at which the column fits at 200 % text, so the furniture keeps
  one size at every text size while the content grows in the room left.
- **Inside it.** A page's description stands under the plates from their
  label inset to a 66 dp margin; the home's message line does too, every
  item's sentence in the room the longest needs, and the plinth under it at
  the column's scale or less, so a long language's links fit the canvas; the
  level block keeps its card, centred, its picture and lines as section 2's
  text scale leaves them. The emblem gives way to the column. Each length is
  the landscape value times the scale (`tall_scale`), so the text-size
  bindings carry over; vector art keeps its box and takes the scale as a
  transform, an identity in a landscape view, so a landscape view presents
  exactly as before (its static captures compare pixel-identical with the
  text-size evidence). The canvas groups run the view's full height
  (`tall_canvas`).
- **Dialogs.** The exit, quit and LOAD DEFAULTS questions stand at 1.6 times
  their size, centred where they were (`tall_dialog`).
- **Generation guards it.** `check_text_bindings` judges each binding's value
  in a landscape view (the tall branch aside), so a binding whose landscape
  value follows another state (the message line's CONTINUE rows) is checked
  against that value.

The native page tests add 3:4 and 9:16 views: the plates and description, the
wordmark, plates, message line and plinth, and the actions, EXIT and card
stand in order between the bands, and furniture keeps one size at every text
size, at most 1.6 times its authored size.

Captured from a candidate runtime (65 scenario runs, 364 engine screenshots,
every run complete): the five page-level scenarios at 200 % on 9:16 in
German, French, Russian and Ukrainian and on 3:4 in German and Russian, at
100 % on 9:16 in English and German and on 3:4 in English, at 150 % on 9:16
in Hungarian and at 200 % on 9:16 with Vulkan; and landscape runs at 100 %
16:9 English and 200 % 4:3 German, whose SETTINGS, Single Player and
Multiplayer pages and LOAD DEFAULTS question compare pixel-identical with the
text-size evidence (the title's montage and the pause's live view change from
run to run).

Evidence: `.tmp/page-text-scale/validation-evidence-tall.json`, SHA-256
`c049efadccaa5a1a382aa003d05e12add1b5e4476b5fbd121bd8d90aae641aec`.

Known limit: the label the home carries into a page's title, and a page's
label carried to a sub-page's, still start from the 4:3 plate's place and
size, since a timeline's keys cannot follow the view.

## List pages in tall views (31 October 2026)

In a view of 3:4 and taller the list pages kept the 4:3 composition at the
view's width and only grew the list, so their text, plates and dialogs read
at about half their landscape size on an upright phone. Load and Save Game,
Demos, Create Server and Join Game now stand in one column across the canvas,
grown to read as they do in a landscape view (visual specification 1.34):

- **Load Game and Demos.** The filter plates (Demos' tags) under the top
  band; the selected item's picture, 1.6 times its landscape size, with its
  details and LOAD GAME and DELETE GAME (PLAY, DELETE and REFRESH) beside it;
  then the headings and the list across the column to the bottom band. A row
  takes two lines at 1.6 times: the save's name over its location, the demo's
  name over its map, date and length.
- **Save Game.** The filters, then the list in two-line rows down to the name
  field and, under it, SAVE GAME at the column's end, 1.2 times their size so
  that each takes a line over the bottom band.
- **Create Server.** MAP SELECTION with the chosen map's levelshot and the map
  list beside it, then SERVER OPTIONS across the column, ADVANCED OPTIONS and
  CREATE SERVER, 1.45 times the landscape size.
- **Join Game.** The mod spinner and source tags in two lines; the sort
  headings and each server in two lines at 1.5 times (its symbols and name,
  then its ping, players, game type and map). Under the list, 1.2 times their
  size so each line fits: the game type spinner with the search tag, the
  filter tags, the status with CLEAR SORTING, the scan bar, the list's
  actions in one column with the levelshot beside them, and JOIN SERVER
  beside ADD TO FAVORITES over the bottom band.
- **Dialogs and backing.** Each page's confirmations and its ADVANCED SERVER
  OPTIONS, ban list, Join IP and Find dialogs grow with the rows, up to the
  column's width: the modal's stage scales about the view's middle, so the
  lengths its text size binds grow with it, and a dialog placed from the
  canvas's top (one whose height follows its text) moves to the view's middle
  in any view taller than the canvas. The page's backing, clear on the leading
  side where a landscape view keeps no content, gains a tall-only shade
  across the whole width that fades with it.
- **One band per row.** A selected row now draws only the selection's light,
  not the hover light over it as well (Appendix B.1: one band per row, the
  selection's first), in every view and every list built on the save rows
  (saves, demos, servers, maps and the ban lists, including the Match Control
  card's and the campaign skill page's), so a focused row's orange name no
  longer sits on a doubled orange band.

The layout is a pass over each fitted document (`tools/ui/retained_tall.py`):
blocks scaled about their origin and moved into the column by bindings whose
landscape value is the fitted document's own, so a view of 3:4 or wider
presents exactly as before. It leaves alone what the runtime writes (now
including a text field's text offset and paint parts, as for a number
field), what a binding already owns and what a timeline animates.

The native pages, Strogg pages, mp-pages, menus, Match Control, card,
welcome, pointer-reach and SETTINGS groups pass, and `ui_retained_gate.py`
passes. Captured on OpenGL at 9:16 (540x960) and 3:4 (768x1024) for the five
pages' scenarios (55 screenshots each), and at 16:9 (1280x720) as the
landscape regression run.

Evidence: `.tmp/menus-portrait/validation-evidence.json`, SHA-256
`3bbe4f57ae023399965a0f9bb3e7fdadb5dc9456fdb1f2438244e6da88c74aac`.

Known limit: Arena, Credits, Restart Level and the multiplayer cards still
take the narrow-view fit, the 4:3 composition at the view's width.

## More pages in tall views (1 November 2026)

The pages that still took the narrow-view fit now stand in one column in a
view of 3:4 and taller as well, at the menu levels' 1.6 times
([menu levels in tall views](#menu-levels-in-tall-views-31-october-2026)):

- **Restart Level** (and its Strogg form): the five difficulty plates, the
  chosen difficulty's description under them from the plates' label inset to
  the column's margin, and RESTART LEVEL under that, centred between the
  docked bands, over a full-width shade.
- **The campaign's difficulty**: its plates with TURBO's value on its plate
  and the description under them, centred the same way. The content keeps its
  centred 720 dp box and the parts move by the column's place less the box's
  top. CHAPTERS stands as the Load Game page does: the chosen level's picture
  with START CHAPTER and BACK beside it, its name and explanation under it
  across the column, then LEVEL and STATUS and the levels in two-line rows
  (name over status) down to the bottom band, the list's height following
  the view. The `campaignskill` capture scenario adds a CHAPTERS shot.
- **Arena pause**: MATCH PAUSED, the actions and the match block under them,
  as the single-player pause's column (`tall_column` and `tall_scale` at its
  scale), and its RESTART MATCH and LEAVE MATCH questions at 1.6 times
  (`tall_dialog`).
- **The restart screen**: the wordmark and its card 1.6 times their size
  about the view's middle, and EXIT GAME's question with them.
- **Credits**: the screens' names, which stand left of the canvas's middle
  beside where the stock page showed its art, grow 1.6 times and move to the
  column's middle as one group (each screen's own transform carries its
  animation); the Raven logo grows 1.3 times; an opaque shade takes the whole
  width, as the backing does its trailing side.

A dialog placed from the canvas's top moves to the view's middle only on a
page whose modal stage runs the view's height; on a page the narrow-view fit
centres, its stage already holds the dialog there. `TallLayout.grow` wraps a
length that follows the text size rather than leaving it, so a grown label
keeps its text-size step.

The native menus, pages, Strogg pages, mp-pages, Match Control, card,
vote/match, welcome, pointer-reach and SETTINGS groups pass, and
`ui_retained_gate.py` passes. Captured on OpenGL at 9:16 and 3:4 (Restart
Level, the campaign's difficulty and its CHAPTERS, Arena pause and the
restart screen), at 9:16 (Credits) and at 16:9 (CHAPTERS, unchanged).

Known limit: the Arena page kept the narrow-view fit here; it stands in
[a tall arrangement of its own](#the-arena-page-in-tall-views-1-november-2026)
since. The multiplayer cards already span the view's width.

## The Arena page in tall views (1 November 2026)

The Arena page was the last list page on the narrow-view fit: on a portrait
or rotated screen its three columns (the tier ladder and progress, the
matches and briefing, the chosen match's detail) shrank into a band. Stacked
one under another they take about 1430 dp at the landscape size, more than a
3:4 view holds between the bands, so a plain stack cannot grow, and a
scrolling column would leave START MATCH out of sight. In a view of 3:4 and
taller the page now stands in two blocks, one under the other, each a column
of plates with what they choose beside it (visual specification 1.35):

- **Tiers.** The five tiers keep their vertical ladder; beside it the chosen
  tier's briefing runs on to the column's margin, and CAMPAIGN PROGRESS, its
  bar as wide, CAMPAIGN COMPLETE and RESET PROGRESS stand at the ladder's
  foot.
- **Matches.** Under the ladder the chosen tier's qualifiers and boss match,
  with DIFFICULTY and START MATCH under them, and beside them the chosen
  match's levelshot with its best result, its name, description, map, game
  type, roster, limits and skill.
- **Size.** The ladder, the matches and the briefing grow 1.4 times, the
  detail as far as the room beside the matches allows (1.29 times) and the
  difficulty and START MATCH to the matches' width (1.25 times). A 3:4 view
  holds the page between the bands; a taller one centres it there.
- **Focus.** The plates, the difficulty's - step and START MATCH run down one
  line: Down from the last open match reaches the step and Up from START
  MATCH returns to it, by authored moves that the landscape geometry already
  made, so landscape focus is unchanged in every lock state. With no column
  of plates beside them, Left and Right from the plates move to the nearest
  control that way: the - step, the last tier or RESET PROGRESS.
- **Ledger, question and entrance.** The result ledger and RESET PROGRESS's
  question grow as the list pages' confirmations do, up to the column's width
  (1.49 and 1.6 times), the entrance's card as far as the column takes it
  (1.19 times), and the backing gains the full-width shade.

`tall_arena_page` in `tools/ui/retained_arena.py` is a `TallLayout` pass over
the fitted document, and the progress bar's fill takes the wider bar's
twentieths. With every tall binding taken wide, the document presents as
before at 16:9, 4:3, 5:4 and 4:5.

The native pages and menus groups pass, and `ui_retained_gate.py` passes.
Captured on OpenGL at 9:16 (540x960), 3:4 (768x1024) and 16:9 (1280x720)
for the `arena`, `arenamatch` and `arenaresult` scenarios.

## Touch in the menus (1 November 2026)

A finger in the menus acted as a mouse: the new cursor stood where it last
touched, the control under it stayed lit after the finger lifted, a list
scrolled only by its 24 dp scrollbar, and a touch that slid off-line still
activated where it was released. Now (section 13.6, INP-014; the routing in
[input routing](input-routing.md#touch-in-the-menus-1-november-2026)):

- **No cursor and no leftover hover.** While a finger leads, the menus draw
  no cursor (the retained adapter's and the stock GUIs'), and a lifted finger
  leaves no pointer behind, so nothing stays lit where it was.
- **Lists follow the finger.** A touch pressed in a scrolling list (Load and
  Save Game, Demos, Join Game's servers, CHAPTERS, the settings pages' rows)
  drags its content once it moves past 8 dp, more up or down than across; the
  press is cancelled, so the row under the finger is not chosen.
- **A moved touch is not a tap.** Past 8 dp in any direction the press no
  longer activates on release. Sliders and scrollbar thumbs keep their own
  drag, and a mouse keeps the desktop rules.
- **Pressed at once.** Touch-down lights the control's pressed treatment in
  the same input, with no tap delay, and a finger that moves on drops it.
- **Lists keep going.** A drag released at speed carries on with the
  finger's velocity and slows over about a third of a second; a finger that
  rests before lifting stops the list where it is, and a new touch,
  navigation or a dialog stops a moving one.
- **The ends light up.** Pulled past its first or last row, a list stays put
  and the rim light at that end brightens with the pull (`marine.rim`, 19 u
  deep, fully lit at 64 dp); a fling that reaches an end lights it by its
  speed, and the light fades over 250 ms once nothing holds it.
- **Swipe between tabs.** A swipe 48 dp across a tab strip (the settings
  pages, the multiplayer card and its summary) shows the next tab, swiping
  toward the leading edge, or the previous one, as Q and E do.
- **Long-press for details.** A finger resting 500 ms on a control that has
  a detail (Join Game's servers) opens its detail card under it instead of
  choosing it; the card stays after the finger lifts, until the next touch.

The real menus had not been receiving any of this: only the
`ui_retainedOpen` preview told the runtime that the pointer was a finger, and
a finger's movement reached a menu only where it lifted. The menus' adapter
now names the source and moves the pointer out after a lift, and the SDL
layer queues a pointer event for each finger motion.

The scrollbar runtime test adds the gestures (a tap under 8 dp, a sideways
move, a drag that the content follows, a finger on the thumb, a mouse that
does not drag) and the rest of the rules: the press at touch-down, inertia
and what stops it, the rim light at both ends and from a fling, long-press
details (with a lift after 500 ms and no frame between), tab-strip swipes
and what is not one. The adapter harness checks the source, the lift and the
swipe events; the input and contract harnesses take the new pointer source.
Not yet: press haptics, which need Android's touch-feedback path, and
qualification on a device.

**Touch target sizes.** Measured in a 9:16 view, most targets are 44 dp of
the canvas or more (rows 96, plates about 58), but a few stay small: the
notices' actions (22), Join Game's footer filters and spinner (29), its sort
headings, Demos' tags and the bottom band's REFRESH LIST (33-36) and the
Arena's difficulty steps (34). The 960 dp canvas spans a phone's width, so a
physical 48 dp target is about 110 canvas dp on a portrait phone; reaching it
takes the Phone viewing profile's interface scale (section 13.2), not more
growth in these layouts. The window's safe area (a camera cutout, the status
and gesture bars) is read since; see [the window's safe area](#the-windows-safe-area-1-november-2026).

## Every portrait view and other aspect ratios (1 November 2026)

A survey at 32:9, 21:9, 5:4, 1:1, 4:5 and 9:21 found every page on the screen
and readable; ultrawide views keep the 4:3 composition centred under the
full-width bands, as designed. The gap was the band between 3:4 and square: a
4:5 window or an unfolded foldable showed the menu levels' 4:3 composition
between two empty bands, since the column waited for 3:4. Now (visual
specification 1.36):

- **A column wherever it reads at its landscape size.** A menu level's column
  scales to the room between the bands, so each document takes its own tall
  condition (`tall_condition`): `tall_column` raises it from 3:4 to the
  aspect at which the column's scale reaches 1 (`tall_fits`), at most a
  square view. The title, Single Player, Campaign, Multiplayer and SETTINGS
  take any portrait view, Arena pause one below 0.97 and the pause one below
  0.83, where its actions, EXIT and level card no longer stand smaller than
  in landscape. The pages that grow a fixed amount declare the room they need
  (`TallLayout.fits`): Restart Level and the restart screen take any portrait
  view, the campaign's difficulty (with CHAPTERS' three rows) one below 0.89,
  Credits one below 0.85. The list pages and the Arena keep 3:4. One
  condition object per document, so every binding of it moves together;
  `check_text_bindings` and `tall_bind` know a tall branch by its form.
- **Focus in tall views.** A walk of every control's Up, Down, Left and Right
  on the tall pages at 9:16 and 16:9 (the harness's semantic menu verb)
  found Up and Down following each column, disabled controls skipped as
  meant. Left and Right still take the runtime's nearest control off the
  line in both views (on Create Server an option's Right reaches the prompt
  bar at 16:9 and the map list at 9:16); a narrower geometric rule would
  change landscape moves the Arena's authored moves were proven against, so
  it stays as it is.
- **Join Game's levelshot frame.** With no server chosen the frame showed an
  empty black square; it now shows the generic art, as Demos' does with no
  demo chosen (the gate's session check expects it).

The native groups pass, the menu levels' text-scale check adding a 0.78 view
(each menu level stands in its column there, `TallMenuLevel`), and
`ui_retained_gate.py` passes. Captured on OpenGL at 4:5 and 0.95 (title,
Multiplayer, SETTINGS, the pause, the campaign's difficulty, Restart Level,
Credits, the restart screen, Arena pause) and at 16:9 (Join Game).

## The window's safe area (1 November 2026)

A phone draws the menus under its status bar, camera cutout and gesture bar,
and its rounded corners cut the view's corners: the top band's titles and
crumbs (10-30 dp from the top), the notices and the prompt bars sat under the
system's own interface. Now (section 13.3, LAY-016):

- **Inside the safe area.** Every retained menu keeps its text and controls
  in the window's safe area: titles and crumbs, notices, columns, lists,
  prompt bars, the bottom band's actions, dialogs, detail cards and opened
  lists. The bands, backdrop, montage, scrims and soft focus still reach the
  view's edges, under the cutout and the bars.
- **The platform.** The SDL layer reads `SDL_GetWindowSafeArea` whenever the
  window's UI viewport changes and on `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED` (a
  rotation, a bar shown or hidden, a fold), as framebuffer-pixel insets
  (`engineWindowState.safeArea*`, rounded outward), and
  `RetainedUI_DefaultViewport` takes the part inside the UI viewport into
  `Viewport::safeLeft`, `safeTop`, `safeRight` and `safeBottom`.
- **The runtime.** A view-height canvas fits the safe area: `DpRatio`
  measures it, as section 13.3's size classes do, so the 960x720 dp canvas
  spans the safe area's width or height. `view.aspect` reads the safe area's
  shape, and four new read-only view sources, `safeLeft`, `safeTop`,
  `safeRight` and `safeBottom`, give its insets in dp
  ([document format](document-format.md#the-windows-safe-area)). A detail
  card keeps 16 dp inside the safe area, and an opened list, bounded or
  not, opens within it 4 dp clear.
- **The documents.** The generator's last pass (`safe_frame` in
  `build_retained_screens.py`) makes each document's root the safe area,
  anchoring its four edges to the sources, and anchors every backdrop that
  fills the root back out by the same insets: a vector, picture, painted
  group or soft focus that fills it through groups that fill it (the lit
  field, the backings and their tall shades, the title's montage, the pause
  screen's scrim and soft focus, each dialog's scrim and soft focus, the
  fades and curtains). A band is drawn 400 dp past its box, so it covers the
  insets already and its edge follows the safe area, carrying its title. A
  root's own backing moves to a node that reaches out (`screen-backing`). The
  tall layouts need nothing more: their view height is the safe area's.
- **Desktop windows.** A desktop window reports the whole window as safe:
  no inset, and every box is where it was. `ui_retainedSafeArea` ("left top
  right bottom" in pixels, unarchived) sets test insets on a desktop; the
  window's own still apply where they reach further.

`openq4-ui-retained-safe-area` lays out the title, the pause screen, the
loading screen, Single Player, the campaign's difficulty, Load Game, Demos,
Credits, the Arena, Join Game, the Welcome card, SETTINGS and GAME OPTIONS at
540x1170 with insets of 6, 72, 6 and 36 px and at 1170x540 with 72, 0, 30 and
24 px, with a notice showing and up to two dialogs open in turn: every text
and control that shows (but the plates that run off the view's edge, whose
labels count) stands in the safe area, the backdrops fill the view, the
canvas fits the safe area and the sources read its insets in dp. With no
inset the root and backdrops fill the view and the sources read nothing, and
a view that loses its insets lays every part out where it stood.
`openq4-ui-retained-screens-menus` checks the sources' schema. The Remastered
HUD's edge anchors have followed the insets since 2 November
([Remastered HUD](remastered-hud.md#the-windows-safe-area-2-november-2026)).
Not yet: the stock GUIs ignore them, swipe controls do not yet keep 24 dp from
the edges, and no device has qualified it.

## The pointer redrawn (2 November 2026)

The 31 October cursor was ear-clipped polygons filled with `_white` and no
edge coverage: its edges stair-stepped, its shadow was two hard offset
copies, and its hand was the bitmap's pixel outline traced.
`src/ui/PointerCursor.cpp` now draws the pointer for the retained menus and
the console. It is vector art in the stock cursor's texel box, tessellated by
the retained vector compiler with analytic edge coverage and drawn with
`_retainedSolid` in one call: a black outline a texel wide, a mid-grey body
darkening downwards, a bright facet and white ridge along the lit 45-degree
edge, a dark tail, and a shadow cast 0.8 texel right and 1.2 down, sampled
nine times over 0.7 texel so it blurs over about a texel and a half. The
hand is gone: a control a press would reach keeps the arrow. Over a number or
text field (`ControlRole::Number`), which a click would edit, the pointer is
an I-beam in the same style, its chamfered serifs stepping into the stem at
45 degrees. Each shape's mesh is built once per size about a hotspot on a
pixel corner and placed at whole pixels, so its edge coverage stays exact.

The pull-down console draws the same pointer in the UI viewport its mouse
maps to, at its text's scale (a texel is 1/480 of the viewport height). Over
the edit line, where a click places the caret, or during a selection begun
there, it is the I-beam; the console no longer loads
`gfx/guis/guicursor_arrow`. The pointer's hotspot is the mouse position its
clicks use. June's shift, which drew the pointer a text row above it, is gone:
the console's rows match their hit rectangles, and with the shift a pointer
on the edit row drew over the log line above.
`conPointerTest <x> <y>` (cheat) opens the console with its mouse at a point
of the 640x480 screen without a device. `screenshot` puts the console away,
so the console's pointer was checked from window captures; the `cursor`
capture scenario shows the menus' arrow over the emblem and over SINGLE
PLAYER and the I-beam over CREATE SERVER's name field. Stock GUIs
(`ui_retained 0`) keep the stock arrow and hand bitmaps. Not yet: the visual
specification's pointer row still lists the hand, and changing it is a
register revision.

## The menu cursor (31 October 2026)

The retained menus drew a placeholder cursor, a flat triangle, and only after
the mouse moved; a menu key hid it again, and with the OS cursor hidden over
the menus the pointer often vanished. The adapter now draws a vector
reconstruction of the stock `guicursor_arrow` and `guicursor_hand` bitmaps at
the stock menus' 32 menu units: the arrow's outline, body, facet, ridge, tail
and soft shadow, and the hand over a control a press would reach (the
runtime's `HoveredControl`). It shows whenever the menus hold the mouse; only
controller navigation hides it until the mouse moves again. Each layer is a
simple polygon, ear-clipped once and drawn with the 2D pass's winding
(`DrawStretchTri` emits its first two corners swapped, which had culled the
placeholder). `openq4_retainedGui pointer <x> <y>` places it without a device,
and the `cursor` capture scenario shows the arrow and the hand.

## Tall views grow the lists (25 October 2026)

In a view taller than the canvas (5:4, and most of all a portrait window)
the list pages centred their 720 dp layout and left the extra height empty
above and below. Load and Save Game, Demos and Join Game now give it to
their list: `fit_narrow`'s `stretch` names the list's viewport, and the
list and its scrollbar keep their top and run to their distance from the
view's bottom, so they show more rows. The parts that start above the
list's foot keep the view's top (the toolbar, the picture, the details and
the plates beside the list) and the parts below it keep the view's bottom
(Join Game's filter row, status, scan bar and plates, Save Game's name and
SAVE GAME, the bottom band and the prompt bar). At 720 dp tall, 4:3 or wider,
nothing moves. Restart Level, Credits, Create Server and the Arena keep the
centred fit: they have no list to grow.

Captured at 9:16 (540x960), 5:4 (1280x1024) and 16:9 for the four pages.

Evidence: `.tmp/ui/stretch-lists/validation-evidence.json`, SHA-256
`9d19f3dae684625dddf4d8c25d1a835238c95dba2fd3a6e88fb0b50b5d4b9322`.

## Shared notices and a lost controller (25 October 2026)

The notices are now the session's layer rather than Join Game's: the
generator adds the three slots to every retained menu (`with_notices`: the
title, the pause, Single Player, Campaign and its difficulty, Load and Save
Game, Restart Level, Demos, the death screen, Credits, the Arena and its
pause, Multiplayer, Create Server and Join Game; not the loading screen, the
Strogg family's pages or the multiplayer cards), and one queue in the session
(`PushRetainedNotice`, `UpdateRetainedNotices`) publishes to whichever of
them shows: a page, or the home over the legacy menu. The stack follows the
player from menu to menu, published in full to each menu as it comes to show;
a home screen, which stays drawn behind a page as its frame, drops it while
the page shows it. A notice's action is the shared `noticeAction<slot>`
(`HandleRetainedNoticeAction`), so Join Game's Undo works from any menu.

Section 13.7's device notice joins it: when the controller in use goes away
(`in_gamepadFamily` falls to `none`), "Controller disconnected." shows with a
controller crossed out (`#str_231541`, every shipped language) and stays past
any hold, since an unresolved state persists, until a controller connects
again; a controller connecting says nothing. `testControllerLoss 1|0`, a
cheat command like `testPromptDevice`, stands in for the loss and return
without touching a device.

The gate's session harness checks the controller notice through the CVar,
its persistence and resolution, the layer following the showing menu and the
home dropping it; the native tests check the slots on Join Game; captures
show the notice on the title, carried to the Load Game page, held past 4 s
and gone on return, and Join Game's stack.

Evidence: `.tmp/ui/shared-notices/validation-evidence.json`, SHA-256
`1f4acec144f950b8a979122487bd200b0726242b2a9c7eb655ab71b06684dea5`.

## Full server names (25 October 2026)

Join Game's rows cut a long server name with an ellipsis, and nothing showed
the rest. The details card now names the server in full over its address
([details card](#join-game-sub-page)): the session publishes
`join.detail.name` with the row it describes, read as a player's text, and a
long name wraps to a second line inside the card. The card grows 30 u for the
two lines and the stock layout below moves down with it.

The mp-pages native test checks a long name wrapping inside the card over the
address and above the rule; the gate's session harness checks the name
follows the described row and clears with it; a capture shows the card on a
real list.

Evidence: `.tmp/ui/card-name/validation-evidence.json`, SHA-256
`ceb19bdb11c43b0f9e39ce7e0921a0642b41dd3ea592beffed199bf5bc390b20`.

## Notices (24 October 2026)

Section 13.13's notices come to the retained pages (`retained_notices.py`):
header-band plates 400 dp wide in the top band's trailing section, 24 dp
from the view's edge, at most three stacked 6 dp apart, newest at the top.
Each holds a status symbol, its Lowpixel 14 dp text and an optional action,
a small plate with its label. The session owns their timing: a notice waits
until the stack has room and no notice is leaving, enters at the top with
`notice.enter` (160 ms, 12 dp in from the trailing edge with opacity) and
holds 4 s, or 8 s when it offers an action; it then leaves with
`notice.leave` (200 ms opacity), its slot rests and the older notices close
up into it. A track runs from the property's present value, so each of these
holds its start from its first millisecond.

Join Game uses them for its favorites: ADD TO FAVORITES says "Added to
favorites." or "Removed from favorites." under the favorite's star, and a
removal offers Undo (`#str_42904`), which puts the server back among the
favorites while the scan holds it and lets the notice leave. Opening the page
drops an earlier visit's notices. The new strings `#str_231539` and
`#str_231540` are in every shipped language.

The mp-pages native test checks the slots' place, contents, Undo's request
and the enter, leave and rest timing; the gate's session harness checks the
holds, the order, the stack of three with a fourth waiting, and Undo. A
capture stacks three notices on a real list and uses Undo.

Evidence: `.tmp/ui/notices/validation-evidence.json`, SHA-256
`b7b8363f05e30ffaa07a6441ccf17f7b4d1f9b314bf0ff3ecbd7424ae934fc5e`.

## Loading rows (24 October 2026)

Section 13.13's loading rows: while a scan has listed no server yet, Join
Game's list shows its ten visible rows at rest without labels, each the
row's own light and rule (`join-loading`), with no shimmer and no spinner,
and neither the empty state nor its actions. The first servers replace them
with `row.reveal` (section 8): each visible row fades in over 120 ms, one
15 ms after the row above, the stagger capped at 90 ms, so all ten are in
by 210 ms (`rowReveal`). The session asks for it (`joinRowsReveal`) only
when servers replace the loading rows: a list a filter emptied and refilled,
more servers arriving, and a page opening on a list show the rows at once. A
track runs from the property's present value, so each row holds 0 from its
first millisecond, and each row's opacity rests at 1.

The mp-pages native test checks the loading rows' place, count and labels,
and the reveal's start, stagger and end; the gate's session harness checks
the request fires once per scan and not for a refilled idle list. A capture
shows the rows during a real scan.

Evidence: `.tmp/ui/loading-rows/validation-evidence.json`, SHA-256
`79800becfa36187923d93fc025c5577a7305bce83bbd7a04f66ab7349c6de10f`.

## Narrow views (24 October 2026)

The retained menus compose a 4:3 canvas that fills the view's height and
widens with the aspect. A view narrower than 4:3 (5:4, or a portrait window)
cut that canvas at both sides: the title's menu lost its left edge and the
pause its level block. The title, both pauses, the Arena pause, the
Multiplayer page and the death screen now fit as the campaign pages already
did (`fit_narrow`, after `retained_menu_levels.fit_page`): `canvas.minWidth`
960 shrinks the canvas to the view's width, each canvas group centres
vertically, and the bottom band and the prompt bar keep the view's bottom
edge while the top band, the page titles and the carried title label keep
its top. The multiplayer cards (Welcome, Escape, summary) shrink so the card
and its 16 dp side margins fit (`fit_card`) and stay centred. In a view of
4:3 or wider nothing changes.

Captured at 9:16 (540x960), 5:4 (1280x1024) and 16:9 for the title, pause,
Multiplayer page, Escape card, death screen and Arena pause.

Evidence: `.tmp/ui/narrow-fit/validation-evidence.json`, SHA-256
`8a2f2b92b83fe012a7649d4a5882b865d3eae801d6997c3f9ebd4db4f664411a`.

The pages that set their prompt bar and bottom-band controls inside their
canvas group (Load and Save Game, Restart Level, Demos, Credits, Create
Server, Join Game, Arena) fit too, through `fit_narrow`'s page mode: their
canvas groups run the view's full height, and each part they place, directly
or through full-size groups, centres by its own offset from the canvas's
middle. A part that lies in the top band (Demos' title) keeps the view's top
edge, and one that starts in the bottom band (the prompt bar, Refresh List)
keeps its distance from the view's bottom, so in a portrait window the bar
stays on the bottom band under a centred page. Join Game's details card
keeps its place: the runtime sets it under the focused row. The gate pins
every menu's `canvas.minWidth` and the Load page's bottom-anchored bar.

Captured at 9:16 for each of those pages, and at 5:4 and 16:9 for Load
Game, Demos and Join Game. The list pages have since taken a tall view's
extra height for their lists ([tall views](#tall-views-grow-the-lists-25-october-2026)).

Evidence for the pages: `.tmp/ui/pages-fit/validation-evidence.json`, SHA-256
`cc5f11fad8fa0b33f019b57dc33c4d6e07db99cd2046ffeb89becb308e7629db`.

## Joined lines and capitals (24 October 2026)

The session no longer joins text in C++ where a language may join it
differently. Each table gives the form:

- `#str_231533` joins two parts of a line as the language joins a list
  (`Session_RetainedList`; a middle dot in every shipped table): the title's
  CONTINUE detail, the pause's time in the mission and the run's total, and
  the loading card's spectators and players connecting.
- `#str_231532` places the time in the mission ("%s in mission"), whose
  order the code fixed before; `#str_231535` orders a save's date and time
  where CONTINUE shows them.
- `#str_231534` writes the load time from its whole and tenth seconds, so a
  language chooses its decimal mark and unit: "1.5 s" in English, "1,5 s" in
  most tables, "1,5 с" in Russian and Ukrainian, "1,5 mp" in Hungarian and
  "1,5 sn" in Turkish.

The Demos page's plates and headings are capitals, as Join Game's and the
save pages' are, in every language: RmlUi's `text-transform` changes ASCII
letters only, so each has a key. PLAY and DELETE are the pages' prompt
verbs, MAP and DATE the stock capital keys, and openQ4 adds REFRESH
(`#str_231536`; the save pages' REFRESH LIST runs past a plate in German),
NAME (`#str_231530`; the stock capital NAME names a player in Ukrainian) and
DURATION (`#str_231531`; Polish and Russian use the shorter DŁUGOŚĆ and
ВРЕМЯ, which fit the column). The gate harness checks that every table's
formats take exactly their arguments, and generation fails if a capital
heading or plate runs past its slot in any language.

Evidence: `.tmp/ui/joined-strings/validation-evidence.json`, SHA-256
`9142f36637682668facf126154792e78c6d94b1235fbe0585b47010438cee682`.

## Page prompt bars

Every page in the docked state carries section 13.4's prompt bar in the
bottom band's raised trailing section, which spans 375.1-717.9 u of the
canvas at every aspect. Back keeps the stock Back position: its 45 dp target
starts where the stock BACK plate's did, at 532,441 u, with its glyph 8 dp in
and its verb, BACK, 6 dp after the glyph. Accept ends 22 dp before Back's
glyph. Each prompt is the title's construction: the active device's glyph
([prompt glyphs](#prompt-glyphs)) and a Marine 14 dp verb at 0.80.

- **Accept follows focus** (section 13.4). Its verb names what the focused
  control does, in the same frame as the focus moves. A button may name its
  verb with its `prompt` (a localization key, or a string expression over
  state, [document format](document-format.md)); the runtime's
  `AcceptPrompt` answers with it for the focused button, or with the bar's
  own SELECT for any other control, and with nothing while no focused
  control can act. The adapter publishes the answer to `prompt.accept`
  before the frame draws, once per change, and Accept leaves the bar while
  it is empty. The pages name LOAD on LOAD GAME and on the chosen save,
  DELETE on DELETE GAME and the Demos page's DELETE, SAVE on SAVE GAME and
  on the chosen save, PLAY on PLAY and on a chosen demo that can play, and
  JOIN on JOIN SERVER; a row that is not chosen yet keeps SELECT, which
  chooses it. The five short verbs are capitals in every shipped language,
  in each page's own terminology (JOIN is the Join Game card's).

- **Pages.** Load Game, Save Game, Restart Level, Demos,
  Credits, Create Server, Join Game and the pause's Objectives page carry the
  bar in place of the stock BACK plate. Back is a button with the plate's
  control ID (`load_back`, `credits_back` and so on) and the page's Back.
  With the pointer anywhere in its target, its verb turns orange and its
  glyph lights from 0.85 to full; focus also underlines it, and a press
  insets it 1 dp. SELECT is a legend for Enter or the south button and never
  takes the pointer: a click would act on a focus that the pointer
  presentation hides. Credits and the Objectives page show Back alone, since
  Accept has nothing else to do there. The Strogg Objectives page tints its
  glyph #FFCC00 and sets BACK in R_Strogg #FCFFC8, turning #FFCC00.
- **Single Player, Campaign, the campaign difficulty page and Multiplayer**
  show the same bar in the same place, as legends, since their own BACK is
  in the content. The bar keeps its place between Multiplayer and Create
  Server or Join Game. At 21:9 it stays on the raised section, where the
  view-anchored bar hung past it.
- **Fit.** `check_page_prompts` fails generation unless every shipped
  language's prompts fit, with the wider of each keycap and a controller
  button: Back's verb ends at least 8 dp inside the 4:3 view, and Accept,
  with SELECT and each verb the page's controls name, starts inside the bar,
  5 u past the riser. The Save and Load pages' toolbar must end before the
  bar begins. German REFRESH did not fit, so the Refresh plates keep SELECT;
  their own labels already name the action.
- **Pointer.** The bar's box runs from 380 u to the 4:3 view's trailing edge
  and covers no control of the page's own. Legend bars, the title's and the
  pauses' included, never take the pointer, so the pause's faded bar cannot
  cover the Objectives page's Back.

Checks:

- The native screens test lays out each page's bar at 16:9 and 21:9 in the
  same canvas place, Back's glyph at every `prompt.glyphs` value and SELECT
  where it shows. The pointer over SELECT lights nothing and a click there
  does nothing; anywhere in Back's target the pointer lights Back, and a
  click runs the page's Back. Single Player's, Campaign's and Multiplayer's
  legends take no input. On the Load, Save, Demos and Join Game pages it
  checks each control's Accept verb, and that the bar shows a published
  verb and drops Accept while none is published.
- The retained runtime test checks `AcceptPrompt` for a named verb, the
  fallback, an expression over state, a state without a localization key,
  a disabled focus and no focus, and that only buttons take a `prompt`,
  which must be text and a localization key when literal.
  `ui_retained_adapter.py` checks the publication: only to a document that
  declares `prompt.accept`, before the frame draws, once per change, and
  empty while nothing focused can act.
- The `prompt-verbs` capture saves twice in airdefense1 and walks the Load
  and Save pages from the pause, taking the bar on the chosen save, another
  save, DELETE GAME and a newly chosen save.
- `ui_retained_gate.py` checks every page's glyph groups with the others'
  ([prompt glyphs](#prompt-glyphs)).

## Prompt glyphs

Every prompt shows the active device's glyph (section 13.5). This covers:

- the prompt bars of the title, Single Player, Campaign, the Marine and
  Strogg pause menus and the Multiplayer page, and the pages' own
  ([page prompt bars](#page-prompt-bars));
- the loading screen's continue prompt;
- the multiplayer cards' prompt bars and the Escape card's tab-strip keys
  ([multiplayer menus](multiplayer-menus.md)).

While the adapter-owned `prompt.glyphs` is 0, a prompt shows the keyboard's
keycap. At 1–5 it shows the controller family's button that does the same in
menus ([input routing](input-routing.md)): south for Enter's Accept, east for
Escape's Back, and the left and right shoulders for the cards' Q and E.

The glyphs are vector symbols in the family text color, whose shape and
letter carry the identity:

- **Xbox and Nintendo** print letters on a disc. Nintendo's A and B swap
  places: B on the south, A on the east.
- **PlayStation** prints its cross, circle, square and triangle on a disc.
- **Steam Deck** prints its letters inside a ring.
- **Generic** draws the four positions as a diamond, with the button's one
  filled.

Shoulders draw the bumper as a 13 dp plate rounded on its top corners with
the family's label in strokes:

- LB and RB on Xbox.
- L1 and R1 on PlayStation, and in an outlined plate on Steam Deck.
- L and R on Nintendo.
- A generic controller's plate is outlined, with a chevron pointing to its
  side.

On the Escape card's tab strip, the shoulder takes the keycap's 20x22 dp and
answers the tab key's states.

Accept and Back prompts follow Swap Confirm and Back
([input routing](input-routing.md)). Each also carries every family's button
at the other of the two positions (`<prompt>-<family>-swapped`), shown while
the adapter-owned `prompt.swapped` is set. So a Nintendo pad's SELECT shows
its east A and EXIT its south B, and a forced swap on an Xbox pad shows B and
A.

The letters and symbols are strokes in a 22 dp box, so no font or string
stands in for them. The prompt bar's glyph is 22 dp square and centered on
its verb. The title's and the pauses' bars still end where they did
whichever glyph shows, because they are anchored at their trailing edge; a
page's bar is anchored at Back's target instead. The Strogg pause tints
its glyphs as it tints its keycaps. The loading screen draws its south button
at 18 dp, beside CONTINUE.

Checks:

- `ui_retained_gate.py` compiles the backend's family mapping,
  `RetainedUI_PromptGlyphs` and the swap (`in_swapConfirmBack` and
  `idKeyInput::MenuControllerKey`) against stand-ins, pins every menu input
  path's translation, and checks that the 35 Accept and Back glyph groups
  carry both positions. It pins where the backend sets
  and clears the family, and checks that the adapter publishes before the
  frame draws. It also checks that each of the 17 documents with prompts
  binds every glyph to its family's index, and that both cards give the
  shoulders to their tab keys and Tabs prompt.
- The adapter harness checks the publication: once per change, in the
  session's state pushes, and never to a document without the state.
- The native screens test lays out the title's bar at every value, the
  pages' bars ([page prompt bars](#page-prompt-bars)), and the Escape card's strip
  keys and prompt bar at every value.
- The `prompts` capture shows each presentation, `testPromptDevice`
  standing in for the device and `in_promptStyle` forcing the family, with
  Confirm kept south; then a forced Nintendo style's automatic swap and Xbox
  with `in_swapConfirmBack 2`. It touches no device. The `loading` capture
  shows the Xbox continue prompt.
  `mpcard-prompts` does the same on the Escape card over a listen server.

## Hand-off to the Welcome card

A multiplayer load that ends at the Welcome card, with `ui_autoJoin` off and
the card on, keeps the loading screen up, reading JOINING, until the card
presents (`BeginRetainedLoadingHold`), in place of the load's wipe: the join
never shows the bare match or the stock menu in between. The session draws
the held screen above the game and its menu, under any wipe and the console,
and user commands wait while it holds. On the frame after the card first
presents, the screen fades out over 250 ms (the runtime's 80 ms under reduced
motion; the document's `handoff`) over the card and its softened match, so
the card's first frame, the slowest, draws under the screen. Another screen
taking over (the stock menu in the card's place, another GUI, a retained
dialog) ends the hold with the same fade, and so does its ceiling, 5 s; a new
load or a stop drops it at once. Each load presents the screen whole again
(`present`).

The presentation clock stands still through the load and the rest of its
frame, then catches up at once, so the hold counts its 5 s, and may fade,
only from the first frame the clock moves. A listen server's card presented
in the load's own frame; a remote client's came 2 to 4 s after it, the menu's
first opening among that time.

## Validation

- `openq4-ui-retained-screens-*` (native) loads the four production documents,
  lays them out at 1280x720 (the title also at 1920x1080) and drives their
  state. It checks the schema additions, the additive and multiplied draws,
  the transformed clip, the emblem glint and its reduced-motion rule, the
  levelshot drift, the phase line, the percentage and the controller prompt,
  the multiplayer server card and JOINING, the hand-off's fade over 250 ms
  (80 ms under reduced motion) and `present` making the screen whole again,
  the arsenal's fade, tint and two rows, the tips (hidden until published, the slot cross-fade, the tag and
  two-line box inside the band, and the band given to the continue prompt
  when the level is ready, or to the arsenal in multiplayer), the title
  carry on both menus, the
  depth lean and its reduced-motion hold, the exit confirmation's lit margin,
  title placement, body placement and single 0.85 outline composite, its
  enter and leave timing with the contents and focus waiting for the
  entrance, a completion due before an event running before its program and
  a reopen during the leave, and that every action is an allowlisted session
  verb. For the Strogg pause it checks the faces, the rune copies of the
  published lines, the grain, the label wrapper that the scan bar spans, the
  translation's order and timing (RESUME by 320 ms, QUIT TO MENU and the
  level block still waiting, GAME PAUSED white and settling to #FCFFC8 at
  0.80), the focus scan bar, reduced motion and the soft focus. For the
  Objectives page it checks the hand-off and the entry motion, a plate per
  open objective with its description and screenshot, the title's 308 u
  wrap, a long description growing its plate, the completed rows, the
  scrollbar's focus and the down input scrolling the list, a snapshot
  restored with the page open keeping its scroll, Back closing the page with
  the bands home and a second Back ignored, a reopened list at its top, a
  pause opening without the page, the empty state's static and line, the
  level block's rows with their state, and the Strogg page's heading, serial,
  BACK prompt and return. It checks that the back bar lies under the bands.
  It runs as one meson test per group of documents,
  `openq4-ui-retained-screens-<group>` (`menus`, `loading-hud`, `pages`,
  `mp-pages`, `mp-card`, `mp-vote-match` and `mp-settings-welcome`), side
  by side in the suite; given no group, its executable runs every check.
- `openq4-retained-ui` and `openq4-ui-document` cover timeline completion
  programs: firing once at the end, cancellation, replay, pause, reduced
  motion and a snapshot taken while a completion is due, and the rejected
  schemas (unknown event, endless timeline, actions and completion cycles).
- `tools/tests/ui_retained_layer_pool.py` runs the host's layer allocation
  and its opacity and mask composites against a counted renderer, and pins
  the Vulkan executor's render-texture row order that the composites sample.
- `tools/tests/ui_retained_gate.py` qualifies the gate as described above,
  including the startup preload, the level-load precache, the loading phase
  line and the published pointer, and pins the frame pump order that lets
  RESUME retire the pause screen. It covers the release after RESUME: the
  closed screen kept for 250 ms and then let go, a new pause, a disconnect
  or a level load ending it, and nothing released when the pause hands over
  to another screen or the title closes; it pins the release's draw over the
  game and its 250 ms against the documents' timeline. It covers the Strogg pause's selection: the
  game asked once a level and again after each level load, the title never
  asking, and the Marine pause standing in for a Strogg pause that is not
  installed or stopped drawing. It pins the game's `retainedPauseFamily`
  answer, and compiles the host's face names and rune fold to check that
  every code point of the shipped string tables, and every other one below
  U+FFFF, folds onto a rune the face has or onto a space. It covers the
  Objectives page's data: the level's screenshots resolved inside the load
  (after the game spawns the map, before the media finish), only installed
  plain image names shown, the held objectives' screenshots among them, and
  the time line's last save: read once in the level load, not when the pause
  opens, absent with the gate off, and restamped by a save.
- The screens test also lays out the Single Player page and its Campaign
  sub-page: the page title in the slot, the sub-page's band one notch on with
  the bottom band docked, the crumb's rise, fit, floor (in Italian) and cut,
  the sub-page title at the step's foot, both changes' timing (the plates
  half gone, the band stepping and the page a quarter swept at 75 ms, the
  crumb by 150 ms, the step, sweep and carry at 350 ms, the backing fading
  in, the title dropping at once on Back, the crumb returning over the last
  150 ms, the backing out and the sub-page swept by 300 ms, the page's first
  frame already docked and fading back in place), the rest each document
  takes when hidden, the French crumb at 80 %, Back reversing a change from
  where the band stands, and the reduced-motion arrival. The gate test covers
  the hand-over: each half's event, the 350 ms and 300 ms waits, the
  immediate hand-over under reduced motion, a press held and a release
  forwarded mid-change, no second request, Back reversing over 150 or 250 ms
  by the step's distance (and not reversing a Back), the change abandoned
  when another screen takes over, and the stock selector at once when the
  sub-page cannot present. It pins the frame pump and input hold, the halves'
  durations against the session's waits, the four reversal lengths and that
  rest returns everything a change moves. It covers the loading tips: the
  first as the screen presents, the next every 6 s in the other slot, a new
  load starting elsewhere in the list, none in multiplayer or while another
  screen presents the load, the calls from the load's redraws, and that every
  tip in every shipped language fits its two lines.
- The gate test also compiles the server card's production sanitizer, roster
  store and publish with idlib's own escape parser: escapes, control bytes,
  malformed and cut UTF-8, line limits and `#str_` names; players by team,
  six names and "+N more", spectators and players connecting, a game that does
  not answer, a load that knows no players, the store's 32 slots, and the
  roster cleared after each publish. It pins where the network code captures
  the roster (before `InitClient` and the listen host's map load, before a
  client's `InitGame`, and the connect response's block between the server
  info and the send, read behind its tag and length and never ending the
  connection), game_mp's answer, `si_motd` in both game modules, and that
  every language's roster lines carry one count and fit the card.
  `openq4-connect-roster-test` covers the block's codec: a round trip, every
  truncation, another version, counts, slots, teams and flags out of range,
  the longest name and one byte more, a full roster and capacity. The screens
  test lays the card out from its foot: its base height with no players and
  no message, the address in Profont, the team columns under their heads in
  the team colors, a list's line breaks drawing no glyph, the spectators line
  beneath, a one-line message and a long one clipped at three lines, the
  card clear of the top band, one list outside team modes and the line alone
  without names.
- `tools/ui/capture_retained_screens.py` captures the screens in the engine
  from a hidden window with engine screenshots and semantic
  `openq4_retainedGui` operations, never OS input (`lines` sets a multi-line
  state, one argument per line, as the console turns backslashes into
  slashes). With `--gate 0` the title
  and pause scenarios, including the pause scenario's real map load, check
  that nothing retained loads; the loading scenario drives its document through
  `testGUI` and is not a gate check. Its default, `--gate default`, leaves
  `ui_retained` unset, as a player has it. The `fallback-*` scenarios run as
  a private mod in the savepath, whose files take precedence over openQ4's
  packs as any mod's do (loose files in the savepath alone cannot, because
  the savepath is searched last): a title, SYSTEM or Single Player document
  that cannot load, a main menu without its Demos page, and
  (`fallback-loading`) a mod's own `splevel` and `generic` loading GUIs, under
  which game/airdefense2 loads without the retained loading screen. `system-default`
  and `system-optin` press the SYSTEM button with the page off and opted into.
  `pause-strogg` pauses game/recomp, where Kane is Strogg, mid-translation
  and at rest, then focuses a plate, opens the quit confirmation and resumes.
  The pause scenarios wait for the confirmation's 300 ms leave before they
  focus RESUME, because the modal keeps focus until it closes.
  `pause-objectives-page` gives airdefense1's first objective, completes it
  and gives two more, then opens the Objectives page, scrolls and closes it.
  `subpage` opens the Single Player page, goes deeper to Campaign and comes
  back, with shots mid-change and at rest; `subpage-reduced` does the same
  under reduced motion. Back during a change reaches the session only as
  real input, so the gate and screens tests cover the reversal.
- `tools/tests/ui_retained_gate.py` compiles the session's loading hold
  against a stand-in session and drives it: who it holds for (each refusal:
  the stock screen, single player, a demo, `ui_autoJoin`, the card off or
  fallen back, a mod's menu), a listen server's join on the clock the load
  left standing, a remote client's card coming seconds later and fading on
  the next frame, the ceiling counted from the first moving frame, the
  screens that end it and the drops. It pins where the session begins it in
  place of the wipe, draws it under the wipe, updates it before the card and
  holds user commands, and the loading document's `handoff` and `present`.
- Live probes with `ui_autoJoin 0` traced the hold and captured it: a listen
  server on OpenGL in English, its card in the load's own frame and the
  fade over the next 60 and 120 ms; a remote client on OpenGL whose card came
  2 to 4 s after the load, JOINING held until then; Vulkan in German under
  reduced motion; and no hold with `ui_autoJoin 1` or `ui_retained 0`.

## Evidence

Built as a debug engine and staged into `.install`; the six expensive layout
tests ran from an optimized build with assertions enabled. On Windows 11 with
an NVIDIA GeForce RTX 4060 Laptop GPU, twelve scenario runs captured the
screens on OpenGL and Vulkan (1.4.325) at 1280x720, on OpenGL at 1024x768, and
with the gate off. All exited cleanly with no errors. The gate-on runs
presented all three screens, and RESUME retired the pause screen. The gate-off
title and pause runs, including a real map load, loaded no retained document.
The reviewed images include the title and its glint, the exit modal at 4:3,
the pause screen before and after RESUME, the loading bar clipped at 62%, the
Vulkan title and loading screens, and the stock title with the gate off. All
106 other Meson tests, the optimized layout tests and the commit-validation
Python list pass. Evidence: `.tmp/ui/retained-screens/validation-evidence.json`,
SHA-256 `e645a25b3daae42e1b4cc5a6301fc67cfc8c31c85895759f92fb4a179697d5b9`.

The Remastered loading progress was captured the same way. Real single-player
loads traced MAP 1/4, WORLD 2/4, ASSETS 3/4, FINISHING 4/4 and the load time,
in that order, on OpenGL and Vulkan at 1280x720 and on OpenGL at 1024x768.
The loading scenario rendered the phase with its asset count, the percentage,
the load time and the controller prompt on both backends; the gate-off runs
loaded no retained document. Evidence:
`.tmp/ui/retained-loading/validation-evidence.json`, SHA-256
`9961bf8402d8897d4d95be20ab20f780ccd371c674c0b7371b676c4bdcce2fc8`.

The Remastered multiplayer composition was captured on OpenGL and Vulkan at
1280x720 and on OpenGL at 1024x768: the band stays low, the server card stands
clear of the lower corner bracket, the message line reads over a bright
levelshot, and a finished multiplayer load reads JOINING. Evidence:
`.tmp/ui/retained-loading-mp/validation-evidence.json`, SHA-256
`238cca71ae82b18ded323317822f1e770b3db55f4ebeea9afc58753cb73994db`.

The title carry was captured on OpenGL and Vulkan at 1280x720 and on OpenGL
at 1024x768: SETTINGS travels toward the title slot as the bands dock, and
the stock Settings page then shows its own title in that slot. The gate-off
title and pause runs loaded no retained document. Evidence:
`.tmp/ui/retained-title-carry/validation-evidence.json`, SHA-256
`36aa73558fb395d333351f52102d070871fc69352136a168137f07cd57785b69`.

The title's depth lean was captured on OpenGL and Vulkan at 1280x720 and on
OpenGL at 1024x768: with the pointer at the top-trailing corner the bands
and backdrop lean while the wordmark and navigation stay in place. Evidence:
`.tmp/ui/retained-title-depth/validation-evidence.json`, SHA-256
`938294a3434fcd14513e9c5b54fa3efa7c53913a7544700e62a0f4d4d9f835ce`.

The confirmations were captured on OpenGL and Vulkan at 1280x720 and on
OpenGL at 1024x768, beside an engine capture of the stock exit dialog. These
match the stock to within a pixel at 1280x720: the silhouette edges, the
raised top line, the slot floor, the bottom edge, the title's cap line, the
body's baseline and the action row. The retained faces' side bearings start
the title's ink 3 px before the stock's. With the layer fix, the Vulkan
dialogs match OpenGL. Before it, their outline, plates and markers were
missing there, as were the title menu's navigation plates. Evidence:
`.tmp/ui/retained-modal/validation-evidence.json`, SHA-256
`32b9bbf8b67cf36b59dca317274579f637bb2eda2bc7c3000b7122160b9f8945`.

Timeline completion programs and the confirmations' stock motion were
qualified by native tests in an optimized build of a clean worktree. All 107
Meson tests pass, among them the completion schema and timing tests, a
snapshot taken while a completion is due, and the exit confirmation's enter
and leave. Without the completion run that precedes an event's program, the
screens test fails. There are no engine captures of the transitions: the
only client path with firewall rules, `.install`, was in use by another
session. Evidence: `.tmp/ui/retained-modal-motion/validation-evidence.json`,
SHA-256 `a23a96a22a4abd67ded93a9a00624ffa6dba93d4d4275170346aafc0c2ba5b70`.

The retained title and pause screens' section 8 tokens were qualified by the
native screens test. On depart, the plinth and its links are gone at 50 ms
while the home plates are at 0.8. The wordmark sits at 40% while the Exit
modal is open and is restored 200 ms into its leave. Evidence:
`.tmp/ui/retained-motion-tokens/validation-evidence.json`, SHA-256
`0d139aa4bd795cece9cf6e7ebda88883b0b52bd53c1ce8ab9544a9ca3b082218`.

The motion was then captured in the engine, on OpenGL and Vulkan at
1280x720, from a candidate runtime built from the pushed code. The exit
confirmation appeared half way in, open, holding, releasing and closed,
and the plinth was gone 30 ms into a depart. The captures exposed the
additive-picture layer fault above. Fixed, the wordmark's rectangle reads
69 on OpenGL and 68.7 on Vulkan instead of a dark 26. Evidence:
`.tmp/ui/retained-motion-engine/validation-evidence.json`, SHA-256
`b915a8889da4535c7b4a19fc6c9af500ad8bc1dccd5608dafe6057221189cbf9`.

Soft focus was captured in the engine on OpenGL and Vulkan at 1280x720 and
on OpenGL at 1920x1080, from a candidate runtime of this build. Over the
title, the screen beneath the open exit dialog is blurred and desaturated
but not dimmed: away from the dialog its luminance holds, while its
saturation falls to about 0.8. The glow spreads past the dialog, and
Vulkan matches OpenGL. The blur keeps the same look at both sizes, and the
quit dialog softens the paused view. With `ui_retainedOpaqueBacking 1`
the stock scrim and column return, and the blur program never loads.
Native tests pin the ramp, the hold and the release, and a host harness
pins the capture and passes. Evidence:
`.tmp/ui/retained-softfocus/validation-evidence.json`, SHA-256
`ae57a44a9b1ddf3d582d1caa7d8565e82582b76a3c63f09717a7f8e5efec6eaf`.

The softened paused view was captured the same way on OpenGL and Vulkan at
1280x720, over airdefense1. The view is blurred and desaturated, the HUD
crosshair blurs away, and the frame is not darkened. The darkening fallback
keeps the crosshair sharp, and the quit confirmation softens the pause
screen above the softened view. The screens test pins the ramp, a page's
return and the next pause; without the ramp's restart key it fails.
Evidence: `.tmp/ui/retained-pause-softfocus/validation-evidence.json`,
SHA-256 `1443f7b6398db734e30c564d03a831f0aa401f3c3940d726a31bcb0ca9e4268d`.

The SYSTEM page's dialogs were qualified by native tests against the real
document: without soft focus the 0.6 backing shows and no backdrop pass
runs; with it the backing hides and one pass softens the screen beneath.
All SYSTEM tests and updater checks pass; there is no engine capture of
these dialogs yet. Evidence:
`.tmp/ui/system-dialog-softfocus/validation-evidence.json`, SHA-256
`4e33e60981080475ab3a66f6098b885008ebb2dd657a7211c570cc8de342a6c5`.

The default and the fallbacks were captured on OpenGL at 1280x720 and
1024x768 and on Vulkan at 1280x720, from a candidate runtime of this build,
with `ui_retained` unset. The title, pause and loading screens presented in
their retained form. Run as a private mod:

- a title document that cannot load left the stock title, with one warning,
  while the pause screen stayed retained;
- a main menu without its Demos page presented itself, with no warning, and
  nothing retained loaded;
- an opted-in SYSTEM page that cannot load opened the stock SYSTEM page from
  the same click;
- a Single Player selector that cannot load opened the stock selector.

By default the SYSTEM button opened the stock SYSTEM page, and the retained
page when it was opted into. With `ui_retained 0` the title, a real map load
and the SYSTEM button loaded nothing retained. All 18 runs exited cleanly with
no errors. Evidence: `.tmp/ui/retained-default/validation-evidence.json`,
SHA-256 `4aae5c75825a2036a9569bc19496bca79d567c3cf7ce682ca28dd576c19cf7f7`.

The pause screen's level block was captured over airdefense1 on OpenGL and
Vulkan at 1280x720: it shows the intro art the level's loading screen uses,
where it showed the generic loadscreen. Evidence:
`.tmp/ui/retained-pause-shot/validation-evidence.json`, SHA-256
`d3b289962528acd575ddf5fc17033f8ece7e2e159d89d3724d2e10d36ffa6a06`.

The live level block was captured over airdefense1 on OpenGL and Vulkan at
1280x720. With the level's first two objectives given, it lists Retrieve
Medic above Regroup with Rhino Squad and the time in the mission under the
rule; without them it shows the time line alone. Evidence:
`.tmp/ui/retained-pause-objectives/validation-evidence.json`, SHA-256
`43f7a4867c20868c828296b1faf46a1bd64c1282538da0d113b0e58e7df2074b`.

The Strogg pause was captured over game/recomp, where Kane is Strogg, on
OpenGL and Vulkan at 1280x720 and on OpenGL at 1024x768: mid-translation,
with the actions still in runes, then at rest, with a focused plate's scan
bar, the quit confirmation and RESUME. airdefense1 still pauses in the
Marine family. Vulkan matches OpenGL within two levels at rest, and with
`ui_retained 0` the Strogg level loaded nothing retained. All runs exited
cleanly with no errors, and all 109 Meson tests pass. Evidence:
`.tmp/ui/retained-pause-strogg/validation-evidence.json`, SHA-256
`2180c19336bea8e678a54440b5eacb20ab01d31a4a5121893dcbc9e725886894`.

The Objectives page was captured over airdefense1, with its first objective
given and completed and two more given, on OpenGL and Vulkan at 1280x720 and
on OpenGL at 1024x768: docking, the page with both plates and the completed
row, scrolled, and closed, the level block then listing the two open
objectives above the completed one and "saved just now" after the level's
start autosave. Over game/recomp the Strogg page shows its plate, serial and
Strogg BACK. No run loaded a picture outside the level load, the Strogg
page matches across renderers within two levels (the Marine page differs
only in the moving sky behind the plates), and with `ui_retained 0` nothing
retained loaded. All runs exited cleanly with no errors, and all 109 Meson
tests pass. Evidence:
`.tmp/ui/retained-pause-objectives-page/validation-evidence.json`, SHA-256
`f700445cf82cb8e6e7e01ba64b30bdbe8e55adcb22fbb0237e23d9976f0577c6`.

The release after RESUME was captured over airdefense1 and game/recomp on
OpenGL and Vulkan at 1280x720: about 100 ms after RESUME the screen is gone
and the running game, HUD included, is still softened (a quarter of the
resumed view's sharpness on both renderers), and the status line reports
the release; 800 ms after it the view is sharp and the release has ended.
With `ui_retained 0` nothing retained loaded and nothing was released. All
runs exited cleanly with no errors, and all 109 Meson tests pass. Evidence:
`.tmp/ui/retained-pause-release/validation-evidence.json`, SHA-256
`f7e0da538649ac0ccb7f21133ffd74133909e3bf25dc342bad0d57a48ef8f957`.

A private mod supplying its own `splevel` and `generic` loading GUIs loaded
game/airdefense2 on OpenGL and Vulkan at 1280x720 under its own `generic`
screen: the retained loading screen never loaded, and the developer log
names the mod's file. The same sets load airdefense1 under the retained
loading screen without the mod. Evidence:
`.tmp/ui/retained-mod-loading/validation-evidence.json`, SHA-256
`a5bf27df44fef680527d2358f64eac706d3bbac6a0f2f1a97a9a1ddaa11bf874`.

A listen server loading mp/q4dm1 published its eleven kinds of item to the
arsenal, each with the image and tint read from its colour-coded material
(the lightning gun 1, 1, 0.73, the railgun 0, 1, 0, the large armor red),
and the loading scenario's arsenal, published the same way, draws tinted in
the thick band on OpenGL and Vulkan at 1280x720, identically on both.
Evidence: `.tmp/ui/retained-loading-arsenal/validation-evidence.json`,
SHA-256 `a2bd70225491d8ea1ff802bcb3b046b68ac7ca17136e49d46680faf92ceee094`.

The Single Player page went deeper to Campaign and back on OpenGL and Vulkan
at 1280x720 (identical to within one level) and on OpenGL at 1024x768: the
session handed over at 350 ms and 300 ms, the band stepped one notch, the
crumb read SINGLE PLAYER in the band's thin span, and Back returned the focus
to Campaign. Under reduced motion each level presented at once, and with
`ui_retained 0` the stock selectors opened and nothing retained loaded.
Evidence: `.tmp/ui/retained-subpage/validation-evidence.json`, SHA-256
`eca04d36298751db3331776e99e4fde0e3c123a6bb06570d0eed62c1935b44b5`.

Single player tips were captured on OpenGL and Vulkan at 1280x720, on OpenGL at
1024x768 and in German, Polish and Russian: the TIP tag over a two-line tip in
the band's leading part while the level loads, the next one cross-fading in the
other slot, and the band given to the continue prompt once the level is ready.
A real airdefense1 load showed the tips changing every 6 s. Evidence:
`.tmp/ui/retained-loading-tips/validation-evidence.json`, SHA-256
`4f0f69638a05f185f8018379dd746d6cd804d2eef97d7b66d3206dc090dea4b4`.

The server card's players and message were captured on OpenGL and Vulkan at
1280x720, on OpenGL at 1024x768 and in German, Polish and Russian: the team
columns under MARINES and STROGG over the spectators line and a three-line
message, and one list with "+3 more" outside team modes, the card growing up
from its foot. A listen host's map change and a second client's fresh
connection traced the players they published. Evidence:
`.tmp/ui/retained-server-card/validation-evidence.json`, SHA-256
`dc352d1c28dfe32f9057dcb7d8ee69d17ad11ba1712b42cb1472c236b617c113`.

The hand-off to the Welcome card was traced and captured on listen servers and a remote client with `ui_autoJoin 0`: JOINING held until the card presented and then faded out over it, on OpenGL in English and on Vulkan in German under reduced motion, and no hold with automatic joining or the stock screens. Evidence: `.tmp/ui/retained-loading-handoff/validation-evidence.json`, SHA-256
`ed845b87064b11dea753eab7d7f27b70ad79d07e649d400d0c43325619e02c1e`.

The Load Game page: `.tmp/ui/retained-loadgame-evidence/validation-evidence.json`, SHA-256
`0ca1295e613bac04ccb9625f73af37b3bafe0311b1dc7d0b75f4f3eda9ffed49`.

The Save Game page and typed characters: `.tmp/ui/retained-savegame-evidence/validation-evidence.json`,
SHA-256 `a19668e3ec74f32813923274778912d3dadfe6f682bc0867a75f259f61966b9a`.

The Restart Level page: `.tmp/ui/retained-restartlevel-evidence/validation-evidence.json`,
SHA-256 `c59bf89c0bccdfa9615b95eacd3b215bd181b71c5c755769df3d530b9f21d3d9`.

The Demos page: `.tmp/ui/retained-demos-evidence/validation-evidence.json`,
SHA-256 `6af050d9337ba49f1906fc18bfc799af9adde4b8c2aa4b70dee997d98747ae9f`.

The MODS dialog: `.tmp/ui/retained-mods-evidence/validation-evidence.json`,
SHA-256 `6be45e12dadc8a14882242127fc431e54e54504b23ceb43a71a91b6cb329339d`.

The restart screen: `.tmp/ui/retained-restartmenu-evidence/validation-evidence.json`,
SHA-256 `c0efdf55a7abe4f8ac14bd3239fd2fdbae555b9cded737973c565211f2eb8fdf`.

The Credits page: `.tmp/ui/retained-credits-evidence/validation-evidence.json`,
SHA-256 `d6ac5ccd40691bbd7b368514a72dbc9278374db007fab76b14be34e98efc1045`.

The Arena page: `.tmp/ui/retained-arena-evidence/validation-evidence.json`,
SHA-256 `d47a2a28292827e6f85d91cbfef10f21fe7c8aa8c2f010c0883756cc3254360f`.

The Multiplayer page: `.tmp/ui/retained-multiplayer-evidence/validation-evidence.json`,
SHA-256 `f07ddc497858aca4c9c94abb64cb3bd966a53f7078c018c51f882c0f63c40393`.

The Create Server sub-page: `.tmp/ui/retained-createserver-evidence/validation-evidence.json`,
SHA-256 `a307268c17d9e94836bbd26d8f4684825f8787e11c35d2cf89f5a48ded4b0b33`.

Create Server's password rows on masked text fields: `.tmp/ui/retained-createserver-passwords-evidence/validation-evidence.json`,
SHA-256 `608164c9a749f0cd13f931057fb9bc6c3ebe406bd2d89c89c9f470285c7cdbd0`.

Create Server's ban list and the page as the default: `.tmp/ui/retained-createserver-banlist-evidence/validation-evidence.json`,
SHA-256 `aaaeed1f5512c93c181d1fb2895122bc30dcea5437f35b660bd4a2bf057951d8`.

The Join Game sub-page (opt-in) and loopback favorites: `.tmp/ui/retained-joingame-evidence/validation-evidence.json`,
SHA-256 `79aa239d2f10627940727d4a0c5c7306dee0a571ddee62d5dc149ad95b5c7e86`.

Join Game's symbols, mod filter, favorite toggle and levelshot: `.tmp/ui/retained-joingame-symbols-evidence/validation-evidence.json`,
SHA-256 `fc9c671fb6fe43184528d0613b8bdbe4c44794efca2598295c47d0e5a8e538bc`.

Join Game by default, with JOIN IP ADDRESS and FIND PLAYER/CLAN: `.tmp/ui/retained-joingame-default-evidence/validation-evidence.json`,
SHA-256 `4d2c6a2f86f0f1c3fa141acba00768cdbe5783344740d6d0071d74163d51ba4c`.

The server browsers' scan progress: `.tmp/ui/retained-joingame-progress-evidence/validation-evidence.json`,
SHA-256 `86ad58e9f0cb2e487b06fa2e253b38711cb750ecee629ff6b72f3acb43b5cf7f`.

A server's details and the classic browser's server tooltip: `.tmp/ui/retained-joingame-details-evidence/validation-evidence.json`,
SHA-256 `5b7857f09bfd8f65fd8cfa22264375b890b55387090450c5ca5010fd6b034e46`.

The runtime's detail card and Join Game's server details: `.tmp/ui/retained-detail-card-evidence/validation-evidence.json`,
SHA-256 `f756cd836ed5469f16f0f60658c9568d31f35bff637eb694493c464dc0f58854`.

Join Game's network quality bars: `.tmp/ui/retained-network-quality-evidence/validation-evidence.json`,
SHA-256 `05402c1366af7555381f7142104d346e7e019382670c6b9801977120db398e28`.

The Strogg confirmations: `.tmp/ui/strogg-confirmations/validation-evidence.json`,
SHA-256 `eaa963119faf7b24401189911309b07e78a456e2433ea2afeb433663b56a1b50`.

Focus-following prompt verbs: `.tmp/ui/prompt-verbs/validation-evidence.json`,
SHA-256 `71816710d9c56e5637f37b311deddf278eaf32122fe0710fbf49418a408720e2`.

Save details: `.tmp/ui/save-details/validation-evidence.json`,
SHA-256 `f5e59281e2543d90a21a79da6582413744761e0ceb79c26db0249514888e40a7`.

## Known limitations

The [menu audit](menu-audit.md) of 8 October 2026 records what it fixed and
what remains across every retained menu.

- The Load Game page pages by 64 rows rather than virtualizing; its sorting,
  filters and refresh are the [save browser controls](save-browser.md). A
  save's own screenshot loads when first shown, outside a level load, as on
  the stock page. A save from an earlier build shows no difficulty or play
  time, and a run it began keeps none until a new game.

- The completed objectives are kept per map, so a new map lists none, and a
  save from an earlier build restores none. A failed objective is not
  marked (failure ends the mission at once). The
  objectives display held on the scores button in play, its hold or toggle
  setting, the notices and the failure banner are the Remastered HUD's
  ([Remastered HUD](remastered-hud.md)), not this page's.
- The Strogg pause's grain does not drift. The Strogg pages arrive translated:
  only the pause's own labels and the confirmations play the rune
  translation. The Strogg confirmations derive from section 13.7's family
  rules; the specification has no Strogg dialog of its own, so their art
  awaits review with the rest of `ART-037`. Its translation is the menu's short form: the full credits
  translation, with its 2000 ms rune fade-in and 250 ms line stagger, is not
  built.
- Only RESUME releases the softened view over 250 ms: a disconnect, a level
  load or a hand-over to another screen drops it at once, as each replaces
  the view behind it. The HUD's crosshair still shows through the darkening
  fallback.
- Soft focus covers the confirmations, the pause screen and the SYSTEM
  page's two dialogs. The SYSTEM dialogs have no enter or leave motion, so
  their soft focus switches with their visibility. The opaque-backing option
  is Game Options > HUD > Opaque Menu Backing (`ui_retainedOpaqueBacking`). A node
  inside a composition layer gets no soft focus, because only the base
  surface is read. Only the confirmation width exists.
- The multiplayer Escape and Welcome cards present by default
  ([multiplayer menus](multiplayer-menus.md#known-limitations)). The retained
  home screen is never presented in a multiplayer game.
- The Welcome card's own opening runs on the clock the load left standing
  and ends at once, so the hand-off reveals the card already softened. A
  first card frame slower than the fade still cuts it short.
- The sub-page level covers Single Player's Campaign, whose NEW GAME and
  CHAPTERS use the fourth level, and Arena, and Multiplayer's Create Server,
  Join Game and Demos; Player Setup edits in place on the Multiplayer page.
  The retained Join Game shows a server's players and rules in the stock
  tooltip's card rather than a docked detail column, and its SPECTATE and
  favorite toggle carry their stick presses on the plates, since the prompt
  bar has no room for them. The menu levels' sections summarize themselves,
  but the summaries switch without the plates' fade. The classic campaign page
  keeps CHAPTERS on it. The
  Single Player page keeps the home navigation plates and the
  BACK row instead of the section layout (navigation from 14 u, content from
  228 u) and the Back action in the bottom band; Classic is not built. A
  change going back cannot be reversed, and the crumb of a title too long for
  the 13 dp floor (Italian, Polish) is cut at the step.
- The Arena page's result ledger has been captured live only for a launch
  that could not open its server (MATCH ABORTED). A played match's win,
  defeat or draw reaches it through the same campaign path, which the native
  screens check and the gate cover; the in-match ceremony stays game_mp's.
  In the hidden capture window a reloaded game module presents its first
  frame at a screenshot, which reads black.
- The SYSTEM page stays opt-in until every stock setting works there:
  presets cannot be applied from it. Its Display Device list offers the
  first eight displays only
  ([SYSTEM display rows](system-display-rows.md#known-limitations)). Like
  every retained dropdown it steps its value with left and right, so a list
  that cannot open can still be changed.
- The loading screen has no touch prompt (touch counts as desktop input).
  The arsenal fills in only
  where the map spawns its items: a listen server's own load, not a client
  joining another server. The server card lists a joining client under the
  team it asks for, which the server may balance differently, and a
  multi-view demo's load shows no players.
