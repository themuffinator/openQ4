# Client settings guide

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

This page shows where openQ4's most useful options live, so you can set the game up without reading every guide. Open **Settings** from the main menu or the pause menu.

## First things to check

- **Performance preset.** openQ4 picks one for your hardware on first launch. Change it under **Settings → Display → Performance Preset**, or press **Auto-Detect** to pick again. If your PC is near the [minimum requirements](getting-started.md#system-requirements), start low.
- **Screen.** Choose fullscreen or a borderless window, the resolution and, with several monitors, the **Display Device**.
- **Mouse and controller.** Set mouse sensitivity, controller stick dead zone and look sensitivity under **Settings → Game Options**.
- **Volume and subtitles.** Set music and dialogue volume and turn on subtitles under **Settings → Audio**.

## What each Settings tab holds

| Tab | What you'll find |
|---|---|
| **Controls** | Key and button bindings for movement, weapons, attack and look, and everything else (chat, quick chat, voice chat, the weapon wheel, quick save and load). |
| **Game Options** | Gameplay comfort: auto reload and weapon switch, gun position and field of view, corpses and decals, language, cinematics, the HUD and crosshair, mouse feel, and controller options including gyro, touchpad and rumble. |
| **Display** | Performance preset and Auto-Detect, renderer and graphics card, interface and text size, monitor, fullscreen and window size, V-Sync, anti-aliasing, resolution scale, and effects such as shadows, bloom, ambient occlusion, HDR and the CRT filter. Press **Apply Changes** to use them: most take effect without a restart, and screen changes ask you to keep or revert them. |
| **Audio** | Volume, music and dialogue volume, sound device, speaker layout and headphone mode, and dialogue subtitles. |

The classic menus have the same options in a different layout, and call the **Display** tab **System**.

To change difficulty during a single-player game, choose **Restart Level** in the pause menu and pick a difficulty; the level starts again at it. The classic menus have a separate **Difficulty** entry. See [Gameplay Settings](gameplay-settings.md#difficulty).

## Which guide covers what

- [Display Settings](display-settings.md): presets, screen and window modes, multiple monitors, anti-aliasing and the renderer.
- [Input Settings](input-settings.md): key bindings, mouse feel, controllers and your config files.
- [Gameplay Settings](gameplay-settings.md): difficulty, HUD and crosshair, field of view, subtitles, sound and corpses.
- [Shadow Mapping](shadow-mapping.md): shadow quality and tuning.
- [Cel Shading](cel-shading.md): the cartoon-style look.
- [DDS Texture Replacements](texture-replacements.md): installing texture packs.
- [Steam Deck](steam-deck.md): the Deck launcher and handheld controls.
- [Languages](languages.md): menu and dialogue languages.

## New and classic interface

openQ4 0.14.1 uses its new interface by default: rebuilt menus, settings, loading screens and the Remastered HUD, made for controllers, touch, tall screens and large text. The classic Quake 4 menus and HUD are still there.

- To switch, open the console and enter `seta ui_retained 0` for the classic interface or `seta ui_retained 1` for the new one. Your choice is saved, and upgrading never changes a choice you already made.
- If a new screen can't be shown, openQ4 shows the classic one instead. This happens when a mod brings its own main menu, or when a screen's files are missing or damaged. There is nothing for you to fix.
- The centred ammo, health and armour bar has its own size and screen-edge settings; see [Gameplay Status Bar](gameplay-settings.md#gameplay-status-bar).

For console-level configuration and file locations, see [TECHNICAL.md](../../TECHNICAL.md).
