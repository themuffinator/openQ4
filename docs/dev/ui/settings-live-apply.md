# Applying display settings without a device restart

This note records the six stages of making Settings > Display apply its
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
| Upload stream (`r_rendererUploadMegs`, `r_rendererUploadFrameBuffers`) | Device restart | Vulkan: nothing to do, it reads neither. OpenGL: a device restart until stage 5, which resizes the stream at the next frame. |
| A Performance Preset | Device restart, always | Stage 1: no device restart on Vulkan unless the preset changes MSAA. Stage 3: none on Vulkan at all. Stage 5: OpenGL restarts only when the preset changes MSAA. Stage 6: nor then, where its window is single-sample (the default except on Apple). |
| MSAA on OpenGL (`r_multiSamples`) | Device restart: the window carried the samples | Stage 6: a transition. The window is single-sample and 3D views are multisampled in render targets. `r_glWindowMultisample 1`, Apple and OpenGL ES keep the old window and the restart. |
| High-quality texture hints (`image_ignoreHighQuality`) | Written, finished at the next start | Stage 4: the materials that read the hint are re-parsed in the session. |
| Rendering API and GPU (`r_renderApi`, `r_renderDevice`) | Restart Renderer at the main menu only | Stage 4: also inside a single-player game, which is saved, reloaded around and resumed. |

