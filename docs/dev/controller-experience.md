# Controller experience: technical contract

This is the supporting contract for the roadmap's
[Controller experience](roadmap.md#controller-experience) category (PAD-01 to
PAD-10). The roadmap holds the plan and the states; this document says how the
pieces work, what was decided, and how each is tested. Player-facing
instructions are in the [input settings guide](../user/input-settings.md), and
the real-hardware checklist is [controller qualification](controller-qualification.md).

## Where things live

| Piece | Source | Test |
| --- | --- | --- |
| Which controller is in charge, battery notices | `src/sys/ControllerRoster.h` | `openq4-controller-roster` |
| Stick shaping, look rate, turn boost, response presets | `src/framework/ControllerLook.h` | `openq4-controller-look` |
| Gyro calibration, filters, turning space, activation | `src/framework/ControllerGyro.h` | `openq4-controller-gyro` |
| Aim assistance policy | `src/framework/ControllerAimAssist.h` | `openq4-controller-aim-assist` |
| SDL devices, sensors, haptics, lights, settings upkeep | `src/sys/sdl3/sdl3_backend.cpp` | `sdl3_input_parity.py`, `steam_deck_support.py` |
| Virtual test controllers | `src/sys/sdl3/VirtualController.inl` | `controller_runtime.py` |
| Stick and gyro look in the usercmd and the late look | `src/framework/UsercmdGen.cpp` | `presentation_hf_contract.py`, `sp_turbo_weapon_wheel.py` |
| Context actions, layouts, separate resets | `src/framework/KeyInput.cpp` | `controller_runtime.py --scenario layout` |
| Rumble mix, trigger pulses, side-biased hits | `src/sys/sys_local.cpp` | `controller_runtime.py --scenario feedback` |
| Aim assistance targets, controller light and trigger feel | `src/game/Player.cpp`, `src/mpgame/Player.cpp` | `controller_runtime.py --scenario assist` |
| The on-screen keyboard's layouts | `src/framework/OnScreenKeyboard.h` | `openq4-on-screen-keyboard` |
| The on-screen keyboard: its document, when it shows, what its keys type | `tools/ui/retained_keyboard.py`, `Session_menu.cpp` (`UpdateOnScreenKeyboard`), `UI_RetainedFieldInput` | `openq4-ui-on-screen-keyboard`, `controller_runtime.py --scenario keyboard` |
| The test panel, rumble test and first-connection notices | `tools/ui/retained_controller_test.py`, `Session_menu.cpp` (`UpdateControllerTestPanel`), `Sys_GetControllerTestState` | `openq4-ui-retained-controls`, `controller_runtime.py --scenario testpanel` |

The four headers read no cvar, clock or device: the platform layer and the
usercmd generator pass in what they sampled, so each is exercised by a native
test without SDL or the engine.

## Testing without hardware

`virtualController` (cheats required) attaches SDL virtual joysticks inside the
game's own process. They arrive through the same SDL events a real pad raises,
so hotplug, handover, release, the shaped axes, gyro, the touchpad, rumble and
lights all take the real path. Nothing is sent to the operating system: no
other window can receive anything from them.

```
virtualController attach <xbox|playstation|switch|generic|stick>
virtualController button <slot> <name|index> <0|1>
virtualController axis <slot> <leftx|lefty|rightx|righty|lefttrigger|righttrigger> <-1..1>
virtualController gyro <slot> <pitch> <yaw> <roll> <seconds>     radians per second
virtualController gyrobias <slot> <pitch> <yaw> <roll>           a constant sensor bias
virtualController accel <slot> <x> <y> <z>                       metres per second squared
virtualController choose <slot|auto>                             the saved device choice
virtualController context <key> [command]                        a button's context action
virtualController report [slot]                                  what the game sent the pad
virtualController detach <slot|all>
```

`report` prints what the game sent each pad (rumble, trigger rumble, light,
player number, trigger effect, sensor switch) and the controller keys and axes
the engine holds, which is how a test sees that a handover released what the
outgoing pad held.

Two things a virtual pad cannot do have seams of their own:

