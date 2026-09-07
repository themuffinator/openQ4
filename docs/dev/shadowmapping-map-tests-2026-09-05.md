# Individual shadow-map tests — 2026-09-05

Follow-up to [the implementation audit](shadowmapping-audit-2026-09-05.md).
The later [transition test round](shadowmapping-transition-tests-2026-09-05.md)
covers changes during gameplay and a shadows-off fallback-state repair.
Tests use installed Quake 4 assets, staged Windows x64 Debug binaries, isolated
save directories, and bordered 960×540 windows on an NVIDIA RTX 4060 Laptop GPU.
All images come from the engine `screenshot` command. No mouse or keyboard input
is injected. Each view is captured with mapped, stencil, and disabled shadows.

## Repeatable map runner

```powershell
python tools/tests/renderer_shadow_mapping_maps.py --output .tmp/shadow-map-review
```

The runner selects the repository's SP or MP launch configuration for each map,
enters gameplay, writes the exact launch/configuration files, and retains logs,
three TGA captures, and `results.json`. `--maps` and `--apis` select individual
cases; `--extra-cfg` inserts console diagnostics before capture. Use a fresh
output directory. `--dry-run` writes the commands without launching the game.

It checks process completion, capture dimensions, MP gameplay/camera placement,
and explicit shadow-resource, rendering, texture-state, and Vulkan validation
errors. Images require visual review: successful allocation is insufficient
evidence of correct shadow placement. Other stock-content warnings are retained
separately from those failures.

## Map coverage

| Map | View exercised | Coverage and limits |
| --- | --- | --- |
| Air Defense 1 | Outdoor view after the opening cinematic | Point-light shadows; the outdoor setting alone does not establish directional cascade coverage. |
| Air Defense 2 | First corridor, with separate single-light and flashlight captures | World geometry, doors, player/weapon ownership, emitter panels, and projected flashlight. Exposed and verified the Vulkan point-shadow clipping repair below. |
| Storage 1 | Corridor after bypassing the opening drop-pod state | Multiple point lights, Marines, corpses, and effects; avoids signing off an in-flight pod image. |
| Storage 2 | Lift arrival after 85 seconds of simulation | Point/projected lights, alpha casters, and cascade fitting. The opening lift-only frame was rejected as insufficient. |
| Medlabs | Opening gurney sequence | Point/projected lights, animated geometry, and effects. The moving gurney is not identical between API runs; compare each run's own three captures. |
| MCC Landing | Spawn lift/panel view | A limited point-light smoke test, not outdoor, subview, or campaign coverage. |
| Q4DM1 | Fixed view at approximately (-4768, 6624, 324.75), yaw 315 | Joined local multiplayer, eight visible point lights, and both receiver ownerships. |
| Q4DM9 | Fixed view at approximately (1528, -384, 452.75), yaw 180 | Joined local multiplayer, point lights and a parallel light, including Vulkan four-cascade maps. |

Multiplayer explicitly enables `ui_autoJoin 1` and `net_allowCheats 1` in the
isolated test profile. The latter is necessary for `noclip`/`setviewpos`; merely
setting `sv_cheats` left random spawns and was rejected for pose comparisons.
Initial borderless, cinematic, and in-transit captures were also rejected.

Storage 2 also exercised a legitimate retained stencil caster during lift
travel: light 289, `func_mover_11942`, material `small_light4`, has a real
stencil volume while being excluded from the depth map. This differs from the
empty emitter-panel probes repaired in Air Defense 2. The final OpenGL lift
arrival view reports four supported lights mapped with zero receiver fallback.

## OpenGL texture-state repair

The Storage maps exposed `idImage::Bind` warnings about texture unit -1.
Shadow moment binding/cleanup visited sampler unit 8 through the compatibility
renderer's eight-entry texture-state array, even when translucent shadows were
disabled. `BindNull` could write beyond that array. Moment samplers beyond the
tracked range now use explicit GLSL texture binding without changing legacy
client texture state; cleanup follows the same path. Resource probing restores
unit 0, and `BindNull` rejects invalid array indices defensively.

