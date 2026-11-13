# Input settings

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

This guide shows how to set up your keyboard, mouse and controller in openQ4: changing bindings, tuning aim, using gyro and touch controls, and keeping your settings in config files. Steam Deck players should also read the [Steam Deck guide](steam-deck.md).

## Quick start

- To change what a key or button does, open **Settings → Controls**.
- To tune the mouse, open **Settings → Game Options → Mouse Input**.
- To tune a controller, open **Settings → Game Options → Controller**. Start with **Stick Dead Zone** and **Look Sensitivity** before changing anything else.
- Press `` ` `` or `~` to open the console.
- If your controls get into a mess, choose **Load Defaults** on the Settings page. This also resets your custom bindings, so back them up first with `writeConfig my-input-backup.cfg`.

The classic menus (`ui_retained 0`) have the same settings: their Game Options page has **General**, **Mouse** and **Controller** sections in its **Section** chooser.

## Changing key bindings

Open **Settings → Controls**. Its tabs group the actions:

| Tab | Actions |
|---|---|
| **Movement** | Move forward and back, move left and right, jump, crouch, turn, strafe, walk. |
| **Weapons** | One key per weapon, the multiplayer buy menu and the quick-chat wheel. |
| **Attack/Look** | Attack, previous and next weapon, reload, look up and down, mouse look, centre view, zoom, objectives and scores, flashlight, screenshot, weapon wheel. |
| **Other** | Quick save and load, chat and team chat, voting, multiplayer statistics, ready status, emotes and voice chat. |

To bind an action, select its row and press the key, mouse button or controller button you want. Analogue sticks are set up in the controller settings rather than bound as buttons.

Each row shows its keys as pictures: keycaps for the keyboard, a mouse with the bound button marked, and your controller's own buttons. An action keeps up to two keyboard or mouse keys and up to two controller buttons. Binding a third key replaces the two keys of that kind and leaves the other kind alone, so giving Jump a new keyboard key never unbinds it from your controller. Press Delete or Backspace on a row (X or Square on a controller) to clear it, and Escape to cancel while it waits for a key.

To bind from the console or a config file, see [Console commands for bindings](#console-commands-for-bindings).

## Keyboard and mouse

### Mouse settings

The mouse controls are on **Settings → Game Options → Mouse Input**. **Free Look**, **Toggle Zoom**, **Scope Sensitivity** and **Console Access** are on the **General** tab of the same page.

| Menu setting | Console setting | Default | What it does |
|---|---|---:|---|
| Invert Mouse | `m_pitch` | No (`0.022`) | Flips vertical mouse look (a negative value inverts). |
| Smooth Mouse | `m_smooth` | `1` | `1` uses each movement as it arrives; higher values, up to `8`, blend recent movements for smoother but less direct aim. |
| Mouse Sensitivity | `sensitivity` | `5` | Main mouse look speed. With Mouse CPI set, it is in degrees per centimetre of mouse travel. |
| Mouse CPI | `m_cpi` | `0` | Your mouse's CPI (also called DPI), for sensitivity that matches physical distance. `0` keeps the classic behaviour. |
| View Filter | `m_filter` | `0` | Blends the view over a few recent samples, as Quake Live can. Leave it at `0` for the most direct aim. |
| Mouse Accel | `cl_mouseAccel` | `0` | Quake Live-style acceleration. Positive values add sensitivity as you move faster; negative values take it away. |
| Accel Offset | `cl_mouseAccelOffset` | `0` | How fast you must move before acceleration starts. |
| Accel Curve | `cl_mouseAccelPower` | `2` | Shape of the acceleration curve. `2` matches Quake Live. |
| Sensitivity Cap | `cl_mouseSensCap` | `0` | Upper limit for accelerated sensitivity; `0` means no limit. |
| Free Look | `in_freeLook` | On | The mouse always looks around. |
| Toggle Zoom | `in_toggleZoom` | Off | Zoom toggles on and off instead of zooming while held. |
| Scope Sensitivity | `pm_zoomedSlow` | `50` | Look speed while zoomed, as a percentage of normal speed. |
| Console Access | `con_allowConsole` | Tilde (`1`) | **Tilde** opens the console with `` ` `` or `~`; **Ctrl+Alt+Tilde** (`0`) needs all three keys. |