- **Battery.** SDL has no way to give a virtual pad a power state.
  `in_joystickPowerProbe "<battery|charging|charged|wired> <percent>"` (cheats)
  stands in for the active controller's reading.
- **Focus.** The engine drops user input while its window is not the focused
  one, and a hidden test window never is. Events from a virtual pad are exempt
  from that rule, and SDL's background-events hint is on while one is attached.
  Console-command bindings still need a focused window, so the harness reads a
  button's context action through `virtualController context` instead of
  pressing it. The retained menus also take a device's buttons only in a
  focused window (`idUserInterfaceRetained` `AcceptInput`), so a scenario
  moves through a page with `openq4_retainedGui menu <input>`, the semantic
  input, while the state a page shows still comes from the virtual pad.

`tools/tests/controller_runtime.py --retail <Quake 4>` launches the staged
client hidden on a single-player map and runs thirteen scenarios: `handover`,
`twins` (two virtual pads moving together, one through a flatter curve: no
handover and the turn rate of the pad alone), `pause`, `battery`, `turnrate`, `gyro`, `feedback`, `layout`, `assist`, `menu`,
`testpanel`, `keyboard` and a `baseline` exit check. It needs an installed Quake 4, so it is compiled in CI
but not run there.

## PAD-01: the controller in your hands is the one in charge

Every gamepad and every joystick SDL cannot map is opened. `ControllerRoster`
decides the active one:

- **Automatic** (`in_joystickDevice` empty): the last controller used. Use is a
  button press, or a stick or trigger moved past its dead zone. A gamepad that
  connects takes over from a plain joystick, as it always has, never from
  another gamepad.
- **A chosen device** (`in_joystickDevice` holds its identity: GUID, plus the
  serial number where the device reports one): only it can be active while it
  is connected. While it is away the others work as Automatic, and it takes
  charge back when it returns.
- **Put down first.** Use moves charge only from a controller that has been
  put down: it holds nothing (no button, no stick or trigger past its dead
  zone, no hat off centre) and has not been used for one second
  (`CONTROLLER_HANDOVER_REST_MSEC`). One pair of hands often shows up as two
  devices, a remapper's virtual pad (DS4Windows, reWASD, x360ce, Steam Input)
  beside the physical one. Both report every movement, and before this rule
  each event moved charge to the other: held buttons released over and over
  and the stick switched between two differently shaped signals, so turning
  speed jumped about (a player report against 0.15.0, 24 November 2026).
- **Twins.** When the controller that just lost charge is used again within
  250 ms, it is the same hands on a second device. The log says so
  (`controller: '<a>' moves with '<b>': one controller seen as two devices`),
  and the twin takes charge back only after the active controller has rested
  for five seconds, so the race at the start of each movement after a pause
  does not move charge back and forth. A false pairing therefore never lasts.
- When the active controller goes away another connected one takes over (the
  chosen device, else a gamepad, else a joystick).

A handover releases every button, trigger and axis the outgoing controller
held and stops its rumble before the incoming one speaks. SDL re-centres a
device's axes as it disconnects; those last events are not use.

The prompts' family, Confirm and Back, rumble, gyro, touchpad and battery are
the active controller's (`in_gamepadFamily`, `in_gamepadName`,
`in_gamepadInstance`, `in_gamepadPower`, `in_gamepadBattery`).

**Loss during play.** When the active controller goes and none takes over,
`in_gamepadFamily` becomes `none`. If the player was playing with it, single-
player opens the pause menu, which shows the persistent notice, and
reconnecting never resumes play by itself. Multiplayer cannot pause: the game
shows the notice on the HUD until a controller returns
(`ui_controllerNotice`). A pad that goes to sleep on the desk while the
keyboard plays does neither.

**Battery.** One notice when the active controller, on battery, reaches
`in_joystickLowBatteryNotice` percent (default 15, 0 never), and one when it
starts charging. Charge wandering around the threshold does not repeat the
first. They join the menus' notice stack and, in a match, the HUD.

## PAD-02: stick aim

