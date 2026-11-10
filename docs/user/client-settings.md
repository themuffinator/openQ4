# Client Settings Guide

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

This guide helps players find the most useful openQ4 settings without digging through every advanced option.

## Best Places to Start

If you are setting up openQ4 for the first time, these are the areas most players adjust first:

- **Display** for fullscreen mode, resolution, monitor selection, and image quality
- **Controls** for keyboard, mouse, controller, and bindings
- **Game Options** for gameplay comfort settings and a few audio-related tweaks

## Recommended First Checks

- Set your preferred fullscreen or borderless mode
- Run **Auto-Detect** for the Performance Preset if this is a new machine or you are near the [minimum requirements](getting-started.md#system-requirements)
- Adjust mouse sensitivity or controller dead zone
- Confirm your display target if you use multiple monitors
- Pick whether you want extra visual effects such as bloom or shadow mapping
- Lower or raise music volume to taste

## Which Guide Covers What?

- [Display Settings](display-settings.md) - resolution, fullscreen, windowed mode, multi-monitor, anti-aliasing, and aspect behavior
- [Input Settings](input-settings.md) - keybinds, mouse tuning, controller support, and config-file behavior
- [Gameplay Settings](gameplay-settings.md) - convenience toggles and a few gameplay/audio settings
- [Shadow Mapping](shadow-mapping.md) - optional advanced shadow-map tuning
- [Cel Shading](cel-shading.md) - banded lighting and outline settings for the cel-shaded look
- [DDS Texture Replacements](texture-replacements.md) - DXT/BC7 texture-pack layout and diagnostics
- [Steam Deck](steam-deck.md) - launcher and handheld-specific input notes

## In-Game Menus

Most player-facing options are available directly in the in-game menus:

- `Settings -> System`
- `Settings -> Controls`
- `Settings -> Game Options`
- `Difficulty` (single-player) - restart the current level at another difficulty; see [Gameplay Settings](gameplay-settings.md#difficulty)

## Modern and Classic Screens

Current source builds default to the modern interface (`ui_retained 1`) for new profiles. The [0.14.000 release](../dev/releases/v0.14.000.md) shipped with the classic interface (`ui_retained 0`); the modern default was restored afterward. Existing saved preferences are preserved, so upgrading does not change an interface choice already saved in your profile.

The modern UI replaces the title screen and its pages (Single Player with the campaign and difficulty choice and the Arena ladder, Load Game, Save Game, Restart Level, Credits, the MODS list, Settings with its Controls, Game Options, Display and Audio pages, and Multiplayer with its server browser, Create Server, Demos and Player Setup), the single-player pause menu, the screen after a death, loading screens, the multiplayer join and in-match menus, the match summary, and the gameplay HUD. HUD coverage includes crosshairs, objectives, radio, powerups, boss and vehicle displays, multiplayer scores/chat/summary panels and Awakening overlays. See [Gameplay Status Bar](gameplay-settings.md#gameplay-status-bar) for the centered ammo/health/armor bar's independent size and safe-inset settings.

- Enter `seta ui_retained 1` for the modern UI or `seta ui_retained 0` for classic. The choice is saved. An explicit launch option overrides the saved choice; the VS Code retained-UI configurations explicitly enable it for development testing.
- If a modern screen can't be shown, openQ4 shows the classic one instead, with nothing to fix on your side. This happens when a mod brings its own main menu, or when a modern screen's files are missing or damaged.

If you want deeper technical details or console-level configuration, see [TECHNICAL.md](../../TECHNICAL.md).
