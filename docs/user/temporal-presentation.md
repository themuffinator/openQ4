# Temporal AA and dynamic resolution

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

Temporal anti-aliasing (TAA) combines detail from several frames. It smooths edges and, when the 3D scene renders below your screen's resolution, rebuilds a much sharper image than a plain stretch. It works on OpenGL and Vulkan, and a still picture stays still: when nothing moves, nothing on screen moves. Dynamic resolution builds on it, lowering the scene's resolution when your graphics card is busy.

Neither has a menu control; both use console settings ("console variables"). Most players never need to change them.

## Automatic upscaling (default)

Temporal AA is automatic by default (`r_temporalAA 2`). It switches itself on only when the 3D scene renders below your screen's resolution: when **Settings → Display → Image Quality → Resolution Scale** is below 100%, or when dynamic resolution is on. At full resolution it stays off and your MSAA and SMAA settings apply as usual. The **Performance**, **Low**, **Low Power** and **Minimum** presets render below full resolution, so they use temporal upscaling.

Automatic mode leaves alone the cropped and pixelated scale modes (`r_resolutionScaleMode 0` and `3`), because those looks are chosen on purpose; see [Resolution scale](display-settings.md#resolution-scale). It also stays off on OpenGL ES and on OpenGL drivers too old to run its shaders (without GLSL 1.30).

To use temporal AA at full resolution too, in place of SMAA:

```cfg
r_temporalAA 1
```

In VR, `r_temporalAA 1` gives each eye its own history; automatic mode and dynamic resolution stay off there. See the [VR guide](vr.md#display).

## What stays sharp in motion

Everything that moves is tracked to the pixel, so it keeps its anti-aliasing while it moves: the world, doors, lifts and other moving parts of the map, animated characters, and your weapon. Particles, explosions, smoke, in-world screens, mirrors and portals change on their own, so only the pixels they cover rely on the current frame, and the rest of the view keeps its smoothing.

## Dynamic resolution

Dynamic resolution adjusts the 3D scene's resolution to keep up with your graphics card. To turn it on, with the scene allowed to range from 50% to 100%:

```cfg
r_rendererDynamicResolution 1
r_dynamicResolutionMinScale 50
r_dynamicResolutionMaxScale 100
```

Dynamic resolution always uses temporal upscaling unless `r_temporalAA` is `0`. The HUD, menus and console stay at full resolution.

Screenshots and save-game pictures don't feed into temporal AA's history, so they can't leave traces in later frames. `r_dynamicResolutionCaptureNative 1` renders known capture frames at full resolution; the default of `0` keeps the current scale and doesn't disturb the automatic adjustment.

For a fixed scale instead, leave `r_rendererDynamicResolution 0` and lower **Resolution Scale** (`r_screenFraction`).

## Settings

| Setting | Default | What it does |
|---|---:|---|
| `r_temporalAA` | `2` | `0` off, `1` always on (full resolution too), `2` automatic: on below full resolution. |
| `r_temporalAASharpness` | `0.5` | Sharpening applied to the image you see (`0` to `1`). The frames TAA collects stay unsharpened. |
| `r_temporalAAFeedback` | `0.9` | How much of earlier frames is kept (`0` to `0.98`). Higher is smoother but slower to react. Still parts of the picture keep a little more. |
| `r_temporalAAReactiveEffects` | `0.85` | How strongly particles, effects and moving glass ignore earlier frames (`0` to `1`), which stops them smearing. |
| `r_temporalAAReactiveScale` | `1.0` | Multiplies that rejection for everything (`0` to `2`). |
| `r_rendererDynamicResolution` | `0` | `1` turns dynamic resolution on. |
| `r_dynamicResolutionMinScale` | `50` | The lowest scene scale dynamic resolution may use, in percent. |
| `r_dynamicResolutionMaxScale` | `100` | The highest scene scale dynamic resolution may use, in percent. |
| `r_dynamicResolutionCaptureNative` | `0` | `1` renders screenshots and other capture frames at full resolution. |

## Troubleshooting

- **Turn it all off at once:** set `r_temporalAA 0` and `r_rendererDynamicResolution 0`. The plain SMAA and stretched picture returns immediately.
- **Keeping temporal AA off:** configurations saved by older versions with `r_temporalAA 0` move to the automatic default once. Set `r_temporalAA 0` again afterwards and openQ4 keeps that choice.
- **The whole 3D view trembles slightly with temporal AA on:** this was a fault in releases before 0.14.1, such as 0.13.2. Update, or set `r_temporalAA 0`.
- **Checking what it's doing:** `rendererTemporalPresentationStatus` prints the current scene size, the dynamic-resolution target and timing, the reason for the last history reset, and how much of the last frame's motion was tracked (`Temporal ownership`).
