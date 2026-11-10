# Input Settings and Controls Guide

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

This guide covers the main ways to configure keyboard, mouse, and controller input in openQ4, including the in-game menu, console commands, default binds, config files, and common troubleshooting.

## Quick Start

- To rebind actions, open `Settings -> Controls`.
- To tune mouse and controller feel, open `Settings -> Game Options`.
- Start with sensitivity and dead zone before changing advanced values.
- Press `` ` `` or `~` to open the console with the stock default bindings.
- Run `listBinds` to see your current bindings.
- Run `listControllers` to print SDL gamepad, joystick, gyro, touchpad, battery, and Steam Deck routing diagnostics.
- Run `writeConfig my-input-backup.cfg` to save a manual backup before large binding changes.
- Use `Load Defaults` if the controls get into a bad state. This also resets custom binds.

## Where Things Are

| Menu | Use it for |
|---|---|
| `Settings -> Controls -> Movement` | Forward, back, strafe, jump, crouch, turn, run/walk. |
| `Settings -> Controls -> Weapons` | Direct weapon slots, buy menu and the quick-chat wheel. |
| `Settings -> Controls -> Attack` | Attack, reload, zoom, next/previous weapon, flashlight, objectives, weapon wheel. |
| `Settings -> Controls -> Other` | Quick save/load, chat, vote, ready, stats, emotes, voice chat. |
| `Settings -> Game Options` | Free look, toggle zoom, mouse input tuning, controller tuning, console access. |

To rebind an action, select its row and press the key, mouse button, or controller button you want to use. Analog sticks are configured through the controller settings instead of being rebound as buttons.

Each row shows its keys as pictures: keycaps for the keyboard, a mouse with the bound button marked, and your controller's own buttons. An action keeps up to two keyboard or mouse keys and up to two controller buttons. Binding a third key replaces the two keys of that kind and leaves the other kind alone, so giving Jump a new keyboard key never unbinds it from your controller. Press Delete or Backspace on a row (X or Square on a controller) to clear it, and Escape to cancel while it waits for a key.

## Where openQ4 Stores Input Settings

At startup, openQ4 loads input-related config in this order:

1. `default.cfg`
2. `openq4_defaults.cfg` (if present)
3. `openq4_profile_<profile>.cfg`, such as `openq4_profile_steamdeck.cfg` when a platform profile is active
4. `openQ4Config.cfg`
5. `autoexec.cfg` (if present)

What this means in practice:

- Stock defaults come from `content/baseoq4/pak0/default.cfg`.
- openQ4-specific overrides can be layered on top through `openq4_defaults.cfg`.
- Platform-specific overrides can be applied through `com_platformProfile`.
- Your saved personal changes live in `openQ4Config.cfg`.
- Advanced users can place their own final overrides in `autoexec.cfg`.

`openQ4Config.cfg` is the main user config file name, and it is written under the normal writable save/config area (`fs_savepath`, which defaults to `fs_homepath`).

## Mouse Settings

Mouse tuning lives in `Settings -> Game Options`.

| Setting | Backing cvar | Good starting point | What it changes |
|---|---|---:|---|
| Free Look | `in_freeLook` | On | Lets the mouse control view direction by default. |
| Toggle Zoom | `in_toggleZoom` | Off | Makes zoom act as a toggle instead of hold-to-zoom. |
| Mouse Pitch | `m_pitch` | Normal | Flips vertical mouse look. |
| Mouse Smooth | `m_smooth` | `1` | Higher values smooth motion but can feel less direct. |
| Mouse Sensitivity | `sensitivity` | Personal preference | Main mouse look speed. With Mouse CPI enabled, this is degrees per centimeter. |
| Mouse CPI | `m_cpi` | `0` | Set this to your mouse CPI/DPI for physical sensitivity. `0` keeps classic raw-count behavior. |
| View Filter | `m_filter` | `0` | Optional QuakeLive-style view-angle history filtering. Leave it off for the most direct raw input. |
| Mouse Accel | `cl_mouseAccel` | `0` | QuakeLive-style acceleration. Positive values add sensitivity as movement rate rises; negative values subtract it. |
| Accel Offset | `cl_mouseAccelOffset` | `0` | Movement-rate threshold before acceleration starts. |
| Accel Curve | `cl_mouseAccelPower` | `2` | Acceleration curve shape. `2` matches QuakeLive's default power. |
| Sensitivity Cap | `cl_mouseSensCap` | `0` | Caps accelerated sensitivity when set above `0`. |
| Console Access | `con_allowConsole` | Tilde | Controls whether tilde opens the console directly or requires Ctrl+Alt+Tilde. |

Useful console-only fallback:

```cfg
seta m_maxMouseDelta 0
```

`m_maxMouseDelta 0` keeps the modern high-DPI path unclamped. Set a positive value only if a faulty mouse or driver creates large camera jumps.

Debug-only console fallback:

```cfg
seta cl_mouseAccelDebug 0
```

`cl_mouseAccelDebug 1` writes acceleration samples to `logs/mouse.log` under the active save path.

For CPI-normalized tuning, set `m_cpi` to the mouse hardware value and use `360 / sensitivity` as an approximate centimeters-per-360 target.

SDL3 builds use relative mouse capture during gameplay on Windows, Linux, and macOS. On Linux, openQ4 requests unscaled relative deltas from SDL where the desktop stack supports them and preserves fractional movement before it reaches the integer engine event queue. `Mouse1` through `Mouse8` are bindable by name on SDL3 and the native X11 fallback, and wheel up/down remain available as `MWHEELUP` and `MWHEELDOWN`.

Experimental native macOS builds also expose `Mouse1` through `Mouse8`, track high-resolution wheel/trackpad deltas before emitting wheel steps, and synthesize common edit-field control characters such as Return, Tab, Backspace, and Ctrl-letter shortcuts.

## Controller Settings

Controller tuning lives in `Settings -> Game Options -> Controller`.

| Setting | Backing cvar | Default | What it changes |
|---|---|---:|---|
| Controller | `in_joystick` | On | Enables SDL3 gamepad/joystick input and hotplug support. |
| Stick Layout | `in_joystickSouthpaw` | Default | Use `Southpaw` to swap movement and look sticks. |
| Stick Dead Zone | `in_joystickDeadZone` | `0.18` | Raises or lowers the center area ignored by analog sticks. |
| Look Sensitivity | `in_joystickLookSensitivity` | `0.75` | Scales controller turn speed after stick deflection is normalized. |
| Wheel Sensitivity | `in_weaponWheelSensitivity` | `4.0` | Speeds up the Classic weapon-wheel cursor (`ui_retained 0`). Range `0.25–16.0`; higher is faster. The Remastered wheel follows the stick's direction directly beyond 35% deflection. |
| Look Curve | `in_joystickLookCurve` | `1.35` | Higher values damp small aim movements; full-stick turn speed is controlled by Look Sensitivity. |
| Invert Look | `in_joystickInvertLook` | Off | Flips vertical controller look. |
| Trigger Press | `in_joystickTriggerThreshold` | `0.35` | Controls how far LT/RT must be pressed before they count as buttons. |
| Rumble | `in_joystickRumble` | On | Enables gameplay rumble when the device supports it. |
| Rumble Strength | `in_joystickRumbleScale` | `1.0` | Scales rumble intensity from `0.0` to `2.0`. |
| Low-Battery Rumble Threshold | `in_joystickLowBatteryRumbleThreshold` | `0` globally, `20` in the Steam Deck profile | Caps effective rumble output when SDL reports a controller battery at or below this percentage; `0` disables the cap. |
| Low-Battery Rumble Cap | `in_joystickLowBatteryRumbleScale` | `0.75` | Maximum effective rumble scale below the low-battery threshold. This does not rewrite `in_joystickRumbleScale`. |

Suggested adjustments:

- If the view or movement drifts, raise `Stick Dead Zone` a little.
- If aiming feels sluggish, raise `Look Sensitivity`.
- If small aim corrections are too twitchy, raise `Look Curve` or `Stick Dead Zone`.
- If you cannot reach full turn speed comfortably, lower `Look Curve` toward `1.0`.
- If triggers fire too easily, raise `Trigger Press`.
- If triggers feel unresponsive, lower `Trigger Press`.

In the new menus (`ui_retained 1`), the left stick and D-pad move the
selection. The right stick (the left one with Southpaw) scrolls panels that
have no rows to select, such as the pause menu's objectives, the multiplayer
scoreboard and player list and long confirmation messages, and in Player
Setup it turns the preview of your player. Invert Look does not affect the
menus.

Advanced movement tuning is available from the console:

```cfg
seta in_joystickMoveCurve 1.0
```

`1.0` is linear movement. Higher values make walking easier near the center of the stick but require more stick travel to reach full movement.

SDL gamepads use SDL's standard controller database, so Xbox, PlayStation, Steam Input, and similar pads should not need axis remapping. Generic SDL joysticks use raw axes with these auto defaults:

### Confirm and Back

On a Nintendo-layout controller, menus confirm with A (the right button) and
go back with B (the bottom button), as Switch games do. Nintendo prompts
chosen with `in_promptStyle 3` (see [Button Prompts](#button-prompts)) count
as a Nintendo layout. Other controllers confirm with the bottom button. To
choose for any controller, set **Swap Confirm and Back** in **Settings > Game
Options > Controller**: **Off** confirms with the bottom button, **On** with the
right button, and **Automatic** follows the controller. The console setting is
the same choice:

```cfg
seta in_swapConfirmBack 2
```

| Value | Menus confirm with |
| --- | --- |
| `0` (default) | The right button on Nintendo layouts, the bottom button otherwise |
| `1` | The bottom button |
| `2` | The right button |

The menu prompts follow the choice. In-game bindings are not affected.

### Menu Buttons

In the menus, the controller's Menu button (Start, Options or +) takes you
back to the game from the pause menu's Save, Load and Restart Level pages.
The View button (Back, Select or −) hides or shows Join Game's server details;
in play, holding it shows the scores or objectives.
On a multiplayer server's Admin page it applies your server changes, and the
Apply Changes button shows the Menu button's symbol. While a dialog or an
open list is showing, it closes that first, as Back does.

### Button Prompts

Once you use a controller, menu prompts show its buttons instead of
keyboard keys:

- Xbox pads show A and B.
- PlayStation pads show the cross and circle.
- Switch pads show their B and A.
- The Steam Deck's built-in controls show its own A and B.
- Other pads show a diamond with the button to press filled in.

In a multiplayer match's menu, the keys that switch tabs show your
shoulder buttons: LB and RB, L1 and R1, or L and R. On the Match and Admin
pages, the row of sections ends with your triggers (LT and RT, L2 and R2, or
ZL and ZR), which step through the sections. The keyboard or mouse switches
the prompts back to keys.

If Steam Input or another remapper hides your real controller, choose the
controller prompts yourself with **Prompt Style** in **Settings > Game Options >
Controller**, or from the console. The setting covers the menus and the HUD:

```cfg
seta in_promptStyle 2
```

| Value | Controller prompts |
| --- | --- |
| `0` (default) | The controller's own |
| `1` | Xbox |
| `2` | PlayStation |
| `3` | Nintendo; menus also confirm with A |
| `4` | Steam Deck |
| `5` | Generic |

The prompts still switch to keys while you use the keyboard or mouse.
`listControllers` shows the style in use for the connected controller on
its `prompt glyphs:` line.

Linux and macOS SDL3 builds opt into SDL's HIDAPI controller backends by default so PlayStation, Switch, Steam, Steam Deck, and Xbox-class controllers can expose the same button, stick, trigger, rumble, battery, gyro, and touchpad capabilities available through the shared SDL3 path. macOS support is a preview, and controllers have not yet been tested on a Mac. Run `listControllers` to confirm which SDL driver, hint state, and capabilities are active for a connected device.

| Cvar | Auto default | What it changes |
|---|---:|---|
| `in_joystickUseDedicatedLookAxes` | `-1` | `-1` uses look axes only when a paired look stick is available, `0` keeps classic single-stick behavior, and `1` forces dedicated look axes when any mapped look axis exists. |
| `in_joystickMoveAxisX` / `in_joystickMoveAxisY` | `0` / `1` | Raw joystick axes used for movement or classic stick look. |
| `in_joystickLookAxisX` / `in_joystickLookAxisY` | `2` / `3` | Raw joystick axes used for dedicated look on four-axis devices. |
| `in_joystickUpAxis` / `in_joystickUpAxisNegative` | `4` / `5` | Optional vertical/throttle axes; when both exist, openQ4 uses positive minus negative. |

Set an axis cvar to `-1` for auto, or to a raw SDL axis number from `0` through `31`. These advanced joystick cvars only affect generic SDL joysticks; SDL gamepads keep the stable `JOY` button and stick layout described below.

Steam Deck and SDL gamepad native input has additional cvars. The main controls are exposed in `Settings -> Game Options -> Controller`; the cvars remain useful for config files and console troubleshooting.

| Cvar | Default | What it changes |
|---|---:|---|
| `in_gyro` | `0` globally, `1` in the Steam Deck profile | Enables SDL gamepad gyro as mouse-look input during gameplay when the device exposes a gyro sensor. |
| `in_gyroSensitivity` | `1.0` globally, `0.20` in the Steam Deck profile | Scales gyro mouse-look motion. It still flows through normal mouse sensitivity. |
| `in_gyroDeadZone` | `0.015` | Ignores tiny gyro rates in radians per second. |
| `in_gyroYawAxis` / `in_gyroPitchAxis` | `2` / `0` | Selects the SDL gyro data indices used for yaw and pitch. |
| `in_gyroInvertYaw` / `in_gyroInvertPitch` | `0` / `0` | Flips native gyro yaw or pitch if the device orientation feels backwards. |
| `in_touchpadMode` | `0` globally, `1` in the Steam Deck profile | `0` disables motion, `1` moves the menu/console cursor, `2` sends mouse-look deltas during gameplay, and `3` keeps touchpad motion off while preserving touchpad button binds. |
| `in_touchpadSensitivity` | `1.0` | Scales SDL gamepad touchpad motion. |
| `in_touchscreen` | `1` | Routes native SDL touchscreen taps and drags to menu, console, and loading-continue input. |

The menu also exposes `in_joystickLowBatteryRumbleThreshold` and `in_joystickLowBatteryRumbleScale`, so Deck users can tune the battery-friendly rumble cap without editing config files.

## Stock Keyboard and Mouse Binds

The shipped defaults are defined in `content/baseoq4/pak0/default.cfg`.

### Core Movement and Combat

| Action | Default bind |
|---|---|
| Move forward / back | `W` / `S`, `Up Arrow` / `Down Arrow` |
| Move left / right | `A` / `D` |
| Turn left / right | `Left Arrow` / `Right Arrow` |
| Jump | `Space` |
| Crouch | `C` |
| Attack | `Mouse1`, `LeftCtrl`, `RightCtrl` |
| Zoom | `Mouse2`, `Mouse3` |
| Reload | `R` |
| Flashlight | `F` |
| Weapon wheel | `E` |
| Next / previous weapon | `MWHEELDOWN` / `MWHEELUP`, `[` / `]` |

### Common UI and Utility Binds

| Action | Default bind |
|---|---|
| Console | `` ` `` and `~` |
| Menu | `Escape` |
| Objectives / scores | `Tab` |
| Chat | `T` |
| Team chat | `Y` |
| Quick-chat wheel (multiplayer) | `V` |
| Quick save / quick load | `F5` / `F9` |
| Screenshot | `F12` |

