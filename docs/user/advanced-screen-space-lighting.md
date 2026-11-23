# Advanced screen-space lighting

> This guide matches openQ4 0.15.0. Anything new in 0.15.0 is marked **New in 0.15.0**. See [release status](release-status.md) for what each download includes.

openQ4 has three experimental lighting effects you can try on OpenGL and Vulkan:

- **Volumetric lighting:** lamps, spotlights and sunlight light up a thin haze in the air, and their beams stop where something casts a shadow. **New in 0.15.0**: 0.14.1 had a plain haze in its place.
- **Screen-space reflections (SSR):** surfaces reflect what is on screen.
- **Screen-space global illumination (SSGI):** a little light bounces off nearby surfaces.

All three are off by default and are turned on separately with console settings ("console variables"); there is no menu control. They don't change the installed Quake 4 files. Reflections and bounced light work only from what is already on screen (hence "screen-space"), so they add to the original lighting rather than replace it. The HUD, menus and console are unaffected, and screenshots include the effects.

## Turn them on

Enter any of these in the console:

```text
set r_rendererFroxelVolumetrics 1
set r_rendererSSR 1
set r_rendererSSGI 1
```

They also need `r_rendererModernQuality 1`, which is the default. Setting `r_rendererModernQuality 0` switches all three off at once, together with openQ4's other experimental lighting features such as [PBR materials](pbr-materials.md) and reflection probes, even if their own settings are still on.

When reflections or bounced light are on, they do their own upscaling, so every [resolution scale mode](display-settings.md#resolution-scale) looks like the smooth one. They are applied before [temporal AA](temporal-presentation.md). Volumetric lighting is drawn into the scene itself, before glass, smoke, fire and other see-through effects, so those stay in front of the haze; it doesn't change how resolution scaling looks.

## Volumetric lighting

**New in 0.15.0.** Each light in view shines through the haze with its own colour, shape and fall-off, so a spotlight draws a cone, a caged lamp throws its pattern into the air and the sun lights the whole sky. Where the light uses a shadow map, its beams are blocked by whatever blocks its light on the walls: a pillar leaves a dark shaft behind it, and a fan or a swinging lamp moves its shafts as it moves.

Shadow maps are on in the **Quality** and **Ultra** presets (`r_useShadowMap 1`). Without them, every light uses Quake 4's original stencil shadows, which can't block a beam. By default such lights add no glow to the haze at all, so no beam ever shines through a wall; `r_volumetricStencilLights 1` lets them glow anyway, through their shadows. Lights that cast no shadows, such as fire and muzzle flashes, always glow.

Quake 4's own fog and blend lights keep their original look. The haze also dims distant surfaces slightly, as real haze does.

## Settings

| Setting | Default | What it does |
|---|---:|---|
| `r_rendererFroxelVolumetrics` | `0` | `1` turns on volumetric lighting. |
| `r_froxelVolumetricDensity` | `0.00012` | How thick the haze is (`0.00001` to `0.01`). |
| `r_froxelVolumetricMaxDistance` | `2048` | How far into the scene the haze reaches, in game units (`64` to `8192`). |
| `r_froxelVolumetricSlices` | `12` | Steps taken through the haze for each light (`4` to `16`). More is smoother and slower. |
| `r_volumetricIntensity` | `12` | How brightly the haze glows for its thickness (`0` to `64`). **New in 0.15.0.** |
| `r_volumetricSunIntensity` | `0.08` | Extra scale for sunlight and other far-away lights, whose beams cross the whole view rather than one room (`0` to `4`). **New in 0.15.0.** |
| `r_volumetricAnisotropy` | `0.35` | How much the glow gathers around a light as you look towards it (`0` even, up to `0.9`). **New in 0.15.0.** |
| `r_volumetricResolution` | `2` | `2` draws the haze at half resolution and smooths it up, `1` at full resolution (slower). **New in 0.15.0.** |
| `r_volumetricStencilLights` | `0` | `1` lets lights with stencil shadows glow, unblocked. **New in 0.15.0.** |
| `r_volumetricMaxLights` | `32` | Most lights that glow at once (`0` no limit). **New in 0.15.0.** |
| `r_volumetricSubviews` | `1` | `0` leaves mirrors and security cameras without the haze. **New in 0.15.0.** |
| `r_rendererSSR` | `0` | `1` turns on screen-space reflections. |
| `r_screenReflectionIntensity` | `0.35` | Strongest reflection (`0` to `1`). |
| `r_screenReflectionMaxDistance` | `512` | How far a reflection may reach, in game units (`32` to `2048`). |
| `r_screenReflectionSteps` | `10` | Search steps per reflection (`4` to `16`). More is more accurate and slower. |
| `r_rendererSSGI` | `0` | `1` turns on screen-space global illumination. |
| `r_screenGIIntensity` | `0.25` | Strongest bounced light (`0` to `1`). |

Values outside each range are clamped to it. Changing any of these settings resets temporal AA's history, so old lighting doesn't blend into the new result.

`rendererVolumetricStatus` (also part of `gfxInfo`) reports whether volumetric lighting is requested and actually drawn on your renderer, how many lights glowed with a shadow map, without one, or not at all and why, and in how many views it drew (mirrors and camera screens are counted as `subviews`). **New in 0.15.0.**

## Limitations

These are approximations, not replacements for the level designers' lighting:

- **Volumetric lighting** fills the air evenly: it has no patches of thicker fog or drifting smoke. It doesn't run on Apple's OpenGL 2.1 driver, on OpenGL ES (Android and Quest) or with Quake 4's lab-only linear HDR scene; there the scene is drawn without it.
- **Reflections** can only show what is on screen and not hidden behind something else. They don't know how glossy a surface is, and where a reflection finds nothing, the surface keeps its normal colour.
- **Bounced light** samples eight nearby points, so it only lightens and tints close surroundings; it isn't a full light simulation.

If the renderer can't run an effect, for example because a shader failed to load, the scene is drawn normally without it.

## Performance and troubleshooting

Each effect adds work to every frame. The reflection search steps and volumetric lighting have the biggest effect on speed. Volumetric lighting costs more for each light in view, so busy rooms cost the most; lower `r_froxelVolumetricSlices` or `r_volumetricMaxLights` if it is slow. Try the effects one at a time first, especially at high resolutions.

If an effect doesn't appear:

1. Check that `r_rendererModernQuality` and the effect's own setting are both `1`.
2. Run `gfxInfo` (or `rendererVolumetricStatus`), and look in `openq4.log` for shader or render errors.
3. Try the default values at full resolution (Resolution Scale 100%).
4. Set `r_rendererModernQuality 0` to compare with the normal picture.
