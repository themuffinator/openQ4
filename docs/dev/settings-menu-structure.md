# Settings Menu Structure

This document describes the current openQ4 Settings menu as implemented by the main menu GUI scripts. It records the visible menu organization, widget types, cvar or GUI-state targets, discrete values, and slider ranges.

Source files:

- `content/baseoq4/pak0/guis/mainmenu.gui`
- `content/baseoq4/pak0/guis/menu/settings/controls.gui`
- `content/baseoq4/pak0/guis/menu/settings/system.gui`
- `content/baseoq4/pak0/guis/menu/settings/audio.gui`
- `content/baseoq4/pak0/guis/menu/settings/popups.gui`
- `content/baseoq4/pak0/guis/menu/settings/game.gui`
- `content/baseoq4/pak0/guis/menu/settings/game_hovers.gui`
- `src/framework/Session_menu.cpp`
- `docs/dev/settings-menu-registry.json`

`docs/dev/settings-menu-registry.json` is the machine-readable inventory for the Settings surfaces touched by the current refactor. The coverage test uses it to validate source/widget presence, localization keys, cvar, GUI-state, or bind targets, UI ranges, shipped default keys, defaults, restart warnings, and accessibility notes for the registered rows, and it now asserts that every active Game Options value widget has a registry record.

## Widget Type Key

| Type | Meaning |
|---|---|
| `windowDef` | Static label, background, container, preview, action hotspot, or hover surface depending on event handlers. |
| `choiceDef` | Discrete picker. Uses explicit `values` when present. For common boolean `choiceType 0` rows, `#str_200059` maps to `No;Yes` and normally means `0;1`. |
| `sliderDef` | Numeric slider. The GUI range is defined by `low`, `high`, and `step`. |
| `editDef` | Text/numeric value field, usually paired with a slider that writes the same cvar. Decimal or integer settings use `numeric 1` where the valid range is non-negative. |
| `bindDef` | Key/button binding capture widget. Bind widgets have no numeric range. |

Several rows use `gui` instead of `cvar`. These write a GUI state variable first, then apply one or more console commands from GUI event handlers.

Controls registry records use `target_type: "bind"` and validate shipped default keys from `content/baseoq4/pak0/default.cfg` plus `content/baseoq4/pak0/openq4_defaults.cfg`.

## Top-Level Settings Shell

The Settings panel is `p_settings` in `mainmenu.gui`. It owns the left navigation, included content panes, included Settings popups, and shared named events.

| Entry | Widget | Opens or performs | Notes |
|---|---|---|---|
| Controls | `windowDef set_b_controls` | `p_settings_ctrls` | Defaults to the Movement binding category. |
| Game Options | `windowDef set_b_gameoptions` | `p_settings_game` | Includes gameplay, mouse, controller, crosshair, corpse, language, and console options. |
| System | `windowDef set_b_system` | `p_settings_sys` | Video, display, quality, advanced rendering, and post effects. |
| Audio | `windowDef set_b_audio` | `p_settings_audio` | Output backend/device plus volume controls. |
| Android setup | `windowDef set_android_setup` | Standalone Android setup Activity | `androidSetupAvailable` exposes this only on standalone Android. The SDL host opens setup without closing the running game; Return to game keeps the same session. File operations remain blocked while the native storage lease is held. |
| Load Defaults | `windowDef set_b_loaddefaults` | Reset-confirm popup | Sets `desktop::active` and opens `anim_pop_defaultsIn`. |
| Video Restart | `windowDef set_b_vidrestart` | `vid_restart` | Hidden by default, then shown by video-warning paths for changes that require a restart. Back also routes through the warning flow when `desktop::vidwarn` is set. |
| Back | `windowDef set_b_back` | Exits Settings | If `desktop::vidwarn` is set, Back routes through the video warning flow. |

Scroll controls:

| Pane | Scroll widget | Cvar | Range | Step | Notes |
|---|---|---|---|---|---|
| Game Options | `choiceDef set_game_section_choice` | `game_section_choice` | General / Mouse / Controller / Crosshair / Gameplay / View Weapon | `1` | Localized section picker below the pane title and above the scroll frame. Sets live GUI state `gui::gui_set_game_scroll` to the group anchor and calls `applySettingsScroll game`; the engine clamps and applies the matching row-canvas rect. |
| Game Options | `sliderDef set_game_scroll_thumb` | `gui_set_game_scroll` | `0..46` | `1` | Vertical scrollbar. Calls `applySettingsScroll game`; `idSliderWindow` propagates the command from thumb drag as well as key/button events. |
| System | `choiceDef set_sys_section_choice` | `sys_section_choice` | Video / Window / Rendering / Quality / Post FX / Sizing | `1` | Localized section picker above the scroll frame. Sets live GUI state `gui::gui_set_sys_scroll` to the group anchor and calls `applySettingsScroll system`; the engine clamps and applies the matching row-canvas rect. |
| System | `sliderDef set_sys_scroll_thumb` | `gui_set_sys_scroll` | `0..26` | `1` | Vertical scrollbar. Calls `applySettingsScroll system`; `idSliderWindow` propagates the command from thumb drag as well as key/button events. |
| Audio | `sliderDef set_audio_scroll_thumb` | `gui_set_audio_scroll` | `0..0` | `1` | Disabled in practice because Audio fits without scrolling; the engine keeps its scroll thumb hidden and no-evented. |

