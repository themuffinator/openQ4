# Modern lit-frame ownership inventory (REN-02)

This is the inventory that [REN-02](roadmap.md) starts from. It lists every
reason the aggregate OpenGL modern visible path (`ModernGLExecutor.cpp`) still
hands a lit frame back to the classic ARB2 light loop, and compares those
reasons with the shared corridors already implemented. It is a supporting
reference: scheduling stays in the roadmap. Taken from source on 6 November 2026.
Function names are given instead of line numbers, which drift.

## Where things stand

- **No lighting domain is proven by default.** `MODERN_LIGHTING_PARITY_PROVEN_DOMAINS`
  is 0. `r_rendererModernLightingParity` (default 0; bits 1 interaction,
  2 fog/blend, 4 light grid, 8 shadow receivers) is a diagnostic override for
  capture only, and the default-safety report flags any non-zero value. With
  default settings, any frame with lit work therefore declines to ARB2.
- **The one per-material proof is `R_ModernGLExecutor_MaterialLightingProven`.**
  It requires `r_rendererModernQuality 1` and `r_useScissor 1`. Every ambient or
  interaction draw in a non-sidecar 3D view must then be in one of these
  classes:
  - **PBR modern-ready:** an authored BRDF with no PBR fallback.
  - **Classic diffuse:** a single diffuse stage, a flat bump map and a black
    specular map, either opaque or a cutout whose alpha test sits on that
    diffuse stage (added 6 November, see below).
  - **Classic fixed:** bump, diffuse and at most one specular stage. This class
    counts only inside the classic-lighting corridor view.

  The proof also fails on:
  - any AMBIENT light, because the ambient normalization cube is unqualified;
  - enhanced materials;
  - cel shading;
  - `r_skipDiffuse`.
- **Every modern-path corridor ran only in a laboratory mode** (corrected
  6 November: fog/blend never did, and classic lighting and baked grid no
  longer do; see Progress). The classic-lighting and baked-grid corridors
  needed `PBRLinearSceneRequested`, that is `r_pbrMaterials`, `r_hdrToneMap 1`
  and `r_pbrLinearScene 1` (the last two default 0):
  - the classic-lighting corridor (`R_ModernGLExecutor_PrepareClassicLighting`):
    one root view, no MSAA, no shadows, no ambient lights;
  - the baked light-grid corridor;
  - the linear fog/blend corridor.
- **The nine shared-domain corridors** listed below all default to 0. Turning on
  shared world ambient, shared world interaction or shared deform turns the
  aggregate modern visible path off (`R_ModernGLExecutor_ModernVisibleRequested`).
  The two are separate ownership models and do not yet compose.

## Decline reasons

The executor keeps the first blocker of a frame
(`R_ModernGLExecutor_SetOwnershipBlocker`, telemetry
`Modern visible frame: ... blocker='view=N pass=P ... reason=R'`). Fallback
counters decide whether that blocker actually declines the frame
(`RecomputeModernVisibleFallbacks`, `VisibleCompositionReady`).

### Per draw: materials and geometry (`RecordPacketFallbackBlockers`)

| Reason | Trigger |
| --- | --- |
| `flat-diffuse-legacy` | Flat-diffuse surfaces: modern shaders merge ambient and diffuse stages into one albedo |
| `missing-modern-geometry` | No geometry or instance record, or no indices |
| `missingGeometry`, `missingIndexData` | Packet geometry incomplete |
| `unsupportedDeform` | CPU material deform (`GEOMETRY_DEFORM_MATERIAL`) |
| `unsupportedGpuSkinning` | Skinned record without a GPU-skinning contract (seen in the laboratory on packed MD5R) |
| `missingVertexBuffer` | No ambient vertex buffer (seen on packed MD5R stencil-shadow geometry) |
| `missing-material-record` | No material-table record |
| `pbr-material-not-modern-ready` | PBR material that is neither classic- nor PBR-eligible |
| `legacy-scene-gui` | Entity GUIs in the ambient pass of a scene view |
| Material fallbacks | `missingMaterial`, `noDrawStages`, `needsCurrentRender`, `stageCondition`, `textureMatrix`, `vertexColor`, `polygonOffset`, `customGLSL`, `customProgram`, `dynamicImage`, `currentRenderImage`, `screenTexgen`, `skyTexgen`, `unsupportedTexgen`, `missingImage`, `tooManyTextures`, `stageColor` |

