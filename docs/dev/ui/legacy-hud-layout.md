# Stock interface HUD in the Remastered layout

The stock interface (`ui_retained 0`) draws the stock HUD GUIs itself, since no
retained document replaces them there. Since 9 November 2026 those GUIs follow
the Remastered layout of [visual specification](../ui-visual-design.md) 14.4–14.12
whenever `ui_hudLayout` is not Classic. They keep their original bitmap
artwork. `ui_hudLayout 0` keeps the stock rectangles and anchors. The retained
interface's own Classic is unchanged too: its peer-less stock GUI draws on the
centered 4:3 canvas. Its Remastered layouts never draw these windows.

## Engine contract

`idLegacyHudLayout` (`src/ui/RetainedHud.h`) serves every GUI with a Remastered
route (`idRemasteredHud::DocumentFor`), that is, the player, multiplayer, vehicle,
objectives and scoreboard HUDs. It is skipped for an external mod.

- **Layout state.** `idUserInterfaceLocal::Redraw` publishes these keys each frame:
  - `oq4_hudLayout`: 0 to 3.
  - `oq4_hudRemastered`: any layout but Classic.
  - `oq4_hudMinimal`.
  - `oq4_hudShowExtras`: not Minimal.
  - `oq4_hudShowPickups`, `oq4_hudShowStrip` and `oq4_hudShowDamage`: always on
    in Classic; otherwise each follows `ui_hudPickups`, `ui_hudWeaponStrip` or
    `ui_hudDamageDirection`, and is off in Minimal.

  Competitive uses the Remastered positions.
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
  and the vehicle prompt.
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
| `mphud.gui` | See [multiplayer groups](#multiplayer-groups) |
| `vehicles/hud.gui` | The status bar +170.5 u on the bottom edge; electrical warnings at the side edges; the keycap and Exit prompt (or Vehicle locked) on a fitted plate at the bottom-trailing corner; the squad stencil in #FFFF00 |

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
- The ◥ marker (standing, team panels, bracket and kill feed), the chat's
  information symbol and the lock icon are left out.
- The flags are the stock flag bitmap. A dropped flag is the solid flag under
  "?" instead of a dashed outline, and every taken flag beats on one shared
  clock.
- Names in the kill feed and the bracket are clipped at their width (84 u in
  the feed), because a GUI script cannot add an ellipsis. The victim still
  always shows.
- The winner's check is two short strokes, and the Competitive match bar is not
  reproduced: Competitive uses the Remastered positions.
- The stock GUI receives no motion setting, so reduced motion does not hold the
  flags' beat or the Dead Zone flash. The clock's last ten seconds change color
  without the 1 Hz pulse.
- The prompt uses the engine's inline keycap, not the white chip.
- HUD scale and opacity apply only to the retained interface.
- The powerups' tick rings and the scoreboard keep their stock construction.
- The Awakening's `q4xhud*.gui` and the air-defence cannon (which has no
  transmission display) keep the expansion's and stock layouts.
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
