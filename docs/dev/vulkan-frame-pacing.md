# Vulkan frame pacing investigation

Status: unresolved for the conditions described below. Windows x64 NVIDIA/Intel
Vulkan support does not qualify every presentation/power configuration; Linux
and macOS remain experimental. This investigation separates
CPU work, GPU execution and synchronization waits; it does not establish a
performance improvement or complete renderer qualification.

## Hidden windows and a sleeping display (2026-10-06)

Hidden-window Vulkan captures (`r_hiddenWindow 1`) on the RTX 4060 laptop ran
at a few frames per second for most of 2026-10-06. `com_speeds` showed frames
alternating between about 40 ms and 1 s, or holding near 260 ms, and a
retained-UI dialog whose focus lands on the frame its timeline ends missed
fixed `waitMsec` waits. OpenGL captures of the same scenarios kept their usual
length.

Every slow run falls inside a Modern Standby period. The System event log has
the laptop entering it (Kernel-Power event 506) at 10:06:21 and leaving it (507)
at 17:49:10; the display was off in between while agents kept capturing. The
same Create Server capture took 23–29 s before 10:06 and 63–87 s during
standby. Join Game captures took 121–305 s on Vulkan, two of them ending at the
300 s timeout, against 77–103 s on OpenGL. Once standby ended, the identical
hidden scenario paced at an even 16.7 ms (`com_maxfps 60`), and
`rendererDisplayProbe report` counted no failed acquisitions.

The only one-second constant in the Vulkan frame path was the swapchain
acquisition timeout. A hidden window's FIFO presentation engine releases images
at vblank. With the display off, the frame times (about 260 ms, 500 ms and
1 s) show it handing them back only every quarter to whole second, so an
acquisition often waited out the whole second and the frame was skipped
(`RDP_ACQUIRE_FAILED`, `VK_TIMEOUT`), which gives the alternating pattern. The diagnostic `r_vkPresentationFailureTest 3` reproduces that state
with the display on: it holds every image the engine releases until it is set
back to 0, which returns them with a swapchain rebuild. In a hidden window
that still presents (`r_vkHiddenWindowPresent 1`, the old behavior), a
six-second `waitMsec` under the drill took 113 s: all 113 acquisitions timed
out, because a skipped frame retries acquisition at each of its later render
commands. With the change below, the same drill holds 60 fps (median 17 ms,
maximum 30 ms) with no failed acquisition.

A hidden window no longer uses its swapchain for frames. Each frame renders
into its frame slot's offscreen color image, with the swapchain's format,
extent and usage. Screenshots and back-buffer copies read it as they read a
swapchain image. The frame completes without acquiring or presenting, and
counts as presented, as a GL swap of a hidden window does, so display-change
confirmation and `rendererDisplayProbe` behave as before. The swapchain is
still created and recreated for resizes and interval changes, so present-mode
reports are unchanged. Hidden frames are paced by the engine's frame cap:
Windows caps hidden windows at 60 fps, and other platforms use `com_maxfps`.
Visible windows are unchanged and keep the one-second timeout that protects an
occluded Wayland window or a stalled compositor. `VK_Device_PresentClearFrame`,
an uncalled Phase C helper that acquired with no timeout at all, is removed,
and `renderer_display_presentation.py` rejects any unbounded acquisition.
At one frozen pose in Air Defense 1, an offscreen screenshot and one taken
while presenting (switched with `vid_restart partial`) are pixel-identical,
and the retained-UI title capture writes all six screenshots in 20 s. With the
display on and the machine otherwise idle, offscreen frames are as even as
presented ones: 60 fps, median 17 ms and at most 19–23 ms on the title and in
Air Defense 1, with no Vulkan validation-layer message for either path.

Hidden-window measurements no longer include presentation. The hidden-window
stalls in the sections below came from the cross-adapter presentation path. To
study that path in a hidden window, set `r_vkHiddenWindowPresent 1`; it applies
when the swapchain is next created, for example at `vid_restart`.

## Reproduction and localization

The v56 stock `game/storage1` comparison retained five runs before and five
after the point-shadow correction. All ten exceeded the unchanged 28 ms CPU
p99 budget, at approximately 0.39–0.41 seconds. Their GPU p99 stayed below
10 ms. These are live SP scenes, not frozen visual comparisons. The v58 entry
audit confirms that this map command selects the `first` entity filter and
starts the scripted drop-pod sequence; it is not a static indoor viewpoint.