## Default Controller Buttons

SDL gamepads use stable `JOY` button names so binds work across common Xbox, PlayStation, and Steam Input layouts. Controller support is first-class in SDL3 builds on Windows, Linux, and macOS. On macOS, controller support has not yet been tested on real hardware. The native Linux, experimental native macOS, and legacy Win32 input fallbacks currently remain keyboard/mouse focused.

| Button | Default action |
|---|---|
| `JOY1` / LB | Weapon wheel |
| `JOY2` / RB | Last weapon |
| `JOY3` / A or Cross | Jump |
| `JOY4` / B or Circle | Crouch |
| `JOY5` / Y or Triangle | Flashlight |
| `JOY6` / X or Square | Reload |
| `JOY7` / Start | Menu |
| `JOY8` / View, Back or Select | Scores or objectives while held |
| `JOY9` / D-pad Up | Objectives or scores |
| `JOY10` / D-pad Down | Quick-chat wheel (multiplayer) |
| `JOY11` / D-pad Right | Previous weapon |
| `JOY12` / D-pad Left | Next weapon |
| `JOY13` / Left stick click | Run or walk modifier |
| `JOY14` / Right stick click | Center view |
| `JOY15` / RT | Attack |
| `JOY16` / LT | Zoom |
| `JOY17` / Guide | Menu, when delivered by the OS |
| `JOY18` / Touchpad | Objectives or scores, when available |
| `JOY19` / Right paddle 1 | Jump |
| `JOY20` / Left paddle 1 | Crouch |
| `JOY21` / Right paddle 2 | Reload |
| `JOY22` / Left paddle 2 | Weapon wheel |

