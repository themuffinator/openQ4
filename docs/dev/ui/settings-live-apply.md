# Applying display settings without a device restart

This note records the first two stages of making Settings > Display apply its
changes to the running renderer. It is the record for the changes of
11 November 2026 and supersedes, for the settings named here, what
[effect execution](settings-effect-execution.md#combined-executor),
[the resolution rows](system-resolution-rows.md) and
[performance presets](performance-presets.md) say about one device restart
per Apply. Those documents are bound by hash in the UI register and were left
unedited; rebinding them is a register increment of its own.

## What changed

Until this stage every Apply that was not purely immediate ended in
`R_RendererModule_TryDeviceRestart`: the renderer destroyed its device, purged
every image, font and vertex cache, built a new device and reloaded
everything. A V-Sync toggle took that route and then asked Keep or Revert. So
did every texture setting and every Performance Preset.

| Setting | Before | Now |
| --- | --- | --- |
| V-Sync (`r_swapInterval`) | Device restart, Keep or Revert | Immediate. OpenGL applies it on its next swap; Vulkan rebuilds its swapchain before the next frame. No question is asked. |
| Texture reduction (`image_downSize*`, `image_picmip`) | Device restart | The running device reloads its file-backed images in place. |
| Precompressed textures (`image_usePrecompressedTextures`) | Device restart; a console change needed `vid_restart` | Reloads in place, from the page and from the console. |
| Benchmark budget (`r_rendererBenchmarkPreset`) | Device restart | Nothing to do: it is read on each use. |
| Upload stream (`r_rendererUploadMegs`, `r_rendererUploadFrameBuffers`) | Device restart | Vulkan: nothing to do, it reads neither. OpenGL: still a device restart, because the stream is allocated when the device starts. |
| A Performance Preset | Device restart, always | No device restart on Vulkan unless the preset changes MSAA. OpenGL also restarts when the preset changes the upload stream. |

Unchanged in stage 1 (see [stage 2](#stage-2-display-changes-on-the-running-device)
for the display itself): a display change (mode, size, fullscreen, borderless,
display, refresh, MSAA) still restarts the device and asks Keep or Revert. An
`r_renderer` change still restarts it and waits for the renderer's selection
report. A speaker layout or EFX change still reopens the sound device.
`image_ignoreHighQuality` and `s_maxSoundsPerShader` still finish at the next
start.

## How it works

### V-Sync is an immediate setting

`r_swapInterval` is `SystemSettingImmediate` in
`SystemSettingsHost::Catalog()`. Both back ends already followed the setting on
a running device (`GLimp_SwapBuffers`, and the present-mode check before
Vulkan acquires its frame); the page simply did not use that.

The journal's effect catalog pins each setting's effect, so the change is a
new version, `SettingsEffectCatalogV5`. V5 has V4's keys and types, and a key
count cannot tell the two apart. `BuildSettingsEffectPlan` therefore builds V5
for a 76-key catalog unless asked for version 4, and
`ValidateSettingsEffectPlan` asks for 4 exactly when the plan names it. A V4
record written before this change keeps V4's meaning; a V5 plan relabelled as
V4, or the reverse, is refused when the two meanings differ.

A device restart names a swap interval in its request and then owns it until
the setting next changes. A restart that keeps the display (an `r_renderer`
change, or OpenGL's upload stream) used to repeat the interval it captured,
which would now ignore a V-Sync change made in the same Apply. Its target
request carries the batch's own interval instead (`CarrySwapInterval`), and
`ValidateDisplayPreserveActualPair` admits that one difference between the
saved restore and target records, and only when the catalog's own
`r_swapInterval` changed.

### Texture settings reload in place

`idImageManager::CheckCvars` reloads the file-backed images when a reduction
setting changes; console changes have always applied this way.
`image_usePrecompressedTextures` joined its list, because it chooses the file
a texture is read from.

The combined executor (`EngineSettingsDisplayHost`) now separates two
questions:

- `DeviceWork()`: the batch holds a display, image or renderer resource
  change. Its record must carry the captured display pair and the leased
  placement (`Valid2` in `SettingsJournal.cpp` requires them for those
  domains). This is unchanged, so records keep their shape.
- `DeviceRestart()`: the batch needs a new device. True for a display change,
  an `r_renderer` change, and an upload stream change under the OpenGL family.

A `DeviceWork` attempt that needs no restart writes its settings and observes
the presenting device, as an attempt without device work does. The renderer
calls `CheckCvars` when it ends a frame, before that frame is presented, so
the later presented frame the controller waits for already shows the reloaded
images. The attempt never moves its leased placement and releases it when the
record is removed. The trace line is `UI_SETTINGS_INPLACE` where a restart
prints `UI_SETTINGS_DEVICE`.

Cold recovery is unchanged: a record replays its settings before the renderer
starts, and the starting renderer reads them.

## Stage 2: display changes on the running device

A change to the display itself (window size, fullscreen, borderless, the
display, the exclusive mode or its refresh rate) used to end in the same
device restart. It now reaches the window in place.

### The renderer service

`R_TryDisplayTransition` (render API 34, `renderExport_t::TryDisplayTransition`,
engine wrapper `R_RendererModule_TryDisplayTransition`) takes the same strict
`renderWindowRequest_t` as `TryDeviceRestart`. Inside the recoverable-request
scope it calls `GLimp_SetScreenParms`, whose strict branch both back ends
already had for the tail of a device start:

- OpenGL applies the request through the window services'
  `ApplyScreenParmsStrict`, keeps its context, re-reads the window size and
  applies the request's swap interval with readback.
- Vulkan applies the same strict request and rebuilds its swapchain with the
  request's interval.

Each reports a new presentation generation, so the caller's evidence is what
a restart gave it. The console font sheet is re-rasterised and
`videoRestartCount` advances, as after `vid_restart partial`, so size-dependent
resources follow. Nothing is purged: images, buffers, font atlases and the
world's derived data stay.

The service refuses, before any change, a request whose window sample count
differs from the device's: a count is fixed when the context is created. A
failure after the window started to change leaves the device running.

### The executor

`EngineSettingsDisplayHost::Restart` asks `DisplayTransition` first: the
attempt changes the display, holds no `r_renderer` or OpenGL upload stream
change and no `r_multiSamples` change, and the presenting device's sample
count equals the request's. Then it calls the transition. If the renderer
refuses or fails it, the same request goes to `TryDeviceRestart`, which starts
from whatever state the window was left in. Either way the attempt then checks
the new generation and `MatchesDisplay`, and its journal, placement lease,
Keep or Revert, restore and cold recovery are unchanged. The trace line is
`UI_SETTINGS_TRANSITION` (or `UI_SETTINGS_TRANSITION_REFUSED` followed by
`UI_SETTINGS_DEVICE`).

A texture setting in the same Apply reloads through the image manager when the
frame ends, as in stage 1.

Still a device restart: an MSAA change on either back end (Vulkan creates its
scene targets with the device), an `r_renderer` change, and OpenGL's upload
stream.

`rendererDisplayProbe transition <width> <height> [interval] [samples]` runs
the service on a hidden window, with `apply`'s arguments.

### Engine checks, stage 2

Hidden window, console verbs only, 11 November 2026, on the build of the
working tree at `e250473dc` plus this change. OpenGL and Vulkan each ran both
checks.

- **On the page.** The window size was typed as 960 by 540 and applied, then
  kept; then 1024 by 576 was applied and reverted. Each Apply and the Revert
  traced `UI_SETTINGS_TRANSITION`, the renderer reported each size in turn
  (and 960 by 540 again after the Revert), the saved settings ended at 960 by
  540, and no device was started or shut down in between.
- **In a map** (`game/airdefense1`, `rendererDisplayProbe transition`). The
  window went from 1280 by 720 to 960 by 540 and back on the running device.
  All 477 world textures kept their sizes, no device was started, and frames
  kept presenting. `rendererDisplayProbe apply` on the same request then
  started a new device, for contrast.

A hidden window can be neither fullscreen nor borderless, so these checks
exercise a size change only. Fullscreen, borderless, another display and an
exclusive mode go through the same strict window call that a device start
makes after it creates its context, but they were not run here on a visible
desktop, and the transitions were not timed.

## What this stage does not do

- The image reload still blocks while textures load. It is shorter than a
  device restart because the device, fonts, generated images, vertex caches
  and the world's derived data stay, but it is not spread across frames.
- The in-place route has no readback of image dimensions. Its evidence is the
  exact settings readback, the same device generation and a later presented
  frame, as for the combined executor's restart.
- MSAA, the high-quality texture flag and switching the rendering API from
  inside a game are later stages ([roadmap](../roadmap.md), UI-01).

## Engine checks, 11 November 2026

Hidden window, console verbs only, on the build of the working tree at
`55bbe166a` plus this change. OpenGL and Vulkan each ran every check.

- **V-Sync.** Toggled twice on the Screen tab and applied. The setting went
  1, 0, 1 and the renderer reported the same interval each time. No journal
  was written, no route was traced and no confirmation appeared. Vulkan's
  presentation generation rose by one per toggle (the swapchain rebuild);
  OpenGL's did not change.
- **Texture settings in a map** (`game/airdefense1`, set from the console).
  A 128 ceiling reduced 335 of 477 world textures with one reload, the device
  generation and the video restart count unchanged; lifting it restored all
  477 sizes. `image_usePrecompressedTextures` 1 to 0 and back reloaded once
  each way.
- **Every preset on the page**, Enhanced through the nine presets and back,
  then Auto-Detect. After each Apply the 55 preset settings equalled those of
  the console command, and they survived a relaunch. Vulkan restarted its
  device for the six steps that change MSAA and for no other. OpenGL
  restarted for those six and for Low to Performance, which changes the
  upload stream. Low Power to Low holds only immediate settings and journals
  nothing.

The checks did not time the reload, and they ran with sound off, so no sound
device reopened.

## Tests

- `tools/tests/ui_settings_display_service.py` (stage 2): a window change
  transitions on OpenGL and Vulkan, kept and reverted; a failed transition
  falls back to a restart, and a failed fallback fails the attempt; an MSAA
  change and a window whose sample count differs never try one; inside a
  combined batch textures ride along, while `r_renderer` and OpenGL's upload
  stream restart.
- `tools/tests/renderer_display_services.py` (stage 2): the wrapper's exact
  dispatch, no video lease, refusal by a module without the service, and the
  probe's `transition` operation.
- `tools/tests/ui_settings_display_service.py`: a texture batch restarts
  nothing under OpenGL and Vulkan, forward and restored; a preset restarts
  nothing under Vulkan; a V-Sync change taken in place does not contradict the
  attempt; a restart that keeps the display carries the new interval in both
  directions; cold start accepts such a record and refuses a target interval
  the catalog did not change.
- `tools/tests/ui_settings_effect_journal.py`
  (`tools/tests/native/UiSettingsEffectJournalTest.cpp`): V5 differs from V4
  in `r_swapInterval` alone; a plan naming V4 keeps V4's meaning; relabelled
  plans and versions that do not fit the key count are refused.
- `tools/tests/ui_system_settings_host.py`: V-Sync is immediate, asks for no
  confirmation and no device work.
