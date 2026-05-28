# PBR Material Support Plan

## Purpose

Add physically based material support to OpenQ4 without changing how shipped Quake 4 assets render by default. PBR must be an opt-in extension for new OpenQ4 materials and future replacement-content work, while the existing `bumpmap`, `diffusemap`, `specularmap`, ambient-stage, GUI, post-process, light, fog, BSE, and ARB2 compatibility contracts remain intact.

The short version: stock materials stay stock; PBR materials get a modern material model; every unsupported case has a visible fallback reason and a legacy rendering path.

## Suitability Review

This plan is suitable for OpenQ4 only if legacy fallback is treated as a first-class authoring contract, not as a nice-to-have conversion step. OpenQ4's shipped-content compatibility still comes from the classic material parser and the `SL_BUMP`/`SL_DIFFUSE`/`SL_SPECULAR` interaction model, so PBR metadata must never be the only runtime description for a material that can ship in `baseoq4/`.

Required adjustments:

- Use a namespaced `pbr { ... }` block as the canonical syntax. This adds one top-level parser entry point instead of many new general material keywords and keeps classic stage parsing easy to audit.
- Prefer dual-authored materials: classic `bumpmap`, `diffusemap`, and `specularmap` stages remain the seamless fallback for ARB2, legacy GL tiers, PBR-disabled runs, and any modern fail-closed case.
- Treat generated fallback as development convenience only. A generated fallback may keep a test material visible, but release validation should fail if shipped PBR materials depend on approximate generated classic stages.
- Do not promise that older Quake 4 or pre-PBR OpenQ4 binaries will ignore new PBR tokens. They generally will not. Backward compatibility for old binaries requires separate legacy material declarations or content overlays, not mixed new syntax.
- Keep explicit fallback ownership independent of PBR rendering cvars. A user disabling PBR must still get the authored classic material, not a missing/default material.

## Current Baseline

OpenQ4 currently has two material/rendering worlds that need to stay in sync:

- The legacy material parser in `src/renderer/Material.cpp` compiles classic stages into `SL_BUMP`, `SL_DIFFUSE`, `SL_SPECULAR`, and `SL_AMBIENT`. It adds implicit `_flat` and `_white` stages when an interaction would otherwise be incomplete, then sorts interaction stages into the order expected by the classic renderer.
- The classic interaction path in `src/renderer/tr_render.cpp` and `src/renderer/draw_arb2.cpp` decomposes a surface/light pair into one or more `drawInteraction_t` records with bump, diffuse, and specular images. This is the compatibility authority for shipped assets.
- The modern renderer already has a bridge through `ScenePackets`, `MaterialResourceTable`, `ModernGLShaderLibrary`, G-buffer, deferred-lite, forward+, and guarded visible-frame paths. These systems are default-off or promotion-gated and already expose fallback metrics.
- `r_enhancedMaterials` is an opt-in GLSL enhancement for existing classic materials. It is not PBR and should not become the PBR switch.

The design should use the modern material bridge instead of replacing the classic parser contract.

## Compatibility Invariants

- Existing material declarations must parse to the same classic stage list unless they use new PBR-specific tokens.
- Existing stock materials must not be reinterpreted as PBR by default.
- `r_renderer arb2`, `r_glTier legacy`, and default conservative startup must remain valid rollback paths.
- A PBR material intended for shipped content must include authored classic fallback stages or explicit legacy fallback maps. Generated approximation stages are acceptable for tests and local authoring previews, but they are not considered seamless enough for release assets.
- New PBR parser tokens are OpenQ4 material syntax, not legacy Quake 4 syntax. Do not require old binaries to ignore them; if old-binary compatibility is ever needed, ship a separate legacy declaration or content overlay.
- In OpenQ4, unknown tokens should still default the material as they do today, so real typos remain visible. The PBR parser should recognize only the `pbr`/`physicallyBased` block entry point at the top level, then validate all PBR tokens inside that block.
- No repo `q4base/` or replacement-asset dependency may be introduced to make stock maps work.
- All PBR features must fail closed in modern rendering: unsupported texture layout, shader tier, dynamic image, custom program, or material feature means fallback to classic material ownership, not partial lighting.

