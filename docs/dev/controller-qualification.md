# Controller qualification checklist

This is the PAD-09 gate: controller families on real hardware. It needs a
person holding the controller. Automated tests may never synthesize keyboard,
mouse or controller input on a machine, and the in-process virtual controllers
(`virtualController`, see the [technical contract](controller-experience.md#testing-without-hardware))
prove the code paths, not how a controller feels or what its hardware does.
Nothing here is inferred from them.

UI-02, the platform signoffs, AND-02 and the
[multiplayer human checks](mp-human-qualification.md) cite this checklist
instead of repeating its steps.

## How to record a run

- Stage the current build into `.install/` and launch from there:
  `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`.
- One record per controller family and platform. Give the build banner from
  the top of the log (`0.15.0-dev+g<sha>`), the platform, the controller and
  how it is connected, and the output of `listControllers`.
- One line per step: pass, fail (with what you saw) or not run. A failure links
  to an issue or a roadmap task.
- The log is `<fs_savepath>\baseoq4\logs\openq4.log`.

## Families and platforms

| Family | Devices |
| --- | --- |
| Xbox | Series and One over USB, Bluetooth and the wireless adapter; an Elite with paddles |
| PlayStation | DualShock 4; DualSense and DualSense Edge |
| Nintendo | Switch Pro Controller; a Joy-Con pair |
| Generic | Any XInput pad |
| Steam | Steam Deck controls; a Steam Input virtual pad over another controller |
| Raw joystick | One flight stick |

Platforms: Windows x64, Linux x64, Steam Deck in Game Mode and Desktop Mode,
and the macOS preview.

PAD-09 closes when each first-class platform has a recorded pass for the Xbox,
PlayStation and Nintendo or Deck families and every failure links to an issue
or task.

## A. Detection, hotplug and handover (PAD-01)

| # | Do this | Pass when |
| --- | --- | --- |
| A1 | Start with the controller connected; run `listControllers`. | It is listed as active with the right name and type. |
| A2 | Unplug it in the main menu, then plug it back in. | "Controller disconnected." shows and stays until it returns; it works again without a restart. |
| A3 | Connect a second controller. Press a button on it, then a button on the first. | Each press takes charge at once, and the prompts change to that controller's buttons. |
| A4 | Hold a button on one controller and press a button on the other. | The held button's action stops; nothing stays stuck. |
| A5 | Set **Controller** to **This Controller** on one pad; press buttons on the other. | The other pad does nothing while the chosen one is connected. |
| A6 | Unplug the chosen pad, play with the other, then plug the chosen pad back in. | The other works while it is away; the chosen pad takes charge when it returns. |
| A7 | In single-player play, unplug the controller you are playing with. | The game pauses on the pause menu with the notice. Plugging back in does not resume by itself. |
| A8 | In a multiplayer match, unplug the controller you are playing with. | The HUD shows the notice until a controller returns; the match does not pause. |
| A9 | Let a wireless controller's battery reach 15%, then plug it in. | One low-battery notice; one charging notice; neither repeats. |
| A10 | Play with keyboard and mouse while a wireless pad goes to sleep. | Nothing pauses and no HUD notice appears. |

## B. Prompts, Confirm and Back

| # | Do this | Pass when |
| --- | --- | --- |
| B1 | Open the menus and the HUD prompts with this controller. | Its own buttons are drawn: letters, shapes or positions as printed on it. |
| B2 | On a Nintendo layout, confirm and go back. | A (right) confirms and B (bottom) goes back; **Swap Confirm and Back** reverses it. |
| B3 | Walk every menu with `ui_retained 1`, then with `ui_retained 0`. | Every row is reachable, focus is always visible, Back closes one level. |
| B4 | Set **Prompt Style** to each choice. | Menus and HUD follow it. |

## C. Stick aim (PAD-02)

| # | Do this | Pass when |
| --- | --- | --- |
| C1 | Leave both sticks alone for a minute. | The view and the player do not drift. Raise the dead zone of the stick that drifts if it does. |
| C2 | Hold the look stick fully sideways with **Response** on Classic and time a full turn. | About 2.3 seconds (157 degrees per second), walking or running. |
| C3 | Set **Turn Speed** and **Look Up/Down Speed** to different values. | Each axis follows its own. |
| C4 | Choose Precise, then Dynamic. Hold the stick fully sideways with Dynamic. | The turn speeds up after a moment and drops as soon as the stick comes back. No boost while zoomed. |
| C5 | Edit any Aim setting after choosing a preset. | **Response** shows Custom. |
| C6 | On a worn stick that cannot reach its rim, raise **Look Stick Outer Zone**. | Full turn speed is reachable again. |
| C7 | Turn on the mouse's **View Filter** and turn with the stick. | The stick turns the view as before. |
| C8 | Feel review, two families: aim at moving enemies with Classic, Precise and Dynamic. | Record which preset felt right; this decides the fresh-profile default. |

## D. Aim assistance (PAD-03)

| # | Do this | Pass when |
| --- | --- | --- |
| D1 | With **Aim Assist** Off, sweep the crosshair across an enemy. | No change in speed. |
| D2 | With Low, Standard and High, sweep across an enemy. | The stick slows over the enemy, more at each step, and never pulls onto it. |
| D3 | Stand still, sticks at rest, while an enemy strafes past the crosshair. | The view does not move. |
| D4 | Strafe around an enemy without touching the look stick. | The view follows part of the enemy's motion, never all of it. |
| D5 | Aim near a marine, a corpse, and an enemy behind a wall. | No slowdown for any of them. |
| D6 | Aim with the mouse, and with gyro, across an enemy. | Neither is assisted. |
| D7 | Use a terminal or lift panel with the crosshair as its cursor. | The stick is steadier over the screen. |
| D8 | Join a server with `si_aimAssist 0`, then one with `si_aimAssist 3`, then a competitive ruleset. | None, your own setting, none. |
| D9 | Feel review, two families, campaign and SP Arena. | Record whether Standard should be the fresh-profile default. |

## E. Gyro and touchpad (PAD-04)

| # | Do this | Pass when |
| --- | --- | --- |
| E1 | Turn **Gyro** on. Turn the controller left, then tip its far edge up. | The view turns left, then looks up. If either is reversed, record it: this is the direction check the code could not make. |
| E2 | With **Gyro Turn Ratio** 1, turn the controller a quarter turn. | The view turns a quarter turn. |
| E3 | Change Mouse Sensitivity, Mouse CPI and View Filter. | Gyro aim does not change. |
| E4 | Hold the controller flat, then tilted towards you, in **Player Space**, **Yaw** and **Roll**. | Player Space turns level however it is held; Yaw and Roll behave as named. |
| E5 | Set the controller down. | The view is still. Run **Calibrate Gyro** if it drifts; it stays still afterwards, and after a restart. |
| E6 | Set **Gyro Active** to each choice with a button bound to `_gyro`. | Always; paused while held (lift, re-centre, release: no jump); only while held; only while zoomed. |
| E7 | Turn on **Gyro Menu Pointer**. | The gyro moves the menu pointer. |
| E8 | Touchpad: each **Touchpad Mode**. | Off, menu cursor, mouse look and button only behave as named. |
| E9 | Feel review on DualSense, Switch Pro and Deck. | Record the ratio and space that felt right. |

## F. Haptics and lights (PAD-08)

| # | Do this | Pass when |
| --- | --- | --- |
| F1 | Fire each weapon; take a hit from the left, then the right. | Shots pulse; the hit is stronger on the side it came from. |
| F2 | On a controller with trigger motors, fire. | The firing trigger pulses; **Trigger Rumble** off stops it. |
| F3 | On a DualSense, switch between the machine gun, the shotgun and the lightning gun. | The trigger's resistance differs: light, firm, humming. Slack in menus. |
| F4 | Watch the light in the campaign as health falls, and in a team match. | Green through amber to red; the team's colour in a match. |
| F5 | Check the player lights. | Player 1 on the controller in use. |
| F6 | Open a menu, pause, and switch away from the game while firing. | All rumble and trigger effects stop. |
| F7 | Turn **Rumble** off. | No motor, no trigger motor, slack triggers; the light still works. |
| F8 | Elite Series 2, for [#244](https://github.com/themuffinator/openQ4/issues/244). | Weapon fire rumbles. |

## G. Layout (PAD-06)

| # | Do this | Pass when |
| --- | --- | --- |
| G1 | In play, hold View. | The scoreboard or the objectives show while held; the menu does not open. |
| G2 | In the campaign, hold View and press A, then X. | Quick save, quick load. Without View, A jumps and X reloads. |
| G3 | In a match, call a vote and press D-pad left; in the next, D-pad right. | Yes, then No. The vote notice names those buttons. |
| G4 | In a match, hold View and press A, X, Y, B and RB. | Ready, Spectate, Chat, Team chat, Buy menu. |
| G5 | In a match, hold D-pad up. | Voice chat transmits while held. |
| G6 | Choose each **Button Layout**. Rebind one button. | The buttons change as described; the chooser then shows Custom. |
| G7 | Play a level with each One-Handed layout, one hand only. | Every action needed to finish it is reachable. |
| G8 | **Reset Controller**, then **Reset Keyboard and Mouse**. | Each restores its own device and leaves the other, and your name, alone. |

## H. The test panel (PAD-07)

**Settings → Controls → Controller → Test Controller.**

| # | Do this | Pass when |
| --- | --- | --- |
| H1 | Connect a family for the first time on a fresh profile and open the menu. | Two notices name Prompt Style and Swap Confirm and Back, once. |
| H2 | Open the panel. Move each stick round its rim and let it rest. | Each dot follows its stick to the rim and rests inside the filled dead zone. |
| H3 | Change a dead zone and an outer limit on **Aim**, and **Southpaw**, then reopen. | The rings change size, and the look stick's zones move to the other stick. |
| H4 | Squeeze each trigger slowly. | Its bar fills with the travel and its glyph lights at the **Trigger Press** mark. |
| H5 | Press every button, the stick presses, the paddles and the touchpad. | Each lights the glyph your controller's label matches, and only that one. |
| H6 | With **Gyro** on, turn and tilt the controller; run **Calibrate Gyro**. | The gyro's dot follows, and the line reads not calibrated, hold still, then calibrated. |
| H7 | **Test Rumble**, with **Rumble** off. | Each motor the controller has runs in turn and the panel names it; motors it lacks are skipped. |
| H8 | On battery, then charging. | The power line and the battery bar follow. |
| H9 | Through Steam Input with the gyro or touchpad not passed on. | The panel says so and names the Steam Deck guide. |

## I. The on-screen keyboard (PAD-05)

With no keyboard connected, on a system that shows no keyboard of its own.

| # | Do this | Pass when |
| --- | --- | --- |
| I1 | Save Game: select the name, press Confirm. | The keyboard appears clear of the field. |
| I2 | Type a name with a capital, a digit and a space; delete and retype a letter mid-name with the bumpers and the west button. | The field shows exactly what was typed. |
| I3 | Use both triggers: a capital, then each layer. | Shift lasts one letter; the five layers come round in order. |
| I4 | Press Menu. | The game saves under the name and the keyboard closes. |
| I5 | Multiplayer → Player Setup: set a name with an accented or Cyrillic letter. | The name shows in the game exactly. |
| I6 | Join a password-protected server: type the password. | The masked field takes it and the server accepts it. |
| I7 | A number field (a slider's typed value). | It opens on the digits; Done applies the value. |
| I8 | Press Back with text typed. | The edit is cancelled, the field keeps its old value and the keyboard closes. |
| I9 | Touch screen: tap the keys. | Each tap types its key; a tap outside the panel reaches the page. |
| I10 | Steam Deck game mode; Android. | The system's keyboard appears, not openQ4's. |
| I11 | Each shipped language. | The function keys' words fit their keys. |

## J. Play

| # | Do this | Pass when |
| --- | --- | --- |
| J1 | Campaign: combat, a hovertank section and a walker section. | Aiming, driving and firing work throughout. |
| J2 | Use a world terminal and a lift panel. | The crosshair works them. |
| J3 | Use the weapon wheel; name a save. | Both work with the controller alone. |
| J4 | A multiplayer match: voting, Ready, chat, buying, the quick-chat wheel. | All reachable. Typing a chat line still needs a keyboard: the chat line is not a retained field. |
| J5 | SP Arena: a full match. | Menus, play and the ceremony work. |
| J6 | Flight stick: move, look and fire on the raw-joystick path. | The axes map as `listControllers` reports; the axis settings correct them. |
