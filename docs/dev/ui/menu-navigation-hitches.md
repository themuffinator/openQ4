# Menu navigation hitches

A round on 12 November 2026 measured where the new interface's menus still
stalled while a player moves through them, and removed those stalls. It
follows the [page loads](menu-page-loads.md) round of 11 November, which
moved the documents' parse onto the job workers.

## What a walk through the menus showed

`.tmp/menu-hitch/nav.py` walks the menus with console verbs only (no host
input): the title, Single Player, Campaign and its difficulty and CHAPTERS
pages, SETTINGS and its four sub-pages with their tabs, Multiplayer, Create
Server, Player Setup and Load Game, then the same tour again. Each action is
marked in the log, `com_speeds 1` numbers the frames and `profileFrameWork`
records every frame's phases; `--sample` adds CPU stacks from
`tools/debug/cpu_sampler.py`, attributed to the frames they fell in. The
client ran hidden at 1280x720 from `builddir` (debugoptimized) on a shared
laptop with other clients running, so absolute times are high; the shape
repeated on OpenGL and Vulkan.

Every first visit to a page held three long frames:

- **The press** built the page's view: CONTROLS 88-206 ms, GAME OPTIONS
  61-212 ms, the Campaign pages 9-30 ms. A third of CONTROLS' load was not
  the view at all: the load read and inflated the whole 5.5 MB document again
  to compare it with the parsed one, and encoded the whole source as JSON for
  the snapshot identity.
- **The first presented frame** compiled every vector path the page draws
  (40-140 ms on OpenGL, up to 300 ms on Vulkan under load). SETTINGS' arrival
  also read and inflated its four sub-pages' documents on the main thread to
  hand them to the parse workers (46 ms of the same 189 ms frame).
- **The third frame** rasterized the page's coverage atlas on the main thread
  and uploaded whole 2048-square atlas images, each first assembled into a
  fresh 16 MiB copy (25-60 ms).

A frame that drew more GUI vertices than any before it also copied the GUI
model's lists a thousand elements at a time.

DISPLAY was worse on every visit, not only the first: 330-620 ms on OpenGL,
120-280 ms on Vulkan, in the session phase. Its GPU list loaded the Vulkan
renderer module, created a Vulkan instance across both GPUs of this hybrid
laptop and unloaded it again, and the page is a new instance each open.

Second visits to the other pages were 10-30 ms frames.

## What changed

### Pages are made ready while the page that leads to them rests

`idSessionLocal::WarmRetainedPages` runs before the page on screen draws.
Once the page and its focused control have been unchanged for 500 ms, each
frame takes one step for the pages it leads to:

| Page shown | Pages made ready |
| --- | --- |
| The title | Single Player, SETTINGS, Multiplayer, Load Game |
| The pause | SETTINGS, Load Game |
| The Strogg pause | SETTINGS |
| Single Player | Campaign, Arena |
| Campaign | Campaign difficulty, CHAPTERS |
| SETTINGS | CONTROLS, GAME OPTIONS, AUDIO |
| Multiplayer | Create Server, Join Game, Demos |

- A page whose parse the shown page started, and that has finished, is
  loaded: the view is built now, at rest, instead of in the frame that opens
  it.
- A loaded page that has never presented is prepared out of sight
  (`UI_RetainedWarm`): a hidden frame gathers the vector compilations its
  first presented frame would make (`openq4::ui::DeferVectorCompilations`)
  without tessellating anything; the job workers compile them in sixteen
  interleaved slices (tessellation is pure); this thread installs them in
  each element that has still never drawn; and a last hidden frame converts
  the meshes and draws the layers. The frame that presents the page then
  finds its artwork compiled.
- Any navigation restarts the wait, a change of page, a message box or a
  multiplayer game stops it, and a compilation in flight is still installed.
  An element that drew, changed or went in the meantime takes nothing.
- The hidden frames never count as resting frames for the coverage atlas, and
  a frame that lacked its artwork is never replayed.
- DISPLAY is left alone: it is a new instance each time it opens.
- `ui_retainedWarm 0` turns the preparation off.

This revives, in a general form, the title warm-up of 15 October
(`refs/archive/claude/peaceful-jemison-661a2c`), which was never merged: it
drew four preloaded pages in full on the resting title, about 50-75 ms of
main thread each.

### The coverage atlas works off the main thread

Also from that unmerged line, brought up to date:

- A mesh that has rested for two frames is rasterized on the job workers from
  a copy of its vertices, and a later frame places its tiles, all of them or
  none. It draws as geometry meanwhile.
- Each page uploads only the rectangle its new tiles cover, straight into
  the image's storage: `idRenderSystem::UploadImageRegion`, render API 35.
  Vulkan stages a pitched rectangle's own rows instead of reading past it.
- The atlas images keep their pages in place, so an upload no longer
  assembles a 16 MiB copy first.
- Rasters that finish in the frame after an upload wait one frame, because
  uploads in successive frames stall Vulkan.
- A full atlas takes back the tiles of meshes not drawn for 60 frames.
- A print inside the atlas pass that redraws the loading screen during a map
  change leaves the atlas to the pass already running.

### Loads read less on the main thread

- The parse workers now read the documents too. The file opens on the main
  thread (the open walks the shared search paths, as a streamed sound's
  does) and the worker reads and inflates it.
- A load takes the prefetched source when the file's length and content
  stamp (a pk4 member's CRC) still match, instead of reading the whole
  document again to compare it.
- A document's snapshot identity is encoded once and shared by its copies;
  the parse worker encodes it for a prefetched document.

### Smaller fixes

- The DISPLAY page reads the GPU list the last probe took
  (`R_RendererModule_RecentRenderingDevices`). A live Vulkan module still
  answers from its own instance, and applying a GPU choice still probes
  afresh, which also refreshes the list.
- The GUI model's lists grow by half instead of 1,000 elements at a time.

## Results

## Evidence, 12 November 2026

## Left for later

- DISPLAY is still a new instance, load and first draw each time it opens.
  Keeping it, as the other settings pages are kept, needs the settings
  transaction's reopen path qualified first.
- A view build is still one main-thread step. Loaded ahead, it lands on a
  resting frame (CONTROLS and GAME OPTIONS are the largest); building a view
  across frames would need a resumable `Runtime::LoadDocument`.