## Material Authoring Model

Add a top-level PBR metadata block to the material language. The preferred OpenQ4 authoring shape is dual-authored: classic stages first for the legacy interaction path, then a `pbr { ... }` block for modern PBR ownership.

```text
materials/example/pbr_panel
{
    bumpmap textures/example/panel_local
    diffusemap textures/example/panel_d
    specularmap textures/example/panel_s

    pbr {
        workflow metallicRoughness

        albedoMap textures/example/panel_albedo
        normalMap textures/example/panel_normal
        normalFormat tangentRG
        ormMap textures/example/panel_orm
        emissiveMap textures/example/panel_emit

        metallic 0.0
        roughness 0.55
        normalScale 1.0
    }
}
```

PBR-only materials are allowed during bring-up, but they still need a classic fallback before they are treated as shippable:

```text
materials/example/pbr_panel_preview
{
    pbr {
        workflow metallicRoughness
        albedoMap textures/example/panel_albedo
        normalMap textures/example/panel_normal
        normalFormat tangentRG
        ormMap textures/example/panel_orm

        legacyBumpMap textures/example/panel_local
        legacyDiffuseMap textures/example/panel_d
        legacySpecularMap textures/example/panel_s
    }
}
```

Recommended initial tokens:

| Token | Meaning | Notes |
|---|---|---|
| `pbr { ... }` or `physicallyBased { ... }` | Starts PBR metadata for this material | Does not alter classic stage parsing by itself. Bare flags are not the canonical form. |
| `workflow metallicRoughness` | Uses metallic/roughness BRDF inputs | Initial supported workflow. |
| `workflow specularGlossiness` | Future compatibility path | Parse can record it before rendering supports it. |
| `albedoMap <imageProgram>` | Base color texture | Color data; sampled as albedo in PBR shaders. |
| `normalMap <imageProgram>` | Tangent-space normal texture | Must declare or infer normal encoding. |
| `normalFormat quake4AGB | tangentRG | tangentXYZ` | Normal-channel convention | Required for new `normalMap` authoring. `quake4AGB` may be inferred only when the PBR normal deliberately reuses a classic `bumpmap` image. |
| `metallicMap <imageProgram>` | Metallic data texture | Linear data. |
| `roughnessMap <imageProgram>` | Roughness data texture | Linear data. |
| `ormMap <imageProgram>` | Packed occlusion/roughness/metallic map | glTF-compatible: R = AO, G = roughness, B = metallic. |
| `aoMap <imageProgram>` | Ambient occlusion data texture | Multiplies indirect/ambient only. |
| `emissiveMap <imageProgram>` | Emissive color texture | Optional. |
| `metallic <expr>` | Scalar fallback or multiplier | Material expression register. |
| `roughness <expr>` | Scalar fallback or multiplier | Clamp to `[0.02, 1.0]` in shaders. |
| `ao <expr>` | Scalar fallback | Default `1.0`. |
| `emissiveColor <expr> <expr> <expr>` | Emissive multiplier | Default black. |
| `normalScale <expr>` | Normal XY scale | Default `1.0`. |
| `legacyBumpMap <imageProgram>` | Optional explicit ARB2 fallback bump | Used only when no authored classic `bumpmap` stage exists. |
| `legacyDiffuseMap <imageProgram>` | Optional explicit ARB2 fallback diffuse | Used only when no authored classic `diffusemap` stage exists. |
| `legacySpecularMap <imageProgram>` | Optional explicit ARB2 fallback specular | Avoids poor generated approximations. |
| `legacyEmissiveMap <imageProgram>` | Optional explicit ambient fallback | Generates an ambient/additive stage only when the material has no authored equivalent. |
| `autoLegacyFallback 0 | 1` | Allows generated preview fallback | Defaults to `1` for local development, but release validation should require explicit fallback for shipped content. |

