# Quake 4 Light Entity Parity Notes

This note tracks retail Quake 4 light entity key handling and renderer behavior, and records OpenQ4 parity status.

## Research Inputs

- Quake 4 SDK game source:
  - `E:\_SOURCE\_CODE\Quake4-1.4.2-SDK\src\game\Light.cpp`
- Retail engine decomp references:
  - `E:\_SOURCE\_CODE\Quake4Decompiled-main\renderer\interaction.cpp`
  - `E:\_SOURCE\_CODE\Quake4Decompiled-main\renderer\renderworld.cpp`
  - `E:\_SOURCE\_CODE\Quake4Decompiled-main\renderer\renderworld_portals.cpp`
  - `E:\_SOURCE\_CODE\Quake4Decompiled-main\renderer\rendersystem.cpp`

## Retail Light Entity Keys

Canonical parser keys (game-side):

- Position/orientation:
  - `light_origin` (fallback: `origin`)
  - `light_rotation` (fallback: `rotation`, fallback: `angle`)
- Projected-light vectors:
  - `light_target`
  - `light_up`
  - `light_right`
  - `light_start`
  - `light_end` (fallback: `light_target`)
- Point-light shape:
  - `light_center`
  - `light_radius` (fallback: scalar `light`)
- Shader/light parms:
  - `_color` -> shaderParms 0-2
  - `shaderParm3`..`shaderParm7`
  - `shaderParm4` defaults to `-gameTime` when omitted
- Light flags:
  - `noshadows`
  - `nospecular`
  - `noDynamicShadows`
  - `parallel`
  - `detailLevel`
  - `globalLight`
- Light material:
  - `texture`

## Retail Engine Behaviors (Renderer)

- `noDynamicShadows` participates in shadow eligibility checks for interactions.
- `noDynamicShadows` participates in light "shape change" checks that trigger derived-data rebuilds.
- Fog/blend lights use the `noFog` contract instead of ordinary lighting-stage emission tests.
- Light/material shader register evaluation uses `referenceSoundHandle` amplitude inputs.
- `detailLevel` is compared against `r_lightDetailLevel` during area light-ref collection; OpenQ4 defaults this cvar to `0` so all stock-authored lights remain visible unless a user explicitly requests detail culling.
- `globalLight` bypasses portal-based light culling in area light-ref collection.
- `globalLight` bypasses per-surface light-tri frustum culling in interaction submission.
- Tiny interaction/shadow batches can be dropped by `r_limitBatchSize` before drawsurf allocation.
- View entities are processed in sort-by-model order during ambient/light submission.
- Entity scissor tightening is enabled by default through `r_useEntityScissors`, and retail entity LOD clears those scissors for very small on-screen coverage.
- Shadow submission keeps `noSelfShadow` ownership tied to the original interaction material instead of any later shader override.

## OpenQ4 Parity Updates Implemented

- Added `globalLight` parsing to canonical light parser:
  - `src/game/Light.cpp`
- Added `noDynamicShadows` to interaction shadow eligibility:
  - `src/renderer/Interaction.cpp`
- Added `globalLight` bypass for interaction light-tri frustum cull:
  - `src/renderer/Interaction.cpp`
- Added `noDynamicShadows` to light derived-data update comparison:
  - `src/renderer/RenderWorld.cpp`
- Added `detailLevel` filter + `globalLight` portal bypass in area light refs:
  - `src/renderer/RenderWorld_portals.cpp`
- Added renderer cvar `r_lightDetailLevel` with the stock-compatible default of `0`:
  - `src/renderer/RenderSystem_init.cpp`
  - `src/renderer/tr_local.h`
- Added retail ARB2 renderer hook to force `r_lightDetailLevel` to `0` when simple lighting is not preferred:
  - `src/renderer/RenderSystem.cpp`
- Added retail-style default detail level in render light local ctor:
  - `src/renderer/RenderEntity.cpp`
- Restored fog/blend-light `noFog` interaction routing:
  - `src/renderer/Interaction.cpp`
- Restored sound-driven shader register evaluation for light and entity material expressions:
  - `src/renderer/tr_light.cpp`
  - `src/renderer/RenderWorld_portals.cpp`
- Added retail tiny-batch dropping via `r_limitBatchSize` in light/shadow submission:
  - `src/renderer/RenderSystem_init.cpp`
  - `src/renderer/tr_local.h`
  - `src/renderer/tr_light.cpp`
- Restored retail sort-by-model view-entity submission order in the light front end:
  - `src/renderer/tr_light.cpp`
- Restored retail entity scissor LOD handling and retail `r_useEntityScissors` default:
  - `src/renderer/RenderSystem_init.cpp`
  - `src/renderer/tr_light.cpp`
- Restored original-material `noSelfShadow` routing for interaction lights and shadows:
  - `src/renderer/Interaction.cpp`

## Current Result

OpenQ4 now honors retail Quake 4 light entity keys and runtime handling for:

- `noDynamicShadows`
- fog/blend-light `noFog`
- light/material shader `referenceSoundHandle` evaluation
- `detailLevel` + `r_lightDetailLevel`
- `globalLight`
- `r_limitBatchSize`
- retail sort-by-model view-entity submission
- retail default `r_useEntityScissors`
- retail entity scissor LOD
- original-material `noSelfShadow` routing

across parser, renderer culling, and interaction shadow behavior.
