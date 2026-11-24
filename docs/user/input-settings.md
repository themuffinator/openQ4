# Input settings

> This guide matches openQ4 0.15.0. Anything new in 0.15.0 is marked **New in 0.15.0**. See [release status](release-status.md) for what each download includes.

This guide shows how to set up your keyboard, mouse and controller in openQ4: changing bindings, tuning aim, using gyro and touch controls, and keeping your settings in config files. Steam Deck players should also read the [Steam Deck guide](steam-deck.md).

## Quick start

- To change what a key or button does, open **Settings → Controls**.
- To tune the mouse, open **Settings → Controls → Mouse**.
- To tune a controller, open **Settings → Controls** and use its **Controller** and **Aim** tabs. Start with **Response** and **Turn Speed** on **Aim** before changing anything else.
- Press `` ` `` or `~` to open the console.
- If your controls get into a mess, choose **Reset Controller** or **Reset Keyboard and Mouse** on **Settings → Controls → Controller**. Each restores its own device and leaves the other alone. **Load Defaults** on the Settings page resets everything, bindings included, so back them up first with `writeConfig my-input-backup.cfg`.

**New in 0.15.0:** the mouse and controller settings, with **Free Look**, **Toggle Zoom** and **Scope Sensitivity**, are on the **Mouse** and **Controller** tabs of **Settings → Controls**, beside the key bindings. In 0.14.1 they are on **Settings → Game Options**: its **Mouse Input** and **Controller** tabs, and the three aim settings on **General**.

The classic menus (`ui_retained 0`) have the same settings: their Game Options page has **General**, **Mouse** and **Controller** sections in its **Section** chooser.

## Changing key bindings

Open **Settings → Controls**. Its tabs group the actions:

| Tab | Actions |
|---|---|
| **Movement** | Move forward and back, move left and right, jump, crouch, turn, strafe, walk. |
| **Weapons** | One key per weapon, the multiplayer buy menu and the quick-chat wheel. |
| **Attack/Look** | Attack, previous and next weapon, reload, look up and down, mouse look, centre view, zoom, objectives and scores, flashlight, screenshot, weapon wheel. |
| **Other** | Quick save and load, chat and team chat, voting, multiplayer statistics, ready status, emotes and voice chat. |
| **Mouse** | Not bindings: the [mouse settings](#mouse-settings), led by Free Look, Toggle Zoom and Scope Sensitivity. **New in 0.15.0.** |
| **Controller** | Not bindings: the [controller settings](#controller-settings). **New in 0.15.0.** |
| **Aim** | More controller settings: [stick aim](#stick-aim) and [gyro, touchpad and touchscreen](#gyro-touchpad-and-touchscreen). Not in 0.15.0. |

To bind an action, select its row and press the key, mouse button or controller button you want. Analogue sticks are set up in the controller settings rather than bound as buttons.

Each row shows its keys as pictures: keycaps for the keyboard, a mouse with the bound button marked, and your controller's own buttons. An action keeps up to two keyboard or mouse keys and up to two controller buttons. Binding a third key replaces the two keys of that kind and leaves the other kind alone, so giving Jump a new keyboard key never unbinds it from your controller. Press Delete or Backspace on a row (X or Square on a controller) to clear it, and Escape to cancel while it waits for a key.

To bind from the console or a config file, see [Console commands for bindings](#console-commands-for-bindings).

## Keyboard and mouse

### Mouse settings

The mouse controls are on **Settings → Controls → Mouse**, which starts with **Free Look**, **Toggle Zoom** and **Scope Sensitivity**. **Console Access** stays on **Settings → Game Options → General**.

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
| Last weapon | `G` (**New in 0.15.0**) |
| Weapon wheel | `E` |
| Next / previous weapon | Mouse wheel down / up, or `[` / `]` |
| Weapons | `1` to `0` and `-`, in weapon-slot order |
| Grappling hook (Classic multiplayer ruleset) | `Mouse4` (**New in 0.15.0**) |
| Console | `` ` `` and `~` |
| Menu | `Escape` |
| Objectives / scores | `Tab` |
| Chat / team chat | `T` / `Y` |
| Quick-chat wheel (multiplayer) | `V` |
| Buy menu (multiplayer) | `B` |
| Vote yes / no (multiplayer) | `F1` / `F2` |
| Quick save / quick load | `F5` / `F9` |
| Screenshot | `F12` |