The v57 probe uses the same 1280×720 windowed, 240 FPS limit, vsync-off,
stencil-shadow profile with 360 settling frames and 1,200 sampling frames.
PBR, HDR, bloom, temporal AA, post AA and MSAA are disabled. Mouse/controller
input is disabled. Engine `screenshot` supplies the retained images.

The first unchanged-runtime CSV records six CPU frames over 28 ms in its
256-frame history. Command scopes then localize the long frames to the first
render-target command. Finer scopes identify the frame-slot fence wait:

| Diagnostic | Observed result |
|---|---|
| Ordinary wait, with status queried first | 28 waits of 364–401 ms; the fence is unsignaled before each wait and signaled afterward. |
| Repeated 1 ms waits, still requiring completion | 29 waits of 371–461 ms, requiring 292–370 calls. This does not fix the stall. |
| Hidden window explicitly unable to take input focus | 28 waits of 358–405 ms; the stall persists. |
| Visible window explicitly unable to take input focus | 16 waits of 405–456 ms; visibility alone does not fix it. |

All are on the local RTX 4060 Laptop GPU / Windows / NVIDIA 591.74 setup.
The visible probe is still a background window, not foreground gameplay
qualification. Shader work, presentation scheduling, driver behavior and
system scheduling have not yet been independently isolated. A wait on a frame
fence does not, by itself, identify which of those causes is responsible.

GPU timestamps retain their own frame number and generation. In the detailed
probe, the slow CPU rows collect results for the frame being retired, two
frames earlier, with GPU spans of roughly 0.3–1.7 ms. GPU execution spans do
not measure every delay before queue execution or host wake-up.

An OpenGL control on the same staged package also fails: CPU p99 is 464.720 ms
and GPU p99 is 458.548 ms. Its five slow CPU rows spend 438–459 ms in the
existing upload/resource-retirement scope. Vulkan's two new wait counters are
zero on every OpenGL row, as intended. This establishes a similar symptom on
both APIs; it does not yet prove that their underlying causes are identical.
Keep the GPU frame IDs when comparing the asynchronous records.

The stock capture also exposes a test limitation: the live start/cinematic
state can leave the camera obscured by nearby geometry. These runs reproduce
stalls but do not establish representative scene coverage or visual parity.
The separate v58 gameplay case below checks a settled viewpoint. Broader scene
coverage remains part of performance qualification.

The fence interpretation follows the Vulkan reference for
[`vkGetFenceStatus`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetFenceStatus.html)
and [`vkWaitForFences`](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForFences.html).
All temporary logging, status polling, bounded waits and window-behavior changes
are removed from production. The original completion/error handling is retained.

## Retained diagnostic counters

With `r_rendererMetrics 1`, `rendererBenchmarkCapture "logs/frame-timing.csv"`
writes the current 256-frame history under the active game's save path. The CSV
appends `frameFenceWaitUs` and `acquireImageUs` to the existing columns. Acquisition
retries accumulate their API-call durations; swapchain recreation is not charged
to the acquisition counter.

Vulkan also supplies the existing final-post, window-state, submission/present
and resource-retirement counters. `swapUs` includes command-buffer submission
and presentation. `uploadRetireUs` covers reuse of the retired frame slot,
including readbacks, probe/descriptor reset, upload retirement and deferred
destruction. These are CPU durations. The GL-only finish/context phases remain
zero on Vulkan; zero in an uninstrumented field is not proof that work is free.

The probes use a diagnostic wrapper to request the optional CSV and admit the
existing stable candidate runtime path. They are not unmodified, replay-qualified
benchmark runs. Original failed reports are retained. No performance threshold,
rendering quality or support status changes.

Evidence, exact runtime/source identities, temporary source variants, engine
logs and captures are under `.tmp/vulkan-gap-closure/frame-stall/`.
`analysis.json` correlates the CPU rows, fence scopes and delayed GPU records.

The final staged build passes the existing benchmark contract self-test and
the Vulkan startup/render-target, HDR/restart and temporal-motion GPU groups,
with zero tracked warning signatures. The shared benchmark self-test also
passes with an explicit hidden, input-disabled launch override; its generic
matrix entry does not itself supply those settings.