**Physical sensitivity.** Set **Mouse CPI** to your mouse's CPI, then `360 / sensitivity` is roughly the centimetres of mouse travel for a full turn: a sensitivity of `10` is about 36 cm per turn, whatever CPI your mouse uses.

**High-resolution mice.** `m_maxMouseDelta 0` (the default) never limits a large mouse movement. Set a positive value only if a faulty mouse or driver makes the camera jump.

**Mouse buttons.** `Mouse1` to `Mouse8` can all be bound by name, and the wheel is `MWHEELUP` and `MWHEELDOWN`.

### Default keyboard and mouse bindings

| Action | Default |
|---|---|
| Move forward / back | `W` / `S`, or `Up Arrow` / `Down Arrow` |
| Move left / right | `A` / `D` |
| Turn left / right | `Left Arrow` / `Right Arrow` |
| Jump | `Space` |
| Crouch | `C` |
| Run / walk | `Shift` |
| Attack | `Mouse1`, `Ctrl` |
| Zoom | `Mouse2`, `Mouse3` |
| Reload | `R` |
| Flashlight | `F` |
| Weapon wheel | `E` |
| Next / previous weapon | Mouse wheel down / up, or `[` / `]` |
| Weapons | `1` to `0` and `-` |
| Console | `` ` `` and `~` |
| Menu | `Escape` |
| Objectives / scores | `Tab` |
| Chat / team chat | `T` / `Y` |
| Quick-chat wheel (multiplayer) | `V` |
| Buy menu (multiplayer) | `B` |
| Vote yes / no (multiplayer) | `F1` / `F2` |
| Quick save / quick load | `F5` / `F9` |
| Screenshot | `F12` |

## Controllers

openQ4 supports Xbox, PlayStation, Nintendo Switch, Steam Deck and other controllers out of the box, and you can plug them in or unplug them at any time. Standard gamepads need no axis setup.

### Controller settings

Tune your controller on **Settings → Game Options → Controller**.

| Menu setting | Console setting | Default | What it does |
|---|---|---:|---|
| Controller | `in_joystick` | On | Turns controller input on or off. |
| Stick Layout | `in_joystickSouthpaw` | Default | **Southpaw** swaps the movement and look sticks. |
| Stick Dead Zone | `in_joystickDeadZone` | `0.18` | How much of the stick's centre is ignored. |
| Look Sensitivity | `in_joystickLookSensitivity` | `0.75` | How fast the view turns at full stick. |
| Wheel Sensitivity | `in_weaponWheelSensitivity` | `4.0` | Cursor speed on the classic interface's weapon wheel, `0.25` to `16`. The new interface's wheel follows the stick's direction once you push it past about a third. |
| Wheel Offset (0=center) | `ui_weaponWheelOffset` | `27` | Moves the weapon wheel sideways, as a percentage of screen width from the centre, `-40` to `40`. `0` centres it; `27` keeps the usual right-hand position. |
| Look Curve | `in_joystickLookCurve` | `1.35` | Higher values make small stick movements gentler for fine aim; full-stick speed stays set by Look Sensitivity. |
| Invert Look | `in_joystickInvertLook` | Off | Flips vertical look. It does not affect the menus. |
| Trigger Press | `in_joystickTriggerThreshold` | `0.35` | How far a trigger must be pressed to count. |
| Rumble | `in_joystickRumble` | On | Vibration, on controllers that have it. |
| Rumble Strength | `in_joystickRumbleScale` | `1.0` | Vibration strength, `0` to `2`. |
| Low Battery Rumble | `in_joystickLowBatteryRumbleThreshold` | `0` (`20` on Steam Deck) | Battery percentage at or below which vibration is limited; `0` never limits it. |
| Low Battery Cap | `in_joystickLowBatteryRumbleScale` | `0.75` | The most vibration allowed while the battery is low. Your Rumble Strength setting is kept. |
| Prompt Style | `in_promptStyle` | Automatic | Which controller's button pictures the menus and HUD show. See [Button prompts](#button-prompts). |
| Swap Confirm and Back | `in_swapConfirmBack` | Automatic | Which face button confirms in menus. See [Confirm and back](#confirm-and-back). |

The gyro, touchpad and touchscreen rows are described in [Gyro, touchpad and touchscreen](#gyro-touchpad-and-touchscreen).

**Tuning tips:**

- View or movement drifts on its own: raise **Stick Dead Zone** a little.
- Aim feels sluggish: raise **Look Sensitivity**.
- Small aim corrections are twitchy: raise **Look Curve** or **Stick Dead Zone**.
- Full turn speed is hard to reach comfortably: lower **Look Curve** towards `1.0`.
- Triggers fire too easily: raise **Trigger Press**. Triggers feel unresponsive: lower it.
- Walking slowly is hard: `in_joystickMoveCurve` (default `1.0`, linear) works like Look Curve for movement. Higher values make slow walking easier near the stick's centre but need more stick travel for full speed.

### Default controller buttons

Controller buttons have fixed names (`JOY1`, `JOY2` and so on) so bindings work the same on every brand of controller.

| Button | Name | Default action |
|---|---|---|
| Left bumper (LB, L1, L) | `JOY1` | Weapon wheel |
| Right bumper (RB, R1, R) | `JOY2` | Last weapon |
| Bottom face button (A, Cross) | `JOY3` | Jump |
| Right face button (B, Circle) | `JOY4` | Crouch |
| Top face button (Y, Triangle) | `JOY5` | Flashlight |
| Left face button (X, Square) | `JOY6` | Reload |
| Menu (Start, Options, +) | `JOY7` | Menu |
| View (Back, Select, −) | `JOY8` | Scores or objectives while held |
| D-pad up | `JOY9` | Objectives or scores |
| D-pad down | `JOY10` | Quick-chat wheel (multiplayer) |
| D-pad right | `JOY11` | Previous weapon |
| D-pad left | `JOY12` | Next weapon |
| Left stick click | `JOY13` | Run / walk |
| Right stick click | `JOY14` | Centre view |
| Right trigger | `JOY15` | Attack |
| Left trigger | `JOY16` | Zoom |
| Guide / home button | `JOY17` | Menu, when the system passes it to the game |
| Touchpad click | `JOY18` | Objectives or scores |
| Right paddle 1 | `JOY19` | Jump |
| Left paddle 1 | `JOY20` | Crouch |
| Right paddle 2 | `JOY21` | Reload |
| Left paddle 2 | `JOY22` | Weapon wheel |

Extra buttons on some controllers (`JOY23` to `JOY32`, and `AUX1` to `AUX16` on other devices) can be bound but start unbound. VR motion controllers press these same `JOY` buttons, so changing a binding here changes it in VR too; see [Virtual reality](vr.md) for which controller button is which.

### Using the menus with a controller

- The left stick and D-pad move the selection, and holding the D-pad, stick or bumpers repeats for long lists. The right stick (the left one with Southpaw) scrolls panels that have nothing to select, such as the pause menu's objectives, the multiplayer scoreboard and player list, and long messages. In Player Setup it turns the preview of your player.
- Confirm selects the highlighted item. Back, Menu and View go back, except where Menu or View has its own job on a page, as below.
- The **Menu** button opens the in-game menu. In the menus it takes you back to the game from the pause menu's Save, Load and Restart Level pages. On a multiplayer server's Admin page it applies your server changes, and the Apply Changes button shows the Menu button's symbol.
- The **View** button hides or shows the server details on Join Game. While a dialog or an open list is showing, the Menu and View buttons close that first, as Back does.
- In a multiplayer match's menu, your bumpers switch tabs. On the Match and Admin pages, the triggers step through the sections.
- Using the controller hides the mouse pointer and clears the mouse highlight; moving or clicking the mouse brings the pointer back. Navigation follows what is on screen, in either direction, and wraps at the ends.

### Confirm and back

On a Nintendo-layout controller, menus confirm with A (the right button) and go back with B (the bottom button), as Switch games do. Other controllers confirm with the bottom button. Choosing Nintendo prompts with **Prompt Style** counts as a Nintendo layout.

To choose for any controller, set **Settings → Game Options → Controller → Swap Confirm and Back**. The menu prompts follow your choice; in-game bindings are not affected.

| Menu choice | `in_swapConfirmBack` | Menus confirm with |
| --- | --- | --- |
| Automatic (default) | `0` | The right button on Nintendo layouts, the bottom button otherwise |
| Off | `1` | The bottom button |
| On | `2` | The right button |

### Button prompts

Once you use a controller, the menus and the new HUD's prompts (such as Exit, Respawn and the quick-chat wheel's channel switch) show your controller's buttons instead of keyboard keys, drawn the way the controller labels them:

- Xbox controllers show A, B, X and Y, LB/RB and LT/RT.
- PlayStation controllers show the cross, circle, square and triangle, L1/R1 and L2/R2.
- Switch controllers show their own letters, L/R and ZL/ZR.
- The Steam Deck's built-in controls show its own buttons.
- Other controllers show a diamond of four buttons with the one to press filled in.

Using the keyboard or mouse switches the prompts back to keys.

If Steam Input or another remapping tool hides your real controller, choose the prompts yourself with **Settings → Game Options → Controller → Prompt Style**. It covers the menus and the HUD.

| Menu choice | `in_promptStyle` | Prompts shown |
| --- | --- | --- |
| Automatic (default) | `0` | Your controller's own |
| Xbox | `1` | Xbox |
| PlayStation | `2` | PlayStation |
| Nintendo | `3` | Nintendo; menus also confirm with A |
| Steam Deck | `4` | Steam Deck |
| Generic | `5` | Positions instead of letters |

`listControllers` shows the style in use for the connected controller on its `prompt glyphs:` line.

### Other joysticks

Flight sticks and other devices that are not standard gamepads use raw axes. openQ4 picks sensible axes automatically (`-1` means automatic); set an axis number from `0` to `31` to override it. These settings have no effect on standard gamepads.

| Console setting | Automatic choice | What it does |
|---|---:|---|
| `in_joystickUseDedicatedLookAxes` | `-1` | `-1` uses separate look axes when the device has a second stick, `0` uses one stick for everything, `1` always uses look axes when any are mapped. |
| `in_joystickMoveAxisX` / `in_joystickMoveAxisY` | `0` / `1` | Axes for movement, or for looking on a single-stick device. |
| `in_joystickLookAxisX` / `in_joystickLookAxisY` | `2` / `3` | Axes for looking on four-axis devices. |
| `in_joystickUpAxis` / `in_joystickUpAxisNegative` | `4` / `5` | Optional up/down or throttle axes; with both, openQ4 uses the first minus the second. |

## Gyro, touchpad and touchscreen

Controllers with motion sensors or a touchpad (PlayStation controllers, Switch Pro controllers, the Steam Deck and others) can aim with gyro and use the touchpad. Set them up on **Settings → Game Options → Controller** with the **Gyro**, **Gyro Sensitivity**, **Touchpad Mode**, **Touchpad Sensitivity** and **Touchscreen** rows. The Steam Deck profile turns gyro aiming and the touchpad menu cursor on for you.

| Console setting | Default | What it does |
|---|---:|---|
| `in_gyro` | `0` (`1` on Steam Deck) | Aims with the controller's gyro during play, as if moving a mouse. |
| `in_gyroSensitivity` | `1.0` (`0.20` on Steam Deck) | Gyro aiming speed, on top of your mouse sensitivity. |
| `in_gyroDeadZone` | `0.015` | Ignores very small, slow movements. |
| `in_gyroYawAxis` / `in_gyroPitchAxis` | `2` / `0` | Which sensor axes turn and tilt the view. |
| `in_gyroInvertYaw` / `in_gyroInvertPitch` | `0` / `0` | Flips gyro turning or tilting if it feels backwards. |
| `in_touchpadMode` | `0` (`1` on Steam Deck) | `0` Off, `1` Menu Cursor (moves the menu and console pointer), `2` Mouse Look during play, `3` Button Only (no motion, but touchpad clicks still work). |
| `in_touchpadSensitivity` | `1.0` | Touchpad pointer speed. |
| `in_touchscreen` | `1` | Taps and drags on a touchscreen work the menus, the console and the loading screen's continue prompt. |

If gyro or the touchpad does nothing, run `listControllers` to see whether the game can see them, and check that Steam Input is not turning them into mouse movement first (see the [Steam Deck guide](steam-deck.md)).

## Using the console

The console takes the keyboard and mouse while it is open.

| Key | What it does |
|---|---|
| `` ` `` or `~` | Opens and closes the console. Hold `Shift` to open it at a reduced height. |
| `Esc` | Closes the completion list if one is showing, otherwise the console. |
| `Enter` | Runs the line. If the highlighted completion would change the line, takes it first. |
| `Tab`, `Shift+Tab` | Completes the word at the caret and steps through the matches. |
| `Up`, `Down` | Steps through the commands you ran before; `Down` returns to the line you were typing. While the completion list shows, they move through the list. |
| `Page Up`, `Page Down`, mouse wheel | Scrolls the log by `con_scrollLines` lines. While the completion list shows, the page keys move through it a page at a time, and the wheel moves through it when the pointer is over it. |
| `Ctrl+Home`, `Ctrl+End` | Jumps to the oldest and the newest line of the log. |
| `Home`, `End`, `Left`, `Right` | Moves the caret; with `Ctrl`, `Left` and `Right` move a word. Add `Shift` to select. |
| `Ctrl+A`, `Ctrl+C`, `Ctrl+X`, `Ctrl+V` | Select all, copy, cut and paste. `Shift+Insert` also pastes; `Insert` switches between inserting and overwriting. |
| `Ctrl+L` | Clears the log. |