Legacy-feedback and sidecar views (subviews, remote cameras, demos) are exempt
from most of these.

### SSAO

`pbr-ssao-indirect-only`: a frame that requests SSAO and draws a PBR material
goes to the classic owner, which applies occlusion to PBR indirect light only.

### Light descriptors (`LightDescriptorBlockReason`)

| Reason | Trigger |
| --- | --- |
| `special-light-unrepresentable` | Parallel/sun or unclassified light types |
| `missing-falloff-image`, `missing-projection-image`, `missing-point-projection-image` | The light's image is missing |
| `per-light-images-unbindable` | The light image atlas or this light's cell is not ready |
| `lighting-parity-unproven`, `fog-blend-parity-unproven` | Consumable light whose domain is not proven (see above); this is the usual reason on stock maps |
| `light-descriptors-unavailable` | Draws exist but clustering produced no descriptors |

### Light grid (`RB_LightGridSurfaceModernRepresentable`)

| Reason | Trigger |
| --- | --- |
| `no-area-light-grid` | The surface's area has no light grid |
| `light-grid-atlas-missing` | The grid's atlas images are missing |
| `portal-blend-multi-grid` | The surface is near a portal that blends two grids |
| `light-grid-unrepresentable` | Default when no other reason applies |
| `light-grid-parity-unproven` | Representable, but light-grid parity is unproven and no baked-grid corridor covers the view |

### Shadows (`ModernVisibleShadowReceiversReady`)

`shadow-receivers-unrepresentable` has three triggers:

- a mapped or cache-reuse light without receiver sampling or an atlas slot;
- any stencil-fallback light, because stencil volumes cannot be drawn in modern
  frames;
- more than one consumable point light, because only one cube is supported
  until a cube-array pool exists.

`shadow-parity-unproven` applies when the material contract did not pass and
the shadow bit is clear. Unit, binding or atlas failures decline as
`modern-visible-shadow-fallback` without a blocker string.

### Frame-level statuses

`modern-visible-handoff-not-armed`, `modern-visible-unavailable`,
`modern-visible-legacy-blocked` (no blocker string, for example a frame with
only a stencil-shadow pass), `modern-visible-shadow-fallback` and
`modern-visible-resource-fallback`.

## Shared-domain corridors (REN-01)

These are transactional per view: a view rejected by a domain draws entirely
through the classic path. All default to 0 and are guarded in the
default-safety report.

| Cvar | Covers | Implementation |
| --- | --- | --- |
| `r_rendererSharedGui` | Fixed-function root 2D GUI views | `ClassicGuiDomain` |
| `r_rendererSharedInWorldGui` | GUI quads on 3D surfaces | `ClassicGuiDomain`, scene packets |
| `r_rendererSharedCinematicPost` | Root cinematic views and post-process range | `ClassicCinematicPostDomain` |
| `r_rendererSharedSpecialFrame` | Demo playback and Raven special effects | `ClassicSpecialFrameDomain` |
| `r_rendererSharedWorldAmbient` | Classic world ambient/material surfaces | `ClassicWorldAmbientDomain` |
| `r_rendererSharedWorldInteraction` | Fixed-classic lights, shadow casters, local/global/translucent receivers | `ClassicInteractionDomain` |
| `r_rendererSharedWorldFogBlend` | The whole fog/blend light phase | `ClassicFogBlendDomain` |
| `r_rendererSharedSubview` | Mirrors and capture-backed remote, mirror, reflection, refraction and x-ray views | `ClassicSubviewDomain` |
| `r_rendererSharedDeform` | Sealed CPU material-deform contract | `ClassicDeformDomain` (consumed by the GUI, ambient, interaction and fog domains) |

The shared interaction stream rolls back on shadow maps and translucent-moment
casters, custom lighting, deform, GPU skinning, special surfaces, depth hacks,
negative scale, flat diffuse, enhanced materials, cel shading, alternate
interaction programs, unsupported state, dynamic or missing resources, and pool
overflows (`ClassicInteractionDomain.h`).

