# Vulkan captures and diagnostic readbacks

This page records how the Vulkan renderer reads pixels back for engine
captures and debug tools, how each path labels its result, and the changes
under roadmap item VK-07.

## Paths

| Path | Reads | Timing | Identity |
|---|---|---|---|
| `glReadPixels` (`VK_GuiExecutor_ReadPixels`): `screenshot`, tiled screenshots, `envshot`, `bakeReflectionProbes`, levelshots, `CaptureRenderToFile` | The frame image after temporal composite and display colour mapping: a swapchain image, or the slot's offscreen image for a hidden window | Synchronous: submits and waits for the current slot's fence, then resumes the same frame when `tr.takingScreenshot` is set | The current frame, by construction |
| `_currentRender`/`_currentDepth`, `RC_COPY_RENDER`, post captures (`VK_Exec_CopyRender`) | Colour (resolved when multisampled) or depth (sample zero) | GPU copy, no CPU wait | Not read by the CPU |
| Stencil overdraw and histogram (`r_showLightCount 3`, `r_showShadowCount 2-4`) | The active target's stencil, sample zero when multisampled | Asynchronous: printed after the slot's fence, a frame or two later | Frame, view and target, printed with the result |
| `r_showIntensity`, `r_showDepth` | A GPU pass over a copy | No CPU readback | Not read by the CPU |
| HDR auto-exposure | Scene luminance | Asynchronous by default | Generation and frame; stale samples are rejected |
| `screenshot image` / `screenshot linear` (`VK_Exec_ReadImage`) | A named image or the linear scene | Synchronous | Frame, restart count, image generation and handle |
| PBR probe source | Atlas faces on a cache miss | Synchronous upload-fence wait | Stamped and re-checked |
| `benchmark` | Whole-frame GPU timing | Deliberate `vkDeviceWaitIdle` | Frame and generation |

Colour captures match OpenGL's contract: bytes after display mapping from a
UNORM (not sRGB) frame image, alpha forced to 255, rows bottom-up. `envshot` and
`bakeReflectionProbes` share the front end's face axes and row order with GL.

## Changes, 23 October 2026

- **No current-frame result from nothing.** `glReadPixels` used to open a fresh,
  cleared frame when none was open and return its clear colour as a successful
  capture. It now fails with a warning.
- **No skewed capture after a resize.** A rectangle the frame no longer covers
  used to be clipped silently. Each row then came back at the clipped width
  while the caller strided by the width it asked for. Such a request now fails.
  When the drawable size changed during a capture, `R_ReadTiledPixels` keeps the
  new size instead of restoring the old one, and fails the capture.
- **Failed captures are not saved.** The Vulkan `glReadPixels` stub latches a
  failure, `R_ReadTiledPixels` returns it, and `TakeScreenshot` writes no file and
  warns instead of saving uninitialized memory as a capture. The `screenshot`
  command no longer prints "Wrote" for it. Capture buffers start zero-filled on
  every backend.
- **Hidden windows.** The readback is gated on the frame image it actually reads.
  Hidden-window offscreen images always allow transfer reads, so a surface
  without transfer-source support no longer blocks their captures.
- **Labelled stencil readbacks.** Every asynchronous stencil result now prints
  the frame it was recorded in, its age, the view ID and subview flag, and the
  target size and kind, ahead of the unchanged GL-format line:

  ```text
  Vulkan stencil readback: frame 449 (2 frames ago), view 1, 1280x720 render target
  overdraw:   2.8
  ```

  This also explains the alternating `overdraw: 0.0` lines seen on
  `sp-storage1` and `airdefense1`: they belong to a second view, ID -1, drawn
  each frame with nothing in its stencil. A failed submission now discards that
  slot's pending readbacks rather than printing stale buffer contents later.
  A refusal names its cause, such as a full per-frame slot, no stencil on the
  target, or a failed multisample resolve.

## Evidence