Unchanged in stage 1 (see [stage 2](#stage-2-display-changes-on-the-running-device)
for the display itself): a display change (mode, size, fullscreen, borderless,
display, refresh, MSAA) still restarts the device and asks Keep or Revert. An
`r_renderer` change still restarts it and waits for the renderer's selection
report. A speaker layout or EFX change still reopens the sound device.
`s_maxSoundsPerShader` still finishes at the next start, as
`image_ignoreHighQuality` did until
[stage 4](#stage-4-the-high-quality-hint-and-the-renderer-switch-inside-a-game).

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
a restart gave it. The console font sheet is re-rasterised. Nothing is purged:
images, buffers, font atlases and the world's derived data stay.

`videoRestartCount` does not advance (see
[the two findings](#the-two-findings-fixed-the-same-day)). It tells its
readers that every GPU resource was lost, and none was. Whatever depends on
the output size follows the size itself, as it does when the window is
resized by hand; the executor asks the classic GUI fonts, which are
rasterised for the display's height, to rebuild on their next use
(`idDeviceContext::DisplayChanged`).

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

Still a device restart at this stage: an MSAA change on either back end, an
`r_renderer` change, and OpenGL's upload stream.
[Stage 3](#stage-3-msaa-on-the-running-device) takes Vulkan's MSAA change out
of that list.

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
exercise a size change only, and the transitions were not timed.

### Visible window, stage 2

The same day, on a visible window on a 2560 by 1440 desktop, started as a
1280 by 720 window and driven by console verbs only. Other sessions were
using the machine, which matters: the controller undoes a visible display
change whose window has no keyboard focus or is minimized, a rule that
predates this stage, and Windows gives a window launched in the background no
focus.

- **Vulkan, with focus.** Desktop fullscreen was applied and kept three
  times, and the return to a window applied and kept once. Each was a
  transition; the renderer reported 2560 by 1440 fullscreen and then 1280 by
  720. Borderless applied as a transition and its confirmation was presented,
  then the window lost focus and the change was undone.
- **Both back ends, mostly without focus.** Desktop fullscreen, borderless and
  exclusive fullscreen at the desktop's mode each applied as a transition (the
  executor checks the observed window against the request before it traces
  one), were undone by the focus rule, and returned to the 1280 by 720 window
  at its position as a transition. Over all visible runs that is 13 changes
  and 9 undos on Vulkan, and 8 changes and 11 undos on OpenGL, where the extra
  undos are retries after the missed deadline below. No transition was refused
  and none fell back to a restart. The only device restarts were two on Vulkan
  for a preset the test script changed by mistake, which changes MSAA.

Not confirmed: Keep under borderless or exclusive fullscreen on either back
end, and any Keep on OpenGL, because the window had no focus in those runs.

Two observations from these runs, both fixed the same day (next section):

- In two of the unfocused OpenGL runs the window presented about one frame a
  second for a few seconds after an undo, and the controller reported
  `Restored display did not present before its deadline` three times, which
  leaves the page offering Retry. A later unfocused OpenGL run of four changes
  did not show it. Whether a focused window or the restart path shows it is not known.
- Returning from fullscreen to a window through Apply places the window at
  the fullscreen window's origin (0,0), because the request keeps the captured
  window's position (`BuildDisplayRequest`). An undo returns it to where it
  was.

### The two findings, fixed the same day

**The missed deadline was a reload the transition caused.** As first written,
`R_TryDisplayTransition` advanced `videoRestartCount`, as `vid_restart partial`
does. The retained interface answers that count by saving, shutting down and
reloading every document (`RefreshResources` in `RetainedUI.cpp`), which is
right after a device was lost and pointless after a window changed in place.
Measured on a hidden window with the DISPLAY page open, a transition from
1280 by 720 to 2560 by 1440 and one back:

| | Transition call | First new frame after it | Documents reloaded |
| --- | --- | --- | --- |
| Vulkan, count advanced | 56 ms, 31 ms | 4990 ms, 4722 ms | 20, 20 |
| Vulkan, count left alone | 73 ms, 42 ms | 289 ms, 157 ms | 0, 0 |
| OpenGL, count advanced | 12 ms, 13 ms | 1989 ms, 2911 ms | 20, 20 |
| OpenGL, count left alone | 10 ms, 8 ms | 210 ms, 114 ms | 0, 0 |

The frame counter was polled every 100 ms, so the last two rows are bounded
by the poll, not measured to the millisecond. On a machine other sessions had
saturated, a Vulkan run with the count advanced took 35.7 s and 46.6 s to its
first new frame. The controller allows a restored display 20 s to present,
which is the deadline the unfocused OpenGL runs missed.

The count now stays where it was. The page drawn after a transition to 1600
by 900 is identical, pixel for pixel, to the page drawn after a device restart
at that size, on both back ends. The page flow and the in-map transition
above pass again on the changed build, with the count unchanged across each
transition and advanced by the device restart. What was not run: the classic
fonts' rebuild in the engine (the display service test covers the request),
and the visible unfocused OpenGL case that showed the deadline.

**A window that leaves fullscreen is centred.** `BuildDisplayRequest` keeps a
bordered window's position on its display, and now centres the new window when
the captured one was fullscreen or borderless: that window sits at the
display's origin, where a bordered window's title bar would be off the
screen. This applies to the restart path as well. Covered by
`tools/tests/ui_system_display.py`; not run on a visible desktop.

## Stage 3: MSAA on the running device

MSAA lives in two places. The game owns the main scene's forward target and
builds it at `r_multiSamples` (`idGameLocal::InitGameRenderSystem`). Under
OpenGL the window also has a sample count, fixed when its context is created
and used only by the 3D views drawn straight to the window. Vulkan's window is
single-sample, and its own targets (the temporal scene target and the window
view target) already compared their sample count with `r_multiSamples` on
every frame.

So the one thing that waited for a restart was the game's target: it was
rebuilt only when the renderer's restart count changed, or when a map loaded.

- **The game follows the setting.** `idGameLocal::RenderScene` now rebuilds
  its targets when `r_multiSamples` differs from the request they were built
  for, as it already did for the HDR scene format. This is in the game module,
  so it holds on both back ends and for a console change.
- **Vulkan's Apply is a transition.** `DisplayTransition` no longer refuses an
  attempt that holds `r_multiSamples` unless the back end multisamples its
  window. On Vulkan the request asks for the single-sample window the device
  already has, the swapchain is rebuilt, and the game rebuilds its targets on
  the next scene. `r_multiSamples` stays in the display domain, so the attempt
  still asks Keep or Revert and its record is unchanged.
- **OpenGL's Apply still restarts.** An MSAA change there is a change to the
  window's sample count, which the strict display request names and checks.

With stage 1 this means no Performance Preset restarts Vulkan's device.

### Engine checks, stage 3

Hidden window, console verbs only, 11 November 2026, on the build of the
working tree at `56e1f6c08` plus this change.

- **In a map** (`game/airdefense1`, from the console), on both back ends:
  `r_multiSamples` 0 to 4 to 0 to 2. Each step rebuilt the game's targets
  once, `Forward render target MSAA` reported the requested count as the
  effective one, `gfxInfo` agreed (`vulkan-scene` on Vulkan, `active` on
  OpenGL), no device was started, the presentation generation and the restart
  count did not move, and no warning was printed. The Vulkan frame at 4x was
  looked at and draws correctly.
- **On the page**, MSAA one step up, applied and kept. Vulkan traced
  `UI_SETTINGS_TRANSITION` with no device start; OpenGL traced
  `UI_SETTINGS_DEVICE`. Both ended with `r_multiSamples` at 2.

Not run: an MSAA change from the pause menu's page inside a map (the page
flow ran at the main menu, the map check from the console), a revert of an
MSAA change in the engine, and 8x or 16x.

### What stage 3 leaves

- OpenGL's Apply. Its main scene already follows the setting; the restart is
  only for the window's own count. Removing it means a single-sample OpenGL
  window with the direct 3D views routed through a multisampled target, as
  Vulkan's window view is.
- OpenGL's upload stream, which was allocated when the device starts
  ([stage 5](#stage-5-opengls-upload-stream-on-the-running-device) resizes it).

## Stage 4: the high-quality hint, and the renderer switch inside a game

Two settings were left that no device operation could finish: one is read
while a material is parsed, and the other replaces the renderer itself.

### The high-quality hint

`image_ignoreHighQuality` decides, while a material is parsed, whether a stage
that asks for `highquality` or `uncompressed` gets the uncompressed image
identity or the ordinary one, which the texture reduction settings shrink. A
level load re-parses only the materials that level owns. Menu, HUD and
light materials parsed outside a level kept the old answer, so the page wrote
the setting and reported that it finished at the next start.

`R_CheckMaterialQualityInputs` (`Material.cpp`) now applies a change in the
session:

- Each parse records whether it resolved a non-forced hint
  (`ReadsHighQualityHint`) and the answer it used
  (`ParsedIgnoringHighQuality`). `forceHighQuality` ignores the setting and
  does not count.
- A changed setting re-parses, in place, the parsed materials that read the
  hint under the other answer. Stock Quake 4 has about 25 such materials, so
  the pass is short. Material pointers do not change.
- It runs at the start of `idRenderSystemLocal::BeginFrame`, after the
  previous frame's back end has finished and before the frame has a draw
  surface, so nothing references the stages being rebuilt. It waits while a
  level load or a checked image-policy attempt is running.
- An image a re-parsed material stops using is purged unless another parsed
  material still holds it. The image that replaces it inherits whether a
  level load may purge it, so a toggle does not turn level images into
  permanent residents.
- The materials are found through a registry of parsed materials, not through
  the declaration manager, whose `DeclByIndex` allocates a material object
  for every declaration it is asked about.

A console change behaves the same way. `SystemSettingsHost::
ChangedNeedsNextStart` no longer names the setting, so the page's notice
`#str_230076` now appears only for the per-sound sample limit.

A new renderer's first frame runs the same check, because every setting
starts out marked as changed. On a fresh profile it re-parsed one material
that had been parsed before the configuration set the value.

### Restart Renderer inside a single-player game

Another rendering API or GPU still needs the renderer module replaced, which
is an engine reload. The page offered that at the main menu only, because a
reload ends the running game.

In a single-player game the restart now carries the game across the reload:

1. `idSessionLocal::SaveForRendererRestart` writes the `RendererRestart`
   autosave and waits for it to finish. Like the Android `Suspended` save it
   takes no screenshot, shows no HUD message and rolls over each time. The
   Load page lists it under the Restart Renderer label.
2. The settings are written and the saved selection is armed, as before.
3. `idSessionLocal::QueueRendererRestart` queues `reloadEngine menu` with the
   post command `resumeAfterRendererRestart`, which loads that save. If the
   save does not load, the player is left at the main menu, where the reload
   alone would have put them.

`settings.render.canRestart` is true in such a game, and the notice reads
`#str_231767` (Restarting the renderer saves your game and resumes it
afterwards), added in the twelve interface languages. The restart is not
offered, and the notice still asks for the main menu, where a save would be
refused or would not resume cleanly: multiplayer, a load in progress, demos,
the editor, a cinematic, death or a failed objective. A save that fails stops
the restart before anything is written.

The switch is not instant. It is one engine reload and one load of the saved
game inside the running program.

### Engine checks, stage 4

Hidden window, console verbs only, working tree on `d3a73d37d`, OpenGL and
Vulkan. The build also held another session's two uncommitted OpenGL shadow
shader edits.

- **The hint at the title screen.** `image_ignoreHighQuality` 0 to 1 to 0 to
  1 from the console: one pass per change, 2 of 2 materials re-parsed and 2
  images released each time, 2 to 6 ms, no device start, no warning.
- **The hint in a map** (`game/airdefense1`, under a 128 texel ceiling): one
  pass per change, 5 of 5 materials re-parsed and 6 images released each time,
  7 to 44 ms, the presentation generation unchanged, no warning.
- **Presets on the page** (Vulkan): the two steps that change the setting
  each ran one pass, and the next-start notice did not appear.
- **Restart Renderer in a map**, from the pause menu's Display page with the
  other API chosen: `RendererRestart` saved, one engine reload, the other
  renderer module loaded and the game back in the map at the same position
  and view angle (moved 0.0 units), with the HUD drawn. Vulkan to OpenGL and
  OpenGL to Vulkan each passed twice.

Not run: a GPU change (the test machine exposes one usable Vulkan device), the
Awakening campaign, a restart that the refusals above block in a real game,
a save that fails to load after the reload, the classic HUD's materials
(`ui_retained 0`), and timing of the reload.

### What stage 4 leaves

- `s_maxSoundsPerShader` still finishes at the next start.
- A multiplayer client or server is not carried across a renderer restart.
- A holder of an image pointer outside the materials (a cached light image,
  a page's atlas) keeps drawing the image it had; it reloads on its next
  bind if it was purged.

## Stage 5: OpenGL's upload stream on the running device

`r_rendererUploadMegs` and `r_rendererUploadFrameBuffers` size the per-frame
upload stream of the OpenGL family. `idUploadManager::Init` read them once,
when the device started, so a change of either restarted the device. A
Performance Preset moves both, which was the one reason left for OpenGL to
restart on a preset that keeps MSAA.

Nothing outlives a frame in that stream: an allocation belongs to the frame
that made it, and `idUploadManager::BeginFrame` runs after the previous
frame's back end has finished recording. `ResizeFrameStream` therefore
replaces the stream alone, there, when either setting differs from the
request the stream was built for:

- one `glFinish`, then the old frame buffers are unmapped and deleted and new
  ones are created on the path the device chose at start;
- a persistent stream that cannot be created at the new size falls back to
  map-range as it does at start, and a stream that cannot be created at all
  retires to the legacy uploads and is not retried every frame;
- the static buffers, their pool and their counters are untouched;
- the storage readback reports the new size under a new generation.

A console change behaves the same way. `EngineSettingsDisplayHost::
DeviceRestart` now restarts only for the display itself or the `r_renderer`
fallback, on every back end; `UploadStreamChanged` is gone.

### Engine checks, stage 5

Hidden window, console verbs only, working tree on `39e4ce8a1`.

- **In a map** (`game/airdefense1`, OpenGL): 16 MB x 4 to 64 MB x 8 to
  2 MB x 3 and back from the console. One resize line per step, the
  persistent path kept, no device start, the presentation generation
  unchanged, no warning, and the frames drawn. Vulkan, which reads neither
  setting, did nothing and restarted nothing. The scene's brightness varies
  between screenshots with and without a resize (a control run with the
  settings left alone shows the same), so the screenshots were not compared
  pixel for pixel.
- **Every preset on the page** (OpenGL): six device restarts, one for each
  step that changes MSAA, where there were seven. Low to Performance, which
  changes only the upload stream among the device settings, resized it in
  place.

## Stage 6: MSAA on OpenGL without a new window

OpenGL created its window with `r_multiSamples` samples, so an MSAA change
was a change of window: a device restart. The window's samples did three
things, and each now has another home.

**The policy.** `r_glWindowMultisample` (archived, read when the renderer
starts) says whether the window carries the samples. It defaults to 0, a
single-sample window, and to 1 on Apple platforms, whose OpenGL 2.1 context
has no multisample textures, so the window is the only MSAA there. An OpenGL
ES window always carries them. `R_WindowMultiSamplesForNewDevice` latches the
answer for the device (`R_WindowCarriesMultisampling`). A context without
multisample textures and with a single-sample window draws without MSAA and
says which setting brings it back.

**The main scene** already took its samples from the game's forward target,
which follows the setting on the next frame (stage 3). The renderer's own
scene target took its count from the window; it now takes it from
`R_ViewFramebufferSamples`, which is the window's count where the window
carries samples and the setting otherwise.

**Views drawn straight to the window** were antialiased by it: GUI
`renderDef` windows, and remote cameras, mirrors and other capture-backed
subviews before they are copied into their textures. On a single-sample
window such a view draws into a window-sized multisampled target that stands
in for the window (`RB_WindowView_Begin`, `RB_WindowView_End` in
`draw_common.cpp`, through `R_SetDefaultRenderTarget`, the mechanism a VR eye
uses). The back end keeps drawing "to the window" and none of its target
logic changes. The same idea as
[Vulkan's window views](../vulkan-captures.md#window-view-multisampling-6-november-2026):

- The view's rectangle is first seeded with the window's pixels, because a 3D
  view clears only depth and draws over what the GUI put there.
- Afterwards the rectangle is resolved back into the window. A subview, which
  is copied into a texture next, is averaged into a single-sample target
  first, because that copy read a multisampled window with
  `glCopyTexSubImage2D`, a plain average. Any other view is blitted straight
  into the window, the window system's own downsample.
- A window stores its rows top-down, so its sample pattern is the mirror of a
  framebuffer object's. Where sample positions are programmable
  (`ARB_sample_locations`) the stand-in takes the mirrored pattern.
- A colour copy out of the multisampled stand-in resolves with a blit
  (`idImage::CopyFramebuffer`); the depth copy already handled a multisampled
  default framebuffer.
- It does not engage where the window carries the samples, under VR, or for a
  view that already draws into a render texture. `r_glWindowViewMultisample 0`
  turns it off, and 2 prints each view it takes.

**The Display page.** `EngineSettingsDisplayHost::WindowMultisampling` is
true for OpenGL ES, and for desktop OpenGL only with `r_glWindowMultisample
1`. Otherwise an MSAA change is a display transition that asks for the window
the device has, as on Vulkan, with Keep or Revert unchanged. If the setting
was changed without restarting the renderer, the request names another
sample count than the window has, the transition is refused and the Apply
falls back to the device restart.

### What changes for the picture

- With MSAA on, 2D drawn straight to the window (interface edges that are
  geometry, not texture) is no longer multisampled under OpenGL. It never was
  under Vulkan.
- Without `ARB_sample_locations` a window view keeps the framebuffer
  object's own sample pattern: in the laboratory's GUI view that was 52 edge
  pixels different by up to 5 levels.
- At 8 samples the old window's own downsample was not a plain average on the
  test driver. See the comparison below.

### Qualification, stage 6

`tools/tests/renderer_view_capture.py` (six cases at 4x MSAA: a remote camera
on a screen, its control, the same inside the legacy 50% crop, a cropped
`envshot` cube, a GUI `renderDef` window away from the origin, and
`vid_restart`), one build, captured with `r_glWindowMultisample 1` (the
multisampled window, the reference) and 0. NVIDIA, hidden window, in a
worktree build of `f08a72d24` plus this change:

- **4 samples.** Eleven of the twelve images (six screenshots, six cube
  faces) are identical pixel for pixel. The GUI view differs in 17 pixels by
  at most 2 levels. The harness's own OpenGL against Vulkan comparison passes
  with the new OpenGL, as it does with the old.
- **8 samples.** Ten images are identical. The cube face that shows the
  remote camera differs in 96 pixels by up to 55 levels, and the GUI view in
  12 pixels by up to 3. Vulkan, which averages its samples plainly, agrees
  with the new OpenGL within 2 levels on every image and fails against the
  old window on exactly those two. The window's 8-sample positions are the
  mirror of the target's, so the pattern is the same; the old window's
  downsample was the driver's.
- The in-game remote camera views of the sequence draw into the game's
  render target, not the window, in both modes.

How the differences were found: a blit straight from the multisampled target
into the window made a captured subview's edges up to 59 levels brighter than
the reference at 4 samples, and the two-step resolve removed that exactly.

### Engine checks, stage 6

Hidden window, console verbs only, the same build:

- **MSAA on the Display page** (OpenGL): one step up applied as a transition
  and kept, with no device start. With `r_glWindowMultisample 1` the same
  step restarted the device, as before.
- **MSAA from the console in a map** (`game/airdefense1`): 0 to 4 to 0 to 2,
  the forward target and `gfxInfo` reporting each count as effective, no
  device start.
- **Every preset on the page** (OpenGL): no device restart at all, where
  there were six.

Not run: a visible window, an Intel or AMD driver, a driver without
`ARB_sample_locations`, the classic interface's `renderDef` pages in a real
menu, a tiled screenshot larger than the window with a window view in it, VR
on OpenGL, macOS and OpenGL ES (both keep the multisampled window), and a
context without multisample textures.

## What these stages do not do

- The image reload still blocks while textures load. It is shorter than a
  device restart because the device, fonts, generated images, vertex caches
  and the world's derived data stay, but it is not spread across frames.
- The in-place route has no readback of image dimensions. Its evidence is the
  exact settings readback, the same device generation and a later presented
  frame, as for the combined executor's restart.
- Apple platforms and OpenGL ES keep the multisampled window, so an MSAA
  change still restarts the device there.

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

- `tools/tests/renderer_gl_window_view_contract.py` (stage 6): the policy and
  its latch, the sample counts, the window view's conditions, seed, resolve
  and placement in the view, the copy out of a multisampled stand-in, and the
  Display page's gate.
- `tools/tests/renderer_gl_display_policy.py` (stage 6): a window that carries
  no samples leaves the count to the setting, a stand-in reports its own, and
  unknown window samples still authorize nothing.
- `tools/tests/ui_settings_display_service.py` (stage 6): an MSAA change and a
  preset that changes MSAA are transitions on a single-sample OpenGL window,
  kept and restored, and a device restart where the window carries the samples.
- `tools/tests/renderer_upload_storage.py`
  (`tools/tests/native/RendererUploadStorageTest.cpp`, stage 5): a changed
  size or depth replaces the frame buffers alone at `BeginFrame`, on each
  upload path, under a new generation with nothing leaked; the persistent
  fallback and a stream that cannot be created at all.
- `tools/tests/ui_settings_display_service.py` (stage 5): a preset that
  keeps the display restarts nothing on OpenGL, and an upload change rides
  along with a window transition.
- `tools/tests/material_quality_reparse_contract.py` (stage 4): every
  non-forced hint site marks the material; the check runs at the start of a
  frame, stays pending during a level load, re-parses only stale readers,
  passes on the level-load lifetime and purges only unheld images; the
  in-game restart saves, writes the settings, then queues the reload.
- `tools/tests/ui_rendering_settings.py` (stage 4): the restart is offered
  in a game that can be saved and refused in one that cannot; a failed save
  or a failed settings write queues nothing; the main-menu path is unchanged.
- `tools/tests/ui_system_settings_host.py` (stage 4): the high-quality
  setting no longer waits for the next start; the sound limit still does.
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