Mouse-wheel, Page Up/Page Down, controller shoulder, Home, and End scrolling is handled by `src/framework/Session_menu.cpp`. The same C++ scroll table owns the scroll cvars, thumb visibility/noevents, section-picker sync, and `set_*_content::rect` row-canvas positions for Game Options, System, and Audio. The handler only acts when `desktop::curr` is the matching Settings page, that page is visible, and no Settings popup parent is visible, so transient menu animation state cannot block the visible pane and modal popup transitions do not scroll the pane behind them. The GUI still keeps `applySetSystemScroll`, `applySetAudioScroll`, and `applySetGameScroll` as compatibility stubs, but they only forward to `applySettingsScroll`; they no longer contain row-canvas transition tables.

Settings popups:

Settings-specific popup window definitions are included from `content/baseoq4/pak0/guis/menu/settings/popups.gui`. Their animation windows and shared event wiring remain in `mainmenu.gui`.

| Popup | Widget/action surface | Type | Effect |
|---|---|---|---|
| Load Defaults confirmation | `pop_b_defaults_yes` | `windowDef` action | Runs `resetdefaults`, sets `desktop::loaddefaults 1`, resets crosshair preview state, and closes the popup. |
| Load Defaults confirmation | `pop_b_defaults_no` | `windowDef` action | Closes the defaults popup without applying changes. |
| Auto Detect confirmation | `set_b_system_auto` | `windowDef` action | Opens `anim_pop_autoIn`; the auto-detect popup handles machine-spec reset/application. |

## Controls

The Controls pane is `p_settings_ctrls`, included from `content/baseoq4/pak0/guis/menu/settings/controls.gui`. It has four subpanes selected by action buttons: Movement, Weapons, Attack/Look, and Other. Every row uses `bindDef`.

Category selectors:

| Category | Action widget | Opens | Notes |
|---|---|---|---|
| Movement | `set_b_controls_1` | `p_set_ctrls_move` | Hides the other controls subpanes. |
| Weapons | `set_b_controls_2` | `p_set_ctrls_weap` | Hides the other controls subpanes. |
| Attack/Look | `set_b_controls_3` | `p_set_ctrls_attk` | Hides the other controls subpanes. |
| Other | `set_b_controls_4` | `p_set_ctrls_other` | Hides the other controls subpanes. |

The numbered `set_b_controls_move_1..13` windows are row hover/action surfaces for the visible binding rows.

### Movement

| Label | Widget | Bind command |
|---|---|---|
| Forward | `set_ctrls_move_forward_key` | `_forward` |
| Backpedal | `set_ctrls_move_backpedal_key` | `_back` |
| Move Left | `set_ctrls_move_moveleft_key` | `_moveleft` |
| Move Right | `set_ctrls_move_moveright_key` | `_moveright` |
| Jump | `set_ctrls_move_jump_key` | `_moveUp` |
| Crouch | `set_ctrls_move_crouch_key` | `_moveDown` |
| Turn Left | `set_ctrls_move_turnleft_key` | `_left` |
| Turn Right | `set_ctrls_move_turnright_key` | `_right` |
| Strafe | `set_ctrls_move_strafe_key` | `_strafe` |
| Walk | `set_ctrls_move_walk_key` | `_speed` |

### Weapons

| Label | Widget | Bind command |
|---|---|---|
| Blaster/Gauntlet | `set_ctrls_weap_blastergaunt_key` | `_impulse0` |
| Machinegun | `set_ctrls_weap_mgun_key` | `_impulse1` |
| Shotgun | `set_ctrls_weap_shotgun_key` | `_impulse2` |
| Hyperblaster | `set_ctrls_weap_hblaster_key` | `_impulse3` |
| Grenade Launcher | `set_ctrls_weap_grenade_key` | `_impulse4` |
| Nailgun | `set_ctrls_weap_nailgun_key` | `_impulse5` |
| Rocket Launcher | `set_ctrls_weap_rocket_key` | `_impulse6` |
| Railgun | `set_ctrls_weap_railgun_key` | `_impulse7` |
| Lightning Gun | `set_ctrls_weap_lgun_key` | `_impulse8` |
| Dark Matter Gun | `set_ctrls_weap_dmg_key` | `_impulse9` |
| Napalm Launcher | `set_ctrls_weap_napalmlauncher_key` | `_impulse10` |
| Buy Menu | `set_ctrls_weap_buymenu_key` | `buymenu` |
| Quick Chat | `set_ctrls_weap_quickchat_key` | `_quickChat` |

### Attack/Look

| Label | Widget | Bind command |
|---|---|---|
| Attack | `set_ctrls_attk_attack_key` | `_attack` |
| Previous Weapon | `set_ctrls_attk_prevweap_key` | `_impulse15` |
| Next Weapon | `set_ctrls_attk_nextweap_key` | `_impulse14` |
| Reload | `set_ctrls_attk_reload_key` | `_impulse13` |
| Look Up | `set_ctrls_attk_lookup_key` | `_lookUp` |
| Look Down | `set_ctrls_attk_lookdown_key` | `_lookDown` |
| Mouse Look | `set_ctrls_attk_mouselook_key` | `_mlook` |
| Center View | `set_ctrls_attk_centerview_key` | `centerview` |
| Zoom View | `set_ctrls_attk_zoomview_key` | `_zoom` |
| Objectives | `set_ctrls_attk_objectives_key` | `_impulse19` |
| Flashlight | `set_ctrls_attk_flashlight_key` | `_impulse50` |
| Screenshot | `set_ctrls_attk_screenshot_key` | `screenshot` |
| Weapon Wheel | `set_ctrls_attk_weaponwheel_key` | `_weaponWheel` |

### Other