Both sticks go through `ShapeStick`: an inner dead zone, an outer dead zone
(travel ignored at the rim, so a worn stick still reaches full output) and a
response curve, radially or, for movement, per axis. Each stick has its own
(`in_joystickLookDeadZone`, `in_joystickLookOuterDeadZone`,
`in_joystickMoveDeadZone`, `in_joystickMoveOuterDeadZone`,
`in_joystickMoveDeadZoneAxial`).

Full-stick speed is in degrees per second, each axis its own
(`in_joystickYawSpeed`, `in_joystickPitchSpeed`). Walking and running do not
scale it; only the keyboard turn keys keep retail's `in_anglespeedkey`. The
turn boost (`in_joystickTurnBoost`, `in_joystickTurnBoostRamp`) adds yaw after
the stick has been held past 92% sideways for 0.12 s, ramps in, drops at once
when the stick comes back or reverses, and is off while zoomed.

`idUsercmdGenLocal::StickLookRate` is the one transform. The usercmd
integrates it and advances the boost; the late-look path calls it with a copy
of the boost, so the drawn view shows what the next usercmd will apply.

**Existing profiles keep their feel.** The new settings start at sentinels (`-1`
dead zones, `0` speeds) and are filled in once, on the first frame after the
configs have run, from the two settings every release up to 0.15.0 had:

- both dead zones from `in_joystickDeadZone`;
- both speeds from `in_yawspeed` x `in_joystickLookSensitivity` x the run
  scale always-run applied (157.5 degrees per second with shipped values).

Afterwards the two classic settings still work as they did: changing either
sets both sticks or both speeds, which is what the stock menu's two rows do.

**Presets** (`in_joystickResponse`) set the look curve, the dead zones and the
boost together. Editing any of them shows Custom when the values match none.

| Preset | Look dead zones | Move dead zones | Curve | Boost |
| --- | --- | --- | --- | --- |
| Classic | 0.18 / 0 | 0.18 / 0 | 1.35 | none |
| Precise | 0.10 / 0.04 | 0.14 / 0.04 | 1.80 | none |
| Dynamic | 0.12 / 0.05 | 0.14 / 0.05 | 2.00 | 200 over 0.25 s |

Classic is the default for every profile. Whether fresh profiles get another
preset waits for the feel review on two controller families.

**View Filter.** `m_filter` rebuilt the view each tick from its own history,
which threw away everything that was not the mouse: a controller could not
turn at all while it was on. Controller look now moves the filter's history
with the view, so it arrives whole and the mouse's part is filtered as before.

## PAD-03: aim assistance

### Decision (22 November 2026)

1. **Replace, do not reuse.** Raven's Xbox code is dormant and incomplete on
   PC: members it reads are not declared, and its effect was an engine call
   (`SetSlowJoystick`) that does not exist. The new code shares nothing with it.
   Raven's `pm_AimAssist*` cvars stay registered and unread.
2. **Two forms, and only these.** *Slowdown*: the stick turns the view more
   slowly while the crosshair is on or beside a target. *Follow*: while a stick
   is moving, the view takes a share of the target's own motion across the
   screen. Follow adds only a fraction of how the target's direction is
   changing, never its offset, so it cannot snap; with both sticks at rest
   nothing happens.
3. **Sticks only.** The engine applies both in `StickLookRate`, which only the
   look stick reaches. The mouse is never assisted. Gyro is not assisted
   either; an option to assist gyro was considered and left out.
4. **No protocol, save or demo change.** The assisted angles travel in the
   usercmd like any others. The game module picks the target and publishes a
   speed scale and a follow rate (`in_aimAssistSlow`, `in_aimAssistYaw`,
   `in_aimAssistPitch`); nothing about the input device enters a usercmd, so
   existing demos and saves play back unchanged.
5. **Multiplayer policy.** A server allows it up to a cap (`si_aimAssist`, in
   the server info), none by default, and never under a competitive ruleset
   (`g_matchProfile competitive_*`). The player's own setting is clamped to
   it. Bots never use it. The cap is enforced by the client build that honours
   it, as every client-side aim setting is; a modified client is outside what
   a server setting can constrain. An SP Arena match is the campaign's, and the
   player's choice stands.
6. **Off by default**, for every profile, until the feel review. This follows
   the rule PAD-02 set for presets.

### Strengths