**Button prompts.** In the new interface's HUD, prompts such as Exit, Respawn
and the quick-chat wheel's channel switch show the control you would press: a
key's name while you play with keyboard and mouse, and your controller's button
once you use a controller, drawn the way the controller labels it (A, B, X, Y
or the PlayStation symbols, LB/L1, LT/L2/ZL, the d-pad's arm). openQ4 picks the
style from the controller SDL reports, or Steam Deck's on the Steam Deck
profile. If a remapping tool hides your controller, force a style with
`in_promptStyle`, which sets the menus' prompts too (see
[Button Prompts](#button-prompts)); generic prompts show positions instead
of letters.

Menu behavior:

- `JOY7` opens the in-game menu; `JOY8` (View) shows the scores or objectives
  while held, as `JOY9` does.
- `JOY3` activates the focused menu item.
- `JOY4`, `JOY7`, and `JOY8` back out of menus, except where `JOY7` (Menu)
  or `JOY8` (View) has its own job on a page: View hides or shows Join
  Game's server details.
- The D-pad and movement stick move menu focus.
- Holding the D-pad, movement stick, or shoulder buttons repeats navigation for long lists.
- Steam Deck packages enable the `steamdeck` platform profile, which applies `openq4_profile_steamdeck.cfg` on top of the normal defaults.
- `JOY23` through `JOY28`, `JOY29` through `JOY32`, and generic `AUX1` through `AUX16` are bindable for extra device buttons but are intentionally unbound by default.
- VR motion controllers press these same `JOY` buttons, so their binds change here too; see [Virtual Reality](vr.md) for which controller button is which.

## Useful Console Commands

| Command | What it does |
|---|---|
| `bind <key> "<command>"` | Assigns a command to a key or button. |
| `unbind <key>` | Removes a bind from one key or button. |
| `unbindall` | Clears all binds. Use with care. |
| `listBinds` | Prints the current binds. |
| `listControllers` | Prints SDL controller diagnostics, including active gamepad/joystick details, touchpad and gyro capability, touchscreen routing, and low-battery rumble state. |
| `writeConfig <filename>` | Writes a config snapshot to a `.cfg` file. |

Examples:

```cfg
bind mouse2 "_zoom"
bind JOY4 "_movedown"
bind q "_weaponWheel"
unbind JOY18
listBinds
writeConfig my-input-backup.cfg
```

Most input cvars are archived, so changes made through the menu or with `seta` persist after exit.

## Using the Console

The console takes the keyboard and the mouse while it is open.

| Key | What it does |
|---|---|
| `` ` `` or `~` | Opens and closes the console. Hold `Shift` to open it at a reduced height. |
| `Esc` | Closes the completion list if one is showing, otherwise the console. |
| `Enter` | Runs the line. If the highlighted completion would change the line, takes it first. |
| `Tab`, `Shift+Tab` | Completes the word at the caret and steps through the matches. |
| `Up`, `Down` | Steps through the commands you ran before; `Down` returns to the line you were typing. While the completion list shows, they move through the list. |
| `Page Up`, `Page Down`, mouse wheel | Scrolls the log by `con_scrollLines` lines. Holding a page key repeats at your system's key-repeat rate. While the completion list shows, the page keys move through it a page at a time, and the wheel moves through it when the pointer is over it. |
| `Ctrl+Home`, `Ctrl+End` | Jumps to the oldest and the newest line of the log. |
| `Home`, `End`, `Left`, `Right` | Moves the caret; with `Ctrl`, `Left` and `Right` move a word. Add `Shift` to select. |
| `Ctrl+A`, `Ctrl+C`, `Ctrl+X`, `Ctrl+V` | Select all, copy, cut and paste. `Shift+Insert` also pastes; `Insert` switches between inserting and overwriting. |
| `Ctrl+L` | Clears the log. |

With the mouse you can click in the edit line to place the caret and drag to select, drag across the log to select text and copy it with `Ctrl+C` (`Shift`+click extends the selection), drag selected text onto the edit line, drag the scrollbar, and click a completion to take it. A click below the console closes it.

Running a command returns the log to its newest line. Blank lines and a command repeated straight after itself are not added to the command history.

| Setting | What it does |
|---|---|
| `con_height` | How far down the screen the console opens, `0.1` to `1` (default `0.5`). |
| `con_scrollLines` | Lines scrolled per wheel notch or page key (default `6`). |
| `con_scrollSmooth` | `1` animates scrolling, `0` jumps. The animation finishes within half a second whatever the distance. |
| `con_completionPopup` | `1` shows the completion list while you type, `0` leaves completion to `Tab`. |

## Example Tweaks

### CPI-Normalized Mouse With Acceleration Disabled

```cfg
seta sensitivity 3.5
seta m_smooth 1
seta m_cpi 0
seta m_filter 0
seta cl_mouseAccel 0
seta cl_mouseAccelOffset 0
seta cl_mouseAccelPower 2
seta cl_mouseSensCap 0
```

### Invert Mouse

```cfg
seta m_pitch -0.022
```

### Higher Mouse Sensitivity With Light Smoothing

```cfg
seta sensitivity 6.50
seta m_smooth 2
```

### Toggle Crouch and Toggle Zoom

```cfg
seta in_toggleCrouch 1
seta in_toggleZoom 1
```

### Controller Tuning With a Larger Dead Zone and Weaker Rumble

```cfg
seta in_joystick 1
seta in_joystickDeadZone 0.24
seta in_joystickLookSensitivity 0.75
seta in_joystickLookCurve 1.35
seta in_joystickTriggerThreshold 0.40
seta in_joystickRumbleScale 0.50
```

### Steam Deck-Style Profile Selection

```cfg
seta com_platformProfile steamdeck
```

On Steam Deck or SteamOS, direct `openQ4-client_x64` launches auto-select this profile when `com_platformProfile` is still `default`. Set `OPENQ4_NO_STEAMDECK_AUTODETECT=1` to keep direct launches on the default profile.

## Troubleshooting

| Problem | Try this |
|---|---|
| Console does not open | Verify you are using the stock console binds (`` ` `` / `~`) or rebind a key through the Controls screen. |
| Controller is not detected | Make sure `Controller` is enabled, reconnect the device, and check whether Steam Input is remapping it. |
| Movement or aim drifts | Raise `Stick Dead Zone` until the drift stops. |
| Aim feels too fast | Lower `Look Sensitivity`; if tiny stick movements are the problem, raise `Look Curve`. |
| Aim feels too slow | Raise `Look Sensitivity`. |
| Aim feels too twitchy near center | Raise `Look Curve` or `Stick Dead Zone`. |
| Triggers activate by accident | Raise `Trigger Press`. |
| Triggers feel unresponsive | Lower `Trigger Press`. |
| Rumble does not work | Enable `Rumble`, set `Rumble Strength` above `0`, confirm the device supports haptics, and reset `s_controllerRumble` to `1` if it was changed from the console. |
| Rumble gets weaker at low battery | Raise `in_joystickLowBatteryRumbleScale`, lower `in_joystickLowBatteryRumbleThreshold`, or set the threshold to `0` to disable the low-battery cap. |
| Steam Deck gyro does not work | Confirm `in_gyro 1`, run `listControllers`, and make sure Steam Input is not hiding the gyro from the game. |
| Steam Deck touchpad moves aim instead of menus | Set `in_touchpadMode 1`; use `2` only when you want touchpad mouse-look during gameplay. Run `listControllers` if SDL reports no touchpad capability. |
| Mouse look is backwards vertically | Change `Mouse Pitch` or `Invert Look`, depending on the device. |
| Binding change did not stick | Run `writeConfig my-input-backup.cfg` to confirm your commands are valid, then quit normally so `openQ4Config.cfg` is rewritten. |

If you want a final personal override layer that survives menu changes, place your preferred `seta` and `bind` commands in `autoexec.cfg`.

For Steam Deck launcher details and Deck-specific notes, see [steam-deck.md](steam-deck.md).

### Switching between controller and mouse in menus

Controller navigation clears the old mouse highlight and hides the menu pointer. Moving or clicking the mouse restores pointer control. Directional navigation follows the visible controls in either direction and wraps at the ends; hidden pages cannot retain navigation focus. Binding capture still receives the original controller button.