| Label | Widget | Bind command |
|---|---|---|
| Quick Save | `set_ctrls_other_quicksave_key` | `savegame quick` |
| Quick Load | `set_ctrls_other_quickload_key` | `loadgame quick` |
| Chat | `set_ctrls_other_chat_key` | `clientMessageMode` |
| Team Chat | `set_ctrls_other_teamchat_key` | `clientMessageMode 1` |
| Vote Yes | `set_ctrls_other_voteyes_key` | `voteyes` |
| Vote No | `set_ctrls_other_voteno_key` | `voteno` |
| Stats | `set_ctrls_other_stats_key` | `_ingamestats` |
| Ready | `set_ctrls_other_ready_key` | `ready` |
| Salute | `set_ctrls_other_salute_key` | `emote salute` |
| Cheer | `set_ctrls_other_cheer_key` | `emote cheer` |
| Taunt | `set_ctrls_other_taunt_key` | `emote taunt` |
| Grab | `set_ctrls_other_grab_key` | `emote grab_a` |
| Voice Chat | `set_ctrls_other_vchat_key` | `_voicechat` |

## Game Options

The Game Options pane is `p_settings_game` and is included from `content/baseoq4/pak0/guis/menu/settings/game.gui`. It uses hover/action overlays from `game_hovers.gui`.

### General (Gameplay And View)

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Free Look | `choiceDef` | `set_game_freelook_value` | `in_freeLook` | `No;Yes` | Boolean picker. |
| Auto Reload | `choiceDef` | `set_game_autoreload_value` | `ui_autoReload` | `No;Yes` | Boolean picker. |
| Auto Switch | `choiceDef` | `set_game_autoswitch_value` | `ui_autoSwitch` | `No;Yes` | Boolean picker. |
| Toggle Zoom | `choiceDef` | `set_game_togglezoom_value` | `in_toggleZoom` | `No;Yes` | Boolean picker. |
| Scope Sensitivity | `sliderDef` + `editDef` | `set_game_scopesensitivity_slider_bar`, `set_game_scopesensitivity_value` | `pm_zoomedSlow` | `1..100`, step `1` | Percent of normal mouse, gyro/touchpad mouse-look, and controller look speed while scoped. Fresh configs default to `50`. |
| Show Decals | `choiceDef` | `set_game_showdecals_value` | `g_decals` | `No;Yes` | Boolean picker. |
| Show Gun | `choiceDef` | `set_game_showgun_value` | `ui_showGun` | `No;Yes` | Boolean picker. |
| Gun Position | `choiceDef` | `set_game_gunXYZ_value` | GUI state `g_gunXYZ` via `applyGunPositionChoice` | `0 Right`, `1 Centered`, `2 Lower Right` | The C++ menu adapter writes the preset `g_gunX`, `g_gunY`, and `g_gunZ` values, and enables `g_weaponFovEffect` when that cvar exists. |
| Pickup Style | `choiceDef` | `set_game_simpleitems_value` | `g_simpleItems` | `0 Original`, `1 Simple Icons`, `2 Flat Color`, `3 Flat + Light Sweep` | Client-side pickup presentation selector. The light sweep applies only to world items. |
| Opponent Weapon Style | `choiceDef` | `set_game_opponentweaponstyle_value` | `g_mpFlatOpponentWeapons` | `0 Original`, `1 Flat Color` | Client-side multiplayer option for weapons held by opponents. View weapons are never affected. |
| Force Model | `choiceDef` | `set_game_forcemodel_value` | GUI state `ui_proskins` via `applyForceModelChoice` | `0 No`, `1 Yes` | The C++ menu adapter writes the pro-skin force-model cvars when enabled and clears `g_forceModel`, `g_forceMarineModel`, and `g_forceStroggModel` when disabled. |
| Auto Skip Cinematics | `choiceDef` | `set_game_autoskipcinematics_value` | `g_autoSkipCinematics` | `No;Yes` | Archived SP/MP gameplay cvar. Affects future cinematics. |
| Cinematic Shake | `choiceDef` | `set_game_cinematicshake_value` | `ui_cinematicShake` | `Off;Low;Full` (`0;0.5;1`) | Scales scripted cinematic camera shake. |

### Mouse

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Mouse Input | `windowDef` label | `set_game_mouse` | N/A | N/A | Section label. |
| Invert Mouse | `choiceDef` | `set_game_mpitch_value` | `m_pitch` | `0.022`, `-0.022` | Displayed as `No;Yes`; selecting Yes writes the negative pitch. |
| Mouse Smoothing | `sliderDef` + `editDef` | `set_game_msmooth_slider_bar`, `set_game_msmooth_value` | `m_smooth` | `1..8`, step `1` | Edit field `maxchars 1`. |
| Mouse Sensitivity | `sliderDef` + `editDef` | `set_game_msensitivity_slider_bar`, `set_game_msensitivity_value` | `sensitivity` | `0.01..30`, step `0.01` | Edit field `maxchars 5`. With `m_cpi` enabled, this is degrees per centimeter. |
| Mouse CPI | `sliderDef` + `editDef` | `set_game_mcpi_slider_bar`, `set_game_mcpi_value` | `m_cpi` | `0..32000`, step `50` | `0` preserves classic raw-count sensitivity; nonzero enables CPI-normalized mouse input. |
| View Filter | `sliderDef` + `editDef` | `set_game_mfilter_slider_bar`, `set_game_mfilter_value` | `m_filter` | `0..31`, step `1` | Optional view-angle history filter. |
| Mouse Accel | `sliderDef` + `editDef` | `set_game_maccel_slider_bar`, `set_game_maccel_value` | `cl_mouseAccel` | `-5..5`, step `0.01` | Signed QuakeLive-style acceleration. |
| Accel Offset | `sliderDef` + `editDef` | `set_game_macceloffset_slider_bar`, `set_game_macceloffset_value` | `cl_mouseAccelOffset` | `0..50`, step `0.1` | Movement-rate threshold before acceleration starts. |
| Accel Curve | `sliderDef` + `editDef` | `set_game_maccelpower_slider_bar`, `set_game_maccelpower_value` | `cl_mouseAccelPower` | `1..8`, step `0.05` | Acceleration curve exponent. |
| Sensitivity Cap | `sliderDef` + `editDef` | `set_game_msenscap_slider_bar`, `set_game_msenscap_value` | `cl_mouseSensCap` | `0..100`, step `0.1` | `0` disables the accelerated-sensitivity cap. |