Do not add `metalmap` or `roughmap` shorthand in the first pass. PBR materials will be rare at first, and clear names are worth the extra typing.

## Data Model

Add a compact PBR metadata record to `idMaterial` rather than encoding PBR as extra classic stages:

```cpp
enum pbrWorkflow_t {
    PBR_WORKFLOW_NONE = 0,
    PBR_WORKFLOW_METALLIC_ROUGHNESS,
    PBR_WORKFLOW_SPECULAR_GLOSSINESS
};

enum pbrNormalFormat_t {
    PBR_NORMAL_QUAKE4_AGB = 0,
    PBR_NORMAL_TANGENT_RG,
    PBR_NORMAL_TANGENT_XYZ
};

struct pbrMaterialStage_t {
    textureStage_t texture;
    int colorRegisters[4];
    int scalarRegister;
    bool present;
};

struct pbrMaterialInfo_t {
    bool enabled;
    pbrWorkflow_t workflow;
    pbrNormalFormat_t normalFormat;
    pbrMaterialStage_t albedo;
    pbrMaterialStage_t normal;
    pbrMaterialStage_t orm;
    pbrMaterialStage_t metallic;
    pbrMaterialStage_t roughness;
    pbrMaterialStage_t ao;
    pbrMaterialStage_t emissive;
    pbrMaterialStage_t legacyBump;
    pbrMaterialStage_t legacyDiffuse;
    pbrMaterialStage_t legacySpecular;
    pbrMaterialStage_t legacyEmissive;
    int metallicRegister;
    int roughnessRegister;
    int aoRegister;
    int normalScaleRegister;
    int emissiveColorRegisters[3];
    bool autoLegacyFallback;
    bool hasExplicitLegacyFallback;
    bool hasAuthoredClassicFallback;
    bool usesGeneratedLegacyFallback;
    bool usesApproximateLegacyFallback;
};
```

Implementation details:

- Store PBR image references outside `stages[]` so `SortInteractionStages`, `AddImplicitStages`, coverage, classic lighting, and GUI/post behavior remain unchanged.
- Initialize and clear the PBR record in `CommonInit`/`FreeData`, then copy it out of parser-temporary state before `pd` is cleared. PBR data must not point into stack-owned parsing storage.
- Add const getters such as `HasPBR()`, `GetPBRInfo()`, and targeted image/scalar helpers. Avoid exposing mutable parser internals.
- Update `ReloadImages`, `AddReference`, `SetImageClassifications`, `FreeData`, `Size`, `Print`, and material validation for the PBR images.
- Keep material expressions as the single scalar system. PBR scalar tokens should call existing expression parsing and use `shaderRegisters` at draw time.
- Add a new texture usage such as `TD_MATERIAL_DATA` for ORM, metallic, roughness, and AO. Do not load these as `TD_SPECULAR`, because that path may modify alpha and compression behavior for classic specular maps.
- Add a color-input usage or explicit PBR image flag for albedo/emissive if the final sRGB policy needs them to bypass the legacy `TD_DIFFUSE` cache identity. Do not let PBR albedo change how stock diffuse maps are cached.

## Parser Strategy

Phase the parser work in carefully:

1. Add a no-op parse path for top-level `pbr { ... }`/`physicallyBased { ... }` that records metadata and reports through `Print`.
2. Keep PBR keywords invalid outside the block. A stray top-level `workflow`, `albedoMap`, or `roughness` should still default the material, matching the current typo-visible parser contract.
3. Add image-token parsing through a shared helper that mirrors `ParseStage` image-option handling where needed: `nearest`, `linear`, `clamp`, `noclamp`, `zeroclamp`, `highquality`, `forceHighQuality`, `nopicmip`, and `nomips`.
4. Do not accept dynamic render maps, video maps, arbitrary `program`/`glslProgram`, or material-stage draw-state tokens inside the PBR block in the first pass. Those features must stay on authored classic stages until explicit modern support exists.
5. Load albedo as color data, normal as bump/normal data, and ORM/metallic/roughness/AO as linear material data.
6. Warn, but do not default, when a PBR material is missing optional maps and has valid scalar fallback values.
7. Default only true authoring errors: missing image program after a PBR image keyword, bad workflow enum, bad normal format enum, invalid packed-map channel specification, malformed scalar expression, duplicate mutually exclusive maps, or unclosed `pbr` block.

The existing classic shortcuts (`diffusemap`, `specularmap`, `bumpmap`) should remain untouched. If a material declares both PBR metadata and classic stages, the classic stages are authoritative for legacy rendering, and the PBR metadata is authoritative only for modern PBR-capable passes.

## Legacy Fallback

PBR must not require the modern renderer to be usable.

Fallback policy:

- If a PBR material already declares classic `bumpmap`, `diffusemap`, and optional `specularmap` stages, keep them and use them for ARB2. This is the preferred release path because it preserves the existing interaction contract exactly.
- If a PBR material declares `legacyBumpMap`, `legacyDiffuseMap`, `legacySpecularMap`, or `legacyEmissiveMap`, generate classic fallback stages from those explicit maps only for missing classic stage types.
- If no explicit classic fallback exists, synthesize an approximation after parsing only when `autoLegacyFallback 1` is in effect:
  - `normalMap` with `normalFormat quake4AGB`, `legacyBumpMap`, or `_flat` -> `SL_BUMP`
  - `legacyDiffuseMap`, `albedoMap`, or `_white` -> `SL_DIFFUSE`
  - `legacySpecularMap`, a constant low-intensity `_white` specular stage if scalar data can be represented safely, or `_black` -> `SL_SPECULAR`
- Do not generate a roughness-derived texture in the first pass. That would introduce a new image-generation/cache path and can be added later if it is proven necessary. Prefer explicit `legacySpecularMap` for assets that need close visual parity.
- Preserve material behavior that affects the legacy path: `coverage`, alpha-test, cull type, sort, polygon offset, `noShadows`/`forceShadows`, `surfaceParm` effects, and editor image selection must come from authored classic stages or explicit fallback metadata, not from PBR guesses.
- Emit a one-line material warning when fallback is generated or approximate, so authors know to provide better classic fallback maps.
- Add metrics for authored classic fallback, explicit generated fallback, approximate fallback, and fallback missing. Release validation should require zero approximate fallback for shipped `content/baseoq4/` PBR materials unless the project explicitly accepts a documented exception.

Generated fallback stages must use the same `ParseStage` machinery as the existing top-level `bumpmap`/`diffusemap`/`specularmap` shortcuts and implicit `_flat`/`_white` stages so draw-state, texture repeat, register, and image-loading behavior stays conventional. Run this before final `AddImplicitStages()`/`SortInteractionStages()` so the existing interaction cleanup still owns ordering and missing-stage completion.

## Material Resource Table

Extend `MaterialResourceTable` and packet material records to carry PBR semantics:

New texture semantics:

- `MATERIAL_RESOURCE_TEXTURE_ALBEDO`
- `MATERIAL_RESOURCE_TEXTURE_NORMAL`
- `MATERIAL_RESOURCE_TEXTURE_ORM`
- `MATERIAL_RESOURCE_TEXTURE_METALLIC`
- `MATERIAL_RESOURCE_TEXTURE_ROUGHNESS`
- `MATERIAL_RESOURCE_TEXTURE_AO`
- `MATERIAL_RESOURCE_TEXTURE_EMISSIVE_PBR`

Record additions:

- `bool hasPBR`
- `bool pbrModernReady`
- `int pbrWorkflow`
- `int pbrNormalFormat`
- `bool hasAlbedo`, `hasNormal`, `hasORM`, `hasMetallic`, `hasRoughness`, `hasAO`
- `bool hasAuthoredClassicFallback`, `hasExplicitLegacyFallback`, `usesGeneratedLegacyFallback`, `usesApproximateLegacyFallback`
- register indices for metallic, roughness, AO, normal scale, emissive color
- per-record fallback reason for PBR-specific unsupported features

Implementation notes:

- Increase `MATERIAL_RESOURCE_TABLE_MAX_TEXTURE_BINDINGS` only if self-tests prove the GL 3.3 path can still bind required classic slots without exceeding limits. Prefer packed `ormMap` over separate maps on GL 3.3.
- Preserve the existing classic `BUMP`, `DIFFUSE`, `SPECULAR`, and `EMISSIVE` semantics for non-PBR materials.
- Add metrics: PBR record count, PBR-ready record count, missing albedo/normal/ORM counts, separate-map count, packed-map count, authored-legacy-fallback count, explicit-generated-fallback count, approximate-fallback count, modern-PBR fallback count, and old-style classic record count.
- Add `rendererMaterialResourceTableSelfTest` cases for PBR packed ORM, PBR separate maps, scalar-only fallback, explicit legacy fallback, and unsupported workflow fallback.
- Keep the existing record `fallbackReason` meaningful for classic modern ownership. Add a separate PBR fallback reason/flag field instead of overloading classic fallback reasons in a way that would make stock material metrics harder to compare.

## Shader And Render Pipeline

Add PBR as new shader families, not as a mutation of existing legacy families.

Initial modern shader families:

- `GBufferPBR` opaque
- `GBufferPBRAlphaTest`
- `DeferredResolvePBR`
- `ForwardPlusPBR`
- `ForwardPlusPBRAlphaTest`
- `TransparentPBR` only after opaque/perforated support is stable

Keep existing legacy G-buffer/forward variants for classic materials.

Recommended G-buffer packing:

| Attachment | Format | PBR payload |
|---|---|---|
| `gbufferAlbedo` | RGBA8 or sRGB-aware equivalent after policy is decided | Linearized albedo RGB, alpha |
| `gbufferNormal` | Existing packed normal target initially | View-space normal, tangent sign/debug as today |
| `gbufferMaterial` | RGBA8 initially | R = metallic, G = roughness, B = AO, A = flags or specular fallback |
| `gbufferEmissive` | Existing RGBA16F where available, RGBA8 fallback | Emissive/light-grid payload |

BRDF requirements:

- Use metallic/roughness Cook-Torrance shading with GGX normal distribution, Smith visibility, and Schlick Fresnel.
- Clamp roughness to a small nonzero floor to avoid unstable highlights.
- Keep direct light inputs compatible with Quake 4 light falloff/projection images and `backEnd.lightScale` behavior.
- Apply AO to ambient/light-grid/indirect contribution, not direct light.
- Treat emissive as additive material output after direct lighting.
- Keep all math in the modern PBR path linear. Do not change the legacy gamma-ish path for classic materials in the same patch.

Color-space policy must be explicit before visible promotion:

- Albedo and emissive are color inputs and need a documented decode path.
- Normal, ORM, metallic, roughness, and AO are data inputs and must not be gamma-decoded.
- If the engine does not yet have robust sRGB texture state, decode albedo/emissive in shader under PBR-only code paths and document that as the initial policy.

## Runtime Controls

Add separate PBR controls instead of overloading enhanced materials:

| Cvar | Default | Purpose |
|---|---:|---|
| `r_pbrMaterials` | `0` initially | Allows modern PBR shader ownership for PBR-authored materials when the modern renderer gates also pass. |
| `r_pbrGeneratedLegacyFallback` | `1` | Allows approximate generated classic fallback stages for development/test PBR materials. Authored classic stages and explicit legacy fallback maps remain valid regardless of this cvar. |
| `r_pbrDebug` | `0` | Debug overlay: albedo, normal, metallic, roughness, AO, emissive, fallback reason. |
| `r_pbrInferFromLegacyMaterials` | `0` | Experimental legacy material reinterpretation for research only. Never required for stock support. |