**Using the weapon wheel without a controller.** Hold the wheel key and move the mouse: a dot shows where you are pointing, and it never leaves the wheel, so pushing past the edge just slides it round the rim. While the wheel is open, a weapon key picks that weapon, the mouse wheel (or `[` / `]`, or the D-pad on a controller) steps to the next or previous weapon you have, and Last weapon picks the one you had before. Let go of the wheel key to switch. A controller's stick points the wheel by direction, so no dot is shown.

`F1` to `F12` keep their bindings while a menu is open, so `F12` also takes a screenshot of a menu. **New in 0.15.0:** 0.14.1's new title and pause screens ignore them.

## Controllers

openQ4 supports Xbox, PlayStation, Nintendo Switch, Steam Deck and other controllers out of the box, and you can plug them in or unplug them at any time. Standard gamepads need no axis setup.

### Controller settings

> The **Aim** tab, and the rows marked *new*, are in the development builds after 0.15.0. In 0.15.0 the **Controller** tab holds **Stick Dead Zone**, **Look Sensitivity**, **Look Curve** and the gyro and touchpad rows instead.

Tune your controller on **Settings → Controls**. The **Controller** tab holds the controller itself, its buttons, and its [rumble, triggers and lights](#rumble-triggers-and-lights):

| Menu setting | Console setting | Default | What it does |
|---|---|---:|---|
| Controller | `in_joystick` | On | Turns controller input on or off. |
| Active Controller *new* | `in_joystickDevice` | Automatic | **Automatic** uses whichever controller you used last. **This Controller** keeps the one in your hands. See [More than one controller](#more-than-one-controller). |
| Test Controller *new* | | | Opens a panel that shows what the controller is doing. See [Testing your controller](#testing-your-controller). |
| Stick Layout | `in_joystickSouthpaw` | Default | **Southpaw** swaps the movement and look sticks. |
| Wheel Sensitivity | `in_weaponWheelSensitivity` | `4.0` | Cursor speed on the classic interface's weapon wheel, `0.25` to `16`. The new interface's wheel follows the stick's direction once you push it past about a third. |
| Wheel Offset (0=center) | `ui_weaponWheelOffset` | `27` | Moves the weapon wheel sideways, as a percentage of screen width from the centre, `-40` to `40`. `0` centres it; `27` keeps the usual right-hand position. |
| Prompt Style | `in_promptStyle` | Automatic | Which controller's button pictures the menus and HUD show. See [Button prompts](#button-prompts). |
| Swap Confirm and Back | `in_swapConfirmBack` | Automatic | Which face button confirms in menus. See [Confirm and back](#confirm-and-back). |
| Button Layout *new* | `in_joystickLayout` | Classic | Rebinds the controller's buttons to a layout. See [Button layouts](#button-layouts). |
| Trigger Press | `in_joystickTriggerThreshold` | `0.35` | How far a trigger must be pressed to count. |
| Toggle Crouch *new* | `in_toggleCrouch` | Off | Crouch toggles instead of holding. |
| Always Run *new* | `in_alwaysRun` | On | Run by default; the Run / walk button walks. |
| Context Buttons *new* | `in_joystickContextActions` | On | Buttons take the vote, match and quick save commands at the right moment. See [Context buttons](#context-buttons). |
| Reset Controller *new* | `resetController` | | Restores the controller's buttons and settings. Keyboard and mouse are left alone. |
| Reset Keyboard and Mouse *new* | `resetKeyboardMouse` | | Restores the keyboard's and mouse's bindings and the mouse settings. The controller is left alone. |

### Stick aim

The **Aim** tab sets how the look stick turns the view. Its right-hand column holds the [gyro and touchpad](#gyro-touchpad-and-touchscreen) settings.

| Menu setting | Console setting | Default | What it does |
|---|---|---:|---|
| Response | `in_joystickResponse` | Classic | A preset for the look curve, the dead zones and the turn boost together. **Classic** is how every earlier release aimed. **Precise** has a smaller dead zone and a gentler centre. **Dynamic** adds a fast turn when you hold the stick over. Changing any of those settings afterwards shows **Custom**. |
| Turn Speed | `in_joystickYawSpeed` | `157.5` | How fast the view turns at full stick, in degrees per second. |
| Look Up/Down Speed | `in_joystickPitchSpeed` | `157.5` | The same for looking up and down. |
| Invert Look | `in_joystickInvertLook` | Off | Flips vertical look. It does not affect the menus. |
| Look Curve | `in_joystickLookCurve` | `1.35` | Higher values make small stick movements gentler for fine aim; full-stick speed is unchanged. |
| Turn Boost | `in_joystickTurnBoost` | `0` | Extra turn speed, in degrees per second, once the stick has been held fully sideways for a moment. It never applies while zoomed. |
| Aim Assist | `in_aimAssist` | Off | See [Aim assist](#aim-assist). |
| Look Stick Dead Zone | `in_joystickLookDeadZone` | `0.18` | How much of the look stick's centre is ignored. |
| Look Stick Outer Zone | `in_joystickLookOuterDeadZone` | `0` | How much of the look stick's travel at the rim counts as fully over. Raise it if a worn stick no longer reaches full speed. |
| Move Stick Dead Zone | `in_joystickMoveDeadZone` | `0.18` | How much of the movement stick's centre is ignored. |
| Move Stick Outer Zone | `in_joystickMoveOuterDeadZone` | `0` | The same at the movement stick's rim. |
| Move Dead Zone Shape | `in_joystickMoveDeadZoneAxial` | Radial | **Per Axis** keeps you walking in a straight line when the stick wanders slightly sideways. |
| Move Curve | `in_joystickMoveCurve` | `1.0` | Higher values make slow walking easier near the stick's centre but need more stick travel for full speed. |

Turning speed no longer drops while you walk: only the keyboard's turn keys change with running.

If you upgrade from 0.15.0 or earlier, your **Stick Dead Zone** and **Look Sensitivity** carry over into these settings the first time you start, so your controller feels the same. The old console settings `in_joystickDeadZone` and `in_joystickLookSensitivity` still work: changing one sets both sticks or both speeds.

**Tuning tips:**

- View or movement drifts on its own: raise that stick's **Dead Zone** a little.
- Aim feels sluggish: raise **Turn Speed**, or try **Response → Dynamic**.
- Small aim corrections are twitchy: raise **Look Curve**, or try **Response → Precise**.
- Full turn speed is hard to reach: raise **Look Stick Outer Zone**, or lower **Look Curve** towards `1.0`.
- Triggers fire too easily: raise **Trigger Press**. Triggers feel unresponsive: lower it.

### Aim assist

**Aim Assist** helps stick aim in two ways and no others:

- The stick turns the view more slowly while the crosshair is on or beside an enemy, so a pass across one is easier to stop.
- While you move a stick, the view follows part of that enemy's own movement, so an enemy strafing past does not slide out from under your crosshair.

It never moves the crosshair onto an enemy, and with both sticks at rest it does nothing. It is for sticks only: the mouse and gyro aiming are never assisted. It ignores allies, corpses and anything behind a wall, and it also steadies the stick over terminals and lift panels.

**Off** is the default. **Low**, **Standard** and **High** are progressively stronger. In multiplayer the server decides how much is allowed (`si_aimAssist`, off by default, and always off in competitive rulesets); your own setting is never raised by a server, only capped.

### More than one controller

Every connected controller is ready to use. With **Active Controller** on **Automatic**, the one in charge is whichever you used last: press a button or push a stick on another and it takes over, with the prompts changing to its buttons. Anything the first controller was holding is released.

Set **Active Controller** to **This Controller** to keep the one in your hands in charge whatever else is connected, for example a Steam Deck with an external pad, or a remapping tool that shows a second, virtual controller. While that controller is unplugged the others work as usual, and it takes charge again when it comes back.

If the controller you are playing with disconnects, single-player pauses on the pause menu, and reconnecting does not resume the game for you. A multiplayer match cannot pause: its HUD tells you until a controller is back.

You get one notice when the controller's battery reaches 15% (**Low Battery Notice** on the **Controller** tab) and one when it starts charging.

`listControllers` lists every connected controller and marks the active one.

### Testing your controller

**Settings → Controls → Controller → Test Controller** opens a panel that shows what the game receives from the controller in use:

- Both sticks, each with a dot where the stick is. The filled circle at the centre is that stick's dead zone and the orange ring near the rim is its outer limit, at the sizes set on **Aim**. A dot that rests outside the filled circle means the stick drifts: raise its dead zone until the dot rests inside.
- Each trigger's travel as a bar, with a mark at **Trigger Press**.
- Every button, drawn the way your controller labels it. A button lights while you hold it.
- The gyro, where the controller has one: a dot that moves as you turn the controller, and whether the gyro is calibrated. The dot moves only while **Gyro** or **Gyro Menu Pointer** is on.
- The controller's name, whether it is on battery, charging or wired, and its battery level where it reports one.

**Test Rumble** runs each motor the controller has for a moment, one after another: the strong motor, the light motor, then each trigger's motor on controllers that have them. It works whatever **Rumble** is set to. The panel opens with **Test Rumble** selected, so pressing Confirm starts it.

Back closes the panel, so the Back button is the one button it cannot show.

If Steam Input is managing the controller and no gyro or touchpad reaches the game, the panel says so. See the [Steam Deck guide](steam-deck.md).

The first time you connect a kind of controller, two notices in the menus name the settings that choose its button pictures: **Prompt Style** and **Swap Confirm and Back**.

### Button layouts

**Button Layout** rebinds the controller's buttons in one step. It changes nothing until you choose a layout, and it shows **Custom** once you rebind any controller button yourself.

| Layout | What it does |
|---|---|
| Classic | The [default buttons](#default-controller-buttons). |
| Bumper Jumper | Jump on the left bumper and crouch on the right, so both thumbs stay on the sticks. The weapon wheel moves to the bottom face button and Last weapon to the right one. |
| One-Handed (Left) | Everything on the left half. The left stick looks (Southpaw), the left bumper walks forward while held, the left trigger fires, the left stick click jumps, and the D-pad reloads (up), crouches (down), opens the weapon wheel (left) and zooms (right). View opens the menu. |
| One-Handed (Right) | Everything on the right half. The right stick looks, the right bumper walks forward while held, the right trigger fires, the right stick click jumps, and the face buttons reload, crouch, zoom and open the weapon wheel. |

The one-handed layouts also turn on **Toggle Crouch** and **Toggle Zoom**, because a thumb on the stick cannot hold another button, and they work well with [gyro aiming](#gyro-touchpad-and-touchscreen) for turning while you walk.

From the console: `controllerLayout classic`, `bumperjumper`, `onehandedleft` or `onehandedright`.

### Context buttons

A controller has fewer buttons than the game has commands. With **Context Buttons** on, a few buttons do something else at the moment it is useful:

| When | Buttons |
|---|---|
| A vote is open and you have not voted | D-pad left votes Yes, D-pad right votes No |
| You hold the scoreboard button (View) in a match | A Ready, X Spectate, Y Chat, B Team chat, RB Buy menu |
| You hold the objectives button (View) in the campaign | A Quick save, X Quick load |
| In a match | D-pad up is voice chat; View still shows the scores |

If you bind one of those commands to a controller button of your own, that button is used instead. The letters are the Xbox names; the game shows your controller's own.

### Rumble, triggers and lights

The right-hand column of the **Controller** tab:

| Menu setting | Console setting | Default | What it does |
|---|---|---:|---|
| Rumble | `in_joystickRumble` | On | Vibration, on controllers that have it. Off also stops the trigger effects below. |
| Rumble Strength | `in_joystickRumbleScale` | `1.0` | Vibration strength, `0` to `2`. |
| Trigger Rumble | `in_joystickTriggerRumble` | On | The trigger you fire with pulses with each shot, on controllers with trigger motors (Xbox One and later). |
| Adaptive Triggers | `in_joystickAdaptiveTriggers` | On | On a DualSense, the trigger's resistance follows your weapon: light for automatic weapons, firm for the shotgun, launchers and railgun, a hum for the lightning gun and gauntlet. |
| Directional Hits | `in_joystickDirectionalRumble` | On | A hit rumbles more on the side it came from. |
| Low Battery Rumble | `in_joystickLowBatteryRumbleThreshold` | `0` (`20` on Steam Deck) | Battery percentage at or below which vibration is limited; `0` never limits it. |
| Low Battery Cap | `in_joystickLowBatteryRumbleScale` | `0.75` | The most vibration allowed while the battery is low. Your Rumble Strength setting is kept. |
| Controller Light | `in_joystickLight` | On | The controller's light shows your team in a team match and your health in single-player, green through amber to red. |
| Player Light | `in_joystickPlayerLight` | On | The controller's player lights show player 1 on the controller in use. |
| Low Battery Notice | `in_joystickLowBatteryNotice` | `15` | Battery percentage at which you get a notice; `0` never shows one. |

Each of these is used only on controllers that have it.

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
| View (Back, Select, −) | `JOY8` | Scores or objectives while held, and the button for [context buttons](#context-buttons) |
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

### Typing with a controller

When you select a text field with a controller, such as a save's name, your player name, a server's address or a password, and press Confirm, an on-screen keyboard appears. On a Steam Deck in game mode, Android and other systems that show their own keyboard, you get that one instead.

| Controller | Does |
|---|---|
| D-pad or left stick | Moves between keys |
| Confirm | Types the selected key |
| West face button (X on Xbox, Square on PlayStation) | Deletes the character before the caret |
| North face button (Y, Triangle) | Adds a space |
| Bumpers | Move the caret left and right |
| Left trigger | Shift, for the next letter |
| Right trigger | The next set of characters |
| Menu / Start | Done |
| Back | Cancels the edit |

Every function is also a key on the keyboard, and with touch or a mouse you can press the keys directly. The right trigger steps through five sets: Latin letters and digits; accented letters for French, German, Italian, Spanish and Portuguese; accented letters for Polish, Czech, Hungarian and Turkish; Cyrillic for Russian and Ukrainian; and symbols. With the game in Russian or Ukrainian it opens on Cyrillic, and a number field opens on the digits.

**Done** accepts what you typed, exactly as Enter does on a keyboard. On the Save Game page that saves the game under the name.

`ui_onScreenKeyboard` controls it: `1` (default) shows it when you last used a controller or touch and the system has no keyboard of its own, `2` shows it whenever a field is edited, and `0` turns it off.

The in-game chat line and the console still need a keyboard, and this keyboard is part of the new interface only.

### Confirm and back

On a Nintendo-layout controller, menus confirm with A (the right button) and go back with B (the bottom button), as Switch games do. Other controllers confirm with the bottom button. Choosing Nintendo prompts with **Prompt Style** counts as a Nintendo layout.

To choose for any controller, set **Settings → Controls → Controller → Swap Confirm and Back**. The menu prompts follow your choice; in-game bindings are not affected.

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

If Steam Input or another remapping tool hides your real controller, choose the prompts yourself with **Settings → Controls → Controller → Prompt Style**. It covers the menus and the HUD.

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

Controllers with motion sensors or a touchpad (PlayStation controllers, Switch Pro controllers, the Steam Deck and others) can aim with gyro and use the touchpad. Set them up in the right-hand column of **Settings → Controls → Aim**. The Steam Deck profile turns gyro aiming and the touchpad menu cursor on for you.

> In 0.15.0 gyro aiming went through the mouse settings and had only **Gyro** and **Gyro Sensitivity**. The settings below are in the development builds after 0.15.0. If you already aimed with gyro, your speed carries over the first time you start.

| Menu setting | Console setting | Default | What it does |
|---|---|---:|---|
| Gyro | `in_gyro` | Off (On on Steam Deck) | Aims with the controller's gyro during play. |
| Gyro Turn Ratio | `in_gyroYawRatio` | `1.0` | How far the view turns for each degree you turn the controller. `1` is one to one. |
| Gyro Tilt Ratio | `in_gyroPitchRatio` | `1.0` | The same for looking up and down. |
| Gyro Turning | `in_gyroSpace` | Player Space | Which movement turns the view. **Yaw**: spinning the controller as if it lay flat on a table. **Roll**: tilting it like a steering wheel. **Player Space**: turning it about the real vertical however you hold it, which suits a tilted handheld; it needs an accelerometer and otherwise acts as Yaw. |
| Gyro Active | `in_gyroActivation` | Always | When the gyro aims: **Always**; **Off While Held**, which pauses it while you hold the gyro button so you can bring the controller back to centre; **Only While Held**; or **While Zoomed**. Bind the gyro button with `bind <button> _gyro`. |
| Invert Gyro Turn | `in_gyroInvertYaw` | Off | Flips gyro turning if it feels backwards. |
| Invert Gyro Tilt | `in_gyroInvertPitch` | Off | Flips gyro looking up and down. |
| Gyro Tightening | `in_gyroDeadZone` | `0.015` | Scales down very slow movement so a still hand does not drift, while a deliberate slow turn still moves. |
| (console only) | `in_gyroSmoothing` | `0` | Averages very slow movement to hide sensor noise; faster movement stays direct. `0` is off, and `0.05` is a gentle setting. |
| Gyro Menu Pointer | `in_gyroMenuPointer` | Off | Moves the menu pointer with the gyro. |
| Calibrate Gyro | `calibrateGyro` | | Set the controller down, choose this and leave it still for two seconds. Use it if the view drifts with the controller at rest. The game also corrects slowly whenever the controller has been still for a moment, and remembers the result for each controller. |
| Touchpad Mode | `in_touchpadMode` | Off (Menu Cursor on Steam Deck) | **Off**, **Menu Cursor** (moves the menu and console pointer), **Mouse Look** during play, or **Button Only** (no motion, but touchpad clicks still work). |
| Touchpad Sensitivity | `in_touchpadSensitivity` | `1.0` | Touchpad pointer speed. |
| Touchscreen | `in_touchscreen` | On | Taps and drags on a touchscreen work the menus, the console and the loading screen's continue prompt. |

Gyro aiming has its own speed. Mouse Sensitivity, Mouse CPI, Smooth Mouse, View Filter and mouse acceleration no longer change it, and it works with the mouse turned off.

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
| `listControllers` | Prints every connected controller with the active one marked, and its details: type, battery, gyro and touchpad support, what its motors and lights can do, touchscreen use and the prompt style. |
| `controllerLayout <name>` | Rebinds the controller's buttons to a [layout](#button-layouts). |
| `resetController`, `resetKeyboardMouse` | Restore one device's bindings and settings and leave the other alone. |
| `calibrateGyro` | Measures the gyro's bias: hold the controller still for two seconds. |
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

Gyro aiming at twice the controller's turn, paused while the right bumper is held:

```cfg
seta in_gyro 1
seta in_gyroYawRatio 2
seta in_gyroActivation 1
bind JOY2 "_gyro"
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
| A button, stick or trigger seems not to work | Open **Test Controller** on the **Controller** tab and watch whether the game sees it. |
| View or movement drifts | Raise that stick's **Dead Zone** on **Aim** until it stops. **Test Controller** shows where the stick rests. |
| Aim is too fast | Lower **Turn Speed**; if tiny stick movements are the problem, raise **Look Curve**. |
| Aim is too slow | Raise **Turn Speed**, or try **Response → Dynamic**. |
| Aim is twitchy near the centre | Raise **Look Curve** or **Look Stick Dead Zone**, or try **Response → Precise**. |
| The wrong controller is in charge | Press a button on the one you want, or set **Active Controller** to **This Controller**. Run `listControllers`. |
| The view drifts with gyro on and the controller at rest | Choose **Calibrate Gyro** and leave the controller still for two seconds. |
| Gyro turns or tilts the wrong way | Turn on **Invert Gyro Turn** or **Invert Gyro Tilt**. |
| Triggers fire by accident, or feel unresponsive | Raise or lower **Trigger Press**. |
| No rumble | Turn on **Rumble**, set **Rumble Strength** above `0`, check that the controller can vibrate, and set `s_controllerRumble 1` if it was changed from the console. |
| Rumble gets weaker at low battery | Raise **Low Battery Cap**, lower **Low Battery Rumble**, or set it to `0` to remove the limit. |
| Gyro does nothing | Turn on **Gyro**, check **Gyro Active**, run `listControllers`, and make sure Steam Input is not hiding the gyro from the game. |
| The touchpad moves your aim instead of the menu pointer | Set **Touchpad Mode** to **Menu Cursor** (`in_touchpadMode 1`); use **Mouse Look** only if you want touchpad aiming. Run `listControllers` if the touchpad is not detected. |
| Vertical look is backwards | Change **Invert Mouse** for the mouse or **Invert Look** for a controller. |
| A binding change did not stick | Quit the game normally so `openQ4Config.cfg` is saved, or put the binding in `autoexec.cfg`. |