### Controller

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Controller Input | `choiceDef` | `set_game_controller_enabled_value` | `in_joystick` | `No;Yes` | Master controller input toggle. |
| Stick Layout | `choiceDef` | `set_game_controller_layout_value` | `in_joystickSouthpaw` | `0 Default`, `1 Southpaw` | Swaps movement and look sticks. |
| Stick Dead Zone | `sliderDef` + `editDef` | `set_game_controller_deadzone_slider_bar`, `set_game_controller_deadzone_value` | `in_joystickDeadZone` | `0..0.95`, step `0.01` | Edit field `maxchars 4`. |
| Look Sensitivity | `sliderDef` + `editDef` | `set_game_controller_look_sensitivity_slider_bar`, `set_game_controller_look_sensitivity_value` | `in_joystickLookSensitivity` | `0.1..4.0`, step `0.05` | Scales analog look speed. Edit field `maxchars 4`. |
| Wheel Sensitivity | `sliderDef` + `editDef` | `set_game_controller_wheel_sensitivity_slider_bar`, `set_game_controller_wheel_sensitivity_value` | `in_weaponWheelSensitivity` | `0.25..16.0`, step `0.25` | Default `4.0`; SP wheel movement is independent of aiming, zoom, and run modifiers. |
| Look Curve | `sliderDef` + `editDef` | `set_game_controller_look_curve_slider_bar`, `set_game_controller_look_curve_value` | `in_joystickLookCurve` | `1.0..3.0`, step `0.05` | `1.0` is linear; higher values soften small stick movement. |
| Invert Look | `choiceDef` | `set_game_controller_invert_value` | `in_joystickInvertLook` | `No;Yes` | Boolean picker. |
| Trigger Threshold | `sliderDef` + `editDef` | `set_game_controller_trigger_slider_bar`, `set_game_controller_trigger_value` | `in_joystickTriggerThreshold` | `0..1.0`, step `0.01` | Analog trigger press threshold. Edit field `maxchars 4`. |
| Rumble | `choiceDef` | `set_game_controller_rumble_value` | `in_joystickRumble` | `No;Yes` | Boolean picker. |
| Rumble Strength | `sliderDef` + `editDef` | `set_game_controller_rumble_strength_slider_bar`, `set_game_controller_rumble_strength_value` | `in_joystickRumbleScale` | `0..2.0`, step `0.05` | `0` effectively mutes rumble. Edit field `maxchars 4`. |
| Prompt Style | `choiceDef` | `set_game_controller_prompt_style_value` | `in_promptStyle` | `0 Automatic`, `1 Xbox`, `2 PlayStation`, `3 Nintendo`, `4 Steam Deck`, `5 Generic` | Controller prompt family for the menus and the HUD; after Low Battery Rumble Cap. |
| Swap Confirm and Back | `choiceDef` | `set_game_controller_swap_confirm_value` | `in_swapConfirmBack` | `0 Automatic`, `1 Off`, `2 On` | Which face button confirms in menus; the prompts follow. |

### Crosshair

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Custom Crosshair | `choiceDef` | `set_game_customxhair_value` | `g_crosshairCustom` | `Weapon Default;Custom` | Enables or disables the custom preview/action row. |
| Crosshair Preview | `windowDef` action | `set_game_previewxhair` and preview size windows | `chooseCrosshair` command, `g_crosshairCustomFile` | Click steps to the next of the twenty custom crosshairs (`chooseCrosshair 1`), right-click to the previous (`-1`), wrapping at both ends | Disabled when `g_crosshairCustom` is off. Each menu build shows the current crosshair; a value outside the set becomes the first. |
| Crosshair Size | `choiceDef` | `set_game_xhairsize_value` | `g_crosshairSize` | `16 Small`, `24 Medium`, `32 Default`, `40 Large`, `48 Extra Large` | Paired with preview update events. |
| Crosshair Color | `choiceDef` | `set_game_xhaircolor_value` | GUI state `g_crosshairColorChoice` | White, red, orange, yellow, green, cyan, blue, magenta | Focusable picker. Selection writes `g_crosshairColor` RGBA values and updates the preview. The in-game crosshair takes the new colour at once. |
| Hit Marker | `choiceDef` | `set_game_hitmarker_value` | `hud_hitMarker` | `No;Yes` | Boolean picker, off by default in both game modes to retain stock hit feedback. Existing archived choices are preserved. Off restores stock Quake 4's crosshair recolour on a hit; `hud_hitMarkerScale` and `hud_crosshairHitFlash` stay console-only. |

