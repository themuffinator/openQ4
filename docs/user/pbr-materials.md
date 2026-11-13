# Authoring PBR materials

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

openQ4 lets mods add physically based (PBR) materials: surfaces described by
their real colour, roughness and metalness, which light realistically under
any light. This guide is for modders and mappers writing those materials.

- Stock Quake 4 content looks exactly as before. openQ4 never converts stock
  materials; only materials you give a `pbr` block change, and a frame with no
  PBR material in it skips all PBR work.
- PBR is on by default and works on both OpenGL and Vulkan (but not on
  macOS's older OpenGL; see [Checking your materials](#checking-your-materials)).
  PBR surfaces draw in ordinary gameplay next to stock ones, and every light
  keeps its shadows.
- Every PBR material keeps a normal ("classic") Quake 4 material alongside it.
  Where the renderer cannot draw a surface as PBR, it draws the classic stages
  instead, so nothing ever goes missing.

## Writing a PBR material

Add a `pbr` block to a complete classic material. The classic stages must stay:
they are what players see when PBR is off or a surface cannot use it.

```text
textures/my_mod/panel
{
    bumpmap textures/my_mod/panel_local
    diffusemap textures/my_mod/panel_d
    specularmap textures/my_mod/panel_s

    pbr
    {
        workflow metallicRoughness
        albedoMap textures/my_mod/panel_albedo
        normalMap textures/my_mod/panel_normal_xyz
        normalFormat tangentXYZ
        normalScale 1
        ormMap textures/my_mod/panel_orm
        metallic 1
        roughness 1
        ao 1
    }
}
```

The example assumes you have made those textures; openQ4 does not ship them.

### Inputs

| Input | What it holds |
| --- | --- |
| `albedoMap` | The surface's own colour (sRGB), with no lighting baked in. Alpha is coverage, used for cutouts and transparency. |
| `ormMap` | Packed data (linear): red = ambient occlusion, green = roughness, blue = metallic. |
| `metallicMap`, `roughnessMap`, `aoMap` | The same data as separate maps (each read from its red channel), instead of `ormMap`. openQ4 packs them together at load, so they look the same as a packed map; maps of different sizes are resized to the largest. |
| `normalMap` | The PBR normal map, in the format named by `normalFormat`. |
| `emissiveMap` | Glow colour (sRGB), multiplied by `emissiveColor r, g, b`. Values above 1 give a brighter-than-white glow. |

Use either `ormMap` or the separate maps, never both.

`metallic`, `roughness` and `ao` multiply their maps. Without maps they are
the material's values: for example, leave out `ormMap` and use `metallic 0`,
`roughness 0.6`, `ao 1` for an even, non-metal surface.

### Normal map formats

The classic `bumpmap` keeps Quake 4's own normal-map convention. The PBR normal
map states its format, so don't assume a plain RGB normal map and a Quake 4
swizzled one hold the same channels.

| `normalFormat` | Meaning |
| --- | --- |
| `tangentXYZ` | X, Y and Z stored in red, green and blue. |
| `tangentRG` | X and Y in red and green; Z is worked out. |
| `quake4AGB` | Quake 4's alpha/green/blue layout. |

### Rules the renderer checks

A material that breaks one of these rules keeps its classic stages, on both
renderers:

- The classic part must have exactly one active `bumpmap`, `diffusemap`,
  `specularmap` sequence, in that order.
- **Cutouts:** set the `alphaTest` threshold on the classic diffuse stage, and
  make that stage use the same image as `albedoMap`, with no texture
  transforms.
- **Transparency:** a `translucent` material with a single `blend blend` stage
  can be drawn as PBR. Keep the alpha in the albedo texture, and make the blend
  stage use that same image, with no texture transforms and no vertex colour.
  Its alpha setting scales the coverage. Unusual blend modes and custom
  material programs stay classic.
- **Glow:** with an `emissiveMap`, keep exactly one plain classic glow stage
  (`blend add`) that uses the same image and colour, with no texture transform,
  condition or vertex colour, and no other blend stages.

## How PBR surfaces are lit

**Brightness matches the classic lighting.** A light that makes a white classic
surface a certain brightness gives a white, rough PBR surface the same
brightness where it faces the light, and each light adds up just as a classic
light does. Author albedo as the surface's true colour; don't brighten it to
compensate. Curved PBR surfaces fall off towards their edges more naturally
than classic ones.

**Roughness** is perceptual: 0 is a mirror and 1 is fully matte (values below
0.045 are treated as 0.045). It controls the size of highlights and how blurry
reflections are. Rough metals keep their full brightness, and both renderers
smooth out the sparkle that fine normal-map detail can cause at a distance.

**Ambient occlusion (AO)** darkens only indirect light: environment
reflections, baked light and ambient lights. It never dims a point or
projected light. Bright surfaces darken less in crevices than dark ones, as in
reality, and smooth surfaces seen head-on keep reflections that a cavity would
take from a rough one. Screen-space ambient occlusion (`r_ssao`, off by
default) works the same way on PBR surfaces: it darkens only indirect light, and
never twice where your AO map already darkens a crevice. Classic surfaces keep
the classic SSAO.

**Indirect light comes from the map.**

- An ambient light acts as light arriving evenly from every direction. It
  lights PBR surfaces' colour and reflections alike, and metals reflect it in
  their own colour.
- [Reflection probes](#reflection-probes) add local reflections.
- A baked [light grid](light-grids.md) replaces the even environment light
  with its own baked light, while reflections stay.
- Where a map has none of these, PBR surfaces get only their direct lights, as
  classic surfaces do.

**HDR.** With `r_hdrToneMap 1`, PBR surfaces use the same HDR tone curve as
classic ones, so turning PBR on never changes how stock surfaces look.

**Translucent surfaces and shadows.** A translucent PBR surface is lit after
the main lighting pass, so its lights cannot be shadowed there. If a
shadow-casting light reaches any translucent PBR surface in a view, every
translucent surface in that view uses its classic lighting and blend for that
frame. Vulkan also falls back to classic for a view's translucent surfaces when
too many of them sit under too many lights at once (more than 256
surface-and-light pairs); fewer lights or surfaces restore PBR on the next view.

**Animated models** are PBR on both renderers, whether the CPU or the GPU
animates them (`r_gpuSkinning 1`). A model converted to the packed MD5R format
(`r_convertMD5toMD5R 1`) keeps its PBR meshes PBR as well, and `printModel`
marks those meshes `[classic: PBR]`. The one exception is a packed MD5R surface
whose entity skin swaps in a PBR material: that surface keeps its classic
stages.

## Reflection probes

A reflection probe gives PBR surfaces in its area accurate local reflections
from a cube map you capture in the map.

> Probes are off by default (`r_rendererReflectionProbes 0`) because they add
> extra rendering work every frame. Players must turn them on to see your
> probes; everyone else sees the plain environment lighting.

**Make a probe** by giving a point light a dedicated light material with an
`openQ4SpecularProbe` block. The light sets the probe's position, axes and
size; the block names its cube map:

```text
lights/my_mod/probe_room
{
    openQ4SpecularProbe {
        cubeMap env/my_mod/room_probe
        tint 1.0 0.95 0.9
        intensity 1.25
        blendFraction 0.25
        priority 16
    }
}
```

- `cubeMap` names a static cube map (faces `<name>_px.tga` to `_nz.tga`);
  `cameraCubeMap` reads faces in the camera layout instead. Use exactly one.
- `tint` (default `1 1 1`) and `intensity` (default `1`) scale the
  reflections.
- `blendFraction` (default `0.25`, up to `1`) sets how far in from the edge
  the probe fades in.
- `priority` (0 to 255, default 0): where more than two probes overlap, the
  highest priorities win. At most two probes affect any spot, and openQ4 holds
  up to eight probe cube maps at a time.
- Give each probe light its own probe material; a material shared by several
  probe lights is skipped. Leave `light_center` at zero on probe lights.

A plain probe is a sphere, so give its light equal radii. **For a room, add
`boxParallax`** to the block: the light's volume becomes the room's box, so
reflected walls stay in place as you and the surface move instead of sliding
along. A box probe's `light_radius` may differ per axis, and it fades in from
the box faces over `blendFraction` of its smallest half-size.

**Capture the cube maps** with the cheat command
`bakeReflectionProbes [size] [blends]` on the loaded map. It captures every
probe from its light, in the light's axes, writes the six faces its `cubeMap`
names (256 pixels by default; any power of two from 16 to 256) to your save
folder, and reloads them so you see the result at once. `blends` optionally
averages several captures for smoother edges. The capture leaves out screen
effects, the view weapon, mirrors and other probes, so baking the same scene
again gives the same faces.

- openQ4 reads your save folder last. If another copy of those faces is in the
  game or install folders, that copy still loads, and the command warns you.
- `cameraCubeMap` probes are not baked.
- Ship the baked faces with your map. Probes are captured when you run the
  command, never during play.

The first time a probe comes into view, openQ4 prepares it in the background so
the game does not stutter: for a fraction of a second the area keeps its plain
lighting, then the reflections appear. Screenshots always wait for them.

## Settings reference

These are console variables (cvars).

| Setting | Default | What it does |
| --- | --- | --- |
| `r_pbrMaterials` | 1 | Draws PBR materials as PBR; `0` uses every material's classic stages. Configs saved with the old default of 0 are switched on once; after that your choice is kept. |
| `r_rendererModernQuality` | 1 | Master switch for openQ4's modern lighting features, including PBR; `0` turns them all off. |
| `r_glPBR` | 1 | OpenGL only: draws PBR surfaces in the normal lighting pass; `0` uses their classic stages. Not saved. |
| `r_pbrIBL` | 1 | Environment lighting and reflections from probes for PBR surfaces. |
| `r_pbrIBLIntensity` | 1 | Scales that environment lighting (0 to 4). |
| `r_rendererReflectionProbes` | 0 | Turns on authored reflection probes. |
| `r_pbrProbeAsyncPrefilter` | 1 | Prepares new probes in the background; `0` prepares them on the frame that needs them, which can stall for tens of milliseconds. Not saved. |
| `r_hdrToneMap` | 0 | HDR tone mapping for the whole scene, PBR and classic alike. Performance presets set it. |
| `r_ssao` | 0 | Screen-space ambient occlusion. |
| `r_rendererModernVisible` | 0 | Experimental alternative OpenGL path with its own PBR. Leave it off unless you are testing it. A frame that uses SSAO with a PBR material stays on the normal path. |
| `r_vkPBRSpecularAA` | 1 | Vulkan only: smooths highlight sparkle; `0` turns it off for comparisons. Not saved. |

**Inspection modes** (not saved; for checking materials, not for play):

| Setting | Default | What it does |
| --- | --- | --- |
| `r_pbrAnalyticEnvironment` | 0 | Lights PBR surfaces with a bright built-in studio sky wherever no probe applies. Use it to inspect materials, not to light a level. |
| `r_pbrLinearScene` | 0 | Builds PBR lighting in a separate linear scene; with `r_hdrToneMap 1` the whole view, classic surfaces included, goes through the PBR film-style tone curve. Views it cannot handle fall back to the normal frame. |

## Checking your materials

**Debug views.** `r_pbrDebug` shows one input at a time: albedo (1), normals
(2), metallic (3), roughness (4), AO (5), emission (6) and ownership (7). In
ownership mode, green marks surfaces drawn as PBR; on the experimental
`r_rendererModernVisible` path, magenta marks classic surfaces.

**Check `gfxInfo` too.** A surface that fell back to its classic stages can
still look plausible, so don't take a good picture as proof. On OpenGL, the
`OpenGL: native PBR:` line counts PBR and declined surfaces and their light,
environment, glow and translucent draws, and names the last reason a surface
was declined:

- `material-contract`: the material breaks one of the
  [rules the renderer checks](#rules-the-renderer-checks).
- `gpu-posed-geometry`: a packed MD5R surface with a PBR skin.
- `resources`: the graphics driver lacks something PBR needs.
- `view`: PBR is not being used for this view at all.

`translucentView` shows whether that view's translucent surfaces are drawn as
PBR.

**macOS OpenGL.** PBR on OpenGL needs GLSL 1.30. macOS's OpenGL 2.1 does not
have it, so there PBR materials use their classic stages, and the console says
so once.

**HDR captures.** In a linear HDR scene (`r_pbrLinearScene 1` with
`r_hdrToneMap 1`), `screenshot linear screenshots/<name>.pfm` saves the scene's
raw lighting values, before exposure, bloom, tone mapping and the HUD, on
either renderer. Menus and views that are not a complete linear HDR scene are
refused.

Renderer developers: the PBR test laboratory and its scripts are described in
the [renderer validation matrix](../dev/renderer-validation-matrix.md).