## Promotion gate

`r_rendererModernAutoPromote` (default 0) and `r_rendererPromotionEvidence`
(free text) feed `RendererBootstrap_EvaluateDefaultPromotion`.

- **Eligibility** requires all of: `r_glTier auto`, `r_renderer` best or empty,
  a modern tier, the compatibility gates, the modern executor, the ARB2 legacy
  bridge, and all nine evidence tokens.
- **The tokens** are `phase8=complete`, `warnings=0`, `visual=pass`,
  `gameplay=pass`, `renderdoc=pass`, `perf=arb2-or-better`,
  `presentation=pass`, `rollback=pass` and `debug=off`.
- **The code checks only that the tokens are present.** Nothing in the engine
  verifies the claims behind them, so the evidence must come from qualification,
  not from setting the cvar.

## Progress

- **6 November 2026, classic cutouts.** An alpha-tested classic diffuse stage
  (flat bump, black specular) now joins the proven diffuse class
  (`R_MaterialResourceTable_ClassicDiffusePathEligible`). The modern forward
  alpha-test shader discards the texels ARB2's depth prepass rejects.
  - The laboratory pair `production-fixed-cutout` / `-native` (fixture
    material `classic_cutout`) shows a modern-owned frame with the material
    contract proven.
  - It matches native ARB2 to a mean of 0.28 and a maximum of 1.6 levels over
    the specimen disc. The opaque diffuse proof reads 0.30 and 2.4.
  - The exception is 5 of 6,073 pixels on hole edges, where the two programs
    evaluate the same threshold differently. The control allows at most 24
    such edge pixels.
  - **Bumped, specular cutouts** (the common stock shape) join the classic
    fixed class the same day. The classic-lighting corridor now also takes
    alpha-tested forward draws. It draws depth-equal against the alpha-tested
    depth prepass, as ARB2's interaction does, and the forward alpha-test
    shader reads the corridor's lighting like the opaque one.
    `production-fixed-cutout-bump` / `-native` (`classic_cutout_bump`) runs
    the corridor and matches native ARB2 to a mean of 0.25 and a maximum of
    0.6 levels, with the same 5 bounded edge pixels.
  - **Texture matrices and vertex colour** become the fallbacks that only the corridor
    may own (`R_MaterialResourceTable_ClassicCorridorPathEligible`, the
    `CorridorOnly` variant). Its interaction program applies the bump,
    diffuse and specular stage matrices as ARB2 does. Such a material now
    gets modern commands, but a view the corridor does not own still declines
    on the material fallback. Two exclusions apply:
    - no texture matrix on a cutout, because the forward pass samples the
      untransformed diffuse alpha;
    - no light grid, because the forward pass adds baked light from the
      untransformed diffuse.

    `production-fixed-texmatrix` / `-native` (a 3x scaled bump stage)
    matches native ARB2 to a mean of 0.25 and a maximum of 0.62 levels, and
    the scaled bump visibly changes the surface.
  - **Vertex colour** follows once the fixture was right. An interaction
    takes its vertex colour from the last diffuse or specular stage it
    processes, so a `vertexColor` diffuse followed by a plain `specularmap`
    renders untinted. That is the engine's own rule, which the first fixture
    tripped over. `classic_vertex` carries `vertexColor` on both stages of the
    vertex-coloured sphere. `production-fixed-vertex` / `-native` then shows
    the tint (native range 0-148 against 0-176 untinted) and matches native
    ARB2 to a mean of 0.25 and a maximum of 0.56 levels. Vertex colour is not
    accepted on cutouts or with a light grid, for the same reasons as texture
    matrices.
  - **Stage expressions and multiple stage sets** follow.
    - The `stageCondition` fallback (`mNumStageOps > 0`) counts every
      per-stage expression: conditions, colours and matrix expressions. The
      corridor builds its interactions through ARB2's own
      `RB_CreateSingleDrawInteractions`, so those expressions are evaluated
      identically, and the fallback joins the corridor-only set.
    - The classic fixed class now accepts more than one bump/diffuse/specular
      set. This class only ever counts inside the corridor's view.
    - `production-fixed-condition` uses a specular stage conditioned on
      `parm7 > 0.5`, left at 0, so ARB2 skips it (native range 0-62). The
      modern frame matches to a mean of 0.25 and a maximum of 0.55 levels.
    - `production-fixed-multistage` uses a bumped specular grey set followed
      by a flat dark set, and matches to a mean of 0.25 and a maximum of
      0.62 levels.
  - **Step 3, display-referred frames.** The classic-lighting corridor no
    longer needs the laboratory linear scene; it now needs only
    `r_rendererModernQuality`.
    - It accumulates classic light the way ARB2 writes its framebuffer, and
      the forward pass passes that through unchanged in a display frame. It
      decodes it only for the linear scene.
    - Each class above has a `production-fixed*-display` control
      (`r_pbrLinearScene 0`): flat, bumped, bumped cutout, vertex colour,
      texture matrix, condition and multi-stage. Each is a fully composed
      modern frame (`exec=1 composed=1`, the corridor executed), and each is
      byte-identical to native ARB2 over the specimen. The cutout keeps its 5
      bounded hole-edge pixels.
    - The baked-grid corridor no longer requires the linear scene either.
      The forward pass adds the baked term before its classic encode.
      `production-fixed-grid-display` is a composed modern frame within
      1 level of native ARB2 (mean 0.018).
    - The fog/blend corridor never required the linear scene; only this
      inventory said it did (`R_ModernGLExecutor_PrepareLinearFogBlend`
      checks modern quality and PBR materials). `production-fixed-fog-display`
      adds a fog light to the fixed scene. It is a composed modern frame with
      22 fog draws, byte-identical to native ARB2. In the linear scene, fog
      blends in linear radiance by design, so that mode is not compared with
      ARB2.
  - **Step 2, ambient lights.** ARB2 never binds its separate ambient
    program. An ambient light uses the ordinary interaction program, with the
    ambient normal cube where the normalization cube usually goes.
    - The corridor now does the same (`R_ModernGLExecutor_CollectClassicInteraction`)
      and no longer rejects ambient lights.
    - The proof accepts an ambient light where the corridor owns the classic
      surfaces. The forward pass alone still does not qualify the cube.
    - `production-fixed-ambient` adds a classic ambient light to the fixed
      scene (15 interactions instead of 12). It matches native ARB2 to a mean
      of 0.25 levels in the linear scene and byte for byte in a display frame
      (`-display`).