### HUD

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| HUD | `windowDef` label | `set_game_hud` | N/A | N/A | Section label below Crosshair. |
| HUD Layout | `choiceDef` | `set_game_hudlayout_value` | `ui_hudLayout` | `0 Classic`, `1 Remastered`, `2 Minimal`, `3 Competitive` | Classic is the stock HUD; Remastered anchors it to the screen edges; Minimal keeps health, armor, ammunition and the crosshair; Competitive adds multiplayer's match bar, team status and network quality and enlarges the readouts (Remastered elsewhere). See `docs/dev/ui/remastered-hud.md`. |
| HUD Size | `sliderDef` + `editDef` | `set_game_hudscale_slider_bar`, `set_game_hudscale_value` | `ui_hudScale` | `0.5..2`, step `0.05` | Independent of menu size; narrow screens limit the status bar to keep every gauge visible. |
| HUD Width | `choiceDef` | `set_game_hudwidth_value` | `ui_hudWidth` | `0 16:9`, `1 21:9`, `2 Full` | How far edge HUD groups spread on wide screens. |
| Safe Area Inset | `sliderDef` + `editDef` | `set_game_hudinset_slider_bar`, `set_game_hudinset_value` | `ui_hudSafeInset` | `0..10` percent, step `0.5` | Moves the HUD inward from every edge. |
| Toggle Objectives | `choiceDef` | `set_game_objectivestoggle_value` | `g_objectivesToggle` | `0 No`, `1 Yes` | No shows the objectives display while the scores button is held (stock); Yes opens and closes it with each press, as the Xbox 360 edition did, and the Remastered display adds a Close prompt. |
| HUD Opacity | `sliderDef` + `editDef` | `set_game_hudopacity_slider_bar`, `set_game_hudopacity_value` | `ui_hudOpacity` | `0.25..1`, step `0.05` | Fades the Remastered and Minimal HUD; Classic keeps the stock HUD. Edit field `maxchars 4`. |
| Damage Direction | `choiceDef` | `set_game_huddamagedir_value` | `ui_hudDamageDirection` | `0 No`, `1 Yes` | No hides the damage direction indicator in the Remastered and Minimal HUD. |
| Pickup Messages | `choiceDef` | `set_game_hudpickups_value` | `ui_hudPickups` | `0 No`, `1 Yes` | No hides the pickup messages in the Remastered and Minimal HUD. |
| Weapon Strip | `choiceDef` | `set_game_hudstrip_value` | `ui_hudWeaponStrip` | `0 No`, `1 Yes` | No keeps the weapon strip hidden on a weapon change in the Remastered and Minimal HUD. |
| Reduced Flashing | `choiceDef` | `set_game_hudflash_value` | `ui_reducedFlashing` | `0 No`, `1 Yes` | Yes caps the damage direction indicator and the vehicle hit glow at 0.40, dims full-screen white flashes (teleports, spawns) and drops the death afterimage, in every HUD layout. |
| Opaque Menu Backing | `choiceDef` | `set_game_opaquebacking_value` | `ui_retainedOpaqueBacking` | `0 No`, `1 Yes` | Yes covers the screen behind the retained menus' dialogs and pause with the stock 0.94 black scrim instead of the softened (blurred) view. |

### Corpse, Language, Console

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Corpse Sink | `choiceDef` | `set_game_corpsesink_value` | `g_corpseSink` | `0 Off`, `1 Ragdoll`, `2 No Ragdoll` | Changes corpse sink behavior. |
| Corpse Time | `choiceDef` | `set_game_corpsetime_value` | GUI state `corpse_time_choice` via `applyCorpseTimeChoice` | `Stock`, `Never`, `5 sec`, `10 sec`, `15 sec`, `30 sec`, `60 sec`, `Custom` | The C++ menu adapter writes `g_corpseRemoveDelaySP` or `g_corpseRemoveDelayMP` depending on current mode. Custom is selected when current cvar does not match a listed value and remains a no-op when chosen. |
| Language | `choiceDef` | `set_game_language_value` | `sys_lang` | `english`, `spanish`, `french`, `italian` | Displayed as English, Spanish, French, Italian. |
| Console Access | `choiceDef` | `set_game_consoleaccess_value` | `con_allowConsole` | `0 Ctrl+Alt+Tilde`, `1 Tilde` | Controls shortcut required to open the console. |

