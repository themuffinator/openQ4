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

GL debug readbacks cover the whole window from (0,0); Vulkan intensity and depth
views cover the view viewport, so a cropped `r_screenFraction` view differs.
Vulkan's overdraw denominator is the active target's extent. `glReadPixels`
always reads the frame image, not a bound render target as GL does. Arbitrary
nested views and the remaining debug-view combinations still need GL-contract
comparisons; the bounded capture sequence below does not qualify them.

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

At 4x MSAA on the RTX 4060 Laptop GPU, both backends passed all independent
checks. The 121 GL/Vulkan capture pairs differ by at most one display level.
All thirty cubemap faces recover exactly within each backend. Vulkan validation
reported no messages. Reports, harness snapshots, exact binary hashes and engine
captures are retained under `.tmp/vk-roadmap/capture-gl4-rerun/`, `capture-vk4/`
and `capture-comparison4/`. The first GL run had incorrectly unquoted hyphens
in the new command arguments and is retained as a failed harness run in
`capture-gl4/`; no image threshold changed for the corrected rerun.

The screenshot, display-presentation and PBR material contracts also pass.
These results qualify this fixture and its tested boundaries, not every
portal/nested-view, stencil interpretation or capture failure combination.
