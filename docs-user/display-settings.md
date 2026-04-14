# Display Settings and Multi-Screen Guide

This guide covers OpenQ4 display/window settings for end users, including multi-monitor behavior and modern fullscreen/window handling.

## Quick Start

- Press `Alt+Enter` to toggle fullscreen/windowed mode (fast path uses `vid_restart partial`).
- Run `listDisplays` in the console to list monitor indices for `r_screen`.
- On SDL3 builds, run `listDisplayModes [displayIndex]` to list available exclusive fullscreen modes.
- After changing video cvars, run `vid_restart` (or `vid_restart partial` for quick window/fullscreen transitions).

## Core Display Settings

| Setting | Default | What it does |
|---|---:|---|
| `r_fullscreen` | `1` | `1` = fullscreen, `0` = windowed. |
| `r_fullscreenDesktop` | `1` | `1` = native desktop fullscreen (recommended). `0` = exclusive fullscreen using `r_mode`/`r_custom*`. |
| `r_borderless` | `0` | Borderless window mode when `r_fullscreen 0`. |
| `r_windowWidth` | `1280` | Windowed width. |
| `r_windowHeight` | `720` | Windowed height. |
| `win_xpos` | (auto) | Window X position (updated automatically when you move the window). |
| `win_ypos` | (auto) | Window Y position (updated automatically when you move the window). |
| `r_mode` | `3` | Preset mode index. Use `-1` for custom width/height. |
| `r_customWidth` | `720` | Custom width used when `r_mode -1`. |
| `r_customHeight` | `486` | Custom height used when `r_mode -1`. |
| `r_displayRefresh` | `0` | Requested fullscreen refresh rate (0 = default/driver choice). |
| `r_screen` | `-1` | SDL3 monitor target (`-1` auto/current, `0..N` explicit index). |

## Anti-Aliasing Settings (New)

| Setting | Default | What it does |
|---|---:|---|
| `r_multiSamples` | `0` | MSAA sample count for the main scene render target (`0`, `2`, `4`, `8`, `16`; `0` = off). |
| `r_postAA` | `0` | Post AA mode (`0` = off, `1` = official SMAA 1x using the medium preset). |
| `r_msaaAlphaToCoverage` | `1` | Enables alpha-to-coverage for perforated/alpha-tested materials when MSAA is active. Helps foliage/fences look cleaner. |
| `r_msaaResolveDepth` | `0` | Also resolves depth during MSAA resolve. Usually leave this off unless debugging a depth-dependent edge case. |

`r_multiSamples` value guide:
- `0`: disabled (fastest, most aliasing).
- `2`: low-cost MSAA uplift for modest GPUs.
- `4`: recommended default quality/performance balance.
- `8`: high quality, noticeably higher GPU cost.
- `16`: enthusiast/high-end setting where supported.
- `1` usually provides no meaningful benefit and is not recommended.

Notes:
- `r_multiSamples` is hardware-limited and may be clamped by the driver/GPU.
- Changing `r_multiSamples` should be followed by `vid_restart`.
- `r_postAA`, `r_msaaAlphaToCoverage`, and `r_msaaResolveDepth` can be changed at runtime, but a `vid_restart` is still safe if behavior looks stale.

## Fullscreen Policy (Desktop vs Exclusive)

- Default behavior is **desktop-native fullscreen** (`r_fullscreenDesktop 1`): fullscreen matches your current desktop resolution and does not change Windows display mode.
- For **exclusive fullscreen** (explicit mode switch), set `r_fullscreenDesktop 0`. In this mode, `r_mode`/`r_customWidth`/`r_customHeight` control the requested fullscreen resolution.
- On Windows, fullscreen windows minimize on focus loss so system UI such as Alt+Tab and the Snipping Tool overlay can take foreground cleanly.

Notes:
- When `r_fullscreenDesktop 1`, `r_mode` and `r_custom*` are ignored for fullscreen sizing (they still exist for legacy configs and exclusive mode).
- Use `listDisplayModes` to see what your monitor actually supports in exclusive mode.

## Windowed Sizing and Placement

- When windowed (`r_fullscreen 0`, `r_borderless 0`), resizing updates `r_windowWidth`/`r_windowHeight` automatically.
- Moving the window updates `win_xpos`/`win_ypos` automatically.
- When switching fullscreen -> windowed, OpenQ4 restores the last remembered windowed size/position (it should not come back as a fullscreen-sized window).
- If you unplug/rearrange monitors and the saved window position becomes off-screen, OpenQ4 will recover by clamping/recentering the window back onto a valid display.
- If you set `r_screen` to an explicit display index (`0..N`), window placement is constrained to that display's usable area. With `r_screen -1`, placement is respected unless it becomes invalid/off-screen.
- SDL3 tip: hold `Shift` while resizing to snap the window aspect ratio to common targets (4:3, 16:9, 16:10, 21:9, etc.).