With the mouse you can click in the edit line to place the caret and drag to select, drag across the log to select text and copy it with `Ctrl+C` (`Shift`+click extends the selection), drag selected text onto the edit line, drag the scrollbar, and click a completion to take it. A click below the console closes it.

Running a command returns the log to its newest line. Blank lines, and a command repeated straight after itself, are not added to the history.

| Setting | What it does |
|---|---|
| `con_height` | How far down the screen the console opens, `0.1` to `1` (default `0.5`). |
| `con_scrollLines` | Lines scrolled per wheel notch or page key (default `6`). |
| `con_scrollSmooth` | `1` (default) animates scrolling, `0` jumps. |
| `con_completionPopup` | `1` (default) shows the completion list while you type, `0` leaves completion to `Tab`. |

## Config files

openQ4 saves your settings and bindings, whether you change them in the menus or the console, in `openQ4Config.cfg`. It writes the file when you quit, inside the `baseoq4` folder of your save folder:

| System | Save folder |
|---|---|
| Windows | `%LOCALAPPDATA%\openQ4` |
| Linux and Steam Deck | `~/.local/share/openq4` (or `$XDG_DATA_HOME/openq4`) |
| macOS | `~/Library/Application Support/openQ4` |

At startup openQ4 reads these files in order, so later ones win:

1. `default.cfg`: Quake 4's default bindings and settings.
2. `openq4_defaults.cfg`: openQ4's own defaults.
3. A platform profile, such as `openq4_profile_steamdeck.cfg` on a Steam Deck.
4. `openQ4Config.cfg`: your saved settings.
5. `autoexec.cfg`, if you create one.

For settings the menus would otherwise overwrite, put your own `seta`, `set` and `bind` lines in an `autoexec.cfg` next to `openQ4Config.cfg`. It runs every time the game starts.

The platform profile is chosen at launch with `+set com_platformProfile steamdeck` (or `couch`, or `default`); it cannot be changed from a config file. The Steam Deck launcher sets it for you, and a direct launch on a Steam Deck or SteamOS picks it automatically while the profile is still `default`. Set the environment variable `OPENQ4_NO_STEAMDECK_AUTODETECT=1` to stop that.

### Console commands for bindings

| Command | What it does |
|---|---|
| `bind <key> "<command>"` | Assigns a command to a key or button. |
| `unbind <key>` | Removes the binding from one key or button. |
| `unbindall` | Clears every binding. Use with care. |
| `listBinds` | Prints your current bindings. |
| `listControllers` | Prints details of connected controllers: type, battery, gyro and touchpad support, touchscreen use, low-battery rumble state and the prompt style. |
| `writeConfig <filename>` | Saves your current settings and bindings to a `.cfg` file, for a backup. |

