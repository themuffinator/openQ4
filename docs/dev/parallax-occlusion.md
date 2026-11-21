# Parallax occlusion mapping (REN-11)

Ray-marched parallax occlusion for authored PBR materials with a `heightMap`,
on OpenGL and Vulkan, and an upgrade of Raven's `glsl/Parallaxbump.glsl`
custom-lighting stage on Vulkan. Default off (`r_parallaxOcclusion 0`), with
exact rollback. The author guide is
[PBR materials: height and parallax](../user/pbr-materials.md#height-and-parallax);
the plan entry is REN-11 in the [roadmap](roadmap.md#modern-rendering-engine).

## What stock content actually contains

The roadmap entry assumed stock `parallaxbump` surfaces. There are none:

- `materials/hardwareshaders.mtr` (pak002) names `inlineGuide parallax_bump(...)`
  in two test materials, `textures/effects/test/parallax` and `parallax_bones`;
  the Awakening drop's `prison.mtr` names it in nine `roger_*_parallax` test
  materials. No shipped map uses any of them.
- No shipped `guides/*.guide` defines `parallax_bump` (the stock guides define
  `customlit`, `refractiveGlass`, the depth sprites, `water` and the generic
  shaders), so those materials never expand. Their textures
  (`textures/test/areis/*`) are not shipped either.
- No pak ships `glprogs/glsl/Parallaxbump.glsl` or `Customlit.glsl`. Vulkan
  translates the two program names natively (`VK_GLSL_PROGRAM_FAMILY_PARALLAX_BUMP`,
  `vk_Interactions.cpp`); OpenGL looks for their sources, finds none and skips
  the stage (`Couldn't find GLSL sources for program 'glsl/Parallaxbump.glsl'`).

So the retail height source exists only as a format a mod could author for
Vulkan. REN-11 keeps it (an opt-in upgrade with the stage's own scale and bias)
and qualifies it in the laboratory; the production height source is the new
`heightMap` keyword. Classic `heightmap( image, scale )` image programs fold a
height into a bump map and never offset texture coordinates; they are not
parallax sources.

## Contract

**One kernel, one ray.** `src/renderer/ParallaxOcclusion.h` holds the trace and
the self-shadow, written once in the shared GLSL subset: OpenGL embeds it as a
string (like `PBRMath.h`), the offline Vulkan shaders `#include` it. Each
backend supplies only `OpenQ4ParallaxHeight(uv, dx, dy)`. A height `h` lies at
`z = h * scale + bias` texture-coordinate units above the polygon
(`heightReference r` gives `bias = -r * scale`; the retail stage passes its
authored pair). The trace marches `z` from the top of the field down along
`uv + v.xy / v.z * z` (tangent-space view, `v.z` floored at 0.02), with
`r_parallaxSamples` layers at grazing angles, a quarter of that head-on and
fewer with distance, then four secant refinements and a final interpolation.
The fade (`r_parallaxDistance`, over its last quarter) scales the field toward
the polygon, so a faded ray returns the flat texel. The self-shadow
(`r_parallaxShadows`) marches 12 steps from the hit toward the light through
the same field and darkens smoothly with the deepest weighted penetration;
ambient lights never take it.

**Every pass traces the same ray.** The decision is made once per admission in
the shared contract, `R_PBRNative_ResolveParallax` (`PBRNativeContract.cpp`),
which both the OpenGL owner (`R_PBRNative_Material`) and Vulkan
(`VK_PBRDirectMaterial`) call, and every pass of an admitted surface uploads the
same inputs from `R_PBRNative_ParallaxParameters`:

| Pass | OpenGL | Vulkan |
| --- | --- | --- |
| Each light, unshadowed or stencil | `glprogs/shadow_interaction` PBR variant | `interaction.frag` |
| Each shadow-mapped light | projected and point PBR receiver variants (`draw_arb2.cpp`) | `interaction_shadow.frag`, `interaction_shadow_point.frag` |
| Ambient lights (uniform environment) | the per-light programs | the per-light shaders |
| Environment, probes, baked grids | `builtin/pbr_environment` | `interaction.frag` modes 4-5, `pbr_probe_environment.frag`, `pbr_baked_*environment.frag` |
| Diagnostics (`r_pbrDebug`), ownership marker, transparent coverage | environment program | same shaders |

The height rides in the alpha of the ORM image (`packORMH( orm | _white,
height )`, `Image_program.cpp`), which every backend's per-light layout already
binds; with no ORM the generated image carries white material data, which
shades exactly as the scalar-only layout. Vulkan's set-6 block is its whole
256-byte slice, so PBR draws carry the inputs in the two fields they never read:
`celParams = (scale, bias, most samples, self-shadow)` and
`specularColor.xy = fade`. Every PBR draw writes them, zero when flat.

**Gradient-correct sampling.** The trace samples the height with the untraced
coordinate's gradients; afterwards every material sample (albedo, coverage,
normal, ORM, separate metallic/roughness/AO) reads the traced texel with its
own untraced coordinate's gradients (`textureGrad`; OpenGL's GLSL 1.10
receivers use `texture2DGradARB` through `GL_ARB_shader_texture_lod`). The
offset applies to all maps of the surface; they share one set of texture
coordinates.

**Declines.** A surface draws flat, on every pass, when its height is not in
the bound ORM image (separate data maps that could not pack), when it is a
cutout (the depth fill keeps the flat outline, so offset colour would shade
texels the depth pass rejected), when it has an `emissiveMap` (emission draws
through its own pass without a tangent frame), when `heightScale` is zero, and
on OpenGL when the context lacks `GL_ARB_shader_texture_lod`. Depth is never
changed; pixel depth offset is not part of REN-11.

**Rollback.** With `r_parallaxOcclusion 0` the OpenGL programs compile from the
same token stream as before: every sample is a macro that expands to the
original `texture2D` call and the parallax code sits under
`OPENQ4_PBR_PARALLAX`, defined only in the variants built while the cvar is set
(`RB_GLPBR_ParallaxCompiled`; the program caches and the shadow-mapped variant
attempts key on it, so toggling the cvar rebuilds them live). Vulkan's helpers
branch on the per-draw inputs and otherwise issue the original `texture()`
call; the retail stage's single step is the original code.

**Report.** `gfxInfo` prints
`Renderer parallax occlusion: requested= samples= distance= shadows= admitted= flat= draws= lastDecline=`
for the last complete frame, and OpenGL adds
`OpenGL: native PBR parallax: requested= textureGrad= programs= lastDecline=`.

## Qualification (21 November 2026)

Reference laptop (RTX 4060 Laptop GPU, NVIDIA GL 4.6 and Vulkan), hidden
1280x800 window, `debugoptimized` build from a worktree of `5bb28c153` plus
REN-11. The laboratory is generated by
`tools/validation/generate_parallax_validation_map.py` and run by
`tools/tests/renderer_parallax_occlusion.py` (`--prepare`, `--backend gl|vk`,
`--compare`, `--timing`); the static contract is
`tools/tests/parallax_occlusion_contract.py`.

The laboratory is original procedural data: one height field of bevelled,
domed tiles, the normal map derived from it, tile-coloured albedo. A floor and
a wall each carry the `heightMap` specimen beside its flat control (the same
material without a height source); a strip carries the authored-ORM, cutout and
emissive specimens; another carries the retail `Parallaxbump` stage with the
height in its RXGB red channel. Two classic pillars cast the key light's
shadow; a low raking light and an ambient light complete the set.

Both backends pass every case (`final3-gl`, `final2-vk`):

| Check | OpenGL | Vulkan |
| --- | --- | --- |
| Grazing floor, off -> on: traced specimen changes | 82% of pixels, mean 37.3 | 82%, mean 37.3 |
| Same: flat controls and classic room | byte-identical | byte-identical |
| Steep view / wall view, traced specimen | 66% / 40% changed | 66% / 40% |
| Environment pass (analytic studio environment) | 79% changed, controls identical | 79%, identical |
| Shadow-mapped receivers (`mapped=2` OpenGL PBR receiver draws) | 82% changed, controls identical | same |
| Self-shadow: darkens only | 12.9% changed | 12.9% |
| Fully faded ray (`r_parallaxDistance 64`) against off | mean 0.001, peak 2 | mean 0.001, peak 3 |
| Off -> on -> off | byte-identical | byte-identical |
| Cutout and emissive specimens | drawn flat, `lastDecline=emissive` | same |
| Retail `Parallaxbump` stage, single step -> ray | cannot draw (no program); unchanged | 51% changed |

**Backend parity** (`--compare`): in every traced case the OpenGL and Vulkan
images differ by no more than the matching flat case (traced floor mean
0.0037, peak 2; flat floor 0.0014, peak 2). The one exception in the run is a
single pixel of the wall view, at a tile edge where the ray grazes a height
discontinuity and either side is a valid hit (1 pixel in 1,024,000, 67 levels).

**Linux** (WSL Ubuntu 24.04, Mesa 25.2.8 under Xvfb: llvmpipe OpenGL 4.5
compatibility and lavapipe Vulkan 1.4; a `debugoptimized` build of `30f05fa50`
with its own packs). Both backends pass every case, with the same figures as
Windows to the second decimal: traced floor 82% changed (mean 37.2), steep 63%,
wall 40%, environment 79%, shadow-mapped receivers 82% with `mapped=2` OpenGL
receiver draws, self-shadow 13% darkened, faded ray within mean 0.06 of flat,
off -> on -> off byte-identical, the cutout and emissive specimens flat, and the
retail stage traced on Vulkan only. Two differences from NVIDIA, both at the
precision limit and both now stated tolerances in the runner:

- With parallax on, llvmpipe draws a few pixels of the wall's flat control one
  level differently: that surface goes through the parallax program variant,
  whose flat path Mesa compiles differently. NVIDIA keeps it byte-identical.
  Classic surfaces never change. The check allows a one-level difference.
- llvmpipe and lavapipe disagree on isolated pixels at tile edges, where the
  ray grazes a height step and either side is a valid hit: 0.27% of the traced
  floor (mean 0.10), against one pixel on NVIDIA. Within one backend every pass
  runs the same code on the same inputs, so passes stay aligned. The parity
  check allows 0.5% more differing pixels and 0.25 more mean difference than
  the matching flat case.

**Android** (x86_64 emulator, API 35, SwiftShader OpenGL ES 3.0; release native
build of `30f05fa50`, debug APK). The GLES renderer has no native PBR owner, so
REN-11's qualification there is a clean decline:
`android_emulator_smoke.py --mode parallax --lab <prepared laboratory>` copies
the laboratory into the app's save path, captures the grazing view with
`r_parallaxOcclusion 0` and `1`, and passes: the two captures are
byte-identical, the `heightMap` specimens draw their classic stages, the report
reads `requested=0` then `requested=1` with `admitted=0 draws=0`, no material or
packing warning appears, and the copied files are retired afterwards. The
ordinary `--mode sp` gameplay, touch and save/load smoke also passes on the
same APK. The GLES module links with `-z defs`, so no GL-only symbol leaked
into it.

**Rollback and unchanged stock materials.** The PBR laboratory
(`renderer_pbr_laboratory.py`; lit, production, production-shadows,
ownership, the five material diagnostics, environment-only, the classic
production-fixed specimen and emission, in one batch) was captured with this
build and with an unmodified build of `5bb28c153`: OpenGL (`--gl-native`) and
Vulkan (`--production`) are **byte-identical in all 12 captures** with
`r_parallaxOcclusion 0`, and also with `r_parallaxOcclusion 1`, since nothing in
it has a height source (OpenGL with `r_pbrProbeAsyncPrefilter 0`: with
background probe filtering the first capture of a batch can be taken before the
probes publish, and the slower first compile of the parallax variants made that
race visible once; no parallax surface is involved). Both builds fail the same
two PBR-laboratory image checks (source-alpha ownership,
production-fixed classic submission), which predate REN-11.

**Cost** (`--timing`, 2560x1440 as FPS-01 measures, traced floor filling most of
the frame at a grazing angle, whole-frame GPU time, minimum of six
120-frame windows on a machine with another client running):

| Setting | OpenGL | Vulkan |
| --- | ---: | ---: |
| `r_parallaxSamples 8` | +0.56 ms | +0.82 ms |
| default (32) | +1.28 ms | +1.46 ms |
| 64 | +1.93 ms | +2.44 ms |
| default + `r_parallaxShadows 1` | +1.75 ms | +2.32 ms |
| 64 + shadows | +2.58 ms | +3.05 ms |

Against FPS-01's Ultra budgets (9-11 ms mean per stock scenario on this
laptop), a frame dominated by a traced surface spends roughly a seventh of the
budget on the default ray. No stock scenario contains a height source, so none
of FPS-01's gated scenarios changes; no preset enables parallax, and the SYSTEM
catalog is untouched.

## Open

- VR stereo was not run: each eye traces with its own view origin, which gives
  correct stereo parallax by construction, but no headset session confirmed it.
- Physical Linux GPUs (NVIDIA, AMD, Intel drivers), macOS and physical
  Android devices belong to platform qualification (PLAT/AND), as for the other
  REN tasks. Apple GL 2.1 declines the native PBR owner already (GLSL 1.30
  gate); GLES has no native PBR owner.
- A preset decision needs authored content and a quiet-machine
  `performance_suite.py` ladder; nothing ships a `heightMap` today.
