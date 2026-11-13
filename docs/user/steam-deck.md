# Steam Deck

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

openQ4 runs natively on the Steam Deck from the Linux x64 download. Its Steam Deck launcher sets up the Deck's controls, gyro aiming, a larger HUD and a frame-rate cap for the handheld screen.

## Getting started

1. Install Quake 4 from Steam on the Deck. openQ4 finds it in any of your Steam libraries, including one on a microSD card.
2. In Desktop Mode, download the openQ4 Linux x64 package and extract it.
3. Run `openQ4-steamdeck` from the extracted folder.
4. To play from Game Mode, add `openQ4-steamdeck` to Steam as a non-Steam game.

The launcher turns on openQ4's Steam Deck profile (it adds `+set com_platformProfile steamdeck` and sets `OPENQ4_STEAMDECK=1`) and passes on any extra options you give it.

If you run `openQ4-client_x64` directly on a Steam Deck or another SteamOS device, openQ4 picks the Steam Deck profile automatically, unless you start it with a different `com_platformProfile`. To stop the automatic choice and play with the normal desktop settings, set `OPENQ4_NO_STEAMDECK_AUTODETECT=1` (or `OPENQ4_DISABLE_STEAMDECK_AUTODETECT=1`).

### What the Steam Deck profile changes

- Gyro aiming is on, at a gentle `in_gyroSensitivity 0.20`.
- The trackpads move the menu and console pointer (`in_touchpadMode 1`), and the touchscreen works the menus (`in_touchscreen 1`).
- Vibration is limited while the battery is at 20% or below.
- The HUD starts at 125% (`ui_hudScale 1.25`) so it stays readable on the small screen.
- The frame rate is capped to suit the screen (see [Performance](#performance)).
- Menu and HUD prompts show the Deck's own buttons once you use its controls.

These are starting points. Anything you change in the menus is saved and kept on later launches.

## Controls

| Deck control | Action |
|---|---|
| R2 | Attack |
| L2 | Zoom |
| A | Jump |
| B | Crouch |
| X | Reload |
| Y | Flashlight |
| L1 | Weapon wheel |
| R1 | Last weapon |
| Left stick click (L3) | Run / walk |
| Right stick click (R3) | Centre view |
| Menu button (☰) | In-game menu |
| View button (⧉) | Scores or objectives while held |
| D-pad up | Objectives or scores |
| D-pad down | Quick-chat wheel (multiplayer) |
| D-pad left / right | Next / previous weapon |
| Back buttons R4 / L4 / R5 / L5 | Jump / crouch / reload / weapon wheel, when Steam passes them to the game |

In the menus, the D-pad and left stick move the selection (the D-pad always does this while a menu is open, whatever it is bound to in play), A selects and B goes back. Holding the D-pad, stick or bumpers repeats for long lists. Rebind anything on **Settings → Controls**; see [Input settings](input-settings.md#default-controller-buttons) for the full button list.

## Tuning the controls

All the controller settings are on **Settings → Game Options → Controller**: stick layout, dead zone, look sensitivity, look curve, invert look, trigger press, rumble, gyro, trackpad mode, touchscreen and the low-battery rumble limit.

- **Gyro aim too slow or too fast:** change **Gyro Sensitivity** (`in_gyroSensitivity`). Turn **Gyro** off (`in_gyro 0`) to aim with the sticks only.
- **Trackpads:** **Touchpad Mode** (`in_touchpadMode`) is **Menu Cursor** on the Deck. Choose **Mouse Look** to aim with a trackpad during play, **Off** to ignore trackpad movement, or **Button Only** to ignore movement but keep trackpad clicks.
- **Touchscreen:** with **Touchscreen** on (`in_touchscreen 1`), taps and drags work the menus, the console and the loading screen's continue prompt.
- **Vibration:** turn off **Rumble** (`in_joystickRumble 0`) or lower **Rumble Strength** (`in_joystickRumbleScale`). The Deck profile sets **Low Battery Rumble** to `20` (`in_joystickLowBatteryRumbleThreshold`) and **Low Battery Cap** to `0.75`, so at 20% battery or below vibration is limited to three-quarters strength; your Rumble Strength setting is kept. Set **Low Battery Rumble** to `0` to remove the limit.
- **Button prompts:** to show another controller's buttons, change **Prompt Style**; see [Button prompts](input-settings.md#button-prompts).
- **HUD size:** change **Settings → Game Options → HUD → HUD Size**.

The [Input settings](input-settings.md) guide explains every option.

## Steam Input

openQ4 reads the Deck's gyro and trackpads itself. For that to work, your Steam controller layout must pass the Deck to the game as a gamepad. If the layout turns the gyro or trackpads into mouse movement, Steam handles them before openQ4 sees them, and openQ4's own gyro and trackpad settings do nothing. A mouse-style layout still works if you prefer it; turn openQ4's **Gyro** off so the two don't fight.

## Performance

- A fresh install picks the Performance preset on a Steam Deck, as does **Auto-Detect** on **Settings → Display**.
- With the Steam Deck profile, openQ4 caps the frame rate to the screen's refresh rate, kept between 40 and 90: an LCD Deck normally gets 60, and an OLED Deck can get more. This only happens while `com_maxfps` is still at its default of `240`, so a cap you set yourself is kept. Set `com_steamDeckAutoFrameCap 0` to turn this off, or `com_steamDeckFrameCap` to a number to choose the Deck's cap.

### Shadow maps on the Deck

The Performance preset uses Quake 4's original stencil shadows. If you want [shadow maps](shadow-mapping.md) instead, these settings run well on the Deck because they limit how many shadows are redrawn each frame:

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapSize 1024
seta r_shadowMapPointSize 512
seta r_shadowMapCSM 1
seta r_shadowMapCascadeCount 3
seta r_shadowMapMaxUpdatesPerView 2
vid_restart
```

## Troubleshooting

### The game window does not start, or looks wrong

Linux desktops draw windows with one of two display systems: **Wayland**, the newer one, and **X11**, the older one. openQ4 uses Wayland directly when the desktop supports it, and otherwise runs through **XWayland**, a compatibility layer that lets X11 programs run on Wayland.

- If the window fails to open or misbehaves, launch with `OPENQ4_FORCE_X11=1` to use X11 through XWayland instead. Both the launcher and a direct `openQ4-client_x64` launch honour it.
- If the problem is with the window's title bar or borders on Wayland, try `OPENQ4_WAYLAND_DISABLE_LIBDECOR=1`. libdecor is the helper some Wayland desktops use to draw those window decorations.
- To force Wayland, set `SDL_VIDEO_DRIVER=wayland`.

Set these in front of the command, for example `OPENQ4_FORCE_X11=1 ./openQ4-steamdeck` in a terminal, or `OPENQ4_FORCE_X11=1 %command%` in the launch options of your Steam shortcut.

### openQ4 cannot find Quake 4

openQ4 looks for `steamapps/common/Quake 4` in every Steam library it can find. It starts from these Steam folders and then adds every library listed in Steam's `libraryfolders.vdf`:

- `OPENQ4_STEAM_ROOT` / `OPENQ4_STEAM_ROOTS`
- `STEAM_COMPAT_CLIENT_INSTALL_PATH`
- `$XDG_DATA_HOME/Steam`
- `~/.steam/steam`
- `~/.steam/root`
- `~/.local/share/Steam`
- `~/.var/app/com.valvesoftware.Steam/.local/share/Steam` (Flatpak Steam)

To point it at the game directly, set `OPENQ4_QUAKE4_PATH` (or `OPENQ4_QUAKE4_ROOT`) to the Quake 4 folder, or `OPENQ4_STEAM_LIBRARY` (or `OPENQ4_STEAM_LIBRARIES`) to a Steam library that contains `steamapps/common/Quake 4`. Separate several folders with `:` or `;`.

### Gyro, trackpads or rumble don't work

Check your Steam Input layout first (see [Steam Input](#steam-input)). Then open the console and run `listControllers`. It shows the controller openQ4 is using, its battery, whether it has a gyro and trackpads, how the touchscreen is handled and whether the low-battery rumble limit is active. Include what it prints when you report a controller problem.

### After suspending

When the Deck suspends or openQ4 goes to the background, the game releases the controls, stops vibration and saves your settings. Controllers reconnect when you return.
