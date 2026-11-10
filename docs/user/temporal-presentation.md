# Temporal AA And Dynamic Resolution

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

openQ4's temporal anti-aliasing (TAA) collects detail from several frames. On
OpenGL and Vulkan it smooths edges and upscales a lower-resolution 3D scene
back to your screen far more cleanly than a plain stretch.

## Automatic upscaling (default)

`r_temporalAA` defaults to `2`, automatic. Temporal AA switches itself on only
when the 3D scene renders below your screen's resolution, either because
`r_screenFraction` is below `100` or because dynamic resolution is on. At
native resolution it stays off, and your MSAA and SMAA settings apply as
before. The **Performance**, **Low**, **Low Power** and **Minimum** presets render below
native resolution, so they now get temporal upscaling.

Automatic mode leaves the `0` (cropped) and `3` (nearest-neighbour) scale modes
alone, because those looks are chosen on purpose. It also stays off on
OpenGL ES and on OpenGL contexts without GLSL 1.30.

| Setting | Default | What it does |
|---|---:|---|
| `r_temporalAA` | `2` | `0` off, `1` always on (native resolution too), `2` automatic: on below native resolution |
| `r_temporalAASharpness` | `0.5` | Sharpening applied to the image you see (`0..1`); the frames TAA accumulates stay unsharpened |
| `r_temporalAAFeedback` | `0.9` | How much of the previous frames is kept (`0..0.98`); higher is smoother and slower to react |
| `r_temporalAAReactiveEffects` | `0.85` | How strongly particles, effects and moving glass reject old frames (`0..1`), which keeps them from smearing |
| `r_temporalAAReactiveScale` | `1.0` | Multiplies every reactive rejection (`0..2`) |

To use temporal AA at native resolution too, in place of SMAA:

```cfg
r_temporalAA 1
```

In VR, `r_temporalAA 1` gives each eye its own history; automatic mode and
dynamic resolution stay off there. See the [VR guide](vr.md#display).

## What moves correctly

Every moving thing on screen is tracked to the pixel. The world and doors,
lifts and other moving brushwork follow the camera and their own motion. Animated
characters follow their skeleton's motion from one frame to the next, and your
weapon follows its own projection. All of these keep their anti-aliasing while
they move. Particles, explosions, smoke, in-world screens, mirrors and portals
change on their own, so only the pixels they actually cover lean on the
current frame, and the rest of the view keeps its history.

## Dynamic resolution

To let the 3D scene adjust its resolution to GPU load as well:

```cfg
r_rendererDynamicResolution 1
r_dynamicResolutionMinScale 50
r_dynamicResolutionMaxScale 100
```

Dynamic resolution always uses temporal upscaling unless `r_temporalAA` is `0`.

The HUD, menus and console remain at your native output resolution. Screenshots
and save previews bypass temporal history so they cannot feed captured pixels
back into later gameplay. `r_dynamicResolutionCaptureNative 1` asks known
capture frames to render the 3D scene at full resolution; its default of `0`
keeps the current scale and avoids disturbing the controller.

For a fixed scale instead of automatic adjustment, leave
`r_rendererDynamicResolution 0` and set `r_screenFraction` below `100`.

Use `rendererTemporalPresentationStatus` in the console to inspect the current
scene size, GPU target, delayed timing sample, history reset reason and what the
last frame's motion tracking covered (`Temporal ownership`). Set
`r_temporalAA 0` and `r_rendererDynamicResolution 0` to restore the plain
SMAA/spatial presentation path immediately.

Profiles saved by an earlier version that had `r_temporalAA 0` move to the
automatic default once. Set `r_temporalAA 0` again afterwards to keep temporal
AA off; that choice is kept.
