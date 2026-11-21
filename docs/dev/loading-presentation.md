# Loading and startup presentation

The startup screen and the level loading screens must keep animating smoothly
while the engine loads and caches. This record describes how openQ4 presents
frames during blocking work, the rules for code that runs inside it, how to
measure it, and what is still open. Roadmap: FPS-07 (hitch catalog), level-load
class.

## Why the screens hitched

Startup (`idCommonLocal::InitGame`) and a level load (`idSessionLocal::ExecuteMapChange`)
run on the main thread, which owns the renderer, the declaration manager and the
interface. Nothing else can present a frame, so a loading screen is drawn only
when the work offers one: the file system every MiB read, every console print,
the image loader after each image, and a few loops. Two separate pacers then
decided whether the offer drew:

- the startup screen's (`InitializationPresentation`) admitted at most 60 Hz,
  and after its 1.4 s entrance reserved four times each frame's cost for
  loading, which held a 7 ms frame to about 28 fps by design;
- the level pacifier admitted at most 60 Hz and backed off to four times the
  last redraw's cost.

Between offers there was nothing to present at all. Measured on the reference
laptop (OpenGL, hidden 1280x720 window, warm caches, Air Defense 1), startup
presented 79 frames in 3.1 s (median gap 30 ms, worst 121 ms) and the level load
203 frames in 9.6 s (median 22 ms, p95 115 ms, worst 2.8 s).

The longest gaps had no offer in them: skipping the opening cinematic in the
settle frames (one `idGameLocal::RunFrame` that runs about 11,600 game frames,
2.8 s), reading the header of every level image before the image loader starts
(0.4-2.2 s), parsing the collision and `.proc` files, model and material
parses, seeking inside compressed model files, the game script compiler, stock
menu parsing at startup, TrueType atlas builds and waits for worker parses.

## How it works now

