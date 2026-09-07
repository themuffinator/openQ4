# Shadowmapping audit — 2026-09-05

This pass inspected the shared light classification/projection code, front-end
caster signatures, OpenGL and Vulkan map caches, classic receiver/caster shaders,
and modern OpenGL clustered receivers. Earlier assessment documents describe
several defects already repaired in the current tree; they were not treated as
evidence that those defects still exist.

See the [final audit and qualification record](shadowmapping-final-audit-2026-09-05.md)
for the later cache, mover, clipping, and authored-material repairs, the retained
Air Defense 1 reference, and seeded random-map results. The budget/subview stale
reuse mentioned below describes this earlier review stage; subsequent mover
tests replaced that policy with exact caster-signature validation.

## Implemented fixes

1. **Preserve projection precision in cache keys.** Both backend float hash
   helpers and the front-end caster signature rounded every float with
   `Ftoi(value * 1024)`. Projection coefficients `0.0001` and `0.0002` therefore
   hashed identically despite representing different light coverage. Large
   inputs also exceeded the float-to-integer conversion range. All three paths
   now share an alias-safe float-bit hash, with signed zero canonicalized.
2. **Validate the fitted projection before ordinary cache reuse.** OpenGL's
   manually enumerated CSM key omitted blend overlap, PCSS guard settings, and
   distant-source filter scaling. Vulkan omitted the view fit altogether when
   CSM caching was enabled. Both now hash the actual shared projection state,
   including the clip planes, cascade splits, and cached receiver parameters.
   Diagnostic sample counters do not invalidate unchanged depth. The existing
   explicit budget/subview stale-reuse policies remain separate.
3. **Keep stabilized crops at their quantized scale.** Clamping a crop center,
   snapping it, and then clipping its edges could change its width after
   quantization. The fitter now intersects the requested bounds with the light
   projection first, reserves half a final texel for snapping, and translates
   the snapped crop back inside the projection without trimming it.
4. **Define modern OpenGL receiver derivatives.** Clustered shadow sampling
   computed `dFdx(depth)`/`dFdy(depth)` inside divergent light/cascade branches.
   Deferred and forward receivers now evaluate position derivatives before
   those branches (and before alpha discard), then transform those directions
   through the selected shadow matrix. The scalar receiver bias also respects
   cascade scales below one, matching the classic receiver's contract.
5. **Honor authored shadow opt-out in Vulkan receivers.** The map scheduler
   excludes lights marked `noShadows`, but the receiver's fallback-need test
   did not check that flag. It now does. Genuine missing-resource warnings
   identify the affected light/material, target stencil support, incomplete
   ownership masks, and available ownership maps.

## Regression coverage

`rendererShadowProjectedDiagnosticSelfTest` now exercises 884 complete cascade
fits across resolutions 128, 512, 1024, and 4096, checking coverage, projection
containment, and preservation of quantized width at both projection edges.
Existing ladder and texel-translation checks remain. Additional numerical
checks cover sub-1/1024 coefficients, large floats, signed zero, changes to
fitted planes, and diagnostic-only state changes.

`tools/tests/renderer_vulkan_shadow_compatibility.py` pins both backend cache
integrations and the modern receiver derivative initialization sites. Gameplay
evidence uses the existing SP shadow-regression launch contract, windowed
960×540 rendering, disabled mouse capture, stock Steam assets, and the engine's
registered `screenshot` command. Temporary plans, logs, and images are retained
under `.tmp/shadow-audit-20260905/`.

## Validation results

- Windows x64 MSVC debug build passed for client, dedicated server, both
  renderer modules, and SP/MP GameLibs. Binaries were staged through
  `meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`.
- Shadow compatibility, classic interaction-domain, and macOS shadow-policy
  source checks passed. `git diff --check` passed.
- The running engine passed the expanded projected diagnostic, caster/LOD
  admission, receiver fallback, and shadow planner self-tests. The modern GL
  shader-library self-test compiled all 56 permutations across 14 shader kinds.
