# Native weapon wheels

The old SP wheel drew hardcoded wedges, bitmap icons and bitmap text directly
from `Player.cpp`; it never checked `ui_retained`. The MP module had no wheel
implementation. The retained path now loads editable `.rui` vector documents
and publishes the local player's inventory, selection, weapon name, clip and
reserve. Stock UI keeps the classic renderer.

## Placement

`ui_weaponWheelOffset` is an archived integer in **percentage points of full
viewport width measured from center**, from −40 to +40. The default `27`
preserves the original position at 77% of a widescreen view. `0` centers the
wheel; `-27` mirrors the default to the left. Settings > Game > Controller
contains **Wheel offset (0=center)** beside wheel sensitivity.
The label is localized in all twelve supported languages.

Marine, Strogg, multiplayer and Classic use the same offset setting. Native
weapon and quick-chat wheels also follow the independent HUD scale and safe
inset. Their ring scales uniformly to fit both viewport dimensions and its
center clamps inside the inset. This affects only presentation, never the
saved scale or offset preference.

## Runtime

- Marine and Strogg have separate constructions. The Marine ring uses rounded
  HUD plates, a continuous bevelled reserve gauge divided into eight cells,
  the scope's double ring and a turning compass over rolling signal bands.
  Strogg uses twisted claw plates, slanted reserve cells and drifting masked
  grain, an octagonal core with its lower tab, counter-rotating dashed rings
  and circuit paths. Its readouts have aligned rune and R_Strogg copies.
  The eleven stock slots and thirteen Awakening slots keep fixed positions.
  MP uses Marine framing, the gauntlet and team color across the selection,
  bezel, hub, compass and pointer.
- Owned slots show vector symbols and live ammunition; unavailable slots
  remain outlines. Eight reserve cells, the previous-weapon notch and a
  selected hub connect the ring to the existing HUD language.
- Mouse and controller selection share four-degree boundary hysteresis, and
  an unowned position holds the previous selection. The native controller
  selector reads the stick's angle beyond 35% physical deflection, sampled
  before the aiming dead zone and response curve and independently of look
  inversion, aim speed, simulation rate and Classic cursor sensitivity.
  The engine publishes transient local `ui_radialStickActive`, `X` and `Y`
  values; they do not change the game API or network command format. The
  mouse keeps an invisible cursor with a 32 dp dead zone. `ui_retained 0`
  retains the legacy cursor and `in_weaponWheelSensitivity` setting.
  Release uses the existing weapon impulse, including MP's reliable message
  and prediction acknowledgement. Empty ammunition refuses the switch;
  Escape, controller east/Back and a second bound wheel control cancel while
  keeping the current weapon. The cancel press and its matching release are
  consumed, so a crouch binding cannot leak into gameplay. A menu or camera
  taking over also dismisses the invocation until the original control is
  released. A tap shorter than 180 ms selects the previous weapon without
  drawing the wheel, softening the view or
  slowing the game. MP keeps movement and normal simulation speed; SP keeps
  the existing transient slowdown.
- Both modules soften the complete scene with an 8 dp separable Gaussian and
  subtle 0.94 saturation, eased with the wheel blend. This pass runs before
  damage indicators, HUD and wheel artwork, so their vectors and labels stay
  sharp. Closing removes the filter pass entirely; the existing opaque-backing
  option disables soft focus. Classic keeps its previous depth-blur treatment.
- Marine opens from the measured slit, deploys its plates clockwise, flashes
  the hub rings and tunes in the name with the briefing's burst and jolt.
  Selection reopens the name's clip from a 2 dp line and flashes the chosen
  ring. Strogg boots and drops its plates in scrambled order, traces the
  circuits and translates its readouts with the scale pop and scan bar.
  Tracks reset at 1 ms before interpolating, so replaying selection actually
  restarts the effect instead of retargeting a property from 1 to 1.
  Native open/close and selection timelines support reduced motion. Classic
  remains available with `ui_retained 0`. Presentation state is transient and
  is reset on initialization/restoration/cancellation.

Authoring: `tools/ui/build_retained_weapon_wheel.py`, with independently
reconstructed first-party symbols carried from the design atlas into
`tools/ui/weapon_wheel_icons.json`. No extracted retail art or external code
was added. `src/game/gamesys/WeaponWheel.inl` and `WeaponWheelState.h` keep
selection, live state and ownership identical across SP/MP; the legacy draw
helpers remain isolated in `WeaponWheelLegacy.inl`. The atlas's barbed Core
Cannon and snowflake glyphs are included; the SVG importer preserves both
filled and stroked paths, including the grenade's hook and the snowflake's
branches. Infinite-ammunition weapons omit the reserve band and use an
authored vector infinity symbol in the hub.

## Quick chat

Multiplayer's quick-chat wheel (section 14.6) is the same component with eight
localized phrases (`#str_231261`-`#str_231268`) in place of the weapons:
`guis/hud/quick_chat.rui`, generated by `chat_document()` in
`tools/ui/build_retained_weapon_wheel.py`, drawn by `src/mpgame/QuickChat.inl`.

- `_quickChat` holds it open (D-pad Down and V by default; Settings >
  Controls > Weapons > Quick Chat), alive, dead or spectating, never from a
  menu, a cinematic or the weapon wheel. The Stock UI (`ui_retained 0`) has
  no quick-chat wheel.
- The selector is the weapon wheel's: 32 dp of mouse travel, or direct
  stick direction past 35% deflection, with 4 degrees of hysteresis.
  Placement follows `ui_weaponWheelOffset`, and the wheel's blend drives the
  same scene soft focus.
