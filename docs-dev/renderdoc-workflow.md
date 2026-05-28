# RenderDoc Workflow

This document covers the OpenQ4 RenderDoc status and the March 2026 black-viewport investigation.

## Root Cause

- The black viewport reports were not caused by missing SMAA assets in the release package.
- The older two-pass SMAA placeholder path had an undefined OpenGL feedback loop in the neighborhood-blend pass:
  - `OpenQ4-GameLibs/src/game/Game_render.cpp` already copied the resolved scene into `_currentRender` before the SMAA passes.
  - [`content/baseoq4/materials/postprocess_openq4.mtr`](../content/baseoq4/materials/postprocess_openq4.mtr) previously drew `postprocess/smaa_blend` into `_postProcessAlbedo0` while also sampling `_postProcessAlbedo0`.
- Some drivers preserved the previous texture contents and appeared to work. Others returned black or undefined data. That is why the issue only reproduced for some users and clustered around `r_postAA 1`.
- Current builds no longer use that path. `r_postAA 1` now runs a three-pass GLSL SMAA 1x implementation: edge detection, blending-weight calculation, and neighborhood blending.

## Current RenderDoc Limitation

- Upstream RenderDoc currently supports `OpenGL 3.2 - 4.6 Core` but not `OpenGL 1.0 - 2.0 Compat`; see the RenderDoc project README: <https://github.com/baldurk/renderdoc>.
- OpenQ4's current renderer still depends on the ARB2 compatibility path (`GL_ARB_fragment_program`, `GL_ARB_shader_objects`, and related fixed-function era state).
- OpenQ4 now has an explicit GL tier/context ladder (`r_glTier`) and can request core-profile contexts for forced modern tiers, but the shipping executor is still routed through the ARB2 bridge. Treat forced core contexts as renderer-modernization bring-up only until the GL 3.3/4.x executor lands.
- On this machine, launching OpenQ4 through RenderDoc injection caused required compatibility features to disappear during startup, which aborted renderer initialization before any frame could be captured.
- OpenQ4 now reports the missing required OpenGL features explicitly during that failure instead of only showing the generic "video card / driver" error.
- The commands and wrapper below are retained as plumbing for future renderer modernization, but today they should be treated as unsupported on the shipping OpenGL renderer.

## Current Workflow Status

OpenQ4 now exposes two console commands:

- `renderDocStatus`
- `renderDocCapture [frames]`

`renderDocCapture` arms a next-frame capture when OpenQ4 is already running under RenderDoc. If `frames` is greater than `1`, it requests a multi-frame capture.

For the current renderer, the scripted wrapper intentionally fails fast unless you pass `-AllowUnsupported`. This avoids booting the game into a guaranteed unsupported capture attempt from VS Code tasks or ad-hoc command lines.

### Experimental Failure Reproduction

If you are explicitly testing future renderer modernization work and still want to reproduce the current limitation, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\debug\renderdoc_capture.ps1 -Mode SP -Map game/convoy1 -AllowUnsupported
```

or:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\debug\renderdoc_capture.ps1 -Mode MP -Map mp/q4dm2 -AllowUnsupported
```

The wrapper still launches `renderdoccmd capture`, starts OpenQ4 from `.install/`, waits for the map to settle, then calls `renderDocCapture`. Captures are written under `fs_savepath/baseoq4/renderdoc/`.

On the current renderer, the launch is still expected to fail before first frame with the explicit RenderDoc compatibility error above. If no `.rdc` file is produced, the wrapper reports that limitation instead of silently succeeding.

## What To Inspect

When reviewing an older or future capture in RenderDoc, inspect the three SMAA passes in order:

- `postprocess/smaa_edge`
- `postprocess/smaa_weights`
- `postprocess/smaa_blend`

Expected bindings on a fixed build:

- `postprocess/smaa_edge`
  - Render target: `_postProcessAlbedo1`
  - Texture slot 0: `_currentRender`
- `postprocess/smaa_weights`
  - Render target: `_postProcessAlbedo0`
  - Texture slot 0: `_postProcessAlbedo1`
  - Texture slot 1: `_smaaArea`
  - Texture slot 2: `_smaaSearch`
- `postprocess/smaa_blend`
  - Render target: `_postProcessAlbedo1`
  - Texture slot 0: `_currentRender`
  - Texture slot 1: `_postProcessAlbedo0`

If the blend pass ever samples the same texture it is rendering to, the build still contains a broken feedback loop.

## Release Package Audit

`tools/build/package_release.py` now fails packaging if `pak0.pk4` is missing any of these required runtime files:

- `materials/postprocess_openq4.mtr`
- `glprogs/smaa_edge.vs`
- `glprogs/smaa_edge.fs`
- `glprogs/smaa_weights.vs`
- `glprogs/smaa_weights.fs`
- `glprogs/smaa_blend.vs`
- `glprogs/smaa_blend.fs`

This check is meant to catch packaging regressions in the postprocess stack before release artifacts ship.

## Temporary User Workaround

If you need to unblock an affected user on an older build:

```cfg
seta r_postAA 0
vid_restart
```