Both Storage maps passed again without texture-state warnings. Medlabs was also
run with `r_shadowMapTranslucentMoments 1`, exercising translucent caster lists
and the enabled moment path without those warnings.

## Vulkan point-shadow clipping repair

Air Defense 2 light 168 (`lights/round`, origin -1000, 1504, 136) produced a false
black block across the corridor. Allocation and caster-completeness reports
were clean. Single-light captures, cache-disabled runs, caster/material
isolation, and CPU rays through the installed world geometry localized the
discrepancy to world triangles rendered with depth clamping at the light plane.

The point caster pipeline now keeps depth clipping enabled and uses a 0.01-unit
near plane. Shader-written radial depth retains its precision; using the old
4/16-unit near distance without clamping would discard meaningful nearby
casters. A fragment-only hemisphere rejection did not solve the issue and was
removed, along with the temporary caster/material filters.

With the small near plane, the isolated light's count of pixels darker than its
stencil reference by more than 12 mean RGB levels fell from 14,570 to 55; RMS
RGB difference fell from 4.41 to 0.73. The engine screenshots confirm removal of
the false block. These measurements compare that light's own mapped/stencil
captures, not differently exposed full scenes or different APIs.

Reproduce the isolated stock-light comparison with:

```powershell
python tools/tests/renderer_shadow_mapping_scenes.py point-origin .tmp/point-origin.cfg
python tools/tests/renderer_shadow_mapping_maps.py --maps airdefense2 --apis vulkan --extra-cfg .tmp/point-origin.cfg --output .tmp/point-origin-review
```

Both Vulkan caster fragment shaders also compute slope derivatives before
alpha/depth discard, keeping cutout bias calculations defined across the quad.

## Evidence and limitations

The final staged build completed all **16 map/API runs**, with **48 engine
captures** inspected and zero runner failures. The fixed MP camera positions
were confirmed on both APIs. Q4DM9's final Vulkan view reports light 179 using
four cascades with static reuse plus dynamic composition.

The retained `point-origin` fixture passed again after diagnostic removal
(67 darker pixels, RMS 0.803 against its own stencil capture). Final GL/Vulkan
flashlight runs also passed with caching disabled and an update budget of one:
the real `gfx/lights/flashlight` maps both ownerships, including its 30 caster
surfaces, with zero receiver/budget fallback. These additional fixtures are
under `final-point-origin/` and `final-flashlight/`; the full matrix is `final/`.

Windows client/dedicated, GL/Vulkan modules, and SP/MP GameLibs built and staged
successfully. Shadow compatibility, world interaction, classic interaction
domain, macOS shadow policy, generated SPIR-V header pinning, Python compilation,
and both repositories' whitespace checks passed. The expanded engine projection,
caster-completeness/planner, and 56-permutation GL shader self-tests had already
passed during the preceding audit.

Local commands, logs, screenshots, isolation runs, and JSON observations are
under `.tmp/shadow-map-matrix-20260905/`. These are targeted Windows map views,
not complete playthroughs or Linux/macOS driver validation. Performance was not
benchmarked. The earlier 36-light stress fixture separately exercises cache
capacity and multiple-caster composition.

Unrelated stock-content warnings include Storage 1's null `bridge_gui` script
reference and penetrating corpse bodies; Storage 2's missing
`default_global_startoff` image; Medlabs material, speaker-distance, tether,
health-definition, and articulated-body warnings; MCC turret `look_joint`
warnings; and MP missing AAS/sound and item placement warnings. Stock path-case
and Vulkan's legacy vertex-array diagnostic also remain. These are recorded in
the per-run logs and were not repaired by altering retail assets.

The final Storage 1 Vulkan captures also show green corpse-effect coloring in
both mapped and stencil modes. Its material/effect cause has not been isolated;
it is not counted as a shadow-map repair or as evidence of cross-API visual parity.