The final Vulkan gameplay trace has 256 consecutive CPU samples and six long
frames. Frame-fence waits account for 96.1–97.8% of those CPU intervals. Maximum
measured acquisition is 36 microseconds and submission/presentation is
1,003 microseconds. Every delayed GPU record retains the retiring frame's
identity. Both final GL/Vulkan gameplay probes exit cleanly, but their timing
budgets still fail. The retained change improves observability, not performance.

## Storage1 entry and viewpoint correction

The old `sp-storage1` description called the stock start a static gameplay view.
The map loader actually defaults this map to `first`, which runs the drop-pod
intro. In the retained v58 control, the camera moves 27,935 world units and
15.6 degrees between the sampling endpoints. That workload remains a useful
scripted-sequence test but cannot establish static indoor coverage.

The stock `second` entry starts on a moving lift. A 360-frame settling probe
still moves 68.7 world units during sampling; 720 settling frames reaches the
stationary landing on a development build. The lift rises in game time, not
frames: on the optimized 0.14.1 release package at a 240 fps cap, 720 frames
cover only 3 s and both backends sampled the lift still rising at about 147
units/s (6 November 2026). Contract `stock-entry-view-v2` therefore follows the
frame wait with `waitMsec 6000`, and replay requires both. The reference script and map were checked byte-for-byte
against the installed retail PK4 entries before defining this case.

The harness preserves `sp-storage1` as an explicit first-entry case and adds
`sp-storage1-second` to the required scene list. Both assert their actual map
and entity filter and record the render-view pose at each endpoint. The second
entry waits at least 720 frames and checks the settled origin
`(-2592, 2504, 4.2)` and angles `(0, 90, 0)`, allowing one world unit and one
degree for pose rounding or small view motion. Schema-5 replay binds the case,
scene contract, generated config and engine-log observations. Missing, altered,
conflicting, non-finite or moving endpoint evidence fails. This is an endpoint
check, not continuous-motion or visual-parity qualification.

Unmodified harness runs against the unchanged v57 staged runtime give:

| Scene | Backend | CPU p99 | GPU p99 | Camera endpoint result |
|---|---|---:|---:|---|
| First-entry intro | Vulkan | 401.216 ms | 1.986 ms | Motion recorded; no static claim |
| Settled second entry | Vulkan | 399.167 ms | 7.262 ms | Expected pose, zero measured endpoint drift |
| Settled second entry | OpenGL | 462.683 ms | 455.567 ms | Expected pose, zero measured endpoint drift |

All three exit cleanly and have zero tracked warning signatures. Performance
probes disable Vulkan validation; these are not validation-layer qualification
runs. Engine screenshots show the same unobscured landing viewpoint on both
APIs, with live animation differences. Each report still fails the unchanged
28 ms p99 budget, and replay preserves that failure. Camera/entry checks and
negative replay tests pass. No engine, renderer, shader or game binary changes
are made for this correction.

Evidence is under `.tmp/vulkan-gap-closure/gameplay-pose/`, including both failed
diagnostic probes, three schema-5 reports, engine screenshots, source/runtime
identities and `view-analysis.json`. First- and second-entry results must be
compared separately; changing the workload does not measure a speedup.

## GPU queue and adapter investigation

The v59 probes retain the settled second-entry workload, unchanged runtime,
1280x720 hidden window, disabled input, live simulation and 28 ms p99 budget.
PBR, HDR, post AA, MSAA and Vulkan validation are disabled. GPU-only Windows
ETW traces span ten seconds of gameplay; every retained trace reports zero
lost buffers and events. A separate CPU scheduler recording was denied by
Windows (`0x80070005`), so these traces cannot establish thread readiness or
host wake-up latency.

Provider state capture maps queue handles through their context and device to
the physical adapter. The ordinary Vulkan and OpenGL runs render on the NVIDIA
RTX 4060 Laptop GPU and submit presentation packets on Intel Iris Xe queues.
This is observed in each mapped trace, not inferred from laptop hardware.

In `vk-v59e-mapped`, all 48 wait packets with more than 28 ms between their
`UnwaitQueuePacket` and `QueuePacket/Stop` events belong to Intel queues. One
example uses synchronization object `0xffffc4096744b740`, fence value 1382:

| Event | Time relative to trace start |
|---|---:|
| NVIDIA signal packet 5517 submitted | 8063.186 ms |
| Intel wait packet 1362 submitted | 8063.273 ms |
| NVIDIA signal packet stop | 8066.916 ms |
| Intel wait packet unwait event | 8066.918 ms |
| Intel wait packet stop | 8576.111 ms |

