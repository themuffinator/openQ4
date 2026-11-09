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
| `mphud.gui` | Deathmatch and Duel standing: ordinals, the leader's limit, TIED, your band and signed gap. The timer plate shows warm-up with the ready count, the countdown, sudden death, overtime, and timed or untimed clocks. The frag notice, main notice and warm-up reason stack from 81 u, each on a fitted 0.35 plate. Chat as above |
| `vehicles/hud.gui` | The status bar +170.5 u on the bottom edge; electrical warnings at the side edges; the keycap and Exit prompt (or Vehicle locked) on a fitted plate at the bottom-trailing corner; the squad stencil in #FFFF00 |

## Deliberate differences

Each of these needs vector art or a layout a GUI script cannot express:

- The rounded and sheared plates reuse the stock `pickupbar` bitmap.
- The standing's ◥ marker, the chat's information symbol and the lock icon are
  left out.
- The prompt uses the engine's inline keycap, not the white chip.
- HUD scale and opacity apply only to the retained interface.
- Team-mode panels, Tourney, the powerups' tick rings, the kill feed and the
  scoreboard keep their stock construction.
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