| `in_aimAssist` | Stick speed on target | Margin beside it | Follow | Range | On a world screen |
| --- | --- | --- | --- | --- | --- |
| 0 Off | 1.00 | none | none | none | 1.00 |
| 1 Low | 0.75 | 2.0 degrees | 15% | 1536 | 0.75 |
| 2 Standard | 0.60 | 3.0 degrees | 30% | 2048 | 0.60 |
| 3 High | 0.45 | 4.5 degrees | 45% | 2560 | 0.45 |

The target is the live enemy nearest the crosshair, in range and in sight: in
the campaign a monster of another team that is not dead and not acting out a
script; in a match an opposing player in the same instance who is not
spectating and not invisible. Allies never are, and a target behind a wall is
no target (one trace to its centre). `pm_showAimAssist 1` prints what was
considered once a second.

## PAD-04: gyro on its own terms

Gyro motion no longer becomes mouse counts. Each sample goes through
calibration, the small-motion filters, the turning space and the gyro's own
ratio, and reaches the usercmd as degrees of view (`Sys_GetGyroLook`). No
mouse setting touches it.

- **Ratio**: degrees of view per degree the controller turns, each axis its
  own (`in_gyroYawRatio`, `in_gyroPitchRatio`). A profile that already aimed
  with gyro starts from what the mouse path made of it, so its feel is kept
  (the shipped Steam Deck values come to 1:1); one that never did starts at
  1:1. Changing `in_gyroSensitivity` afterwards sets both, as it always set the
  gyro speed.
- **Turning space** (`in_gyroSpace`): Yaw, Roll, or Player Space, which turns
  about the real vertical however the controller is held. Player Space needs
  an accelerometer and falls back to Yaw without one.
- **Calibration**: `calibrateGyro` averages two seconds of holding still, and
  starts again if the controller moves. A slower correction runs whenever the
  controller has plainly been at rest for a second. The measured bias is stored
  for each controller in `in_gyroCalibration`.
- **Filters**: tightening (`in_gyroDeadZone`) scales motion below a threshold
  down in proportion, a soft dead zone; smoothing (`in_gyroSmoothing`) averages
  the slowest motion and leaves faster motion direct.
- **Activation** (`in_gyroActivation`, the `_gyro` binding): always, paused
  while the button is held, only while it is held, or only while zoomed. Motion
  made while the gyro is off is dropped, which is what makes the ratchet work.
- **Menu pointer** (`in_gyroMenuPointer`), optional.

**Menu rows.** The Controls page's tab strip has room for one more tab beside
the stock ones in every shipped language (about 78 dp in the tightest), so the
controller has two tabs: CONTROLLER (the device, its buttons, rumble, lights and
prompts) and AIM (stick response, dead zones, gyro and touch). Gyro smoothing
stays a console setting. A page of its own with Controller, Aim, Gyro and
Feedback tabs would hold everything with room to spare; that is a UI-track
decision, as is the stock menu, which keeps its three controller rows (Stick
Dead Zone, Look Sensitivity and Gyro Sensitivity still set both sticks, both
speeds and both ratios) and owes the rest (`STOCK_ROWS_PENDING` in
`ui_retained_gate.py` names each).

**Directions.** The axes are SDL's for a controller held in front of you: x to
the right, y up out of its face, z towards you, rotation positive
counter-clockwise seen from the positive end. Tipping the far edge up looks up,
turning left turns left, rolling the left side down turns left in Roll. Gyro
aim through 0.15.0 read the roll axis for turning and treated both axes with
the opposite sign; nobody had recorded a run on hardware. The new mapping
follows SDL's documented convention and is the first thing the PAD-09 run
checks; Invert Gyro Turn and Invert Gyro Tilt correct either axis.

Gyro aim does not wait for the mouse to be captured: a player with the mouse
turned off still aims with the gyro.

## PAD-06: a complete layout

**Context actions** give a few buttons a command while a context lasts,
without spending a button for good (`idKeyInput::ContextBinding`):