```cfg
bind mouse2 "_zoom"
bind JOY4 "_movedown"
bind q "_weaponWheel"
unbind JOY18
writeConfig my-input-backup.cfg
```

## Example configs

Physical mouse sensitivity, no acceleration:

```cfg
seta m_cpi 1600            // your mouse's CPI
seta sensitivity 10        // about 36 cm per full turn
seta m_smooth 1
seta m_filter 0
seta cl_mouseAccel 0
```

Inverted mouse:

```cfg
seta m_pitch -0.022
```

Toggle crouch and toggle zoom:

```cfg
seta in_toggleCrouch 1
seta in_toggleZoom 1
```

A controller with a larger dead zone and weaker rumble:

```cfg
seta in_joystickDeadZone 0.24
seta in_joystickTriggerThreshold 0.40
seta in_joystickRumbleScale 0.50
```

## Platform notes

Controller support is first-class in SDL3 builds on Windows, Linux, and macOS. SDL3 is the cross-platform library openQ4 uses for windows, keyboard, mouse and controllers, and the openQ4 downloads are built with it. During play the mouse is captured for raw movement, and on Linux openQ4 asks for unscaled movement where your desktop supports it.

Linux and macOS SDL3 builds opt into SDL's HIDAPI controller backends by default, which talk to PlayStation, Switch, Steam, Steam Deck and Xbox-class controllers directly, so they get the same buttons, sticks, triggers, rumble, battery, gyro and touchpad support as on Windows. Controllers have not yet been tested on a Mac, where openQ4 is a preview. Run `listControllers` to see which driver and features are active for a connected controller.

