# Applying display settings without a device restart

This note records the first stage of making Settings > Display apply its
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

Unchanged: a display change (mode, size, fullscreen, borderless, display,
refresh, MSAA) still restarts the device and asks Keep or Revert. An
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

## What this stage does not do

- The image reload still blocks while textures load. It is shorter than a
  device restart because the device, fonts, generated images, vertex caches
  and the world's derived data stay, but it is not spread across frames.
- The in-place route has no readback of image dimensions. Its evidence is the
  exact settings readback, the same device generation and a later presented
  frame, as for the combined executor's restart.
- Window and display changes, MSAA, the high-quality texture flag and
  switching the rendering API from inside a game are later stages
  ([roadmap](../roadmap.md), UI-01).

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
