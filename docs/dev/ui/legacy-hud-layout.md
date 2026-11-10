# Stock interface HUD in the Remastered layout

The stock interface (`ui_retained 0`) draws the stock HUD GUIs itself, since no
retained document replaces them there. Since 9 November 2026 those GUIs follow
the Remastered layout of [visual specification](../ui-visual-design.md) 14.4–14.12
whenever `ui_hudLayout` is not Classic. They keep their original bitmap
artwork. Since 10 November 2026 the HUD scale, opacity and reduced motion
settings apply there too, and the multiplayer HUD has the Remastered follow
card, vote card and respawn prompt, and The Awakening's on-foot HUD and its
dropship gun, MCC gun and speeder bike HUDs follow the layout as well. `ui_hudLayout 0` keeps the stock rectangles and anchors. The retained
interface's own Classic is unchanged too: its peer-less stock GUI draws on the
centered 4:3 canvas. Its Remastered layouts never draw these windows.

## Engine contract

`idLegacyHudLayout` (`src/ui/RetainedHud.h`) serves every GUI with a Remastered
route (`idRemasteredHud::DocumentFor`), that is, the player, multiplayer, vehicle,
objectives and scoreboard HUDs.

A mod that ships its own copy of a stock-named HUD owns it: that GUI stays
Classic, with no Remastered peer and no coverage bridge. A mod without its own
copy gets the HUD of its base game in either interface (`ModOwns`, resolved
once per GUI). Before 10 November the stock interface treated any mod as
Classic while the retained one looked for the file.

