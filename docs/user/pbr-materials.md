# Authoring PBR materials

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

PBR is a material extension for new openQ4 content, on by default. It does not
convert retail Quake 4 materials, so stock content looks exactly as it always
has; only materials with a `pbr` block change. On OpenGL and Vulkan, each PBR surface
is drawn natively inside the regular light loop, so it renders in ordinary
gameplay next to stock surfaces, and every light keeps its own shadows. A
material the renderer cannot prove keeps its authored classic stages. See the
[production-readiness plan](../dev/archive/2026-10-18/plans/2026-10-04-pbr-production-readiness.md)
and the earlier [qualification ledger](../dev/archive/2026-10-18/plans/2026-09-20-pbr-rendering-audit.md)
for current evidence and limitations.

## Material declaration

Keep a usable classic material beside the `pbr` block. The following example
assumes that the named textures have been authored; it does not add assets to
the engine package.

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

The classic bump texture retains the engine's classic normal-map convention.
The PBR normal texture declares its own convention explicitly; do not assume
that a raw RGB normal and an A/G/B-swizzled normal contain the same channels.

| Input | Interpretation |
|---|---|
| `albedoMap` | sRGB color; alpha remains linear coverage. Do not bake lighting into it. |
| `emissiveMap` | sRGB color, multiplied by linear `emissiveColor r, g, b`. Values above one retain HDR energy. |
| `ormMap` | Linear data: red = occlusion, green = perceptual roughness, blue = metallic. |
| `metallicMap`, `roughnessMap`, `aoMap` | Separate linear data maps (each map's red channel), used instead of packed ORM. The engine packs them into one ORM texture at load, sampled like the first map, so they shade exactly like the packed form; maps of different sizes are resampled to the largest. |
| `normalFormat tangentXYZ` | Signed tangent-space XYZ reconstructed from RGB. |
| `normalFormat tangentRG` | Signed tangent-space XY from red/green; positive Z is reconstructed. |
| `normalFormat quake4AGB` | The Quake 4 alpha/green/blue channel convention. |

`metallic`, `roughness` and `ao` multiply their maps. Without maps they are
scalar material values. For example, omit `ormMap` and use `metallic 0`,
`roughness 0.6`, `ao 1` for a uniform dielectric. Do not declare packed ORM and
separate material-data maps together.

Roughness is perceptual (the GGX alpha is its square) and bounded to
`[0.045, 1]` for shading. It shapes every specular term: the direct-light
lobe, the choice of prefiltered environment level, the split-sum response and
the roughness-aware environment Fresnel. Both renderers widen it by the
on-screen normal variance to suppress specular aliasing. Rough metals keep
their full brightness: the renderer adds the multiple-scattering energy that a
single-scattering GGX lobe loses (up to 69% for a white metal at roughness 1).

AO is indirect visibility. It darkens environment lighting, baked diffuse and
authored ambient lights (which stand in for bounced light), never point or
projected lights. Diffuse uses a multi-bounce form, so bright occluded albedo
darkens less than dark albedo. Environment specular uses specular occlusion
derived from AO, roughness and view angle, so smooth surfaces seen head-on keep
reflections a cavity would remove from rough ones. Reflections that a normal
map would send below the geometric surface fade out.

Screen-space ambient occlusion (`r_ssao`, off by default) estimates the same
indirect visibility from the depth buffer. On a PBR surface it darkens only
the indirect light, as the lesser of its occlusion and the material AO, so it
never dims a direct light or a highlight and never doubles a crevice the AO
map already darkens. Classic surfaces keep the classic SSAO, which darkens
their whole lighting.

PBR surfaces take their indirect light from the map itself. An authored
ambient light stands in for light bounced from every direction, so PBR treats
it as a uniform environment: it lights diffuse and specular alike, metals
reflect it in their own color, and AO occludes both. Authored probes add local
reflections, and a baked light grid replaces environment diffuse with its own
irradiance while the reflections stay. Where a map has none of these, PBR
surfaces receive only their direct lights, as classic surfaces do.

PBR surfaces are lit at the level of the map's classic lighting. A light that
makes a white classic surface show a given brightness gives a white, rough PBR
surface the same brightness where the light falls on it head-on, and each light
adds to the picture exactly as a classic light does. Author the albedo as the
surface's own color; do not brighten it to compensate for the renderer. PBR
keeps its physically based cosine falloff and highlights, so curved surfaces
fall off towards their edges more naturally than classic ones. With
`r_hdrToneMap 1`, PBR surfaces share the classic HDR tone curve, so turning PBR
on never changes how stock surfaces look.

For a cutout, author the classic diffuse stage's `alphaTest` threshold. A
conventional `translucent` material with a single `blend blend` stage can use
ordered PBR source-alpha transparency on OpenGL and on native Vulkan. Keep its
alpha in the albedo texture: both backends read the coverage from the albedo
image and scale it by the stage's own alpha register, so the blend stage must
name that same image with untransformed coordinates and no vertex tint.
Unusual blend expressions and custom material programs keep classic ownership.
A translucent PBR surface is lit where its blend stage draws, after the light
loop, so its lights cannot be shadowed there: in a view where a
shadow-casting light reaches a translucent surface, every translucent surface
in that view keeps its classic lighting and blend on both backends.

## Settings and fallback

PBR materials are on by default (`r_pbrMaterials 1`); a profile saved with the
former default (0) switches on once, and a later `r_pbrMaterials 0` returns
every PBR material to its classic stages and is kept. They also need
`r_rendererModernQuality 1` (the default). A frame that draws no PBR material
skips all PBR preparation. On OpenGL, the classic light loop draws admitted PBR
surfaces natively (`r_glPBR`, default 1; 0 returns them to their classic
stages). The experimental modern visible path (`r_rendererModernVisible 1`)
still takes whole frames that qualify for it, with its own PBR, except a
frame that uses SSAO with a PBR material, which stays with the light loop so
that SSAO still darkens only indirect light. OpenGL PBR needs GLSL 1.30: on
a context without it, such as macOS's OpenGL 2.1, PBR materials keep their
classic stages and the console says so once.
`r_hdrToneMap 1` presents the scene through the classic HDR tone map. The
laboratory mode `r_pbrLinearScene 1` (default 0, not saved) instead accumulates
PBR radiance in a separate linear scene, encoded once per pixel, and with
`r_hdrToneMap 1` presents the whole view, classic surfaces included, through
the PBR filmic curve.
`r_pbrIBL 1` enables environment reflections and diffuse from authored probe
lights; `r_pbrIBLIntensity` scales them. Authored probes stay opt-in
(`r_rendererReflectionProbes 1`, default 0) because they start the modern
scene-packet pipeline on every frame. A probe that comes into view for the
first time is prepared in the background, so the frame does not hitch; for a
fraction of a second the area keeps its plain environment lighting, then the
probe's reflections appear. Screenshots always wait for them.
`r_pbrProbeAsyncPrefilter 0` prepares each probe on the frame that needs it
instead (a stall of tens of milliseconds). The
laboratory's analytic studio environment, a bright sky that lights every
surface whether or not the map has probes, is `r_pbrAnalyticEnvironment 1`
(default 0, not saved); use it to inspect materials, not to light a level.
`r_rendererModernQuality 0` restores classic ownership.

`r_pbrDebug` exposes albedo (1), normals (2), metallic (3), roughness (4), AO (5),
emissive (6) and ownership (7). In ownership mode, green identifies PBR draws;
magenta identifies classic draws in the modern path. Always check `gfxInfo` as
well: a rejected frame must not be mistaken for successful PBR just because its
classic fallback is visible. On OpenGL its `OpenGL: native PBR:` line counts
the admitted and declined surfaces, the light, environment, emission and
translucent draws, and names the last decline reason (`material-contract`,
`gpu-posed-geometry`, `resources`, `view`) and whether the view owns
translucency (`translucentView`).

Animated models are PBR on both renderers, CPU-skinned or GPU-skinned
(`r_gpuSkinning 1`). A model converted to MD5R (`r_convertMD5toMD5R 1`) keeps
classic geometry for its PBR-authored meshes on OpenGL, as Vulkan does for
every mesh, so they are PBR too; `printModel` marks such a mesh
`[classic: PBR]`. Only a packed MD5R surface whose entity skin swaps in a PBR
material keeps its classic stages (`gpu-posed-geometry`).

The qualified OpenGL HDR path includes point/projected shadows, cutouts,
ordered source alpha, authored fog/blend lights, and existing baked area
irradiance. Baked diffuse replaces environment diffuse while preserving
environment specular, and respects metalness and AO. The bake remains LDR.

Fixed classic bump/diffuse/specular materials can coexist in a full-viewport
HDR PBR scene with shadows and MSAA disabled. Their original light projections,
normal convention and specular colors are preserved. More complex classic
materials, unsupported receiver semantics, cropped/multiple-root views,
portal/view-weapon grid blending and exhausted resource budgets retain the
complete classic frame. This is not general classic-renderer parity.

Authored probes use the existing `openQ4SpecularProbe` light-material block.
The current atlas holds eight authored cubemaps plus the analytic environment,
with at most 32 frame records and two selected probes per cluster. It filters
rough reflections and diffuse irradiance.

Add `boxParallax` to the block of a probe that shows a room: its light volume
becomes that room's box, so reflected walls stay where they are as you and the
surface move instead of sliding along, and its `light_radius` may differ per
axis (a plain probe is a sphere and needs equal radii). Its influence fades in
from the box faces over `blendFraction` of the smallest half extent.

`bakeReflectionProbes [size] [blends]` (a cheat command) captures every probe
of the loaded map from its light, in the light's axes, writes the six faces
its `cubeMap` names (`<name>_px.tga` to `_nz.tga`, 256 pixels by default) to
your save folder and reloads them, so the probes show the capture at once.
The capture leaves out display filters, the view weapon, mirrors and other
probes' reflections, so baking the same scene again gives the same faces.
openQ4 reads the save folder last: if another copy of those faces exists in
the game or install folders, it still loads, and the command warns. Ship the
baked faces with your map; probes are captured on request, not while playing.
Give each probe light its own probe material, since a shared one is skipped.

## Reproduce the laboratory

After building and staging, run the complete native Vulkan material suite in
an independent laboratory runtime (replace the retail asset path as needed):

```powershell
python tools/tests/renderer_pbr_laboratory.py --prepare --runtime-root .tmp/pbr-native --output-dir .tmp/pbr-native-proof0 --basepath "E:/SteamLibrary/steamapps/common/Quake 4" --backend vk --batch --suite vulkan-direct --samples 0 --timeout 480
python tools/tests/renderer_pbr_laboratory.py --runtime-root .tmp/pbr-native --output-dir .tmp/pbr-native-proof4 --basepath "E:/SteamLibrary/steamapps/common/Quake 4" --backend vk --batch --suite vulkan-direct --samples 4 --timeout 480
```

The suite currently has 73 controls per sample count. Run the separate
`--cases lit,ownership,emissive,master-off` full-map check to expose
unsupported ownership. The emission control is required: transparency is
proven by the difference between the two debug captures, which is the authored
coverage alone, because a single capture also carries the background behind
the surface.

Vulkan currently implements opaque, matching alpha-tested and source-alpha
direct lighting with scalar, packed ORM or separate metallic/roughness maps and
all three documented normal encodings (or no normal map). It requires one
active classic bump/diffuse/specular sequence. A cutout must use the same
image, sampling and untransformed UVs for classic diffuse coverage and PBR
albedo. Emission requires one matching classic additive glow stage; its native
replacement emits once, including without any direct lights.

Source-alpha transparency is ordered: a translucent surface is absent from the
depth fill, so the light pass records its admitted draws instead of adding
them and the material walk composites them where the authored stage would have
drawn. Opacity is applied once across the complete mesh. Each light then adds
through that alpha using its own receiver triangles and scissor; environment
lighting adds across the full mesh. Separate objects using the same model keep
their own lighting. The whole
view returns to classic ownership when a shadowing light reaches a translucent
receiver, because stencil shadow coverage is reset with its light and cannot
be replayed afterwards.

Native transparency also checks its 256-record limit before drawing. A record
is one eligible surface under one active light stage. If that conservative
bound is exceeded, all translucent materials in the view keep their complete
classic lighting and blend stages. `gfxInfo` reports the admission reason,
required record bound, recorded draws and composited surfaces; `reason=capacity`
with zero recorded draws identifies this fallback. The decision resets every
view, so reducing the light or surface count restores native ownership.

The dedicated partial-light and model-instance regression uses the same prepared
laboratory runtime:

```text
python tools/tests/renderer_vulkan_pbr_transparency.py --runtime-root .tmp/pbr-native --output-dir .tmp/pbr-alpha0 --basepath "E:/SteamLibrary/steamapps/common/Quake 4" --samples 0
python tools/tests/renderer_vulkan_pbr_transparency.py --runtime-root .tmp/pbr-native --output-dir .tmp/pbr-alpha4 --basepath "E:/SteamLibrary/steamapps/common/Quake 4" --samples 4
```

`tools/tests/renderer_vulkan_pbr_capacity.py` accepts the same arguments and
checks below-limit, exact-limit, overflow, restoration and image/video reload
controls. Overflow captures must match the complete classic reference image.
`tools/tests/renderer_vulkan_pbr_fog.py` uses the same arguments to verify that
native lighting and authored alpha survive the intervening fog pass.
`tools/tests/renderer_vulkan_pbr_resources.py` also accepts these arguments and
checks complete classic fallback and native recovery when transparent PBR
resources cannot be prepared, including across image and video reloads.

Authored ambient lights are uniform PBR environments on every backend,
including through source-alpha transparency: Fresnel-weighted diffuse and the
split-sum specular, both occluded by material AO, so metals reflect them. The
laboratory's ambient oracle predicts that view-dependent response pixel by
pixel over the specimen sphere (`tools/tests/pbr_reference.py`).
The [ambient-light qualification](../dev/vulkan-pbr-ambient.md) records numerical,
capacity and recovery controls. Both capacity and resource harnesses accept
`--ambient-lights` to exercise those light stages.

Mismatched masks/glows and unsupported stage combinations keep classic
ownership. Vulkan filters environment reflections and diffuse irradiance
through `r_pbrIBL`, with material AO and roughness, from authored probes (and
the analytic studio environment in the laboratory mode). The default
light-grid setting preserves this lighting when no baked grid applies to the
receiver. Experimental authored
reflection probes now have [local LDR/0x/4x qualification](../dev/vulkan-pbr-probes.md)
through `r_rendererReflectionProbes`. A baked light grid lights admitted PBR
surfaces on Vulkan as on OpenGL: [PBR-weighted baked diffuse](../dev/vulkan-pbr-baked.md)
beside the environment specular, in ordinary frames and in the laboratory's
linear HDR scene. Environment light
also participates in the ordered transparent composite when no direct lights
are present. See [native environment lighting](../dev/vulkan-pbr-environment.md)
for its exact scope and qualification. Native material diagnostics now draw
albedo (1), normals (2), metallic (3), roughness (4), AO (5) and ownership (7)
once over the full surface, including when direct and environment lights are
off. Emission (6) retains the authored glow. These views are under
[qualification](../dev/vulkan-pbr-diagnostics.md)
with `renderer_vulkan_pbr_diagnostics.py` and its paired image comparator.
Transparent diagnostics retain authored alpha. The native laboratory separately checks equivalent
layouts, mapped shadows, emission, cutout and source-alpha coverage, rollback
and restart. Its green marker proves the admitted native surface path, not
complete environment lighting.
Vulkan filters both surface curvature and normal-map variation with the same
bounded roughness kernel. `r_vkPBRSpecularAA 0` disables this filtering for
diagnostic comparisons; it defaults to 1 and is not archived.
Full scene linearization and display-color parity also remain unqualified on
Vulkan; controlled material comparisons do not establish full scene parity.

Build and stage the engine using the project's normal Meson wrapper first.
From the repository root, the following creates an independent runtime,
generates original test assets, compiles the map, and captures it through the
engine's registered screenshot command:

```text
python tools/tests/renderer_pbr_laboratory.py --prepare --runtime-root .tmp/stock-runtime/pbr-lab --basepath "<retail Quake 4 directory>" --output-dir .tmp/pbr-proof --batch --linear --gl-debug --tier gl45 --cases lit,ownership,no-probes,direct,legacy,master-off
```

The retail directory must contain `q4base`. Choose new runtime/output paths;
the harness refuses to replace an existing runtime. Subsequent runs reuse that
runtime without `--prepare` and use a new output directory. All game runs are
windowed, use isolated save paths and disable mouse capture. The harness does
not inject input or capture the desktop.

The 24-station map covers curved dielectric/metal roughness series, packed and
separate channels, normal encodings, emissive, cutout/source alpha, authored
probes, colored point lights and a projector. Additional scripted controls
exercise moving shadows, skinning, baked lighting, fallback, resize and reloads.
Reports retain binary/fixture/map/harness hashes, ownership diagnostics, display
TGA images and pre-tone-map PFM radiance for completed linear HDR scenes.
Use `screenshot linear screenshots/<name>.pfm` for a completed linear HDR scene
(`r_pbrLinearScene 1` with `r_hdrToneMap 1`) on either backend; the laboratory's
HDR controls set both. Native Vulkan exports the scene after fog and transparency,
before exposure, bloom, tone mapping and HUD, at the active scene resolution.
Views that cannot use complete linear HDR, menus and invalid paths are rejected.
The engine rejects linear capture of the encoded non-HDR preview. Add `hdr`
to the example's case list to capture linear radiance. Scene-target storage is
bounded to finite, nonnegative half-float values (maximum 65,504) on the OpenGL
path; the Vulkan emission test bounds one texture-modulated emission write,
not arbitrary sums of many lights. Ordinary
overbright energy is retained. Numerical and negative-control
tests accompany the rendered map; a screenshot alone is not a passing result.