`r_pbrMaterials 0` must not change existing rendering. `r_pbrMaterials 1` should affect only materials whose parsed metadata says PBR is enabled, and it is necessary but not sufficient: `r_rendererModernVisible`, G-buffer/deferred/forward+ readiness, material-table readiness, geometry readiness, shadow policy, and pass-owner gates still decide visible ownership.

## Implementation Phases

### Phase 0: Baseline And Gates

- [ ] Record stock-material baseline with PBR code absent or fully disabled.
- [ ] Add `r_pbrMaterials`, `r_pbrGeneratedLegacyFallback`, `r_pbrDebug`, and `r_pbrInferFromLegacyMaterials`.
- [ ] Add `gfxInfo` lines that show PBR parser support, PBR modern support, and fallback status.
- [ ] Add empty metrics counters with zero values on stock startup.
- [ ] Acceptance: safe validation matrix passes, stock startup logs do not gain material warnings, and `r_pbrMaterials 0/1` changes nothing when no PBR materials are loaded.

### Phase 1: Parser Metadata Only

- [ ] Add `pbrMaterialInfo_t` to `idMaterial`.
- [ ] Parse `pbr { ... }` tokens into metadata without changing classic stages.
- [ ] Add image loading for PBR maps with correct texture usage classes.
- [ ] Update material lifecycle methods for PBR images.
- [ ] Add parser validation tests through material decl validation.
- [ ] Acceptance: PBR sample declarations parse, stock declarations compile identically, and no renderer path consumes PBR metadata yet.

### Phase 2: Legacy Fallback For PBR Authored Materials

- [ ] Detect whether a PBR material already has classic interaction stages.
- [ ] Add explicit `legacyBumpMap`/`legacyDiffuseMap`/`legacySpecularMap`/`legacyEmissiveMap` support.
- [ ] Add generated fallback stages for PBR-only materials as development fallback, not as release-quality fallback.
- [ ] Report approximate fallback warnings once per material.
- [ ] Track authored, explicit-generated, approximate, and missing fallback counts.
- [ ] Add a self-test that creates a PBR-only material and verifies ARB2 sees bump/diffuse/specular interaction stages.
- [ ] Acceptance: PBR materials render something sane under the default ARB2 path, explicit fallback maps generate the expected classic stages, approximate fallback is visible in metrics, and stock materials remain unchanged.

### Phase 3: Material Resource Table Integration

- [ ] Extend packet material records with PBR flags and first PBR texture handles.
- [ ] Extend `MaterialResourceTable` semantics, records, fallback reasons, and metrics.
- [ ] Dump PBR metadata in `rendererMaterialResourceTableDump`.
- [ ] Update draw/submit plan fallback checks to understand PBR-ready versus legacy-ready materials.
- [ ] Acceptance: modern side paths can identify PBR materials and explain why they are or are not renderable.

### Phase 4: PBR G-buffer Side Path

- [ ] Add PBR G-buffer shader variants.
- [ ] Pack albedo, normal, metallic, roughness, AO, and emissive into graph-owned attachments.
- [ ] Add `r_pbrDebug` attachment overlays.
- [ ] Keep the pass sidecar/default-off until stock parity and debug overlays are verified.
- [ ] Acceptance: a synthetic PBR material writes expected G-buffer values; stock materials still use legacy variants.

### Phase 5: PBR Direct Lighting

