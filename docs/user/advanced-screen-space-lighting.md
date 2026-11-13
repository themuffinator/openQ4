# Advanced screen-space lighting

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

openQ4 has three experimental lighting effects you can try on OpenGL and Vulkan:

- **Volumetric haze:** light scattering through the air.
- **Screen-space reflections (SSR):** surfaces reflect what is on screen.
- **Screen-space global illumination (SSGI):** a little light bounces off nearby surfaces.

All three are off by default and are turned on separately with console settings ("console variables"); there is no menu control. They don't change the installed Quake 4 files. They work only from what is already on screen (hence "screen-space"), so they add to the original lighting rather than replace it. The HUD, menus and console are unaffected, and screenshots include the effects.

## Turn them on

Enter any of these in the console:

```text
set r_rendererFroxelVolumetrics 1
set r_rendererSSR 1
set r_rendererSSGI 1
```

They also need `r_rendererModernQuality 1`, which is the default. Setting `r_rendererModernQuality 0` switches all three off at once, together with openQ4's other experimental lighting features such as [PBR materials](pbr-materials.md) and reflection probes, even if their own settings are still on.

When any of these effects is on, it does its own upscaling, so every [resolution scale mode](display-settings.md#resolution-scale) looks like the smooth one. They are applied before [temporal AA](temporal-presentation.md).

## Settings

| Setting | Default | What it does |
|---|---:|---|
| `r_rendererFroxelVolumetrics` | `0` | `1` turns on volumetric haze. |
| `r_froxelVolumetricDensity` | `0.00012` | How thick the haze is (`0.00001` to `0.01`). |
| `r_froxelVolumetricMaxDistance` | `2048` | How far into the scene the haze reaches, in game units (`64` to `8192`). |
| `r_froxelVolumetricSlices` | `12` | Depth layers used to build the haze (`4` to `16`). More is smoother and slower. |
| `r_rendererSSR` | `0` | `1` turns on screen-space reflections. |
| `r_screenReflectionIntensity` | `0.35` | Strongest reflection (`0` to `1`). |
| `r_screenReflectionMaxDistance` | `512` | How far a reflection may reach, in game units (`32` to `2048`). |
| `r_screenReflectionSteps` | `10` | Search steps per reflection (`4` to `16`). More is more accurate and slower. |
| `r_rendererSSGI` | `0` | `1` turns on screen-space global illumination. |
| `r_screenGIIntensity` | `0.25` | Strongest bounced light (`0` to `1`). |

Values outside each range are clamped to it. Changing any of these settings resets temporal AA's history, so old lighting doesn't blend into the new result.

## Limitations

These are approximations built from the finished picture, not replacements for the level designers' lighting:

- **Volumetric haze** uses general ambient and directional light. It doesn't take light from every lamp in the scene or cast shadows through the haze.
- **Reflections** can only show what is on screen and not hidden behind something else. They don't know how glossy a surface is, and where a reflection finds nothing, the surface keeps its normal colour.
- **Bounced light** samples eight nearby points, so it only lightens and tints close surroundings; it isn't a full light simulation.

If the renderer can't run an effect, for example because a shader failed to load, the scene is drawn normally without it.

## Performance and troubleshooting

Each effect adds work to every frame. The reflection search steps and the haze depth layers have the biggest effect on speed. Try the effects one at a time first, especially at high resolutions.

If an effect doesn't appear:

1. Check that `r_rendererModernQuality` and the effect's own setting are both `1`.
2. Run `gfxInfo`, and look in `openq4.log` for shader or render errors.
3. Try the default values at full resolution (Resolution Scale 100%).
4. Set `r_rendererModernQuality 0` to compare with the normal picture.
