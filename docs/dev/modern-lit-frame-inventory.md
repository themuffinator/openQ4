# Modern lit-frame ownership inventory (REN-02)

This is the inventory that [REN-02](roadmap.md) starts from. It lists every
reason the aggregate OpenGL modern visible path (`ModernGLExecutor.cpp`) still
hands a lit frame back to the classic ARB2 light loop, and compares those
reasons with the shared corridors already implemented. It is a supporting
reference: scheduling stays in the roadmap. Taken from source on 6 November 2026;
Progress, the stock-view census and the ownership design were added on
23 November 2026. Function names are given instead of line numbers, which drift.

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
  - **Classic fixed:** one or more bump, diffuse and specular stage sets, with
    texture matrices, vertex colour and stage expressions, opaque or a cutout
    (see Progress). This class counts only inside the classic-lighting
    corridor view.

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
    one root view, no MSAA, no shadows, no ambient lights (since then:
    ambient lights, and lights ARB2 shadows with stencil volumes; a
    shadow-mapped light with casters still declines);
  - the baked light-grid corridor;
  - the linear fog/blend corridor.
- **The nine shared-domain corridors** listed below all default to 0. Turning on
  shared world ambient, shared world interaction or shared deform turns the
  aggregate modern visible path off (`R_ModernGLExecutor_ModernVisibleRequested`)
  unless `r_rendererOwnershipCompose` (default 0) asks the two ownership models
  to compose; see [One owner per pass](#one-owner-per-pass-step-5-design).

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
- a stencil-fallback light outside the view the classic-lighting corridor
  owns. The forward pass cannot draw stencil volumes; the corridor draws them
  for its own view (step 4, see Progress) and declines a view it cannot;
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
default-safety report. REN-01 qualified each of them individually on Windows
OpenGL/Vulkan on 13 November 2026, including under the default presets, where
they now draw into the same scene target as the classic walker
([evidence](renderer-validation-matrix.md#shared-classic-domain-qualification-ren-01)).

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

- **23 November 2026, step 1: stage state on cutouts and with baked light.**
  The depth and forward passes now sample a classic stage the way ARB2 does.
  - A per-draw uniform, `uClassicStage`, carries the diffuse and bump texture
    matrices and the vertex-colour mode as offsets from identity, so every
    other draw is unchanged. The depth prepass and the forward alpha test
    sample a cutout's diffuse alpha through the stage's matrix, and the
    forward pass samples baked light's albedo and normal through the diffuse
    and bump matrices, tinted by the stage's vertex colour.
  - The corridor checks each frame what those single samples can stand for
    (`R_ModernGLExecutor_ClassicStageForCommand`) and declines the rest:
    several active alpha-tested stages, an alpha-tested stage that is
    conditioned off or scales alpha, more than one baked albedo layer, or a
    stage whose image is not the one the draw binds. ARB2's light-grid pass
    reports its layers through `RB_LightGridClassicAlbedoStages`, which walks
    the stages exactly as that pass does.
  - Corridor-only materials are now declined with baked light only when the
    surface really receives it. The old rule read the `r_useLightGrid` cvar,
    which defaults on, although stock maps ship no light grids.
  - Rollback: `r_rendererModernClassicShapes` bit 1. Clearing it restores the
    old exclusions, and such a frame goes back to ARB2
    (`production-fixed-stages-fallback`: the frame declines and the specimen
    is pixel-identical to native).
  - Cutout controls (bumped, specular cutouts; holes moved by a translated,
    2x scaled diffuse stage; vertex colour; a conditioned specular stage):

    | Control | Linear frame, mean / max levels | Hole-edge pixels | Display frame |
    | --- | --- | --- | --- |
    | `production-fixed-cutout-texmatrix` | 0.252 / 0.58 | 3 of 6,073 | byte-identical |
    | `production-fixed-cutout-vertex` | 0.250 / 0.56 | 5 of 6,073 | byte-identical |
    | `production-fixed-cutout-condition` | 0.248 / 0.55 | 5 of 6,073 | byte-identical |

    The display frames are byte-identical to native ARB2 outside the same
    hole-edge pixels (limit 24). The edge test now also takes a single
    channel that steps by more than 10 levels: a dim, tinted surface over a
    grey background of similar brightness is a hole edge the summed-level
    test missed. Every differing pixel in these controls shows the background
    in the native frame and the surface in the modern one.
  - Baked-light controls (the laboratory's baked grid at 0.2 intensity): a
    3x scaled bump stage, vertex colour, a conditioned specular stage, and
    `classic_grid_stage`, whose diffuse stage is a 2x scaled checker tinted
    by a stage expression under a 3x scaled bump.

    | Control | Linear frame, mean / max levels | Display frame, mean / max levels |
    | --- | --- | --- |
    | `production-fixed-grid` (flat bump, as before) | 0.253 / 0.65 | 0.019 / 1 |
    | `production-fixed-grid-condition` | 0.394 / 2.48 | 0.263 / 2 |
    | `production-fixed-grid-texmatrix` | 0.380 / 2.58 | 0.243 / 3 |
    | `production-fixed-grid-vertex` | 0.309 / 2.01 | 0.138 / 2 |
    | `production-fixed-grid-stage` | 0.264 / 1.76 | 0.080 / 2 |

    Each is a composed modern frame inside the controls' limits (mean 1,
    maximum 3). At this intensity, though, the baked term itself is only
    1.7 levels on average and 3 to 4 at most, the size of the tolerance, so
    these five bound an error without proving the stage state. Three
    controls repeat the test at ten times the intensity (baked term mean
    15.7, maximum 32 levels) over a flat bump:

    | Control (10x grid intensity, flat bump) | Linear frame, mean / max levels | Display frame, mean / max levels |
    | --- | --- | --- |
    | `production-fixed-grid-strong` (plain baseline) | 0.253 / 0.65 | 0.020 / 1 |
    | `production-fixed-grid-flat-stage` (2x scaled checker, stage-expression colour) | 0.183 / 0.56 | 0.006 / 1 |
    | `production-fixed-grid-flat-vertex` (vertex colour) | 0.247 / 0.55 | 0.008 / 1 |

    A wrong diffuse matrix, colour or tint would cost several levels there,
    so the diffuse stage state is proven. Two limits remain:
    - **Bump-mapped baked light is looser than flat.** The conditioned
      control has identity matrices and no vertex colour, yet differs by up
      to 2.5 levels of a 4-level term where the flat control stays within 1.
      The cause was not investigated; the forward pass builds its own
      tangent basis for the baked term, which is the likely place. This step
      does not cause it: with identity matrices the new uniform is zero and
      the shader's arithmetic is unchanged. It would grow with intensity,
      and the scaled-bump display frame already sits at the 3-level limit.
      Stock maps ship no light grids, so nothing depends on it today.
    - **The bump matrix is weakly tested** for the same reason: a wrong bump
      coordinate would cost little more than the bump-mapped baseline does.
  - `production-fixed-grid-multistage-fallback` (two stage sets with baked
    light) declines, and its specimen is pixel-identical to native.
  - **The old shadow-fallback pair compared too much.**
    `production-fixed-shadow-fallback` was a whole-image exact pair against a
    PBR-off reference. It fails on this build across the PBR stations: a
    declined frame is drawn by the classic light loop, whose native PBR owner
    shades them, while the reference shows their classic stand-ins. The
    classic room and the specimen are byte-identical. The pair now compares
    the specimen pixel for pixel, as the new declined controls do. The
    failure was not reproduced on an unmodified build.

- **23 November 2026, step 4: stencil-shadowed lights.** A light that ARB2
  shadows with stencil volumes no longer declines the frame when the
  classic-lighting corridor owns its view.
  - The corridor groups its interactions by light and walks them in ARB2's
    order: clear the stencil inside the light's scissor, global volumes,
    local receivers, local volumes, global receivers. The volumes are drawn by
    ARB2's own stencil pass (`RB_ARB2_StencilShadowPassForLight`) into the
    corridor's depth-stencil, so they rasterize exactly as they do natively;
    the interactions then test the stencil as ARB2's do.
  - A light counts as stencil-shadowed only where ARB2 chooses stencil by
    policy, never by a resource or budget decision
    (`RB_ARB2_LightShadowsByStencilPolicy`): shadow maps off, a light that
    cannot map, or a point light with `r_shadowMapPointLights 0`, which is
    how the Enhanced preset runs on OpenGL. The view declines when a light
    with casters is shadow-mapped, or when a stencil-shadowed light also
    lights a PBR surface, which the forward pass would leave unshadowed.
  - The ownership gates follow: stencil-shadow packets of the corridor's view
    need no modern geometry, the per-light shadow gate accepts the corridor's
    lights, the clustered passes no longer count them as unshadowed, and the
    stencil-shadow pass is marked owned (`classic-corridor-stencil-volumes`).
  - Rollback: `r_rendererModernClassicShapes` bit 2.
  - Controls. The laboratory's PBR stations are lit by the same lights, so
    these controls remove them and run in their own batch, with a second
    sphere casting across the specimen from the key light. Their native
    reference turns the modern visible path off: without the stations,
    switching PBR off no longer makes that path decline, and a reference it
    drew would prove nothing. Every fixed native reference now fails if the
    modern path drew it.

    | Control | Result |
    | --- | --- |
    | `production-fixed-stencil` | Owned (`exec=1 composed=1`, 4 lights, 2 stencil-shadowed). Linear frame mean 0.251 / max 0.58 levels against native ARB2 |
    | `production-fixed-stencil-display` | Owned. Specimen byte-identical; over the whole 1280x800 frame 3 pixels differ, by 1 level |
    | `production-fixed-stencil-off-fallback` | Bit 2 cleared: the frame declines and is byte-identical to native ARB2 over the whole frame |

- **23 November 2026, step 5: the two ownership models compose.**
  `r_rendererOwnershipCompose` applies the rule described under
  [One owner per pass](#one-owner-per-pass-step-5-design).
  - `production-fixed-compose` requests the shared world-ambient, interaction
    and fog/blend domains together with the aggregate executor. The aggregate
    executor composes the frame (`exec=1 composed=1`), the shared interaction
    domain seals the same view beside it (`views=1 ready=1 fallback=0`) and is
    not drawn. Linear frame mean 0.251 / max 0.62 levels against native ARB2;
    the display frame's specimen is byte-identical, with 3 pixels of the whole
    frame differing by 1 level.
  - `production-fixed-stencil-compose-fallback` clears the stencil bit, so the
    aggregate executor declines (`exec=0 blocked=1`). The shared interaction
    stream then draws the view, stencil volumes included (`mode=stencil`,
    6 volumes), byte-identical to native ARB2 over the whole frame.
  - The shared interaction stream's state restore unbound one texture unit
    past the legacy bind tracker and warned on every draw
    (`idImageManager::BindNull: invalid tracked texture unit 8`); the loop now
    stops at the tracker's last unit, which is all it ever cleared.

- **23 November 2026, qualification.** Windows, OpenGL `gl45`, RTX 4060
  Laptop GPU, on a machine shared with other builds and clients.
  `tools/tests/renderer_pbr_laboratory.py --batch --linear` passes 84
  controls in three batches on the final build: 46 fixed controls (every
  control that existed before this round plus the cutout controls), 29
  light-grid controls and 9 isolated stencil and compose controls. Nine
  renderer self-tests pass through `renderer_validation_matrix.py`
  (default safety, default promotion, modern visible, modern compatibility,
  compatibility gates, forward+, shadow planner, PBR visible, visible depth).
  Editing the shared modern vertex shader makes the driver compile every
  modern program cold, about nine minutes per start on this machine; run the
  first batch after such an edit with `--timeout 3000`.

## Stock views (step 6)

Step 6 measures CPU and GPU benefit against ARB2 on stock gameplay views once
frames are owned. **No stock view is owned yet**, so there is no benefit to
measure. What exists instead is a census of why.

The corridor now names its first reason for refusing a view in the
`Modern classic lighting:` line of `gfxInfo` (`decline=`):

| Token | The corridor refused because |
| --- | --- |
| `no-submit-plan`, `modern-quality-off`, `gbuffer-pipeline`, `msaa`, `no-scissor`, `enhanced-or-cel`, `program` | a frame-level precondition failed |
| `skinned-or-deformed-geometry:<material>` | a classic receiver is skinned or deformed |
| `material-shape:<material>` | a classic forward material is outside the corridor's class |
| `depth-hack`, `negative-scale`, `second-view`, `viewport` | the draw or the frame's views are outside its single full-size root view |
| `cutout-stage`, `baked-light-stage`, `rollback-stages` | step 1's single-stage sampling cannot stand for the material, or bit 1 is cleared |
| `no-classic-fixed-surface` | nothing needs the corridor (not a refusal: the forward pass owns the diffuse class) |
| `overbright` | the light scale needs an overbright pass |
| `shadow-mapped-light`, `rollback-stencil`, `pbr-under-stencil-light` | step 4's light rules |
| `translucent-interaction`, `interaction-without-command`, `interaction-resource` | a lit surface is translucent, has no forward command, or an interaction image is not a loaded 2D texture |
| `target` | the accumulation target could not be made |

Census of 23 November 2026: `game/airdefense1`, the eight outdoor poses of the
shadow tour, 1280x800, modern path requested with the laboratory's executor
cvars, `r_shadows 0`, no view weapon or HUD, game time stopped. No frame was
owned (`exec=0` at all eight).

| Poses | Corridor's reason | Frame's first blocker |
| --- | --- | --- |
| 4 | `skinned-or-deformed-geometry` (marine heads and bodies) | `stageColor` on `models/characters/marine/body` |
| 3 | `interaction-without-command` on `models/vehicles/hovertank/nht_damaged` | `stageCondition` on the same wreck or on `models/monsters/harvester/harv` |
| 1 | `interaction-without-command` on `textures/common_lights/strip_light1` | `stageCondition` on a Strogg door |

**What requesting the path costs today.** The same eight poses, 240 frames
each, `rendererBenchmarkCapture` frame averages in whole milliseconds:

| Run | Frame average per pose, ms | Mean |
| --- | --- | --- |
| ARB2 (executor cvars off) | 5, 5, 6, 3, 5, 5, 7, 3 | 4.9 |
| Modern path requested, every frame declined | 9, 8, 6, 10, 9, 7, 7, 11 | 8.4 |

These are shared-machine numbers, taken while other sessions built and ran
clients and this session's renderer self-tests were running; they are not
budget evidence. The capture's GPU timer total read 0 in both runs, so there
is no GPU figure. They measure a cost, not a benefit: a declined frame pays
for the executor's packets and submit plan (`submit` 5 to 9 ms against 2 to
5) and is then drawn by ARB2. An owned stock frame, which is what step 6
needs, does not exist yet.

Read the census with care: each line names only the first reason met, so a view that
gets past it may meet another. What the census does show is that the next
stock work is not shadows or light grids. It is skinned receivers, lit
surfaces whose ambient draw is not a forward opaque or cutout command, and
the `stageColor` and `stageCondition` shapes on those models, which were not
examined further.

## One owner per pass (step 5 design)

Two ownership models grew side by side:

- **The aggregate executor** (`ModernGLExecutor.cpp`) owns a whole frame. It
  runs before the classic walker, composes the frame or declines it, and
  records the outcome in a per-pass ownership table. The classic walker
  (`RB_STD_DrawView`) asks that table before each pass
  (`R_ModernGLExecutor_LegacyPassCanSkipForView`).
- **The REN-01 shared domains** own one pass of one view. Each seals a
  backend-neutral stream when the frame's scene packets are built
  (`R_Classic*Domain_PrepareFrame`), and the classic walker draws that stream
  in place of its own pass, or rolls the view back to ARB2.

Until step 5 they excluded each other: a shared world domain turned the
aggregate path off (`R_ModernGLExecutor_ModernVisibleRequested`), and the pass
table refused to skip a pass a shared domain might own.

**The rule.** Every pass of every view has exactly one owner, chosen in a
fixed order:

1. the aggregate executor, when it composed the frame and its table marks the
   pass modern;
2. otherwise the shared domain whose sealed stream is ready for that view;
3. otherwise ARB2.

The order needs no new arbitration, because it is the order the code already
runs in. The aggregate executor's decision is final before the walker starts,
so a shared domain's rollback is never suppressed half-way: either the walker
never reaches the pass, or it reaches it with the aggregate executor out of
the frame and decides exactly as it does with the executor off. A stream that
is sealed and then not drawn is a no-op, not a fallback: sealing publishes
data and never draws.

**What is implemented.** `r_rendererOwnershipCompose` (default 0, listed in the
default-safety report) applies the rule. With it off, the two models exclude
each other as before, which is the named rollback. With it on, a shared world
domain no longer turns the aggregate path off, and the pass table skips a pass
the aggregate executor owns even when a shared domain is enabled for it. The
laboratory controls are listed under Progress.

**What the rule leaves for later.** The corridors inside the aggregate
executor still derive their own work. The classic-lighting corridor builds
interactions with `RB_CreateSingleDrawInteractions` and draws stencil volumes
through ARB2's stencil pass, while `ClassicInteractionDomain` seals the same
lights, shadow casters and receivers for the classic walker and for Vulkan.
The fog/blend corridor already shows the intended shape: it draws the stream
`ClassicFogBlendDomain` sealed (`RB_ClassicFogBlend_DrawLinearView`). The end
state is the aggregate executor as a third consumer of each sealed stream,
beside the OpenGL walker and Vulkan:

| Pass | Sealed stream | Aggregate consumer today |
| --- | --- | --- |
| Fog/blend | `ClassicFogBlendDomain` | Consumes the stream |
| Interaction, stencil shadow, shadow map | `ClassicInteractionDomain` | Derives its own (corridor) |
| Ambient | `ClassicWorldAmbientDomain` | Derives its own (forward pass) |
| Deform | `ClassicDeformDomain` | Declines (`unsupportedDeform`) |
| GUI, in-world GUI | `ClassicGuiDomain` | Derives its own GUI replay |
| Subview, cinematic post, special frame | their domains | Legacy sidecar views |

Moving the corridor onto the interaction stream would also give it what the
stream already seals and the corridor declines today: shadow-mapped lights and
translucent receivers.

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

   Done on 23 November for the cutout and baked-light combinations of one
   stage set (see Progress). Still open:
   - several stage sets with baked light, and several alpha-tested stages:
     ARB2 draws one baked layer or one coverage pass per stage, the forward
     and depth passes one per surface, so these decline by name;
   - the class outside the corridor's view. The corridor still owns exactly
     one root view of the frame, so a frame with a second 3D view that is not
     a legacy sidecar declines.
2. **Qualify ambient lights.** This covers the normalization cube, which today
   fails the whole proof as soon as any ambient light exists. Done for classic
   surfaces the corridor owns; ambient light on PBR surfaces is a PBR parity
   question outside this proof.
3. **Run the corridors in shipped modes.** Lift the `r_pbrLinearScene` and
   `r_hdrToneMap` requirement so the classic-lighting, baked-grid and fog/blend
   corridors also run in display-referred frames. Done for all three
   (classic lighting, baked grid, fog/blend), each matching native ARB2.
4. **Shadow receivers.** Represent stencil-fallback lights, which today decline
   the whole frame, and pool more than one point cube. Stencil-fallback lights
   are represented for classic receivers in the corridor's view (23 November,
   see Progress). Still open:
   - a stencil-shadowed light that also lights a PBR surface, because the
     forward pass evaluates every light in one draw and cannot apply one
     light's stencil;
   - shadow-mapped lights on corridor-owned classic surfaces. The corridor
     would have to sample ARB2's maps with ARB2's kernels, or take the lights
     from the sealed interaction stream (step 5);
   - the point-cube pool for the forward pass. Not started.
5. **Reconcile the two ownership models.** The aggregate executor and the
   REN-01 shared domains are mutually exclusive today. Coherent whole-frame
   ownership needs one owner per pass. The rule and its opt-in switch are in
   place (23 November, [One owner per pass](#one-owner-per-pass-step-5-design)).
   Still open: the aggregate executor consuming the sealed interaction,
   ambient, deform and GUI streams instead of deriving its own work.
6. **Measure and promote.** Only after 1-5 can REN-02 measure CPU/GPU benefit
   against ARB2 and qualify the promotion tokens with real evidence, before any
   parity mask, auto-promotion or core-profile default changes. Not reached:
   no stock view is owned ([census](#stock-views-step-6)). The stock shapes
   that census names come first.