- [ ] Add PBR deferred resolve for opaque PBR G-buffer records.
- [ ] Add PBR forward+ path for alpha-tested PBR records.
- [ ] Use existing clustered light records, projected light images, falloff images, shadow descriptors, and light-grid data.
- [ ] Add CPU reference math tests for BRDF helper functions where practical.
- [ ] Acceptance: PBR test materials respond correctly to point/projected lights, roughness changes highlight width, metallic changes F0/base-color behavior, and shadow/light-grid fallbacks remain fail-closed.

### Phase 6: Guarded Visible Ownership

- [ ] Allow `r_rendererModernVisible 1` plus `r_pbrMaterials 1` to modern-own eligible PBR materials.
- [ ] Keep classic materials on existing legacy or modern-classic paths.
- [ ] Block visible ownership if a PBR material has unsupported workflow, texture layout, dynamic image, custom shader, unsafe geometry, or missing graph resources.
- [ ] Add pass-owner metrics for PBR-modern, PBR-legacy-fallback, and PBR-blocked.
- [ ] Acceptance: PBR materials can be visible in a test map without forcing stock materials into PBR interpretation.

### Phase 7: Authoring And Tooling

- [ ] Document material syntax and texture packing in `docs-dev` and user-facing docs when ready.
- [ ] Add Material Editor awareness if the tool is still maintained for OpenQ4 workflows.
- [ ] Add optional import-helper guidance for glTF-style ORM maps.
- [ ] Add sample PBR materials only if the project intentionally wants shipped sample content. Otherwise keep test assets under `.tmp/`.
- [ ] Acceptance: artists can author a PBR material without reading renderer code.

### Phase 8: Optional IBL And Quality Layer

- [ ] Decide whether light-grid data can provide diffuse irradiance for PBR materials.
- [ ] Add optional specular environment probe support only after direct PBR lighting is stable.
- [ ] Consider clearcoat, sheen, anisotropy, height/parallax, and detail normals as later material extensions.
- [ ] Acceptance: optional quality features never become required for base PBR correctness.

### Phase 9: Promotion And Release

- [ ] Extend renderer validation matrix with PBR parser, material table, G-buffer, deferred, forward+, visible, and fallback self-tests.
- [ ] Run SP/MP gameplay with stock assets and PBR disabled/enabled.
- [ ] Run a PBR test scene on `auto`, `gl33`, `gl41`, `gl43`, and `gl45` where available.
- [ ] Capture RenderDoc on forced modern tiers.
- [ ] Update `docs-dev/release-completion.md` only when user-visible PBR support lands.
- [ ] Add curated release notes in `docs-dev/releases/vX.Y.Z.md` before shipping.
- [ ] Acceptance: no stock asset regression, PBR feature documented, rollback documented, shipped PBR assets have authored/explicit legacy fallback with zero approximate fallback unless explicitly waived, and release notes are player-readable.

## Validation Matrix Additions

Add safe tests:

- `renderer-pbr-parser-selftest`: parser metadata, scalar registers, image usage classes, normal format enum, and error cases.
- `renderer-pbr-legacy-fallback-selftest`: generated classic stage fallback and explicit fallback maps.
- `renderer-pbr-material-table-selftest`: PBR semantics, texture binding counts, packed/separate maps, fallback reasons.
- `renderer-pbr-gbuffer-selftest`: G-buffer packing and debug overlays.
- `renderer-pbr-lighting-selftest`: deferred/forward PBR shader readiness and BRDF sanity.
- `renderer-pbr-visible-selftest`: guarded visible ownership on a synthetic packet frame.

Add gameplay/manual coverage:

- Stock SP map with `r_pbrMaterials 0`.
- Stock SP map with `r_pbrMaterials 1` and `r_pbrInferFromLegacyMaterials 0`.
- Stock MP listen-server case with the same two settings.
- PBR test material scene under `r_rendererModernVisible 1`.
- PBR test material scene under `r_renderer arb2`, `r_glTier legacy`, `r_pbrMaterials 0`, and `r_pbrGeneratedLegacyFallback 0` to prove authored/explicit fallback does not depend on modern PBR or approximate generation.
- Legacy rollback run with `r_renderer arb2` and `r_glTier legacy`.