### View Weapon

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| View Weapon | `windowDef` label | `set_game_viewweapon` | N/A | N/A | Section label. |
| Weapon FOV | `sliderDef` + `editDef` | `set_game_cl_gunfov_slider_bar`, `set_game_cl_gunfov_value` | `cl_gunfov` | `0..179`, step `1` | `0` follows the current gameplay view FOV. |
| Weapon FOV Aspect | `choiceDef` | `set_game_cl_gunfov_adjust_value` | `cl_gunfov_adjust` | `0 Direct`, `1 Classic` | Classic keeps 4:3-style weapon framing across screen ratios. |
| Weapon X Offset | `sliderDef` + `editDef` | `set_game_cl_gun_x_slider_bar`, `set_game_cl_gun_x_value` | `cl_gun_x` | `-5..5`, step `0.05` | Additive client-side right offset. |
| Weapon Y Offset | `sliderDef` + `editDef` | `set_game_cl_gun_y_slider_bar`, `set_game_cl_gun_y_value` | `cl_gun_y` | `-5..5`, step `0.05` | Additive client-side forward offset. |
| Weapon Z Offset | `sliderDef` + `editDef` | `set_game_cl_gun_z_slider_bar`, `set_game_cl_gun_z_value` | `cl_gun_z` | `-5..5`, step `0.05` | Additive client-side up offset. |
| Weapon Kick | `choiceDef` | `set_game_weaponkick_value` | `g_weaponMuzzleKick` | `No;Yes` | View model kicks back and tilts when the weapon fires. Presentation only: it never moves aim or the shot origin. Multiplayer weapon defs zero the stock kick values, so the game restores each weapon's single-player kick when this is on. |
| Virtual Reality | `windowDef` label | `set_game_vr` | N/A | N/A | Section label. |
| VR Mode | `choiceDef` | `set_game_vr_enable_value` | `vr_enable` | `Disabled;Enabled` | Starts or ends the OpenXR session at once (OpenGL renderer). |
| Aim With | `choiceDef` | `set_game_vr_aimmode_value` | `vr_aimMode` | `1 Controller`, `0 Head` | Controller aim hides the screen crosshair; the laser sight marks shots. |
| Weapon Hand | `choiceDef` | `set_game_vr_lefthanded_value` | `vr_leftHanded` | `0 Right`, `1 Left` | Swaps the triggers, grips and sticks with the weapon. |
| Turning | `choiceDef` | `set_game_vr_turnmode_value` | `vr_turnMode` | `0 Snap`, `1 Smooth` | Weapon-hand stick left and right. |
| Snap Turn Angle | `choiceDef` | `set_game_vr_snapturnangle_value` | `vr_snapTurnAngle` | `30;45;60;90` degrees | Degrees per snap turn. |
| Walk Direction | `choiceDef` | `set_game_vr_movedirection_value` | `vr_moveDirection` | `0 Head`, `1 Off Hand` | What the off-hand stick's forward follows. |
| Room Scale | `choiceDef` | `set_game_vr_roomscale_value` | `vr_roomScale` | `Disabled;Enabled` | The character walks after the player's steps; in multiplayer through its movement input. |
| Laser Sight | `choiceDef` | `set_game_vr_aimlaser_value` | `vr_aimLaser` | `0 Off`, `1 Dot`, `2 Dot and Beam` | Marks where controller-aimed shots land, at the target's depth. |
| Vibration | `choiceDef` | `set_game_vr_hapticstrength_value` | `vr_hapticStrength` | `0 Off`, `0.5 Low`, `1 Full` | Controller pulses on each shot and hit. |
| Two-Handed Grip | `choiceDef` | `set_game_vr_twohanded_value` | `vr_twoHanded` | `Disabled;Enabled` | The off-hand grip on the gun's foregrip holds it in both hands; elsewhere it opens the weapon wheel. |
| Comfort Vignette | `choiceDef` | `set_game_vr_vignette_value` | `vr_comfortVignette` | `0 Off`, `0.5 Low`, `1 Full` | Darkens the edges of each eye while the stick moves or smoothly turns the player. |
| Physical Crouch | `choiceDef` | `set_game_vr_physicalcrouch_value` | `vr_physicalCrouch` | `Disabled;Enabled` | Crouching in the room crouches the character. |
| Play Space | `choiceDef` | `set_game_vr_playspace_value` | `vr_playSpace` | `0 Automatic`, `1 Seated`, `2 Standing` | Seated play measures height from where the player recentred; standing play from the floor, against the standing eye height (`vr_standingHeight`, learned at the first recentre or measured by `vr_calibrateHeight`). Automatic stands when the headset knows the floor. |

## System

The System pane is `p_settings_sys`, included from `content/baseoq4/pak0/guis/menu/settings/system.gui`. Dynamic resolution and display-device lists are populated by `Session_menu.cpp` when the main menu opens.

The pane has two display layouts:

- Full layout when more than one display is detected. Shows Display Device and Multi-Screen.
- Compact layout when only one display is detected. Hides Display Device and Multi-Screen and repositions the later rows.

### Video Basics

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Auto Detect | `windowDef` action | `set_b_system_auto` | Auto-detect popup | N/A | Opens the existing auto-detect confirmation flow. |
| Performance Preset | `choiceDef` | `set_sys_perf_preset_val` | `com_performancePreset` via `applyPerformancePreset` | `minimum`, `lowpower`, `low`, `performance`, `balanced`, `enhanced`, `quality`, `ultra`, `maximum` | Applies a coordinated profile for machine spec, resolution scale, AA, frame cap, effective anisotropic filtering and texture pressure, renderer budgets, light grids, and audio cost. The engine verifies registered target writes and rolls the profile back atomically if a value is rejected; the session-menu bridge then refreshes video, quality, and aspect widgets. Keeps optional shadow maps and post effects off as separate opt-in settings. |
| Performance Auto-Detect | `windowDef` action | `set_sys_perf_autodetect_button` | `autoDetectPerformancePreset` | N/A | Chooses a conservative supported preset from platform, CPU, memory, VRAM, and renderer capability signals, ignores missing or implausible memory telemetry, forwards stray arguments through the same command rejection path, then refreshes the System page widgets. |
| Display Resolution | `choiceDef` | `set_sys_screensize_val_0` | GUI state `display_mode_choice` via `applyDisplayModeChoice` | Runtime `gui::display_mode_choices` / `gui::display_mode_values` | Visible compatibility picker. Includes Desktop Native, SDL3 fullscreen preset resolutions from the selected display, then Custom; static presets are not shown unless the display reports them. The adapter writes `r_mode -2`, a stable legacy `r_mode` ID for catalog matches, or `r_mode -1` plus exact `r_customWidth`/`r_customHeight` for detected modes outside the catalog. |
| Display Resolution | `choiceDef` | `set_sys_screensize_val_1` | GUI state `display_mode_choice` via `applyDisplayModeChoice` | Runtime `gui::display_mode_choices` / `gui::display_mode_values` | Compatibility duplicate retained for the old aspect-bucketed menu events. It uses the same dynamic list and adapter as `set_sys_screensize_val_0`. |
| Display Resolution | `choiceDef` | `set_sys_screensize_val_2` | GUI state `display_mode_choice` via `applyDisplayModeChoice` | Runtime `gui::display_mode_choices` / `gui::display_mode_values` | Compatibility duplicate retained for the old aspect-bucketed menu events. It uses the same dynamic list and adapter as `set_sys_screensize_val_0`. |
| Fullscreen | `choiceDef` | `set_sys_fullscreen_val` | `r_fullscreen` | `No;Yes` | Marks `desktop::vidwarn`. |
| Gamma / Brightness | `sliderDef` + `editDef` | `set_sys_gamma_slider`, `set_sys_gamma_value` | `r_brightness` | `0.5..2.0`, step `0.1` | Numeric edit field `maxchars 3`, allowing values such as `0.5` and `2.0`. |