**One pacer.** `openq4::LoadingFramePacer` (`src/framework/LoadingFramePacer.h`)
paces both the startup screen and the level pacifier. Frames are scheduled on a
deadline cadence, start to start, at the display's refresh rate or its largest
whole divisor of at most 90 Hz (60 Hz presents 60, 144 Hz presents 72, 240 Hz
80), so the animation advances in equal steps. A lower `com_maxfps` is
honoured. The cadence slows by whole refresh periods only while the cost of a
frame (the median of the last five, so one slow present is ignored) would
exceed 40% of the time, and speeds up again only when the cost fits the faster
cadence with a 10% margin, so it cannot alternate between two rates. A frame
that is less than a quarter period late keeps the cadence; a later one
restarts it, so there are never catch-up bursts. The work always
keeps 40% of an interval after every frame. The first frame of each period
(building the screen's view, uploading the levelshot) is not charged. A paced
level-load frame begins with `openQ4_BeginPacedPresentationFrame`, which starts
the presentation clock without the frame limiter's sleep: with `com_maxfps` at
the display rate (a detected preset's cap) the limiter's own schedule used to
sleep on top of the pacer, up to a frame each time.

**Checkpoints.** `idCommon::LoadingCheckpoint()` offers a frame from anywhere:
the engine, the renderer modules and both game modules. It returns at once off
the main thread, outside blocking work, and when no frame is due, so it is
cheap enough for every item of a long loop. Checkpoints sit between items,
where nothing a loading-screen redraw touches is half-built:

| Where | Granularity |
| --- | --- |
| `idDeclManagerLocal::FindType` | after each declaration parse |
| `idGameLocal::SpawnMapEntities` (SP and MP) | each map entity |
| `idGameLocal::RunFrame` cinematic skip (SP and MP) | each skipped game frame |
| `idCompiler::ParseStatement` (SP and MP) | each script statement |
| `idAnimManager::GetAnim` (SP and MP) | each animation loaded |
| `idWindow::FixupParms` | each stock GUI window |
| `idRenderWorldLocal::InitFromMap`, `ParseModel` | each `.proc` model and surface |
| Collision model vertices, edges, polygons | every 256 vertices/edges, each polygon |
| Level image header prediction | each image |
| `idFile_InZip::Seek` | each 32 KiB re-inflated |

Rules for new checkpoints: only on the main thread's own loop, between items;
never while GL or Vulkan state for an item is half set, never inside the
retained interface's document build (its views are not re-entrant), and never
inside font registration (a redraw can register fonts).

**Presenting while waiting.** Where the main thread waits for a worker it keeps
presenting: a retained document parse (`UI_RetainedTakePrefetched`) and a level
image file still being read by the prefetcher (`idLevelImagePrefetch::Settle`).

**Less work.** Three costs that dominated gaps were removed outright, with
identical results:

- *Hit-material maps* (`MT_GetMaterialTypeArray`): every pixel searched every
  material type, twice when no tint matched. Each distinct tint is now searched
  once.
- *Script immediates* (`idCompiler::FindImmediate`): every constant a script
  names searched the whole list of constants. A value-keyed index returns the
  same def (only `GetImmediate` creates immediates, and only when none matches;
  floats are keyed by `==`, NaN and over-long strings are never indexed, and
  freeing any immediate rebuilds the index). The compiled program is
  unchanged: `disasmScript` output for the game scripts and Air Defense 1 is
  byte-identical, and a save written by the previous build loads (the
  program checksum covers every operand's def number).
- *TrueType atlases*: a face's glyphs rasterise on up to seven worker threads
  plus the main thread, merged in index order (`R_TTFForEachGlyphBlock`).

**Continuity.** The startup screen's view stays alive after startup, so the
session's fallback loading screen (shown until the first menu or level) draws
the same view on instead of building a new one in its first frame, and the
animation does not restart.

## From the startup screen to the first loading screen

A launch with a level on the command line (`+map`, as the VS Code launches
use) went from the startup screen to a 240 ms fade to black, then held black,
with the window not answering, while the session unloaded, purged, reloaded
its menus and built the loading screen: 224-352 ms in Air Defense 1, at least
0.4 s in The Awakening's Cryofac, which builds its pause, save and restart
pages then. When a map change starts with no menu and no level up and the
startup view still alive (`startupLoadingHandoff`), it now skips the fade and
keeps presenting the startup view through that work, handing straight to the
loading screen's first frame. The menu's own fade into a load is unchanged.
Offers draw the view only where the interface can (`RetainedUI_CanUpdateInitialization`),
and the fallback screen's menu watchdog waits while the hand-off is under way.

With the hand-off now measured to the next period's start: Air Defense 1
presents 14-19 frames from the end of startup to the loading screen, longest gap
23-43 ms, on OpenGL and Vulkan; Cryofac 18-20 frames, longest gap 37-39 ms. A
multiplayer server started the same way still has two gaps of 77-164 ms
where it builds its multiplayer menus (retained document builds, below), with
the startup view on screen instead of black.

## The hand-off into the level

When a single-player load ended, the last loading frame stayed on screen for
200-380 ms before the fade into the level began. Timing each step showed three
parts:

- **The working-set trim (100-130 ms).** Doom 3 raised the process's working
  set for a load and reset it afterwards with `Sys_SetPhysicalWorkMemory( -1,
  -1 )`. Only the reset survived, and on Windows it empties the working set,
  after which the first frames page everything back in. It is gone; the heap
  release earlier in the load already returns what the load freed. This alone
  also took the multiplayer hand-off from about 720 ms to 120-240 ms.
- **The start-of-level autosave (50-120 ms).** Its payload write, flush and
  read-back now run on the checkpoint storage worker and it finishes on a
  later frame ([savegame reliability](savegame-reliability.md#performance-characteristics)).
  The hand-off keeps only the 15-25 ms snapshot.
- **The first frame of play (26-86 ms).** Programs linked and render targets
  made on first use, the driver's first draws and the HUD's first raster.
  `idSessionLocal::WarmFirstGameFrame` draws the player's view once, unseen,
  at the end of a single-player load, the way a save preview draws into a
  capture, and the loading screen draws over it. It costs 10-15 ms behind the
  loading screen, and the first frame of play falls to 8-14 ms. A multiplayer
  load skips it: a client has no snapshot to draw yet
  (`com_loadingWarmFirstFrame 0` turns it off).

`com_loadingFrameTrace` now also reports the first two seconds after each
period ends (`LOADING_HANDOFF`, with its longest gaps). Air Defense 1 on the
reference laptop, warm launches (seven before, three after on each renderer,
the last on a busier machine):

| Hand-off | Before | After |
| --- | ---: | ---: |
| Longest gap, OpenGL | 250-382 ms | 33-45 ms |
| Longest gap, Vulkan | 198-342 ms | 26-50 ms |

The fade into the level and its first frames run at 16.7 ms (p95 18-19 ms).

## Measuring

`com_loadingFrameTrace 1` reports every blocking period when the next frame of
any kind presents:

```
LOADING_FRAMES kind=map msec=6393 frames=363 interval p50=16.7 p95=22.9 p99=32.4 max=57.9 over25=15 over50=2 over100=0 draw p50=0.78 p95=1.73 first=2.1 tail=321.9
LOADING_GAP kind=map msec=57.9 at=+3915
```

`first` is the time to the period's first frame and `tail` the time from its end
to the next frame (the hand-off to the menu or the game). `LOADING_GAP` lists the
twelve longest intervals over 25 ms.

`com_loadingFrameTrace 2` (Windows x64) also samples the main thread's stack
whenever no frame has presented for `com_loadingStallMsec` (default 40 ms),
from a watcher thread that suspends the main thread only to copy its stack and
unwinds the copy afterwards (`src/sys/StallSampler.cpp`). Each stall prints the
call path most of its samples share and its hottest leaves, and
`LOADING_STALL_HOT` ranks the functions inside the most stalled samples across
the whole period. `com_loadingStallFocus <name>` ranks only the samples inside a
function whose name contains `name` (`LOADING_STALL_FOCUS`), and a threshold of
5 ms profiles nearly the whole load, frames included. Symbols come from the
PDBs beside the binaries. The trace cvars are not cheats, so a multiplayer
server spawn keeps them.

Probe recipe: a hidden client from a fixed runtime (`.tmp/runtime/candidate`),
an isolated `fs_savepath`, `com_skipLoadingContinue 1`, `g_autoSkipCinematics 1`,
`g_autoExecAfterMapLoad` with a `quit` cfg, `+map game/airdefense1`. Discard the
first launch of a fresh save path (it runs the first-launch preset, a renderer
restart) and judge only runs on a quiet machine: a busy one stretches every
stall.

## Results

Air Defense 1 and startup on the reference laptop (RTX 4060 Laptop), hidden
1280x720 window, warm caches, three interleaved launches of each build per
renderer, medians, with peer sessions keeping the machine about half busy.
Intervals are between consecutive presents, in milliseconds.

| Screen | Renderer | Build | Frames | Median | p95 | p99 | Worst | Gaps over 50 ms | Duration |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Startup | OpenGL | before | 66 | 25.1 | 65.0 | 75.2 | 94.1 | 12 | 2.02 s |
| Startup | OpenGL | after | 82 | 17.3 | 47.2 | 62.4 | 129.7 | 3 | 1.81 s |
| Startup | Vulkan | before | 67 | 23.3 | 66.9 | 88.4 | 89.8 | 11 | 1.94 s |
| Startup | Vulkan | after | 80 | 22.8 | 47.8 | 67.2 | 78.5 | 3 | 2.14 s |
| Level load | OpenGL | before | 157 | 22.0 | 93.2 | 176.0 | 1645.5 | 16 | 6.48 s |
| Level load | OpenGL | after | 389 | 16.7 | 22.6 | 32.9 | 60.3 | 1 | 6.73 s |
| Level load | Vulkan | before | 156 | 21.1 | 88.6 | 162.4 | 2296.1 | 17 | 7.13 s |
| Level load | Vulkan | after | 386 | 16.7 | 21.6 | 38.4 | 53.9 | 2 | 6.75 s |

The level load now holds the display's 60 Hz throughout on both renderers; the
multi-second freeze in the cinematic skip is gone. The load itself takes as
long as before (6.19 against 6.44 s on OpenGL, 6.84 against 6.46 s on Vulkan,
within run-to-run noise): the extra frames cost about what the removed work
saved. The game scripts compile in 0.40-0.46 s instead of 0.77-0.99 s.

Startup is bounded by the startup view's own cost: about 4.5 ms a frame on
OpenGL and 7.5 ms on Vulkan on the busy machine of the table, which put Vulkan
over 40% of a 60 Hz frame for part of startup. The build as committed (cost
median, 10% speed-up margin, no limiter sleep) was then run four more times,
warm, on a quieter machine: startup took 1.44-1.56 s at a 16.8-17.1 ms median
(p95 30-33 ms) and the level load presented at 16.7 ms (p95 21-22 ms, worst
44-55 ms, at most two gaps over 50 ms) on both renderers. The worst startup
gap left is the first frame's driver warm-up on OpenGL (about 117 ms) and the
retained document builds (Open, below).

A multiplayer listen server's `mp/q4dm1` load presented 230 frames in 4.3 s at a
16.8 ms median; the stock interface (`ui_retained 0`) loads and starts normally.

## Open

- **Building a retained document** (`Runtime::LoadCanonical`, 50-80 ms each at
  startup for the preloaded menus, and the HUD at the player's spawn) cannot be
  split: the interface cannot draw another view while one is half-built.
  Making the build itself cheaper is the lever.
- **One large file or animation** (a big `md5anim`, a cube map's DDS on a cold
  disk) is a single read or parse of 40-60 ms.
- **A multiplayer hand-off** still draws its first frame cold (120-240 ms
  under the held loading screen): a client cannot draw before its first
  snapshot, so it gets no warm-up.
- **Cold program compiles in the warm-up.** The first-frame warm-up moves a
  level's first-use compiles behind the loading screen, where a slow one is
  still a gap: Cryofac's shadow-map program takes 171 ms in the NVIDIA OpenGL
  driver. Compiling programs asynchronously would remove it.
- **The first startup frame** waits about 120 ms in the OpenGL driver.
- **A first launch** restarts the renderer for its detected preset after
  startup, with no frame for about three seconds.
- Vulkan render-thread presentation (REN-07) would let presentation overlap the
  work instead of interleaving with it; it is not part of this change.