Builds made with the older native Linux, native macOS or Win32 input paths support the keyboard and mouse only.

## Troubleshooting

| Problem | Try this |
|---|---|
| Console does not open | Check **Console Access** on **Settings → Game Options → General**, or bind a key to the console on **Settings → Controls**. |
| Controller is not detected | Make sure **Controller** is on, reconnect the controller, and check whether Steam Input is remapping it. Run `listControllers`. |
| View or movement drifts | Raise **Stick Dead Zone** until it stops. |
| Aim is too fast | Lower **Look Sensitivity**; if tiny stick movements are the problem, raise **Look Curve**. |
| Aim is too slow | Raise **Look Sensitivity**. |
| Aim is twitchy near the centre | Raise **Look Curve** or **Stick Dead Zone**. |
| Triggers fire by accident, or feel unresponsive | Raise or lower **Trigger Press**. |
| No rumble | Turn on **Rumble**, set **Rumble Strength** above `0`, check that the controller can vibrate, and set `s_controllerRumble 1` if it was changed from the console. |
| Rumble gets weaker at low battery | Raise **Low Battery Cap**, lower **Low Battery Rumble**, or set it to `0` to remove the limit. |
| Gyro does nothing | Turn on **Gyro**, run `listControllers`, and make sure Steam Input is not hiding the gyro from the game. |
| The touchpad moves your aim instead of the menu pointer | Set **Touchpad Mode** to **Menu Cursor** (`in_touchpadMode 1`); use **Mouse Look** only if you want touchpad aiming. Run `listControllers` if the touchpad is not detected. |
| Vertical look is backwards | Change **Invert Mouse** for the mouse or **Invert Look** for a controller. |
| A binding change did not stick | Quit the game normally so `openQ4Config.cfg` is saved, or put the binding in `autoexec.cfg`. |