### Display And Advanced Rendering

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Display Device | `choiceDef` | `set_sys_display_device_val` | `r_screen` | Runtime `gui::display_names` / `gui::display_values` | Only visible when `gui::display_count > 1`. |
| Multi-Screen | `choiceDef` | `set_sys_multiscreen_val` | `r_multiScreen` | `0 Primary Display Only`, `1 Span All Displays` | Only visible when more than one display is detected. |
| Resolution Scale | `choiceDef` | `set_sys_supersample_val` | `r_screenFraction` | `10`, `25`, `50`, `75`, `85`, `100`, `125`, `150`, `200` | Values below `100%` reduce scene resolution for performance; values above `100%` render the root scene into a larger single-sample offscreen target and resolve back to the native back buffer. |
| Anti-Aliasing | `choiceDef` | `set_sys_msaa_val` | `r_multisamples` | `0 Off`, `2 2x`, `4 4x`, `8 8x`, `16 16x` | GUI cvar spelling is lower-case `r_multisamples`; the engine cvar is `r_multiSamples` and cvar lookup is case-insensitive. |
| Post AA | `choiceDef` | `set_sys_postaa_val` | `r_postAA` | `0 Off`, `1 SMAA Medium`, `2 SMAA High`, `3 SMAA Ultra`, `4 Color Edge` | Runtime post-process AA option. |
| VSync | `choiceDef` | `set_sys_vsync_val` | `r_swapInterval` | `No;Yes` | Boolean picker. |
| Borderless | `choiceDef` | `set_sys_borderless_val` | `r_borderless` | `No;Yes` | Boolean picker. |
| Fullscreen Policy | `choiceDef` | `set_sys_fullscreen_policy_val` | `r_fullscreenDesktop` | `1 Desktop`, `0 Exclusive` | Desktop/native fullscreen is first in the displayed order. |

### Quality

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Shadows | `choiceDef` | `set_sys_shadows_val` | `r_shadows` | `No;Yes` | Boolean picker. |
| Specular | `choiceDef` | `set_sys_specular_val` | GUI state `r_specularEnabled` | `No;Yes` | Positive adapter. Selecting Yes writes `r_skipSpecular 0`; selecting No writes `r_skipSpecular 1`. |
| Bump Mapping | `choiceDef` | `set_sys_bump_val` | GUI state `r_bumpEnabled` | `No;Yes` | Positive adapter. Selecting Yes writes `r_skipBump 0`; selecting No writes `r_skipBump 1`. |
| Sky | `choiceDef` | `set_sys_sky_val` | GUI state `r_skyEnabled` | `No;Yes` | Positive adapter. Selecting Yes writes `r_skipSky 0`; selecting No writes `r_skipSky 1`. |
| Ambient Light | `choiceDef` | `set_sys_ambient_val` | GUI state `r_forceAmbientOn` | `No;Yes` | When enabled, sets `r_forceAmbient 0.7`; when disabled, sets `r_forceAmbient 0`. |
| Ambient Brightness | `sliderDef` + `editDef` | `set_sys_ambientbr_val`, `set_sys_ambientbr_valnum` | `r_forceAmbient` | `0.0..1.0`, step `0.025` | Disabled/greyed when Ambient Light is off. Edit field `maxchars 5`. |

### Post Effects

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Renderer Fallback | `choiceDef` | `set_sys_vidqual_val` | `r_renderer` | `best;arb2` | Displays `Auto;ARB2`. This is the compatibility rollback selector, not the historical multi-backend picker; legacy console/config values still fall back in engine code. Marks `desktop::vidwarn`. |
| Preload Light Grids | `choiceDef` | `set_sys_preload_val` | `r_lightGridPreload` | `No;Yes` | Preloads baked lighting on the next map load. Help explains smoother traversal, longer loads, and higher VRAM use. |
| Irradiance Volumes | `choiceDef` | `set_sys_irradiance_val` | `r_useLightGrid` | `No;Yes` | Enables baked light-grid indirect diffuse when available. |
| Bloom | `choiceDef` | `set_sys_bloom_val` | `r_bloom` | `No;Yes` | Boolean picker. |
| SSAO | `choiceDef` | `set_sys_ssao_val` | `r_ssao` | `No;Yes` | Boolean picker. |
| Tone Mapping | `choiceDef` | `set_sys_tonemap_val` | `r_hdrToneMap` | `No;Yes` | Boolean picker. |
| CRT Filter | `choiceDef` | `set_sys_crt_val` | `r_crt` | `No;Yes` | Boolean picker. |

### Display Sizing

| Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|
| Display Sizing | `windowDef` label | `set_sys_display_tuning` | N/A | N/A | Section label below Post Effects. |
| UI Aspect | `choiceDef` | `set_sys_ui_aspect_val` | `ui_aspectCorrection` | `No;Yes` | `Yes` keeps classic 4:3-style UI correction. |
| Refresh Rate | `choiceDef` | `set_sys_refresh_val` | `r_displayRefresh` | Runtime `gui::display_refresh_choices` / `gui::display_refresh_values` | Lists Auto plus SDL3-reported refresh rates for the currently selected fullscreen resolution. Marks `desktop::vidwarn`. |
| Window Width | `editDef` | `set_sys_window_width_val` | `r_windowWidth` | Numeric field, `maxchars 5` | Windowed width. Marks `desktop::vidwarn`. |
| Window Height | `editDef` | `set_sys_window_height_val` | `r_windowHeight` | Numeric field, `maxchars 5` | Windowed height. Marks `desktop::vidwarn`. |
| Custom FS Width | `editDef` | `set_sys_custom_width_val` | `r_customWidth` | Numeric field, `maxchars 5` | Exclusive custom fullscreen width. Entering a value runs `applyCustomDisplaySize`, selecting `r_mode -1`. Marks `desktop::vidwarn`. |
| Custom FS Height | `editDef` | `set_sys_custom_height_val` | `r_customHeight` | Numeric field, `maxchars 5` | Exclusive custom fullscreen height. Entering a value runs `applyCustomDisplaySize`, selecting `r_mode -1`. Marks `desktop::vidwarn`. |

## Audio

The Audio pane is `p_settings_audio`, included from `content/baseoq4/pak0/guis/menu/settings/audio.gui`. It is a single-page layout grouped as Levels, Devices, and Effects.

| Section | Label | Widget type | Value widget | Target | Values or range | Notes |
|---|---|---|---|---|---|---|
| Levels | Sound Volume | `sliderDef` + `editDef` | `set_audio_vol_slider`, `set_audio_vol_value` | `s_volume` | `0..1`, step `0.1` | Numeric edit field `maxchars 3`, matching the engine cvar range. |
| Levels | Music Volume | `sliderDef` + `editDef` | `set_audio_music_slider`, `set_audio_music_value` | `s_musicVolume` | `0..1`, step `0.05` | Numeric edit field `maxchars 4` for exact music-volume entry. |
| Devices | Sound System | `choiceDef` | `set_audio_backend_val` | `s_useOpenAL` | `0 Default`, `1 OpenAL` | Runs `sound drivar` on release. |
| Devices | Sound Device | `choiceDef` | `set_audio_device_val` | `s_deviceName` | Runtime `gui::device_name` / `gui::device_value` | Disabled when OpenAL is off or failed to load. Runs `sound drivar` on release. |
| Effects | EAX Reverb | `choiceDef` | `set_audio_eax_val` | `s_useEAXReverb` | `No;Yes` | Disabled when OpenAL is off or failed to load. Runs `sound eax` on release. |
| Effects | Surround Speakers | `choiceDef` | `set_audio_speakers_val` | `s_numberOfSpeakers` | `0 Auto`, `2 Off`, `6 On` | Displayed through `Auto;Off;On` (`#str_229980`, shared with Headphone HRTF). `Auto` (default) sends no output-mode request, so OpenAL Soft uses the device's own layout and its automatic HRTF; `Off`/`On` request stereo or 5.1 through `ALC_SOFT_output_mode` when available. Runs `sound speakers` on release. |
| Effects | Headphone HRTF | `choiceDef` | `set_audio_hrtf_val` | `s_openALHRTF` | `0 Auto`, `1 Off`, `2 On` | Requests OpenAL Soft HRTF through `ALC_SOFT_HRTF` when available. Runs `sound drivar` on release. |

## Dynamic Lists

`Session_menu.cpp` fills several GUI state lists at menu-open time:

| GUI state | Used by | Source behavior |
|---|---|---|
| `display_mode_choices`, `display_mode_values` | System Display Resolution | Built from Desktop Native, SDL3 fullscreen resolutions for the selected display, and Custom. The visible list does not fall back to static presets that the display did not report. |
| `display_mode_choice` | System Display Resolution | GUI-state index consumed by `applyDisplayModeChoice`; the adapter writes `r_mode`, `r_customWidth`, and `r_customHeight`. |
| `display_refresh_choices`, `display_refresh_values` | System Refresh Rate | Built from Auto plus the selected display's SDL3 fullscreen mode refresh rates for the currently selected resolution. |
| `4_3_choices`, `4_3_values` | System Display Resolution compatibility aliases | Kept in sync with `display_mode_choices` / `display_mode_values` for old GUI visibility events. |
| `16_9_choices`, `16_9_values` | System Display Resolution compatibility aliases | Kept in sync with `display_mode_choices` / `display_mode_values` for old GUI visibility events. |
| `16_10_choices`, `16_10_values` | System Display Resolution compatibility aliases | Kept in sync with `display_mode_choices` / `display_mode_values` for old GUI visibility events. |
| `display_names`, `display_values`, `display_count` | System Display Device / Multi-Screen | Built from SDL display enumeration. Multi-display rows are hidden when `display_count <= 1`. |
| `device_name`, `device_value` | Audio Sound Device | Built from the available audio device list. |

## Hidden Or Legacy Definitions

The GUI file still contains several dormant settings definitions that are not reachable from the current top-level Settings flow:

| Area | Widgets | Status |
|---|---|---|
| System Advanced popup | `pop_setAdv_*` widgets | Popup and rows exist, but the current `showSetSystem` event keeps `set_b_system_adv` hidden. Most options are now represented directly in the System scroll pane. |
| Sound Advanced popup | `pop_set_sndAdv_*` widgets | Popup exists and duplicates Sound System, Sound Device, EAX, and Surround Speakers. The current Audio pane exposes those rows directly. |
| System-embedded audio sliders | `set_sys_vol_slider`, `set_sys_vol2_slider`, `set_sys_vol3_slider` | Removed in the dead-code audit. The extracted Audio pane owns Sound Volume and Music Volume. |
| Video Restart button | `set_b_vidrestart` | Hidden by default, but still has a live action block and is shown by selected video-warning paths. Back handles the warning path when the button is not shown. |