| Context | Buttons |
| --- | --- |
| A vote is open and waiting for you | D-pad left votes Yes, D-pad right votes No |
| Scoreboard button held, in a match | A Ready, X Spectate, Y Chat, B Team chat, RB Buy menu |
| Objectives button held, in the campaign | A Quick save, X Quick load |
| In a match | D-pad up is voice chat, while View still shows the scores |

A command the player has put on a controller button of their own is left to
that button. `in_joystickContextActions 0` turns them all off. A key releases
the action its press began, so a context ending while its button is held
cannot leave an action stuck. Prompts name the context buttons for a
controller player with no button of their own for the command.

**The View button** was hard-wired to open the menu in play, although the
shipped layout, the guide and the visual specification all give it the
scoreboard or the objectives while held. It now follows its binding in play;
the Menu button stays hard-wired, and View still goes back inside menus.

**Layouts** (`in_joystickLayout`, `controllerLayout <name>`) are applied only
when chosen, and the chooser shows Custom after any rebinding:

| Layout | What changes |
| --- | --- |
| Classic | The shipped buttons (`default.cfg`) |
| Bumper Jumper | Jump and crouch on the bumpers, the weapon wheel and last weapon on A and B |
| One-Handed (Left) | Southpaw; LB walks forward, LT fires, L3 jumps, the D-pad reloads, crouches, zooms and opens the wheel |
| One-Handed (Right) | RB walks forward, RT fires, R3 jumps, the face buttons reload, crouch, zoom and open the wheel |

The one-handed layouts also turn on Toggle Zoom and Toggle Crouch, since a
thumb on the stick cannot hold another button, and suit gyro aiming.

**Resets.** `resetController` restores the controller's shipped buttons and
settings and leaves the keyboard and mouse alone; `resetKeyboardMouse` does the
reverse. Neither runs `default.cfg` whole, which would also reset the player's
name and match settings. Measured gyro calibrations are kept.

**D-pad weapon direction** is unchanged: D-pad right is still the previous
weapon. VR's stick paging presses the same two keys, so swapping one swaps the
other. This is left for a decision with the VR track.

## PAD-05: typing with only a controller