- It opens on Team in a team game and on All otherwise; in a team game the
  zoom control switches the channel. The usercmd button
  (`BUTTON_QUICKCHAT`) masks attack, zoom and scores and freezes the view on
  both ends of the wire, so prediction agrees; while it is held, zoom passes
  through as pressed and never flips a toggled zoom.
- Releasing over a phrase sends `say` or `sayTeam` with the phrase in the
  sender's language; releasing inside the dead zone, Escape, controller
  east/Back, a second bound quick-chat control or another
  screen taking over sends nothing.
- Retained documents read a key escape (`^ikHH`) in their string state as
  the key's name, so the hub can show `common->KeysFromBinding("_zoom")`.
- `testQuickChat open|slot N|channel|release|cancel` (cheat protected)
  drives it without input; `tools/ui/capture_remastered_hud.py --scenario
  mp-quickchat` and `mp-quickchat-team` capture it, and
  `openq4-ui-quick-chat` checks the document.

## Verification and remaining scope

`tools/ui/capture_weapon_wheels.py` launches hidden, windowed gameplay with
host mouse/controller input disabled, executes semantic cheat-protected
`testWeaponWheel` commands, and uses the engine's `screenshot` command. It
checks release/equip, cancellation, persistent placement, the native gate,
and MP simulation speed. Evidence is retained under `.tmp/weapon-wheel/`.
The native screen suite exercises all four documents, narrow/wide/4K layouts,
inventory/ammo bindings, transitions and reduced motion. Existing sensitivity
and transient time-scale harnesses follow the shared implementation.

The earlier placement/soft-focus increment built and staged successfully. Its native screen suite
passes 64,704,897 checks, including scene-only focus, full-view coverage and no
filter pass after release. OpenGL gameplay covers Marine, Strogg and multiplayer
at 1280×720; Vulkan covers multiplayer at 1920×540 and Strogg at 540×960, with
focus enabled/disabled, offset changes, equip/refusal/cancel and Classic fallback.
Reports and engine screenshots are in `focus-gl/`, `focus-vulkan-wide/` and
`focus-vulkan-portrait/` under `.tmp/weapon-wheel/`. The Game options capture
checks the localized label, active control, signed value and zero readback;
config write/reload preserves a nondefault offset. Reduced motion caps the
shared wheel/focus blend at 80 ms. Existing stock multiplayer missing-AAS and
non-precached-declaration warnings remain unrelated to these UI changes.

The continuation's focused native wheel run passes 33,514,721 checks across
all four family/roster documents, and quick chat passes 1,560,052. Source-projected input tests exercise the
selector thresholds, direction, boundary hysteresis and cancel/release
ownership without touching a host device. The native input driver passes
16,581 checks for each of its Windows and POSIX paths plus 44 pump/fatal-edge
checks. Current evidence lives under `.tmp/claude-continuation/hud/`.
`tools/tests/weapon_wheel_behavior.py` executes the production lifecycle with
counted weapon and chat receivers: 64 checks cover tap/hold timing, release,
empty-ammunition refusal, Back/menu cancellation, channel changes and one
send per release. It uses no host devices or game window. Both commit
validation and push verification run its syntax and executable checks.

The continuation's OpenGL gameplay captures at 1280×720 contain 23 Marine,
23 Strogg and 18 multiplayer screenshots under `final-gl-marine/`,
`final-gl-strogg/` and `final-gl-mp/`. Equip, refusal, cancellation, saved
placement and normal MP simulation checks pass; the isolated MP listen
server received exactly one selected quick-chat phrase. Settled native
wheels were visually checked at normal scale, Couch 125% and a 10% inset.
The initial Marine/Strogg default shots caught the cold-load blend, so the
selected and enlarged captures supply the settled reference. The capture
wrapper now waits for rendered frames before its first static wheel view.
The first captures exposed separate Classic hit-feedback and MP medal
precache defects. The follow-up stage repairs both: the 12 engine captures
in `classic-hit-settled-marine/` and `classic-hit-settled-strogg/` show soft
directional glows at full and reduced intensity, including repeated hits,
180-degree rotation and clearing. `classic-hit-verification.json` also
checks that each event started before its screenshot. The 18-shot cold MP
rerun in `medal-fixed-gl-mp/` passes the strict retained-warning gate and
the wheel/quick-chat assertions. The initial failures and independent
successful MP semantic checks remain in `final-gl-mp/`.

The earlier Tourney capture in `tourney-clock-adaptive/` waits for two real
arenas to enter their round and sudden-death states. Both Remastered and
Competitive then show the infinity clock, sudden-death label and no expired
countdown warning. Its `verification.json` checks the actual arena state
and HUD state at each engine screenshot. The earlier invalid-countdown
and fixed-delay attempts remain as failed harness evidence in
`tourney-clock-final/` and `tourney-clock-casual/`.

The later MP/grain correction is recorded separately in
`mp-followup-validation.json`: its Tourney rerun also verifies both named
events on the actual arena transition, and a final text-layout stage keeps
the complete countdown instructions readable. The wheel evidence retains
its original source, native-binary and runtime identities; these follow-ups
do not retroactively qualify the earlier captures against later binaries.

The full section 14.6 product requirement remains partial: physical-device
qualification, refusal rumble, touch controls, editor round trips and
non-Windows/GLES/VR qualification remain open. Awakening
multiplayer remains outside the project's supported scope.