The wait remains pending for 509.193 ms after its unwait event. The neighboring
Intel presentation packet also remains pending for 512.722 ms. OpenGL's mapped
repeat has the same pattern: 64 long waits on Intel queues, with up to
507.276 ms between unwait and stop. Its 2,874 render packets all finish within
13 ms. Earlier Vulkan evidence additionally follows a render-queue dependency
to a matching signal submitted 929.563 ms after the wait; that establishes late
submission, but does not explain why its worker submitted late.

Queue intervals include time pending and must not be described as shader
execution time. See Microsoft's [context CPU queue explanation](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/context-cpu-queue).
The task-local join audit checks context, submission sequence, packet identity
and event ordering. It retains repeated queue-info events and incomplete
boundary packets separately. It also preserves completed context/sequence
reuse by other processes instead of overwriting earlier packets. There are no
ambiguous active joins or packet-identity mismatches. Exact shared-object and
positive fence values bind wait/signal candidates; OpenGL's two-context signal
is retained as two candidates, not silently collapsed to one.

### Direct-adapter control

A process-local `VK_DRIVER_FILES` override selects the installed native Intel
driver for one Vulkan run. The engine, shaders, scene, quality and benchmark
remain unchanged; GPU/driver selection changes. The override uses the
[Vulkan loader's documented driver selection](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderInterfaceArchitecture.md#environment-variables)
and is not installed as a global setting. ETW confirms that this control's
submitted game work uses Intel alone. Returning immediately to ordinary NVIDIA
selection reproduces the stalls.

| Run | Render/presentation path | CPU p99 | GPU p99 | Budget result |
|---|---|---:|---:|---|
| `vk-v59e-mapped` | Vulkan, NVIDIA/Intel | 395.288 ms | 9.144 ms | Fail |
| `gl-v59f-mapped` | OpenGL, NVIDIA/Intel | 503.884 ms | 497.638 ms | Fail |
| `vk-v59g-intel` | Vulkan, direct Intel | 17.583 ms | 10.624 ms | Pass |
| `vk-v59h-return` | Vulkan, NVIDIA/Intel again | 400.538 ms | 7.702 ms | Fail |

All four runs exit cleanly, retain the expected camera at both endpoints and
have zero tracked renderer warning signatures. Engine screenshots show the
expected landing; live animation still prevents a pixel-parity claim. The
direct-Intel trace has no half-second packet intervals; its largest render
packet interval is 48.934 ms, so the passing p99 does not mean every sample
meets the budget. These externally traced diagnostics do not replace the
required repeated, foreground, validation and platform qualification.

A VSync-on/FIFO control is worse under the hidden-window conditions: it never
reaches the sampling marker and the harness terminates it at 180 seconds.
Its separate warmup trace supplies adapter metadata, but no settled-view
timing result. The failure remains recorded. No present-mode default changes.

The evidence narrows the recurring stalls to the cross-adapter presentation
path in these tests. It does not isolate the responsible driver, scheduler,
power state, window condition or application interaction. It is not evidence
that all Vulkan performance gaps are external. No synchronization workaround,
quality reduction, threshold relaxation or renderer implementation change is
retained. The NVIDIA performance gate remains failed.

Traces, decoded events, join audits, adapter mappings, engine captures, failed
reports and exact runtime/source identities are retained under
`.tmp/vulkan-gap-closure/fence-scheduling/`.

## Visible-window diagnostics (3 November 2026)

`renderer_vulkan_presentation.py` now retains actual presented-frame intervals,
CPU/GPU timing identity, phase CSVs, camera endpoints, foreground observations,
competing-client observations, power state and engine captures. It fails closed
on missing presentation support, lost foreground ownership, contention or a
missed 28 ms presented-p99 budget. Selecting a rendering GPU does not prove its
display route; no new ETW route trace was taken.

The clean `29e237378` build ran three windowed, 1280x720, 120 FPS capped Air
Defense 1 intervals per adapter on AC, with mouse/controller input disabled.
All six are **failed qualification**, retained in
`.tmp/vk-roadmap/presentation-ac/`:

| Rendering adapter | Presented p99, repeats 1 / 2 / 3 | Additional qualification failures |
| --- | --- | --- |
| NVIDIA RTX 4060 Laptop, 591.74 | 12.001 / 65.141 / 12.115 ms | No foreground ownership; another client overlapped repeat 3 |
| Intel Iris Xe, 101.5590 | Unavailable in all three | Driver lacks `VK_KHR_present_wait`; no foreground ownership; another client overlapped repeat 1 |

NVIDIA repeat 2 exceeds the unchanged budget without another observed openQ4
client. Its last 256 CPU/GPU samples have p99 7.204/2.390 ms; these shorter
windows are not a per-present causal trace. Foreground activation through the
window-control helper failed, including its retry. The observer recorded zero
focused samples. The earlier one-run diagnostic also records contention and
lost focus in `.tmp/vk-roadmap/presentation-diagnostic/`.

This first set additionally exposed a fixture issue: first-run hardware
detection chose enhanced/modern on NVIDIA and balanced/baseline on Intel.
The harness now pins balanced/baseline and retains the saved configuration;
the old set is not a controlled cross-adapter comparison. Resource operation
counts and deferred optimization decisions are recorded in
[resource profiling](vulkan-resource-profiling.md).

The corrected one-repeat-per-adapter set in
`.tmp/vk-roadmap/presentation-balanced/` verifies balanced/baseline settings,
unchanged runtime/harness hashes, camera endpoints, phase traces and valid
engine captures. No competing openQ4 client was observed. NVIDIA presented p99
is 11.983 ms (2,403 samples), with CPU/GPU p99 6.807/2.069 ms in the retained
256-frame window. Intel CPU/GPU p99 is 9.445/8.394 ms, but presented timing is
still unavailable. Both reports still fail foreground qualification (zero
focused observations). These are corrected diagnostic fixtures, not a passing
repeated foreground test.

The 4 November visible retry in
`.tmp/vk-roadmap/presentation-visible-nov4/` retains unchanged runtime/harness
hashes and records 2,398 presented samples with p99 13.286 ms. Foreground
ownership was observed in 116 of 188 polls, so the continuous-focus gate still
fails. Two MP-worktree clients appeared after launch and were observed during
sampling, independently failing the contention gate. The window-control helper
reported the live test window under the installed `E:\Games\OpenQ4` application
identity and refused activation because of an ownership mismatch, although the
observed process was the worktree executable. Permission to show the test was
already granted; this is not an authorization failure or passing qualification.

No driver, power-state or support-scope exception is approved by these results.
Current foreground repeats, verified direct/hybrid routing, alternate power
states and suitable presentation tracing on Intel remain open under VK-02.

### Foreground readiness and event observation (4 November 2026)

The runner now waits for fifteen seconds without another openQ4 client,
dedicated server or known compiler/build process before launch, then holds an
engine command-file gate until the actual game window has owned foreground
focus for three uninterrupted seconds. `windowFocusTest report`
identifies that window without changing focus. An explicitly authorized run may
use `--request-focus` to invoke `windowFocusTest` instead: this cheat-gated
development command requests focus once through SDL for an existing visible,
non-minimized, windowed game. It does not inject input, and a successful SDL
request is not treated as proof that the compositor granted focus.

Windows foreground events supplement the periodic polls. The observer checks the
exact reported window and its process, so focusing the engine console cannot
pass. A brief intervening focus loss resets readiness or fails an active sample,
even if both adjacent polls were focused. Hook failures and event-buffer overflow
fail closed. `foreground.json` retains readiness, event, focus and competing-client
observations; no other application's title or input is recorded. Sampling starts
only after the gate is released and ends before the engine closes its window.

Native controls cover refused window states, accepted-but-unfocused requests,
SDL refusal and report-only operation. Parser/observer controls cover brief
interruptions, same-process wrong windows, missing observations and interrupted
readiness. These controls improve the fixture; they are not presentation results.
An idle interval also does not replace waiting for an active MP test suite to
finish before beginning a qualification run.

The optional `--presentmon <console-executable>` collects a separate ETW display
trace through Intel's portable [PresentMon console](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/README-ConsoleApplication.md).
The runner uses its v2 metric schema, disables input tracking, and never installs
a service, injects code, registers a hotkey or replaces an existing trace session.
The executable hash, command and raw CSV/log are retained. The tester supplies the
tool; it is not part of the engine or release package. The local 2.6.0 executable
has a valid signature and matches its published release SHA-256
`b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af`.

`--timing-source presentmon` permits a missing Vulkan present-wait report, but
requires ETW display timing to meet the same 28 ms p99 budget. Available engine
timing must still pass its existing gate. Displayed durations use nearest-rank
percentiles and only complete intervals within the QPC-stamped foreground
window. At least 120 displayed frames, a single PresentMon stream key, 95% interval coverage,
continuous display intervals and zero reported event/buffer/present loss are
required. Non-displayed presents are recorded separately from lost trace data.
The raw hybrid-present indicator is retained; selected GPU identity alone still
does not establish a direct display route. Permission/startup and synthetic
parser controls do not qualify runtime presentation.

### Coordinated foreground repeats (4 November 2026)

The user-approved quiet window followed a completed MP test batch and held new
game launches/builds in the active Awakening chats. It began at 13:17:40 UTC and
was released at 13:27:12 UTC. The clean `3866569f` package ran the fixed
airdefense1 camera at 1280×720, balanced/baseline, a 120 fps cap, swap interval 0
and no MSAA or post AA. All six sampled intervals retained the exact game
window's foreground ownership, with no intervening focus events, observer
errors or competing game/build processes. AC power and the reported power state
were unchanged. Mouse/controller input remained disabled.

| Rendering adapter / repeat | ETW displayed p99 (ms) | Engine presented p99 (ms) | ETW maximum (ms) |
| --- | ---: | ---: | ---: |
| NVIDIA RTX 4060 Laptop, probe (reanalysed) | 11.7779 | 11.878 | 22.0940 |
| NVIDIA, fresh repeat 1 | 11.8475 | 11.709 | 27.5162 |
| NVIDIA, fresh repeat 2 | 11.2839 | 11.421 | 33.7555 |
| Intel Iris Xe, repeat 1 | 20.8023 | unavailable | 50.0175 |
| Intel, repeat 2 | 20.8484 | unavailable | 45.8209 |
| Intel, repeat 3 | 20.8631 | unavailable | 45.7961 |

Every p99 meets the unchanged 28 ms budget; the individual maximum intervals
above are retained and are not claimed to meet a maximum-frame-time budget.
Each trace has 54–55 loss-status observations with zero lost events, buffers or
pending presents and more than 99.8% display-interval coverage. The three Intel
runs contain 1,940/1,742/1,679 displayed frames and 476/651/702 non-displayed
submissions in the CPU-time window; these compositor dispositions are distinct
from trace loss. The engine's independent 256-frame CPU/GPU timing windows are
retained, rather than substituting ETW's less precise CPU estimates for Vulkan.

The first probe initially failed the status-log gate because PresentMon redirects
wide output as BOM-marked UTF-16. Strict BOM-aware decoding fixes that harness
error; UTF-8/UTF-16 zero-loss and loss controls pass. Its original failed report
is unchanged. A separate, hash-checked reanalysis of its original CSV/log finds
55 zero-loss observations and passes; it is not a new measurement. The other
five runs passed directly with the corrected decoder.

NVIDIA reports `Hardware Composed: Independent Flip`; Intel reports
`Composed: Copy with GPU GDI`, with an implicit zero stream key rather than an
available swapchain address. The [PresentMon consumer](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/PresentData/PresentMonTraceConsumer.cpp)
tracks that GDI copy through the target window and DWM. These are process-scoped
display measurements, not verification of a Vulkan swapchain address or physical
adapter-to-panel route. The raw hybrid flag is zero in both sets and does not
establish either route. Verified direct/hybrid routing, alternate power states,
the earlier failure conditions and the broader workload/platform matrix remain
open; VK-02 is not closed by these results.

Evidence is under `.tmp/vk-roadmap/presentation-foreground-nov4-{probe,nvidia,intel}/`;
the immutable-input reanalysis and its source snapshot are in
`presentation-foreground-nov4-analysis/`. Runtime, harness and tool hashes were
unchanged during each run. `runtime-3866569f.zip` retains every file from the
measured staged package, verified against the runtime manifest before later
renderer builds (SHA-256
`9dbe39c11394dab0af08b10194d37a66f5592e9fbaac3d60374c70de071da45e`).

## Next work

Distinguish presentation scheduling and host wake-up on the observed
NVIDIA-to-Intel path before changing synchronization. Further foreground tests
must respect the project's separate permission requirement for input/focus
control. The full scene/preset/platform performance matrix, visible gameplay
and the other implementation requirements remain open; this diagnostic result
does not reduce their scope.