- Active `game/airdefense2` gameplay completed under confirmed OpenGL and native
  Vulkan modules, with windowed engine screenshots inspected. OpenGL reported
  no shadow render/mask failures; Vulkan reported three exact point-cache hits.
  The first pass also logged a missing-shadow fallback. The deeper investigation
  below identifies its emitter-panel cause and records the successful fix.
- The existing interaction profile's crate/light setup on stock `tools/mv2`
  exercised projected maps. OpenGL reported four cascades, four mapped GLOBAL
  ownerships, and no render/mask failures or stencil fallbacks. Vulkan reported
  an exact projected-cache hit. Both produced inspected engine screenshots.
- Native Vulkan runs used `r_vkValidation 1` and logged no Vulkan validation
  diagnostics. Machine-readable observations are retained in
  `.tmp/shadow-audit-20260905/validation-summary.json`.

## Deeper fallback, flashlight, and multiple-caster audit

The second pass resolved the first pass's missing-shadow warning and added four
rendering fixes plus more accurate diagnostics:

1. **Empty emitter probes no longer invalidate a light.** Air Defense 2 light 3
   (`lights/round`) was invalidated by entity 330 (`func_static_54073`), material
   `textures/common_lights/strip_light2`. The panel is intentionally excluded
   from its owning point map, and its forced stencil-generation probe returned
   no volume. Treating material eligibility as missing occlusion set all three
   incomplete masks to `0x3`. Seven such lights were identified in the diagnostic
   run. An excluded emitter now requires fallback only if the probe produced a
   real volume. Admitted map geometry failures and other missing stencil
   provenance retain their conservative failure behavior. The subsequent native
   Vulkan Air Defense 2 run has zero missing-resource warnings and zero missing
   caster diagnostics; the earlier warning was not suppressed.
2. **OpenGL budgets cannot discard map-only casters.** Both budget/admission
   misses and restrictive subview policy now consult the requested receiver
   ownership's stencil completeness, including prelight readiness. Required
   maps bypass those optional limits, matching Vulkan's existing priority.
   This can exceed the nominal update budget; it preserves shadow coverage.
3. **Every caster must succeed.** OpenGL previously accepted a depth map if any
   caster rendered or was conservatively culled, even if another caster failed
   geometry resolution. Composed dynamic layers were always accepted. Projected
   and point chains now propagate any geometry/space failure through all four
   ownership/static/dynamic chains to map publication and receiver fallback.
   Successful static depth cannot disguise a missing dynamic caster.
4. **Cube-face culling encloses transformed bounds.** OpenGL transformed caster
   centers into world space but kept their local bounding radius. Scaling or
   shearing could therefore reject geometry overlapping a face. The radius now
   encloses the transformed bounds, including rotation and reflection.
5. **Reports distinguish missing maps from stencil rendering.** Vulkan's map
   preparation report used `LOCAL=stencil` for lights with no local receivers.
   It now reports `unused` for absent receivers and `unmapped` when a needed map
   is unavailable; the later receiver path determines whether stencil is usable.
   Front-end report level 2 also names the first caster making a light retain
   stencil fallback, with its entity/model/material and coverage provenance.

### Flashlight evidence

The actual SP weapon flashlight was exercised through the new cheat-protected
`testFlashlight 0|1` command in the companion GameLibs repo. It changes the current
weapon's flashlight state directly without injecting player input. Stock Air
Defense 2 already starts with a compatible weapon; no replacement assets or
synthetic flashlight material are used.

OpenGL reports `gfx/lights/flashlight` with one projected map, LOCAL and GLOBAL
`result=mapped`, 28 ordinary plus two no-self-shadow caster surfaces, and zero
receiver fallbacks. Vulkan reports both ownerships published/reused. Disabling
flashlight CSM is intentional preservation of its authored projection and is
not a stencil selection. With `r_shadowMapMaxUpdatesPerView 1` and caching off,
the final OpenGL run still maps both flashlight ownerships; required updates
exceed the optional limit rather than dropping map-only geometry.

### Dense-scene evidence and regression coverage