## What REN-02 has left, in dependency order

1. **Prove the interaction domain on stock content outside the laboratory.**
   Extend `MaterialLightingProven` beyond the flat-diffuse and fixed classes to
   the stock material shapes that dominate gameplay views:
   - alpha-tested surfaces (flat-diffuse and bumped/specular cutouts are done;
     cutouts in shipped modes wait on step 3);
   - vertex colours on cutouts and with light grids (plain vertex-coloured
     surfaces are done);
   - stage expressions and multi-stage sets on cutouts and with light grids
     (plain ones are done);
   - texture matrices on cutouts and with light grids (plain texture
     matrices are done);

   Each shape needs image-level parity evidence against ARB2. The classic-fixed
   class must also count outside the corridor view.
2. **Qualify ambient lights.** This covers the normalization cube, which today
   fails the whole proof as soon as any ambient light exists. Done for classic
   surfaces the corridor owns; ambient light on PBR surfaces is a PBR parity
   question outside this proof.
3. **Run the corridors in shipped modes.** Lift the `r_pbrLinearScene` and
   `r_hdrToneMap` requirement so the classic-lighting, baked-grid and fog/blend
   corridors also run in display-referred frames. Done for all three
   (classic lighting, baked grid, fog/blend), each matching native ARB2.
4. **Shadow receivers.** Represent stencil-fallback lights, which today decline
   the whole frame, and pool more than one point cube.
5. **Reconcile the two ownership models.** The aggregate executor and the
   REN-01 shared domains are mutually exclusive today. Coherent whole-frame
   ownership needs one owner per pass.
6. **Measure and promote.** Only after 1-5 can REN-02 measure CPU/GPU benefit
   against ARB2 and qualify the promotion tokens with real evidence, before any
   parity mask, auto-promotion or core-profile default changes.