Hidden, input-free airdefense1 runs on the RTX 4060 with validation enabled
wrote a window-sized and a 1920x1080 tiled screenshot, all six `envshot` faces
and labelled stencil results for every frame of `r_showLightCount 3`. The
validation layer reported nothing. The static capture and presentation
contracts pass.

## Remaining scope

GL and Vulkan intensity/depth tools now cover the active viewport, and stencil
statistics use the attachment extent. Legacy cropped, offset and nested
viewports now have GPU comparisons (see
[Nested, cropped and offset views](#nested-cropped-and-offset-views-6-november-2026));
their multisampled-edge differences are fixed by
[window-view multisampling](#window-view-multisampling-6-november-2026). Setting `r_resolutionScaleMode 0` alone
does not exercise that path when the game owns a resampled scene target.
Vulkan's general `glReadPixels` shim always reads the frame image, not a bound
render target as GL does. Arbitrary
nested views and the remaining debug-view combinations still need GL-contract
comparisons; the bounded capture sequence below does not qualify them.

## Tiled captures (6 November 2026)

`screenshot W H` and `envshot` larger than the window render the frame once
per window-sized tile, with `tr.viewportOffset` moved to that tile, and stitch
the readbacks (`R_ReadTiledPixels`). OpenGL adds the offset to each view's
viewport and scissor. Vulkan ignored it, so every tile repeated the first: a
2560x1600 capture held the same 1280x800 image four times. The Vulkan backend
now moves each view of a tiled frame by the offset once, before planning or
drawing it. View-derived scissors are cut at the attachment edge, because
Vulkan forbids negative scissor offsets; the cut rectangle covers exactly the
same pixels. Ordinary frames have a zero offset and are unaffected.

`renderer_tiled_capture.py` writes a window capture, a 2x2-tiled 2560x1600
capture and a 1920x1200 capture with partial tiles. It uses the SMAA laboratory
fixture without MSAA, in three cases:

- `direct` draws straight into the window. Each backend must place every
  tile's own part of the frame, and the 2x capture box-filtered to window size
  must correlate with the window capture at 0.98 or better.
- `post` draws through the game-owned SMAA target, which the capture sizes
  to the whole capture. It is gated the same way.
- `restored` repeats `post`, is gated the same way, and must match it exactly.

`--compare` checks GL against Vulkan per tile, within the existing
two-display-level limit.

On the clean `1243eb6a` build with the RTX 4060 Laptop, Vulkan passes all of
its own checks. Its `direct` tiles all differ (4,868 to 115,776 changed
pixels per pair), and the box-filtered correlation is 0.999. The previous
build failed this: its four tiles were identical. OpenGL fails its own
`direct` check: its (1280,0) tile repeats the (1280,800) tile (correlation
0.806). Of the 27 GL/Vulkan tile comparisons, 26 differ by at most one display
level. The one failure is that defective GL tile. In `post`, both backends
produce the same incorrect image (three tiles identical, correlation 0.140) and
match each other within one level. These two OpenGL/shared tiled-capture
defects were recorded as open, outside the Vulkan parity fix. Reports and
engine images: `.tmp/vk-roadmap/nov6/tiled-{gl,nvidia}/` and `tiled-comparison/`.

**Window draws only.** Both backends added the tile offset to every view,
including views drawn into a render texture. A target sized to the whole
capture then held the scene shifted by the tile, and the view that composites
it to the window shifted it again. Now only drawing to the window takes the
offset. OpenGL applies it through `RB_TileOffsetX`/`RB_TileOffsetY`, which are
zero while `backEnd.renderTexture` is bound. Vulkan's
`VK_Backend_ApplyTileOffset` follows `RC_SET_RENDERTEXTURE` through the frame
and moves only the views drawn to the window. The scene is drawn at its
target's origin, and the composite moves to the tile.

Re-run with every case gated on the RTX 4060 Laptop (OpenGL and Vulkan
`--device 0`) and the Iris Xe (Vulkan `--device 2`). All three pass `direct`,
`post` and `restored`. The box-filtered correlation is 0.999 in `direct` and
0.9998 in `post`/`restored`, and every pair of full tiles differs in at
least 4,868 pixels. `restored` repeats `post` exactly. All 27 GL/Vulkan
tile comparisons are within one display level on each device. The OpenGL
`direct` tile repeat is gone too. Its runtime no longer exists, so that run's
route could not be re-examined. The game draws through its own render target
whenever its scene size differs from the screen's, so a tile probably took
that route and was offset twice. Reports and engine images:
`.tmp/vk-roadmap/nov6/tiled-fix/{gl,nvidia,intel}/` and
`comparison-{nvidia,intel}/`.

## Nested, cropped and offset views (6 November 2026)

`renderer_view_capture.py` adds six cases to the same laboratory scene at 4x
MSAA, with only the emissive specimen lit:

- `nested`: a `func_cameraview` close in front of the specimen and a flat
  screen beside it with a `remoteRenderMap` material, so a subview is cropped,
  captured and sampled in the main view. The screen must show the specimen's
  cyan, and the constant-material control must not.
- `nested-control`: the same screen with a constant material.
- `nested-cropped`: `nested` inside the legacy 50% scene crop
  (`r_screenFraction 50`, `r_resolutionScaleMode 0`).
- `cropped-cube`: an `envshot` cube inside that crop.
- `offset-gui`: a test GUI whose `renderDef` window starts away from the origin.
- `nested-restarted`: `vid_restart`, which must reproduce `nested` exactly.

Building it exposed two things:

- **The screen must be flat.** As in Doom 3, precise subview culling rejects
  any surface with a back-facing triangle, so a sphere never renders a
  subview. Stock screens are flat patches.
- **Captures into file textures failed on both renderers.** The stock idiom
  captures into an ordinary texture (`map textures/common/camera1.tga`), which
  loads block-compressed (BC1) with a mip chain. Vulkan blitted into the BC1
  image (a validation error; the screen kept old contents), and a copy that
  resized it kept its mip count (another validation error). OpenGL attached the
  immutable compressed texture to a framebuffer (incomplete; the screen stayed
  black). Quake 4's `glCopyTexImage2D` re-specified the destination as a plain
  texture. Both backends now do the same: a colour capture into a 2D file
  texture whose format, mip count or size does not match re-creates it as one
  RGBA8 level of the capture size
  (`idImage::NeedsColorCopyRespecification`/`RespecifyColorCopyTarget`).
  Generated and scratch targets keep their existing storage rules. The image
  policy metadata harness executes both new bodies and rejects removal of
  either content-mutation guard.

On the clean `b430a18f` build, both backends pass all independent checks on the RTX 4060
Laptop. The screen shows 28,408 specimen pixels in `nested` and
`nested-restarted` (an exact repeat), 27,808 in `nested-cropped`, and none in
the control. GL/Vulkan comparisons of all four nested cases are within one
display level. Neither run logs a GL error or Vulkan validation message.
The Iris Xe also passes every independent check, with the same specimen pixel
counts (27,810 when cropped). Against the NVIDIA OpenGL reference it differs
only on silhouette edges, where the two vendors' 4x MSAA coverage differs, and
in faint near-black speckle in the cropped case. OpenGL on this hybrid laptop
runs on the NVIDIA GPU, so there is no same-vendor reference for that
comparison. The tiled-capture harness also passes on the Iris Xe
(`.tmp/vk-roadmap/nov6/tiled-rerun/`, built from the same sources). Its GL
comparisons fail only on the defective OpenGL tile described above.

Two comparisons failed on `b430a18f`:

- **Cropped `envshot` (shared, fixed afterwards).** Under the legacy 50%
  crop, both renderers drew the face that sees the specimen at half size in
  its bottom-left corner. OpenGL also left the same fragment in three faces
  that cannot see the specimen. During any capture `BeginFrame` sizes
  `glConfig` to the capture, so the legacy crop could not tell a 256x256 face
  from the window and cropped it like the display. `BeginFrame` now records
  the window's size (`tiledWindow`), and the legacy crop skips captures of any
  other size. Its pin is in `renderer_native_ui_output.py`. On the fixed
  build all three runs pass again, every face is full-size, and the three
  stale OpenGL faces match Vulkan exactly. The face that sees the specimen
  differs in 113 pixels, all on the edges of the remote image shown on the
  screen: in this path OpenGL's capture of that view is antialiased and
  Vulkan's is not. This is the same multisampling difference as the next
  item, and has the same fix. The two existing larger-than-window checks against
  `glConfig.vidWidth` (`PushSceneResolutionScale` for the ES modes and
  `PresentScaledScene`) compare the capture with itself for the same reason,
  so they never refuse. They are recorded here, not changed.
- **Offset GUI window edges.** 112 silhouette pixels differ by up to 11
  levels: at 4x MSAA OpenGL antialiases the GUI `renderDef` view's edges and
  Vulkan does not. Interiors match. OpenGL creates its window with 4x MSAA
  (`actualSamples=4` in the log), so whatever it draws straight to the window,
  GUI 3D windows included, is multisampled. Vulkan's swapchain image is
  single-sample, and only the game's scene target carries MSAA. Matching it
  needs a multisampled target and resolve for GUI 3D views. Fixed by
  [window-view multisampling](#window-view-multisampling-6-november-2026).

Reports and engine images: `.tmp/vk-roadmap/nov6/view-{gl,nvidia,intel}/` and
`view-comparison*/`; the crop-fix repeat is in `envcrop/`.

## Window-view multisampling (6 November 2026)

OpenGL creates its window with `r_multiSamples` samples, so whatever it draws
straight to the window is multisampled: GUI `renderDef` windows, and remote
camera, mirror and other capture-backed subviews before they are copied into
their textures. The Vulkan swapchain is single-sample, and only the game's
scene target carried MSAA, so those views were aliased: the two differences
recorded under [nested, cropped and offset views](#nested-cropped-and-offset-views-6-november-2026).

With `r_multiSamples` above 1, a 3D view that would draw straight to the
window now renders into a window-sized multisampled target. The view's
rectangle is first seeded with the window's pixels, because a 3D view clears
only depth. The target is then resolved with the same box average as other
Vulkan targets, and only that rectangle is copied back.

- **Row order.** The target keeps the swapchain's top-down rows, not the
  lower-origin convention of other offscreen targets. A lower-origin first
  version still differed in 54 GUI edge pixels (up to six levels): OpenGL's
  window also stores rows top-down, so its sample positions sit mirrored
  relative to a lower-origin target.
- **Scene views are left out.** Full-window scenes already use the scene
  target, and OpenGL draws a capture's own view (an `envshot` face)
  single-sample. Multisampling it made the face's directly seen specimen
  differ in 168 edge pixels (up to 128 levels) where OpenGL is aliased.
  Portal-sky and `SS_SUBVIEW` contributors follow their scene.
- **Native fallbacks.** The HDR scene path, capture tiles outside the window
  and a window the backend cannot read keep the single-sample path, as does
  `r_vkWindowViewMultisample 0`.
- **Cost.** Each such view adds one rectangle copy, one seed draw over the
  target, one resolve and one rectangle copy back. The targets are allocated
  on first use: about 41 MB at 1280x800 with 4x MSAA.

Results on `e5ad574d4` with this change, validation enabled and no
validation messages:

- NVIDIA 591.74: every image and cube face of the view sequence passes.
  `offset-gui` is within two levels (was 112 pixels, up to 11) and the
  `envshot` face showing the remote image within one (was 113 pixels, up to
  97). With `r_vkWindowViewMultisample 0` both old differences return
  exactly. All 121 image-capture comparisons, including the 4x and 8x
  cubemaps, stay within one level.
- Intel 101.5590: the cube face passes and `offset-gui` differs from the
  NVIDIA GL reference in 125 edge pixels by up to five levels. Its nested,
  cropped and cube specimen-edge differences (420 pixels up to 96; about
  2,800 up to 117) are byte-identical to the earlier Intel evidence: Intel's
  multisampled scene edges against NVIDIA's OpenGL, not this path.
- The 29-map stock single-player sweep (OpenGL, NVIDIA and Intel Vulkan, Balanced preset with 2x MSAA, validation on) passes 84 of 87 runs and all 58 warning comparisons. The two failures are `mcc_2` captures on Vulkan taken 120 frames in, which land on the map's scripted fade to black. At 240 and 600 frames both renderers pass, and an older build failed the same capture on OpenGL instead.

Reports and engine images: `.tmp/vk-roadmap/nov6/windowview/`.

## Stencil debug views and the weapon depth hack (6 November 2026)

`renderer_stencil_debug_capture.py` lights the original laboratory with its
key light, which casts a stencil shadow from an occluder onto a sphere. It
captures, on each backend:

- `r_showShadowCount 1` and `2`, and `r_showLightCount 1`, `2` and `3`, the
  last with a second light;
- the printed stencil overdraw of the two counting modes;
- the player's view weapon (`weaponDepthHack`) lit, and under `r_showDepth`;
- `vid_restart partial`, which must reproduce the shadow count exactly, and
  clearing every view, which must reproduce the scene exactly.

**Defect: the scissor test at the debug tools was undefined.** The first run
showed OpenGL counting shadow volumes only inside each shadow surface's
scissor rectangle, as Doom 3 intends: `RB_SimpleSurfaceSetup` sets that
scissor. Vulkan counted the infinite volumes across the whole view and printed
3.9 against OpenGL's 1.8. The Vulkan translation of the tools assumed the
scissor test is off when they start, because SMAA and other post passes leave
it disabled. Without such a pass it was still on, so OpenGL's result depended
on which passes ran before the tools. `RB_RenderDebugTools` now enables the
test itself after setting the view scissor, and the Vulkan translation
receives the same call. `R_ColorByStencilBuffer` still turns it off for its
colour pass, as before.

Results on `badc9343c` with this change, RTX 4060 Laptop (591.74), Vulkan
validation enabled and no validation messages:

| MSAA | Stencil views and prints | Weapon depth view | Lit scene and weapon |
| --- | --- | --- | --- |
| 0x | All images identical; overdraw 1.8 (shadows) and 1.1 (lights) on both | Within one level | Within two levels |
| 4x | All images identical; the same overdraw | Within one level | 1 scene pixel (5 levels) and 20 weapon pixels (8) differ |

Restart and restore recover exactly on both backends. The 4x lit-edge
differences remain with `r_msaaAlphaToCoverage 0`, so they are not
alpha-to-coverage patterns; they stay open with the other multisampled
geometry-edge differences. The weapon cases skip specular: lit with specular,
eight highlight pixels on the gun differ by three levels at 0x, which belongs
to lighting parity rather than the depth hack.

The existing 34-case depth and intensity debug sequence still passes on both backends at 0x after this change, within one display level.

Reports and engine images: `.tmp/vk-roadmap/nov6/stencil/`.

## Cubemap and write-failure qualification (3 November 2026)

`TakeScreenshot` now checks the filesystem's written-byte count. A failed or
short write marks the capture failed; `envshot` stops at that face and reports
an incomplete cube, while `bakeReflectionProbes` does not reload or count that
probe. The existing frame-data cleanup still runs before a failed bake exits.
The Vulkan readback rectangle check also avoids signed addition overflow.

`renderer_image_capture.py` compares seven captures for each of thirteen image
cases, plus six cubemap faces at baseline, partial restart, full restart,
resized window and restored window. It checks RGBA channels, orientation,
repeated/interleaved reads, exact recovery within each backend, and the existing
two-display-level GL/Vulkan limit. A directory at the first face's output path
forces a write failure: both warnings must appear once, later faces must not
be written, and the command must not report success.

At both 4x and 8x MSAA on the RTX 4060 Laptop GPU, both backends passed all
independent checks. Each set of 121 GL/Vulkan capture pairs differs by at most
one display level.
All thirty cubemap faces recover exactly within each backend. Vulkan validation
reported no messages. Reports, harness snapshots, exact binary hashes and engine
captures are retained under `.tmp/vk-roadmap/capture-gl4-rerun/`, `capture-vk4/`
and `capture-comparison4/`, with the 8x repeats in `capture-gl8/`, `capture-vk8/`
and `capture-comparison8/`. The first GL run had incorrectly unquoted hyphens
in the new command arguments and is retained as a failed harness run in
`capture-gl4/`; no image threshold changed for the corrected rerun.

The screenshot, display-presentation and PBR material contracts also pass.
These results qualify this fixture and its tested boundaries, not every
portal/nested-view, stencil interpretation or capture failure combination.

## Debug-view qualification fixture (4 November 2026)

`renderer_debug_capture.py` extends the same original laboratory with depth and
intensity captures at 50%, 75%, 100%, 125%, 150% and 200% render scale, then image reload and partial
and full video restarts. Reduced scales cover resampled game-owned scene targets
with resolution modes 0 and 1; they do not cover the legacy cropped viewport.
Each case records and verifies the latched scene/output extents, checks that
reduced scale changes sampling, and requires both mode settings to retain the
same resampled result. It checks grayscale depth, the inherited byte-domain
intensity ramp (including its black wrap), exact recovery within each backend,
and the existing two-display-level GL/Vulkan comparison. The scene is frozen,
HUD and input are disabled, and all images come from engine screenshots. MSAA
counts are separate runs. The balanced preset and retained UI are pinned before
startup so GPU detection cannot insert an automatic preset restart. Runtime
evidence is required before these controls can close a VK-07 debug-view gate.

The initial balanced 25-case sequence (50–100%) passes on both backends at
0x, 4x and 8x MSAA, with exact restart/reload recovery and at most one display
level between paired images. A separate 12-case larger-target probe exposed
five failing GL/Vulkan pairs: GL read and redrew only the window-sized corner
of the larger scene attachment. At 150% and 200%, even the depth output was
partly left in colour. The failed reports are retained under
`.tmp/vk-roadmap/debug-superscale-{gl0,vk0}/` and
`debug-superscale-before-comparison/`.

GL debug colour/depth readbacks now use the validated active viewport, with
allocation and draw dimensions matching that rectangle. MSAA resolves use the
actual attachment extent, and stencil statistics use its complete extent.
The helper preserves read/draw framebuffer bindings and pack alignment and
refuses unknown targets, out-of-bounds rectangles and allocation-size overflow.
Native controls execute the actual functions for window, scaled, offset and VR
stand-in targets, MSAA success/failure and state restoration; they pass on
Windows and Linux (WSL). The clean `da0080eb` build now passes the expanded
34-case sequence on both backends with the RTX 4060 Laptop GPU:

| MSAA | Independent cases per backend | Paired captures | Maximum display-level error |
| --- | ---: | ---: | ---: |
| 0x | 34 | 34 | 1 |
| 4x | 34 | 34 | 1 |
| 8x | 34 | 34 | 2 |

All reload, partial-restart and full-restart images recover exactly within
each backend. Vulkan validation reports no messages. The two-level full-frame
limit is unchanged. Reports, harness snapshots and engine images are retained
under `.tmp/vk-roadmap/debug-fixed-{gl0,vk0,gl4,vk4,gl8,vk8}/` and their three
`debug-fixed-comparison*` directories. The exact tested package is retained in
`runtime-da0080eb.zip`, SHA-256
`daf52c9e2804fe3536233cf34ebdf6e33a7298b4804dcd2e182df0dc9d23467e`.
These results qualify scaled game-owned targets from 50% through 200%; the
cropped, offset and nested GPU cases in Remaining scope are still open.

The earlier unpinned `debug-gl0-extents/` attempt remains failed: an automatic
first-run preset restart emitted 65 uncached menu/model/crosshair warnings.
Pinning the balanced preset removed those warnings from the controlled runs;
it does not resolve that separate first-run initialization issue.