## Aspect Ratio and FOV

- `r_aspectRatio` is **deprecated/ignored**. Aspect ratio and FOV behavior are derived automatically from the current render size, so the game follows any aspect ratio without manual selection.
- Weapon gameplay zoom uses the same gameplay FOV conversion path as normal view FOV, so authored weapon zoom values keep consistent framing/magnification across aspect ratios.
- In multiplayer, zoomed first-person view suppresses view bob while scoped so reticle tracking stays stable during movement.
- Scope GUI yaw tracking for zoom overlays follows the weapon/player view axis path, improving scope alignment while turning.

## View Weapon FOV and Placement (New)

These settings control first-person viewmodel rendering (the weapon on screen). They are client-side tuning controls and are not gameplay/network authority cvars.

| Setting | Default | What it does |
|---|---:|---|
| `cl_gunfov` | `0` | View-weapon FOV override (`0` = follow current view FOV). |
| `cl_gunfov_adjust` | `1` | Aspect policy for `cl_gunfov`: `1` keeps classic 4:3-style weapon framing across screen ratios, `0` uses direct viewport-based FOV conversion. |
| `cl_gun_x` | `0` | Client weapon X/right offset. |
| `cl_gun_y` | `0` | Client weapon Y/forward offset. |
| `cl_gun_z` | `0` | Client weapon Z/up offset. |

Notes:
- `cl_gunfov` values above `0` are clamped to a safe range internally for weapon projection.
- Weapon projection is handled in renderer weapon-depth path, so narrow/wide aspect changes are handled consistently.
- `cl_gun_x/y/z` are additive with legacy `g_gunX/Y/Z` offsets. Prefer `cl_gun_*` for user config.
- OpenQ4's legacy baseline keeps `g_gunX` at `1` and `g_gunZ` at `-1` so the default widescreen viewmodel framing stays out of the viewport edge.

## UI Aspect Correction (New)

This controls 2D UI layout behavior (menu, HUD, console, loading/initializing screens):

| Setting | Default | What it does |
|---|---:|---|
| `ui_aspectCorrection` | `1` | `1` keeps classic 4:3-style correction for all 2D UI. `0` stretches 2D UI to the full 2D draw region. |

## Multi-Monitor Behavior (New)

When the render surface spans multiple monitors:

- 2D elements (console, HUD, menus, loading/initializing UI) are constrained to the **primary display region**.
- 2D aspect behavior inside that region is controlled by `ui_aspectCorrection`.
- Menu cursor mapping follows the same 2D region so mouse interaction stays aligned.

3D world rendering is unchanged by these UI cvars.

## Useful Console Examples

### Recommended Modern Defaults

```cfg
seta r_screen -1
seta r_fullscreenDesktop 1
seta r_fullscreen 1
seta r_multiSamples 4
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
seta ui_aspectCorrection 1
vid_restart
```

### Borderless Window on a Specific Monitor

```cfg
seta r_fullscreen 0
seta r_borderless 1
seta r_screen 1
vid_restart
```

### Custom Fullscreen Resolution

```cfg
seta r_fullscreen 1
seta r_fullscreenDesktop 0
seta r_mode -1
seta r_customWidth 2560
seta r_customHeight 1440
vid_restart
```

### Stretch Menu + HUD (No 4:3 Correction)

```cfg
seta ui_aspectCorrection 0
```

### View Weapon: Classic Framing + Slight Lowering

```cfg
seta cl_gunfov 90
seta cl_gunfov_adjust 1
seta cl_gun_z -1.15
```

### View Weapon: Match World FOV, Personal Position Offset

```cfg
seta cl_gunfov 0
seta cl_gun_x 0.5
seta cl_gun_y -0.5
seta cl_gun_z -0.5
```

### Performance-Focused AA Preset

```cfg
seta r_multiSamples 2
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
vid_restart
```

### Maximum Clarity (Higher GPU Cost)

```cfg
seta r_multiSamples 8
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
vid_restart
```

## Troubleshooting

- If a display change does not apply, run `vid_restart`.
- If monitor targeting looks wrong, run `listDisplays`, then set `r_screen` to the correct index and restart video.
- If UI appears too centered/boxed on wide displays, set `ui_aspectCorrection 0`.
- If the window opens off-screen after a monitor change, set `r_screen` explicitly to the target monitor and restart video; OpenQ4 will also attempt to recover automatically.
- If AA settings seem unchanged, check values with `r_multiSamples`, `r_postAA`, and `r_msaaAlphaToCoverage`, then run `vid_restart`.
- If enabling `r_postAA 1` turns the 3D viewport black on an older build, set `r_postAA 0`, run `vid_restart`, and attach `openq4.log` plus the output of `gfxInfo`. Current builds use a three-pass GLSL SMAA path and should no longer hit the old feedback-loop failure. RenderDoc capture is not yet supported on the current OpenQ4 renderer.