Failure conditions:

- Any new stock material parser warning.
- Any PBR-disabled visual delta in stock captures.
- Any unsupported PBR material silently drawn by a partial modern path.
- Any `r_pbrInferFromLegacyMaterials 1` behavior appearing in default settings.
- Any generated fallback stage changing classic materials that are not PBR-authored.
- Any shipped PBR material depending on approximate generated fallback without an explicit release waiver.

## Risks And Mitigations

| Risk | Mitigation |
|---|---|
| Stock `specularmap` content is mistaken for roughness or metallic data | Never infer PBR from classic stages by default. Keep `r_pbrInferFromLegacyMaterials 0`. |
| PBR normal maps use different channel conventions than Quake 4 bump maps | Require `normalFormat` for new PBR maps or warn loudly; support both Quake 4 A/G and common RG encodings. |
| Color-space mismatch makes PBR look wrong | Define PBR-only albedo/emissive decode policy before visible promotion; keep legacy path unchanged. |
| Texture unit pressure on GL 3.3 | Prefer packed `ormMap`; fail closed to legacy fallback when separate maps exceed limits. |
| Legacy fallback looks poor | Prefer authored classic stages; support explicit `legacyBumpMap`, `legacyDiffuseMap`, and `legacySpecularMap`; warn and fail release validation when fallback is approximate. |
| PBR shader variants explode | Add dedicated PBR families with compact workflow flags; do not cross-product every legacy feature. |
| Modern visible path drops special effects or shadows | Reuse existing pass ownership and fallback gates; PBR cannot override those gates. |
| Authors ship materials that only work on modern tiers | Keep generated or explicit classic fallback as part of the definition of done. |
| New PBR material syntax is loaded by an old binary | Do not promise old-binary parsing. Provide separate legacy material declarations or overlays if old-binary compatibility becomes a project goal. |

## Code Targets

Primary:

- `src/renderer/Material.h`
- `src/renderer/Material.cpp`
- `src/renderer/Image.h`
- `src/renderer/Image_load.cpp`
- `src/renderer/ImageManager.cpp`
- `src/renderer/ScenePackets.h`
- `src/renderer/ScenePackets.cpp`
- `src/renderer/MaterialResourceTable.h`
- `src/renderer/MaterialResourceTable.cpp`
- `src/renderer/ModernGLShaderLibrary.h`
- `src/renderer/ModernGLShaderLibrary.cpp`
- `src/renderer/ModernGLExecutor.h`
- `src/renderer/ModernGLExecutor.cpp`
- `src/renderer/RenderSystem_init.cpp`
- `src/renderer/tr_local.h`

Secondary:

- `tools/tests/renderer_validation_matrix.py`
- `tools/tests/renderer_gameplay_benchmark.py`
- `docs-dev/gl-renderer-modernization.md`
- `docs-dev/renderer-validation-matrix.md`
- `docs-dev/release-completion.md`
- `README.md` when user-facing support is ready

## Definition Of Done

PBR support is complete enough to ship when:

- Existing shipped Quake 4 materials render through the same default compatibility path unless explicitly opted into modern visible rendering.
- New PBR-authored materials parse, load, reload, reference-count, validate, and dump correctly.
- PBR-authored materials have a working ARB2 fallback, and shipped PBR materials use authored classic stages or explicit legacy fallback maps rather than approximate generation.
- Modern PBR G-buffer, deferred, and forward+ paths are cvar-gated, fail closed, and covered by self-tests.
- PBR debug overlays show albedo, normal, metallic, roughness, AO, emissive, and fallback state.
- SP and MP validation passes with PBR disabled and enabled on stock assets.
- A PBR test scene validates modern visible output and legacy rollback.
- Documentation and release notes describe the feature, authoring syntax, compatibility behavior, and rollback path.
