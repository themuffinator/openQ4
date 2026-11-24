# Stereoscopic 3D

openQ4 can show the game in 3D without a VR headset: on a 3D television or
monitor, through video glasses with a side-by-side 3D mode, on a passive 3D
display, or with cheap red/cyan paper glasses. Each eye sees the world from
its own position, so walls recede, enemies stand out from the room and your
weapon sits in front of everything. The HUD, menus and console appear at the
screen's own depth in both eyes, so they stay readable.

For a headset, use [VR](vr.md) instead.

## Turning it on

Open **Settings > Game Options > Virtual Reality** and set **Stereoscopic 3D**
in the **STEREOSCOPIC 3D** section. With the classic interface it is the last
section of **Game Options**. It applies at once; no restart is needed. Pick
the layout your display or glasses expect:

| Layout | Use it with | What it does |
| --- | --- | --- |
| **Side by Side** | 3D televisions, 3D monitors, autostereoscopic laptops | Each eye is squeezed into half of the screen, left eye on the left. Switch the display to its side-by-side 3D mode. |
| **Side by Side (Full)** | Video glasses with a full side-by-side mode (for example 3840x1080), double-width displays | Each eye fills half of a double-width window at full resolution. Set your resolution to the glasses' 3D mode first. |
| **Top and Bottom** | 3D televisions and projectors | Each eye is squeezed into half of the screen's height, left eye on top. |
| **Anaglyph (Red/Cyan)** | Red/cyan glasses, red over the left eye | Both eyes in one picture, coloured so each lens lets through its own eye's view. Colours are muted to keep ghosting low. |
| **Interlaced** | Passive (polarised) 3D monitors and televisions | Alternate rows go to each eye, starting with the left eye on the top row. Play fullscreen, so the rows line up with the display's. |

## Getting comfortable

- **Stereo Depth** sets how far apart the eyes are (2.5 is a person's). Lower
  it if the 3D strains your eyes or the gun looks doubled; raise it for more
  depth. 0 turns the effect flat.
- **Stereo Convergence** sets how far ahead the world meets the screen's
  surface. Things nearer than that stand out of the screen and farther ones
  sink into it. Raise it to push more of the world out of the screen; lower it
  to push the scene back behind it.
- **Swap Eyes** shows each eye's picture to the other eye. Use it if the depth
  looks inside out (near things look far away), if your display starts with
  the right eye, or to view side-by-side images cross-eyed.

## What to expect

- The 3D scene is drawn twice, once per eye, so expect a lower frame rate,
  most of all with **Side by Side (Full)** and **Anaglyph**, which draw both
  eyes at full size. Lower the graphics preset if you need the speed back.
- As in VR, motion blur, dynamic resolution and some screen-space effects
  stay off while stereo is on, and temporal anti-aliasing keeps a history for
  each eye.
- The crosshair is at the screen's depth, so it can look doubled when you aim
  at something far behind or close in front of the screen. Look at the target,
  not the crosshair.
- Effects that blur the whole view, such as double vision after a big
  explosion, are skipped in stereo; menus over the game show a flat, blurred
  copy of the scene behind them.
- Screenshots taken at the window's size capture the 3D layout as you see it.
  Larger, tiled screenshots are flat.
- Stereo works with the Vulkan renderer and the desktop OpenGL renderer. The
  macOS OpenGL renderer and Android keep a flat picture and say why in the
  console, as does a Vulkan window in HDR.

## Console settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `r_stereo3D` | `0` | `0` off, `1` side by side, `2` side by side at full resolution, `3` top and bottom, `4` anaglyph, `5` interlaced. |
| `r_stereoSeparation` | `2.5` | Distance between the eyes in game units (0 to 32; the menu goes to 8). |
| `r_stereoConvergence` | `128` | Distance to the screen's surface in game units (8 to 4096; the menu covers 32 to 512). |
| `r_stereoSwapEyes` | `0` | `1` shows each eye's picture to the other eye. |

All four are saved with your settings. When a headset is presenting, VR takes
over and these settings wait until it stops.