- Stock `tools/mv2` with 16 spawned crate entities, 20 additional point lights,
  and 20 additional projectors produced 36 visible shadowed lights: 22 point and
  14 projected. OpenGL reported 890 caster submissions (786 static, 104 dynamic),
  all 36 GLOBAL ownerships mapped, eight static/dynamic compositions, and zero
  stencil fallback, render failure, or mask failure. Those are per-light surface
  submissions, not 890 distinct objects. Some requested CSM fits reduce to one
  valid tile; the requested cascade count is not the rendered tile count.
- Vulkan mapped the same visible light set with cache reuse and scratch maps,
  zero missing-resource warnings, and zero budget/admission/subview fallbacks.
  The scene exceeds the usual 8 projected/12 point cache slots; those cache
  limits do not themselves force stencil. The backend's separate bounded
  resource capacities still apply (including Vulkan's 64-light table).
- Engine screenshots were inspected for both renderers. The developer floor
  is unlit; these captures establish fixture visibility and lighting, not a
  ground-contact quality measurement. Stock Air Defense 2 supplies the separate
  gameplay/flashlight capture. This is not broad campaign or platform signoff.
- `rendererShadowPlannerSelfTest` now also invokes a GPU-free
  caster-chain regression: a valid culled caster followed by missing geometry,
  the reverse order, and scaled/mirrored bounds crossing a cube face. It passed
  in the staged engine along with the earlier projection/planner/shader tests.
- `tools/tests/renderer_vulkan_shadow_compatibility.py` pins every caster-chain
  integration, budget completeness guards, emitter probe policy, and truthful
  per-ownership diagnostic labels. The other shadow/shared-interaction/macOS
  source checks and Windows x64 build/staging passed.

Generate repeatable console fixtures with
`python tools/tests/renderer_shadow_mapping_scenes.py <scene> <output.cfg>`.
Scenes are `flashlight`, `flashlight-budget` (stock `game/airdefense2`) and
`stress` (stock `tools/mv2`, fresh map). Put the cfg beneath the test save root's
`baseoq4/` directory and execute it after active gameplay has settled. Launch
with `r_fullscreen 0`, `r_borderless 0`, `r_borderlessDefaultMigrated 1`,
`r_fullscreenDesktop 0`, `r_windowWidth 960`, `r_windowHeight 540`,
`in_mouse 0`, `in_nograb 1`, `r_shadows 1`,
`r_useShadowMap 1`, and `r_shadowMapReport 2`; use `r_renderApi gl` or `vulkan`.
Retain the engine's `screenshot` output and `logFileName` log. The fixture changes
test settings, so use an isolated save root. Actual runs, commands, screenshots,
and machine-readable observations are under `.tmp/shadow-audit-20260905/`, with
the second pass summarized in `deeper-validation-summary.json`.

## Unrelated observations

- The existing `shadow-regression` profile freezes Air Defense 1 at tic zero.
  Its initial screenshot contained a black world and HUD with no visible
  lights, so it was rejected as rendering evidence. Validation used active
  simulation and the existing interaction fixture commands instead. The
  `interaction-shadow-stock` profile already documents and avoids this trap.
- Air Defense 2 reports stock asset path-case warnings for
  `models/mapobjects/strogg/Canyons/elements`. Native Vulkan also emits the
  legacy `vertex array range in virtual memory (SLOW)` warning at startup.
  These were retained in the logs; this pass did not modify retail assets or
  the unrelated vertex-cache diagnostic.
- Native Vulkan's first Air Defense 2 draw also reports skipped post-process
  surfaces after feedback capture fails. That separate capture issue remains
  unresolved. Dynamically spawning the stock crate/light fixture produces
  expected non-pre-cached material/material-type warnings.
- An early flashlight fixture redundantly used `give weapon_machinegun`, which
  spawned an item outside world bounds. The retained fixture uses the weapon
  already held at Air Defense 2 startup and does not issue that command.

## Limits

The subsequent [individual stock-map tests](shadowmapping-map-tests-2026-09-05.md)
found and repaired additional Vulkan point-caster clipping and OpenGL texture
state problems that the allocation/stress checks alone did not expose.

No default quality or update-budget setting changes. Vulkan remains experimental.
This Windows validation does not establish Linux or macOS driver behavior, or
claim a measured performance improvement. Hashes remain 32-bit content
signatures; this removes systematic precision loss, not all possible collisions.