- **Definitions.** A GUI is parsed from `idLegacyHudLayout::DefinitionFor`.
  The Awakening's HUDs are the stock HUDs of their day with a few additions
  and know no layout, and q4xbase mounts above openQ4's own content. So while
  the expansion's file is present and no mod owns it, the engine parses an
  openQ4 definition in its place:

  | The expansion's GUI | Definition under `guis/openq4/awakening/` | Generated from |
  | --- | --- | --- |
  | `guis/q4xhud.gui` (on foot) | `q4xhud.gui` | `guis/hud.gui` |
  | `guis/vehicles/airassault/turretplayerhud.gui` (the dropship's guns, m03) | `turretplayerhud.gui` | `guis/vehicles/hud.gui` |
  | `guis/vehicles/mcc/cannonplayerhud.gui` (the MCC's side gun, m06) | `cannonplayerhud.gui` | `guis/vehicles/hud.gui` |
  | `guis/vehicles/speederplayerhud.gui` (the speeder bike, m07) | `speederplayerhud.gui` | `guis/vehicles/hud.gui` |

  The GUI keeps the expansion's name, which the game, the Remastered route
  and saves use. `tools/ui/build_awakening_legacy_hud.py` generates the
  definitions (`--check` runs as `openq4-ui-awakening-legacy-hud-generated`),
  so an edit to `hud.gui` or `vehicles/hud.gui` reaches them once they are
  regenerated. Every other GUI is its own definition.
- **Layout state.** `idUserInterfaceLocal::Redraw` publishes these keys each frame:
  - `oq4_hudLayout`: 0 to 3.
  - `oq4_hudRemastered`: any layout but Classic.
  - `oq4_hudMinimal`.
  - `oq4_hudShowExtras`: not Minimal.
  - `oq4_hudShowPickups`, `oq4_hudShowStrip` and `oq4_hudShowDamage`: always on
    in Classic; otherwise each follows `ui_hudPickups`, `ui_hudWeaponStrip` or
    `ui_hudDamageDirection`, and is off in Minimal.
  - `oq4_hudReducedMotion`: `ui_retainedReducedMotion` outside Classic, for the
    looping motion a script starts itself.
  - `oq4_hudScale`: the HUD scale outside Classic (1 in Classic).

  Competitive uses the Remastered positions. The keys are written when one
  changes, not every frame.
- **Scale.** `ui_hudScale` sizes the laid-out HUDs (`hud.gui`,
  `hud_strogg.gui`, `mphud.gui`, `vehicles/hud.gui` and openQ4's definitions
  of The Awakening's HUDs) as it sizes the
  retained documents: each group scales about its screen anchor. A window
  that an alignment moves to a screen edge starts a group anchored on that
  edge, and an axis it leaves alone keeps its ancestors' anchor, the centre by
  default. The centre column on the bottom edge (the status bar, the strip and
  the pickup lines) takes `centerScale`, which a narrow view limits. The
  device context applies the group as coordinates leave virtual space, after
  clipping, so clip rectangles scale with what they clip
  (`idDeviceContext::SetGroupScale`, `idLegacyHudGroup`).
- **`hudanchorx` and `hudanchory`.** A window names its scale anchor
  (`left`, `middle`, `right`; `top`, `middle`, `bottom`) where its alignment
  does not say: the vehicle status bar and prompt, which Remastered places on
  an edge through `oq4_hudEdge*` while the stock layout keeps them centered,
  and the Tourney strip, which returns to the centre screen inside a
  leading-edge group. `none` keeps a window and its children out of the scale:
  the Arena presentation's letterbox and cards. A window that fills the canvas
  is a full-screen layer (the mission-failed fade, a damage vignette) and
  draws itself unscaled unless it names an anchor, as the damage direction and
  missile indicators do; its children keep their groups.
- **Opacity.** `ui_hudOpacity` fades what the stock windows of a gameplay HUD
  draw (every covered HUD but the objectives display and the scoreboard, as
  the retained documents' gameplay roots). Every colour passes through
  `idDeviceContext::SetDrawColor`, which multiplies its alpha; additive art,
  which ignores alpha, fades by its colour.
- **Edges.** Outside Classic, `screenalignx`/`screenaligny` move a window by the
  Remastered edge distances instead of the full expansion. `HudLayoutFor` gives
  those distances, the same rule the retained documents use: side screens capped
  by `ui_hudWidth`, and every edge moved in by `ui_hudSafeInset`. The engine also
  publishes them as `oq4_hudEdgeLeft`, `oq4_hudEdgeTop`, `oq4_hudEdgeRight` and
  `oq4_hudEdgeBottom` (0 in Classic). A window the stock centers can then follow
  an edge in Remastered only, such as the vehicle status bar and prompt.
- **Pickup slots.** Before a HUD runs `itemPickup`, the engine fills two line
  slots: `oq4_pick0_*` and `oq4_pick1_*` (`text`, `icon`, `width`), plus
  `oq4_pick_newest`. A pickup whose name repeats the newest line while that line
  shows (3250 ms) merges into it as "Name ×N"; any other pickup takes the older
  slot. `width` is the line measured with the `pickup<slot>_text` window's own
  font and scale. GUI scripts can neither compare nor measure strings, which is
  why the engine does this.
- **`textwidth` and `textlines`.** Any window's expression can read
  `<window>::textwidth`, the widest explicit line of that window's text in virtual
  units, and `<window>::textlines`, its line count. The window measures only after
  something references either value, as it draws, so a reader sees the previous
  frame's value. Neither is saved. They fit plates to centre notices, the timer
  and the vehicle prompt. The measurement is cached on the text, its scale,
  font and spacing and the rectangle's width.
- **`ellipsis`, `keynames` and `oneline`.** `ellipsis 1` ends a single line too
  long for its rectangle with "..." instead of cutting it at the edge, and
  `textwidth` then reports the shortened line. In a Remastered layout only
  (Classic keeps the stock look), `keynames 1` writes a key escape as the key's
  name instead of drawing its keycap, for a notice that names a key in passing,
  and `oneline 1` joins the text's explicit lines.
- **Chat.** In the Remastered and Minimal layouts, the multiplayer HUD's passive
  `idChatWindow` draws the Remastered chat box (14.9) from the lines the game
  publishes for it (`oq4_chat<slot>_*`). The message-mode input keeps its stock
  frame.
- **Kill feed.** The stock feed lives in the player HUD (`hud.gui`), whose window
  tree is part of the save format. Outside Classic the multiplayer game skips
  that HUD's `addDeathLine` (`idMultiplayerGame::ReceiveDeathMessage` reads the
  HUD's `oq4_hudRemastered`), so its stock lines stay empty, and `mphud.gui` draws
  the feed from the game's `oq4_feed<slot>_*` fields. Lines that arrive while
  the layout is Classic use the stock feed.
- **Air gauge.** `air_marine.q4ui` and `air_strogg.q4ui` (the Classic overlay)
  follow the centered health gauge through `oq4_hudRemastered`.
- **`ui_hudTest`.** It now runs the GUI on that GUI's own clock. Stock HUD
  timelines (pickup, save and exit notices) therefore play in captures instead of
  expiring at once.

## What each HUD changes

Each script scales its offsets by `"gui::oq4_hudRemastered"`, so Classic computes
the stock rectangles. Minimal and the HUD settings hide groups through `noclip`
wrapper windows: `p_radio_layout`, `p_powerups_layout`, `p_alerts_layout`,
`p_missile_layout`, `p_hit_layout` and `p_effects_layout`.

| HUD | Remastered layout |
| --- | --- |
| `hud.gui` | Gauges +88 u (ammo at 101 u); the ride gauge centered; the strip's slots at 350 u with the name above them; two fitted pickup lines at 401 and 377 u that merge repeats; exit, save and missile notices stacked below the boss plate; powerups 61 u higher |
| `hud_strogg.gui` | The same, with the Strogg gauges +85.5 u, the tram gauge centered and R_Strogg lines |
| `q4xhud.gui` (The Awakening) | Everything `hud.gui` does, since its definition is generated from it, plus the expansion's additions: strip slots 11 and 12 (13 weapons, 388 u wide), the death fade sized to the whole view, the failed-objective title, and the dropship panel at the top trailing edge under the transmission display. On foot the panel now answers m03's damage, marker and warning events, which the expansion's HUD had windows for but no handlers; Remastered lets the view through its disc (0.85) and leaves out the gun's view cone, as the retained panel does |
| `turretplayerhud.gui` (The Awakening's dropship guns) | `vehicles/hud.gui` without its status bar, damage flash and electrical warnings, so the gun has the transmission display, boss bar, notices and the Exit prompt, with the dropship panel always shown under the transmission display. Remastered turns the view cone relative to the ship's nose and draws it under the ship; the stock layout keeps it over the ship on the view's yaw. The expansion's HUD was a copy of the on-foot HUD with its gauges hidden; nothing it kept from that HUD is sent to a vehicle's HUD |
| `cannonplayerhud.gui` (The Awakening's MCC gun) | The same base without the boss bar, with the U.S.S. Hannibal's hull readout at the top trailing edge. Remastered stacks it 36 u higher under the transmission display, fills the track with the hull (`mcchealth` of 2000) and turns the track and number red at a quarter with the vehicle gauges' pulse; the stock layout keeps the static track |
| `speederplayerhud.gui` (The Awakening's speeder bike) | `vehicles/hud.gui` with the bike's weapons (0 the machinegun, 1 the cannon with its charge bar), the race display on the top edge under the boss bar and notices (18 u lower in Remastered, the timer at the Remastered sizes and red in the last ten seconds, shown only once the race runs it), and the boost in the HUD prompt before Exit: the crouch key and Boost while one is ready, the hazard and Boosting while it pushes. The stock layout names the crouch key on a line above the exit line and shows the expansion's hazard while boosting, which its own timelines hid for good |
| `mphud.gui` | See [multiplayer groups](#multiplayer-groups) |
| `vehicles/hud.gui` | The status bar +170.5 u on the bottom edge; electrical warnings at the side edges; the keycap and Exit prompt (or Vehicle locked) on a fitted plate at the bottom-trailing corner; the squad stencil in #FFFF00. The prompt and the stock layout's exit line follow the game's `vehicle_locked` as well as the lock events, as the Remastered documents do: a script that locks a vehicle sends no event (the Awakening's race locks the bike, m06 the side gun). The definitions generated from this HUD inherit that |

### Multiplayer groups

`mphud.gui` covers every mode. Its Remastered windows read the `oq4_*` keys that
`idMultiplayerGame::PublishRemasteredHud` already publishes for the retained HUD.
The stock windows sit in wrappers that show only in Classic: `oq4_stock_ctf`
(team panels, flags and clock) and `oq4_stock_tourn` (strip, top bar, clock and
lines). The standing, the top-leading group, the Tourney strip and banners and
the kill feed hide in Minimal, as the retained document's do.

| Group | Windows | Remastered layout |
| --- | --- | --- |
| Standing | `oq4_standing` (in `p_ffa`) | Deathmatch and Duel: ordinals, the leader's limit, TIED, your band and signed gap |
| Team panels | `oq4_teams` in `oq4_rem_tl` | Two 150x22 u plates from y 36, your team (or the followed player's) first: the team block at 0.80 with the faction mark, the name in the team's text color, and the score in Chain against the limit. The Dead Zone score washes toward the text color with the game's `*_pulse_amount` |
| Flags | `oq4_team<t>_flag`, `_nflag`, `oq4_nflag_free` | Capture the Flag and Arena CTF put each team's flag at the panel's trailing end: at base at 0.45, taken at 0.90 under "!" and growing with `oq4_flag_beat`, dropped under "?". The carrier's name follows in their team's text color. The one flag rides the carrying team's panel and swings to that team's color with the beat. At base or dropped, it sits between the panels in #FFFF8D |
| Round modes | `oq4_team<t>_pip<k>` | Clan Arena, Freeze Tag and Red Rover show up to eight alive pips after each panel during play |
| Dead Zone | `oq4_zone` | A ZONE plate at y 86 with the holder's color block and name, CONTESTED or EMPTY |
| Timer | `oq4_timer` in `oq4_rem_tl` | One plate for every mode: y 82 under the standing, 90 under the panels, 107 in Dead Zone, and in Tourney at (6, 58) in the centre screen. The clock turns #FFBE23 in the last minute and #E46D56 in the last ten seconds |
| Timer column | `oq4_round`, `oq4_credits`, `oq4_status`, `oq4_tplace` | The round modes' ROUND plate with the round clock, then credits and the Dead Zone status on fitted plates. In Tourney, the ROUND and ARENA plate follows the timer. The stock `oq4_roundText`, `oq4_roundTime`, `oq4_overtime`, `sq_credits` and `sq_gameStatus` show only in Classic |
| Tourney strip | `oq4_tourney_rem` (in `p_tourntop`) | The bracket cells for the current round (8, 4, 2 and 1) on plates at 0.55 in the centre screen. Each cell reads the stock cell's state: the names and scores with their colors, the blue settle (`hover_*`), your row aqua at 0.40 (`sel*_*`), and the empty text. A finished arena's winner gets a check, and the loser keeps the stock 0.50 |
| Message bar | `tourn_topmsg`, `oq4_tbar_text` | The stock bar bitmap moves into the centre screen. It already has the Remastered 45-degree end. It shows only with a round name, and the instructions follow at y 77 |
| Banners | `tourn_overbar`, `oq4_warmbanner` | The stock slide and timing. The round banner's black slab gives way to a 640 u slab at 0.60 over the centre screen (`oq4_overbar_slab`). The warm-up banner is its own slab, which `warmupTransitionIn` and `warmupTransitionOut` slide with the stock bar, because the engine pins the stock bar and title to the full width (`AdjustTournamentWarmupLayout`) |
| Kill feed | `oq4_feed` | Top trailing, four lines (two under the Tourney strip), each on its own plate at 0.35, or the header-band plate at 0.30 when it involves you. A line reads attacker, the Quad mark, the weapon's colored icon (a skull for suicides and world deaths, the telefrag mark), a TEAM tag on a team kill, then the victim. Names are white for you, in the team's text color in team modes and #FFFF8D otherwise. The plate fits the line through `textwidth`. The game times the lines and the fade |
| Centre column | `oq4_frag_plate`, `oq4_main_plate`, `oq4_warm_plate` | The frag notice, main notice and warm-up reason stack from 81 u, each on a fitted 0.35 plate |
| Chat | `idChatWindow` | As above |
| Follow card | `oq4_follow` | Spectators: a 236x30 u plate at 0.58 above the status bar (y 360 following, 404 free camera) with the followed player's team colour at its leading end, FOLLOWING over the name (SPECTATING over Free camera), their place in #FFBE23 and score in Chain, and the controls line below. The stock status line gives way to it |
| Vote | `oq4_vote` | A 150 u card at the leading edge, y 150, at 0.50: VOTE and the caller in #FFBE23, the fields, the Yes and No keys with their tallies as eight cells each, and the time left as a 20-tick ramp. After voting the keys go. The stock vote lines show only in Classic |
| Respawn prompt | `oq4_respawn` | While you lie dead and may return: the attack control and Respawn on a fitted plate at 0.50, y 318, with the seconds left in Chain while the server holds the respawn |
| Marker | `oq4_row<n>_mark`, `oq4_team<t>_mark`, `oq4_feed<n>_mark` | The ◥ marker in #E38900 on your standing row, your team's panel and a kill feed line that involves you: the stock triangle turned so its legs lie on the axes |
| Voice | `p_aud1`, `p_aud2` | The stock bars, moved directly above the chat box |
| Managed-match card | `oq4_match_context_hud` | Under the kill feed (y 90, 106 in Tourney), and left out in Minimal |

Three GUI script rules shaped these windows:

- **Expressions group to the right.** The parser reads `a - b - c` as
  `a - (b - c)` and `a - b + c` as `a - (b + c)`. Write a chained difference as
  one subtraction of a sum, for example `"mx" - ("qw" + "aw")`.
- **A container clips its children.** A window with any expression or child
  clips its children to its rectangle, so every group here sets `noclip 1`. The
  taken flags grow past their nodes, and the sheared plates lean past their
  rectangles.
- **Reference a colour component from an unfinished subtree.** A
  `<window>::matcolor_w` (or another colour component) reference to a window
  that has finished parsing reads 0 and logs a warning. References inside the
  still-parsing `oq4_match_hud` resolve after parsing, so `oq4_flag_beat` lives
  there. A hidden window keeps its last `textwidth`, so gate the plate, not the
  measured text.

## Deliberate differences

Each of these needs vector art or a layout a GUI script cannot express:

- The rounded and sheared plates reuse the stock `pickupbar` bitmap.
- The ◥ marker is left out of the bracket and chat, and the chat's
  information symbol is left out. The standing, team panels and kill feed
  draw the marker from a stock triangle, and Vehicle locked shows the stock
  padlock.
- The flags are the stock flag bitmap. A dropped flag is the solid flag under
  "?" instead of a dashed outline, and every taken flag beats on one shared
  clock.
- A name too long for the kill feed (84 u) or a bracket cell ends in three
  full stops, not the ellipsis character. The victim still always shows.
- The winner's check is two short strokes, and the Competitive match bar is not
  reproduced: Competitive uses the Remastered positions.
- The stock GUIs receive `oq4_hudReducedMotion` but do not read it yet: reduced
  motion does not hold the flags' beat or the Dead Zone flash. The clock's
  last ten seconds change color without the 1 Hz pulse.
- The prompt uses the engine's inline keycap, not the white chip, and the
  follow card's controls are the stock instruction line under the card. The
  vote card names its keys without chips, and the damage direction's mark is
  a filled triangle, not a chevron.
- The voice plates keep their stock construction, and the follow card has no
  chevrons at its ends.
- The powerups' tick rings and the scoreboard keep their stock construction.
- The Awakening's dropship panel keeps the expansion's bitmap ship, markers
  and darts, which sit up to 2 u from the vector ones. Its
  `q4xhud_strogg.gui` keeps the expansion's layout: nothing loads it (the
  expansion's Strogg player names `guis/hud_strogg.gui`, which is laid out).
- The speeder's race timer shows the script's hundredths as written ("8.5"
  for 8.05 s; a GUI script cannot pad a number), its red is immediate, not
  a 100 ms transition, and the speed streak of the boost is not drawn. The
  pips are the expansion's bitmaps, a little darker than the vector ones.
- The Awakening's GEV HUD (`gev_hud.gui`) keeps the expansion's script: no
  map places that vehicle, and ten of its materials ship with neither game.
  The air-defence cannon, which has no transmission display, keeps the stock
  layout.
- Text uses the bitmap fonts at scales matched by eye to the Remastered TTF
  layout, about 10% under the 72 dp-per-textscale rule.

## Saves

Player and vehicle HUD window trees are part of the save format. The previous
definitions are kept as `guis/savecompat/hud_r2.gui`, `hud_strogg_r2.gui` and
`vehicle_hud_r1.gui`, listed in `SaveGameReleases.h` from build 2280 with the
marker windows `p_pickup0` and `oq4_prompt`. The revision lookup now walks a GUI's
revisions oldest first (see the
[compatibility policy](../savegame-compatibility-policy.md)).

A save written by build 2271 restored in the candidate through `hud_r2.gui`,
carrying 85 changed window values, including a visible boss bar. A save the
candidate wrote restored on the current definition.

The 10 November refinements changed expressions in all three (a restore
refuses a register whose expression index moved), so the definitions that
builds from 2280 saved are kept as `hud_r3.gui`, `hud_strogg_r3.gui` and
`vehicle_hud_r2.gui`, with the marker windows `hit_chevron` and
`oq4_prompt_padlock`.

The Awakening's HUD record was written from the expansion's own
`guis/q4xhud.gui` until build 2330. Its revision entry names that file as
the saved definition, and `ReadFromSaveGameWithLayout` parses a saved
definition as written, so an earlier record is read through the expansion's
script and its state carries over to openQ4's definition (marker window
`p_pickup0`). A GUI still parsed from the saved definition, such as a mod's
own `q4xhud.gui`, reads its record directly. From here on `q4xhud.gui`'s
definition changes whenever `hud.gui` does, and needs its own
`guis/savecompat/` revision each time.

The dropship gun, MCC gun and speeder bike HUDs are saved with their vehicles,
whether or not the player rides them. Their records followed the expansion's
scripts until build 2334 and are read the same way (marker window
`oq4_prompt`); their definitions change whenever `vehicles/hud.gui` does.

It did the same day: the prompt and exit line began to follow `vehicle_locked`
(a new window, `lock_state_msg`, and changed expressions). The definitions
that builds up to then saved are kept as `vehicle_hud_r3.gui` and, for the
three Awakening vehicles, `q4x_turretplayerhud_r1.gui`,
`q4x_cannonplayerhud_r1.gui` and `q4x_speederplayerhud_r1.gui`, with the
marker window `lock_state_msg`. An Awakening vehicle therefore has two
entries: the expansion's own script for records older than build 2334, then
openQ4's first definition for records up to build 2342.

## Evidence, 9 November 2026

Debugoptimized `builddir` at 1280x720 on OpenGL, from a candidate runtime with
loose GUI overrides. Local captures are in `.tmp/legacy-hud/out/` (untracked),
paired with the retained Remastered HUD:

| Capture | Directories |
| --- | --- |
| Single player | `cmp-new-sp` |
| Strogg (`recomp`) | `cmp-strogg` |
| Deathmatch | `cmp-new-mp` |
| Tank (`convoy1`) | `cmp-tank` |
| 32:9 width cap | `wide-sp-full` |
| Stock Classic | `cmp-classic` |
| Retained Classic | `cmp-ret-classic` |
| Retained Remastered regression | `cmp-rem-regress` |

Contract checks that passed:

- `savegame_gui_layout_contract.py`, `savegame_v3_contract.py` and
  `savegame_restore_gui.py`.
- `gui_expression_storage.py` and `ui_legacy_expression.py`.
- `key_bind_presentation.py`, `ui_retained_adapter.py` and `ui_retained_gate.py`.
- The GUI, HUD and save contracts listed in the commit.

The UI inventory (`inventory-sources.json`, `migration-manifest.json`) still
binds the previous GUI hashes. Its next refresh (UI-10) picks up the edited
GUIs and the three save-compat revisions.

## Multiplayer evidence, 9 November 2026

These captures cover the [multiplayer groups](#multiplayer-groups). They used
the debugoptimized `builddir` (main at `bdbb75c54` plus working-tree changes) at
1280x720 on OpenGL, from the candidate runtime with `mphud.gui` loose, and
`.tmp/legacy-hud/run.py`. Each live match had three bots, no warm-up, a ten-minute
limit and default HUD width and inset. The kill feed came from `kill` and the
bots' own frags. Each `cmp-mp-*` directory sets the retained Remastered HUD
(`ref-mp-*`) above the stock interface (`v4-mp-*`), shot for shot, ending with
Classic:

| Mode | Directory | What it shows |
| --- | --- | --- |
| Team DM | `cmp-mp-tdm` | Panels, limit, timer, feed with suicides, frags and your line |
| CTF | `cmp-mp-ctf` | Taken flag with carrier, flags at base, the enemy-flag notice |
| One Flag | `cmp-mp-oneflag` | The one flag on the carrying team's panel, then between the panels |
| Arena CTF | `cmp-mp-actf` | As CTF |
| Arena One Flag | `cmp-mp-aoneflag` | As One Flag |
| Dead Zone | `cmp-mp-dz` | ZONE plate, the score's wash, timer at 107 u |
| Clan Arena | `cmp-mp-ca` | Alive pips, ROUND plate with the round clock |
| Freeze Tag | `cmp-mp-ft` | As Clan Arena |
| Red Rover | `cmp-mp-rr` | As Clan Arena |
| Tourney | `cmp-mp-tourney` | Pre-game: warm-up banner, message bar, timer with READY, ARENA plate, instructions |
| Tourney bracket | `cmp-mp-tbracket` | Round 1 with six decided arenas and two empty, round 2 with yours, the round banner, Minimal and Classic |
| Deathmatch | `v4-mp-dm` | Standing and timer after the timer moved into `oq4_rem_tl` |
| Kill feed | `v4-mp-feedforge` | Forged lines: attacker and victim of every width, suicide, Quad with a Strogg team kill and TEAM tag, and your death to an 84 u-clipped name |

Live Tourney stays in pre-game on one machine, so the bracket stage sends the
stock HUD's own `arenaInit`, `arenaDone` and `arenaFocus` events through
`ui_hudTest`, as `capture_remastered_hud.py` does for the scoreboard. Tourney
publishes only feed slots 0 and 1, so the feed stage forges slots 2 and 3.
The logs carry no GUI warnings.

Contract checks that passed: `savegame_gui_layout_contract.py` (`hud.gui` is
unchanged), `key_bind_presentation.py`, `gui_transition_operand_contract.py`,
`gui_state_debug_log_contract.py`, `arena_campaign_presentation_contract.py`,
`competitive_match_layer.py`, `mp_match_control_live_adapter_contract.py`,
`ui_hud_coverage.py`, `ui_embedded_icons.py` and `docs_link_integrity.py`.

## Evidence, 10 November 2026

Debugoptimized `builddir` (main at `9cc5e67a4` plus the working tree) at 1280x720
on OpenGL, from the candidate runtime with the HUD GUIs loose. Each state was
shot twice in one run, first with the retained Remastered HUD and then with the
stock interface's (`ui_retained 0` set between the shots), so a pair shares
its scene and state: `.tmp/hud-round/run.py` with `st_pairs.py`, in a hidden
window with no host input. Multiplayer pairs draw over a flat clear
(`r_skipRender 1`). Text and plate extents were measured per element
(`extent.py`) and overlaid (`overlay.py`); sizes and offsets were corrected
until the pairs agreed to about 1 u. Local captures are in
`.tmp/hud-round/out/` (untracked):

| Pair set | Directory | What it shows |
| --- | --- | --- |
| Marine on foot | `final-sp` | Gauges, strip, pickup lines, transmission, notices, boss bar; HUD scale 1.25 with a 4% inset and 0.75; opacity 0.5; Minimal |
| Strogg on foot | `final-strogg` | The same for `hud_strogg.gui` |
| Tank | `final-tank` | Notices stacked under the boss plate, the prompt and padlock, the transmission, scale 1.25, Minimal |
| Deathmatch | `final-mp` | Standing with the marker, timer, one-line warm-up notice, chat, kill feed, scale 1.25, Minimal, Competitive |
| Capture the Flag | `final-ctf` | Team panels with the marker, flags at base, timer |
| Tourney | `final-tourney`, `final-bracket` | Message bar with its key named, timer and ARENA plate, the bracket's cells and names, the round banner |
| Kill feed | `final-feed` | Forged lines: a long attacker name ending in "...", a team kill, your line with the marker |
| Spectator | `final-spec` | The follow card in the free camera with its controls |
| Vote and respawn | `final-vote`, `p7-vote` | The vote card (forged fields), the respawn prompt, a dead player's 0 |
| Damage direction | `final-hit` | The mark at the dome's peak, turned with the indicator |

`final-fade` shows the stock HUD at 0.75 and 1.25 with the mission-failed fade,
which now covers the whole view (`hud.gui` and `hud_strogg.gui` sized it to the
4:3 canvas, leaving lit strips at the sides of a wide view in every layout).

A follow card with a followed player, a called vote's live tally and the voice
plates were not captured: one machine with bots gave no followed player and no
accepted vote.

Saves, with `.tmp/hud-round/savecompat.py`: a save the candidate wrote with the
previous `hud.gui` loose restored with the current one through `hud_r3.gui`
("Restored 'guis/hud.gui' from a save written for its earlier definition (107
changed window values)"), keeping a visible boss bar; a save written with the
current definition restored directly. On `convoy1` the tank's HUD restored
through `vehicle_hud_r2.gui` the same way (10 changed window values).

The retained multiplayer document changed too: its vote, statistics and
managed-match cards now hold their plate in a block beside the column of rows.
As a direct child of the column the plate was laid out before the rows gave
the card its height, and drew as a thin band across the card's top
(`p7-vote`, before and after).

Contract checks that passed: `savegame_gui_layout_contract.py`,
`savegame_v3_contract.py`, `gui_clipping_contract.py` (which now covers the
scale group), `ui_stock_level_load.py` (the peers' rejoin),
`competitive_match_layer.py`, `arena_campaign.py`, `remastered_hud_behavior.py`,
`weapon_wheel_behavior.py`, `key_bind_presentation.py`, `ui_hud_coverage.py`
and `docs_link_integrity.py`.

## The Awakening, 10 November 2026

Debugoptimized `builddir` (main at `e9022bef1` plus the working tree) at
1280x720 on OpenGL, from the candidate runtime with q4xbase read from the local
expansion files (`fs_awakeningpath`). `.tmp/hud-round/run.py` stage `awk`
(`m01_stranarus_trench1`) with `st_pairs.py` (`PAIRS=awk`) shot each state with
the retained HUD and then the stock interface's, in a hidden window with no
host input. Captures are in `.tmp/hud-round/out/awk-pairs/` (untracked).

| Pair | What it shows |
| --- | --- |
| `a-base`, `a-strip`, `a-pickups` | Gauges, the 13-slot strip with its name line, a merged pickup line, at the retained positions |
| `a-ship`, `a-ship-events` | The dropship panel at the top trailing edge; forged m03 events light the left wing and nose, two markers and the north-east and west darts in both |
| `a-ship-radio`, `a-notices` | The panel under the transmission display; boss bar, exit and save notices |
| `a-scaled`, `a-small` | HUD scale 1.25 with a 4% inset, and 0.75: the panel scales about the top trailing corner |
| `a-minimal`, `a-classic` | Minimal keeps the panel; Classic keeps the stock rectangles, the opaque disc and the view cone |
| `a-ship-off` | `aa_showShipStatusOff` hides the panel in both |

The pairs also showed the retained HUD dropping the panel's damage and markers
whenever its document was rebuilt (a layout switch, a level change or a
restore): only events carried them, and the expansion's HUD had no windows
holding them. openQ4's definition does, so the Awakening documents now replay
them as join events (`tools/ui/build_remastered_hud.py`).

Saves, with `.tmp/q4xhud/savecompat_awk.py`: a save written on
`m01_stranarus_trench1` by a 4 November build (`.tmp/runtime/awakening-missions`),
with the boss bar and the panel showing, restored in the candidate ("Restored
'guis/q4xhud.gui' from a save written for its earlier definition (72 changed
window values)") with both still showing in the Remastered layout; a save the
candidate wrote restored directly.

## The Awakening's vehicles, 10 November 2026

The same build and method, one stage a vehicle (`.tmp/hud-round/run.py` stages
`awk-speeder`, `awk-turret` and `awk-mcc`; `st_pairs.py` with `PAIRS=speeder`,
`turret` or `mcc`). The bike is ridden from the start of `m07_race`; the gun
levels board the player through the console's `script` command. Captures are in
`.tmp/hud-round/out/awk-speeder/`, `awk-turret/` and `awk-mcc/` (untracked).

| Vehicle | Pairs | What they show |
| --- | --- | --- |
| Speeder bike | `sp-base`, `sp-progress`, `sp-last` | The status bar centred, the race display with four pips lit, the timer at the retained size, red under ten seconds |
| | `sp-boost`, `sp-recharge` | The prompt: crouch key and Boost, the hazard and Boosting while the boost pushes (driven by `openq4_testInput`), then Vehicle locked alone, from the game's `vehicle_locked` |
| | `sp-radio`, `sp-notices`, `sp-scaled`, `sp-minimal`, `sp-classic` | Transmission display; the boss bar and notices over the race display; scale 1.25; Minimal; the stock layout |
| Dropship gun | `tu-base`, `tu-events`, `tu-turned` | The panel and the Exit prompt; forged damage, markers and darts; the cone under the ship after a 54 degree turn |
| | `tu-radio`, `tu-notices`, `tu-scaled`, `tu-minimal`, `tu-classic` | The same groups as the bike |
| MCC gun | `mc-base`, `mc-hull`, `mc-warn` | The readout under the transmission display; the hull at 1450 of 2000 with the fill at 72%; at 379 the red track and number (the level's own script wrote both after `setHealth`) |
| | `mc-radio`, `mc-notices`, `mc-scaled`, `mc-minimal`, `mc-classic` | The same groups as the bike |

`awk-speeder-classic` shows the stock layout's boost line ("C Boost" above the
exit line) and the expansion's hazard with Boosting while the boost pushes.

Saves, with `.tmp/q4xhud/savecompat_awk.py speeder|turret|mcc`: saves written on
`m07_race`, `m03_airassault` and `m06_mcc_invasion` by the 4 November build
restored in the candidate through the expansion's scripts ("Restored
'guis/vehicles/speederplayerhud.gui' from a save written for its earlier
definition (29 changed window values)", the gun's with 5, the MCC gun's with
0); saves the candidate wrote restored directly.

Not captured: the timer and pips driven by a race actually run (the pips and
the low timer were forged events), the darts fired by the level's own flyers,
and any of this on a 4:3 or ultrawide view.

## Locked vehicles, 10 November 2026

`.tmp/hud-round/run.py tank` with `.tmp/q4xhud/st_lock.py` on `convoy1`: the
tank boarded, then locked through its `lock` script event, which sends the HUD
no event. Pairs in `.tmp/hud-round/out/lock/` (untracked): `lk-open` shows Exit
in both; `lk-locked` shows Vehicle locked with the padlock in both, where the
stock interface showed Exit before; `lk-locked-classic` shows the stock
layout's lock line in place of the exit line; `lk-open-classic` shows the exit
line again once unlocked.

Saves: with `.tmp/hud-round/savecompat.py tank`, a save written with the
previous `vehicles/hud.gui` restored through `vehicle_hud_r3.gui` (12 changed
window values, the boss bar and lock line among them). With
`.tmp/q4xhud/savecompat_awk.py speeder|turret|mcc previous`, saves written with
openQ4's first definitions of the three Awakening vehicles restored through
their `q4x_*_r1.gui` revisions (106, 13 and 7 changed window values), and the
4 November Awakening save on `m07_race` still restored through the expansion's
script.