A retained text or number field shows openQ4's own keyboard when its edit
opens by controller or touch and the platform has no keyboard to show
([interface record](ui/retained-screens.md#the-on-screen-keyboard-22-november-2026)).

| Piece | Where |
| --- | --- |
| Layouts: five layers of 44 keys, shift, the first layer for a field | `src/framework/OnScreenKeyboard.h` (pure, native-tested) |
| The keyboard's document | `guis/menu/keyboard.rui`, from `tools/ui/retained_keyboard.py` |
| When it shows, where it stands, routing the controller and pointer to it | `idSessionLocal::UpdateOnScreenKeyboard`, `OnScreenKeyboardEvent` |
| What a key does to the field | `HandleOnScreenKeyboardRequest` through `UI_RetainedFieldInput` |
| Whether the platform has a keyboard | `Sys_HasScreenKeyboardSupport` (`SDL_HasScreenKeyboardSupport`) |

`ui_onScreenKeyboard` (archived): `1` shows it when the last input was a
controller or touch and the platform has no keyboard; `2` whenever a field is
edited; `0` never.

The keyboard is a retained screen of its own because a field's edit ends when
the focus leaves the field in its own document. The session draws it over the
screen that owns the field and sends it the controller's keys (`K_JOY*`) and
any click while the pointer is over its panel; everything else goes to the
field's screen as before, so a real keyboard keeps working. Keys reach the
field through its edit's own calls, not imitated key events: a character can
be any Unicode scalar value, where the platform's character event stops at
U+00FF.

`openq4_onScreenKeyboard report | key <0-43> | shift | layer | space | left |
right | delete | done | cancel` reports the keyboard and presses its keys
without a device, as `openq4_retainedGui` does for a screen.

Limits: only retained fields. The stock chat line (`mpmsgmode.gui`), the
console and the summary card's chat are keyboard-only. No recent-address or
other typing-saving defaults were added. On the Save Game page DONE saves, as
Enter does there. Not exercised: device buttons or touch on the keys, the
player name, a server password, number fields, other languages' captures,
Steam Deck game mode.

## PAD-07: the test panel

**Settings > Controls > Controller > Test Controller** opens CONTROLLER TEST,
a pop-up on the Controls page ([interface record](ui/retained-screens.md#controller-and-aim-on-controls-and-controller-test-22-november-2026)).

| Shown | Source |
| --- | --- |
| Sticks before any dead zone, triggers, buttons, gyro presence, motors present, Steam Input | `Sys_GetControllerTestState` (SDL, the active controller) |
| Dead zones, outer limits, Trigger Press | the page's own settings (`cvar.in_joystick*`) |
| Name, power, battery, gyro calibration | `in_gamepadName`, `in_gamepadPower`, `in_gamepadBattery`, `in_gyroCalibrationState` |
| The gyro's dot | the view rate the last sample asked for, against 180 degrees a second; it rests while gyro aiming and the gyro pointer are both off |
| The running motor | the rumble test's step |

The session publishes these as `controllerTest.*` every frame the Controls page
is the active page (`idSessionLocal::UpdateControllerTestPanel`). Unchanged
values are not sent, and sticks and triggers are published in hundredths.

**The rumble test** (`controlsRumbleTest`, `Sys_StartControllerRumbleTest`)
gives each motor the controller has a 600 ms turn and runs it for 400 ms of it:
strong, light, left trigger, right trigger. It drives SDL directly, so the
Rumble switch, its strength and the menus' silence do not apply, and play's
rumble waits while it runs.

**The first connection of a family** (Xbox, PlayStation, Nintendo, Steam Deck,
generic) raises two menu notices naming Prompt Style and Swap Confirm and
Back. `in_joystickFamiliesSeen` (archived) keeps which families have been
introduced, so each is said once per profile. An existing profile hears it
once for the controller it already uses.

**Steam Input**: when `SDL_GetGamepadSteamHandle` reports a Steam virtual pad
and it has no gyro sensor or no touchpad, the panel says so and names the
Steam Deck guide.

Limits: Back closes the panel, so it cannot be shown held, and Accept starts
the rumble test as it lights. The panel shows the active controller only. No
capture of each family's glyph set or of any language but English exists yet,
and the Steam Input line has been exercised only through published state, not
with Steam running.

## PAD-08: haptics and lights

Each is used only where SDL reports the capability, and each has its own
switch. The haptic ones also answer to Rumble.

| Feature | Setting | Where |
| --- | --- | --- |
| The firing trigger's motor pulses with each shot | `in_joystickTriggerRumble` | Controllers with trigger motors |
| The trigger's resistance follows the held weapon | `in_joystickAdaptiveTriggers` | DualSense |
| A hit rumbles more on the side it came from | `in_joystickDirectionalRumble` | Every rumble controller |
| The light shows your team, or your health in the campaign | `in_joystickLight` | Controllers with an RGB light |
| The player lights show player 1 on the controller in use | `in_joystickPlayerLight` | Controllers with player lights |

The trigger that pulses and takes the weapon's feel is whichever trigger is
bound to attack. Menus, the console and a handover clear the trigger effect;
focus loss stops all rumble. `GAME_API_VERSION` 62 added the three `idSys`
calls the game uses for these.

## PAD-10: recommendation, not yet decided

None of these is implemented, and none is credited.

- **Flick stick**: worth offering once PAD-04 has had its hardware review,
  because it only makes sense with gyro for vertical aim. It would be a fourth
  response mode beside the presets and reuse `StickLookRate`'s inputs.
- **Co-pilot** (two controllers driving one player): the roster already holds
  every controller, so the engine side is small; the open question is how
  prompts and handover behave with two active pads.
- **HD haptics**: needs a decision on authoring. Quake 4's sounds would drive
  them as they drive rumble today; without authored effects the gain over the
  trigger and side-biased rumble is small.

## Still open

See the roadmap for the authoritative list. In short: the on-screen keyboard
for the stock chat line and console, its harness for the player name and a
server password, and typing-saving defaults (PAD-05), captures of the test
panel for each controller family (PAD-07), hints on the held scoreboard for its context buttons, a multiplayer HUD capture of the loss
notice, the server cap in the server details and browser, the feel reviews,
and every physical run (PAD-09).
