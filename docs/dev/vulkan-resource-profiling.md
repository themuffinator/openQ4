# Vulkan executor resource profiling

Roadmap VK-06 requires a measured workload before changing submission or resource
lifetime. `r_vkResourceProfile 1` enables bounded operation counters in the
executor. It defaults off and creates no GPU queries, readbacks or extra GPU
waits. `rendererVulkanMemoryInfo` prints them alongside heap and pipeline data;
`rendererVulkanMemoryInfo reset` prints the old interval and clears the counters.
A device restart clears them too. The report synchronizes the renderer thread
before reading its counters.

| Field | Meaning |
| --- | --- |
| `frames` | Newly opened executor frame slots; screenshot resumes do not start another slot |
| `imageHits`, `imageUpdates` | Reused image-generation descriptors and writes of new descriptors |
| `imageRetirements` | Old image descriptors queued for fence-protected retirement |
| `residentSets`, `retiredSets` | Current image-cache and deferred descriptor counts, even when profiling is disabled |
| `ringBytes`, `ringAllocs` | Bytes copied by the executor's mapped-ring allocator, excluding alignment padding and GPU-written reservations |
| `targetChanges` | Render-target assignments |
| `scopes`, `barriers`, `submits` | Executor dynamic-rendering scopes, synchronization2 barrier calls and queue submission attempts |

These are operation counts, not elapsed CPU/GPU time. They exclude commands
recorded in other translation units, including dedicated upload and shadow
command buffers. They cannot establish asynchronous transfer overlap or identify
a slow shader. Counts include attempts that a later view failure rolls back.
Pair them with the existing renderer CPU/GPU phase timings, pipeline creation
times, VMA heap counts and unchanged engine captures. Compare steady-state
intervals separately from loading and first-use compilation.

`tools/tests/renderer_vulkan_presentation.py` collects these counters in a visible
1280x720 stock Air Defense 1 workload, together with presented-frame timing,
engine CPU/GPU metrics, Windows foreground observations and AC/battery state.
It verifies the camera at both endpoints and samples for competing openQ4
clients once a second; an observed competitor fails the pacing gate.
It disables mouse/controller input and uses the engine's screenshot command.
It defaults to three repeats, a 120 FPS cap and a 28 ms presented-p99 budget.
It pins the balanced performance preset before applying the declared classic
rendering overrides, checks the baseline CPU/GPU timing identity, and retains
the saved configuration and per-frame phase CSV. CPU/GPU percentiles describe
the last 256 retained samples; presented intervals cover the sampled 20 seconds.
The report preserves the selected rendering GPU but labels the display-adapter
route **unverified**: identifying a cross-adapter route still needs the separate
queue trace described in [frame pacing](vulkan-frame-pacing.md).

```powershell
python tools/tests/renderer_vulkan_presentation.py --runtime-dir .install `
  --basepath 'E:\SteamLibrary\steamapps\common\Quake 4' `
  --output-dir .tmp/vk-presentation --devices 0 2
```

The caller must have permission to show the game window. Unsupported presented
timing, missing samples, an offscreen path, unobserved/lost foreground ownership,
timeouts and a missed budget fail the run. Non-Windows hosts currently lack
the foreground observer and therefore cannot obtain a foreground qualification
from this harness. A pass is scoped evidence, not support promotion or an
input-to-photon measurement.

## Initial resource measurements (3 November 2026)

The clean `29e237378` build ran three 20-second intervals per adapter on AC.
NVIDIA 591.74 and Intel 101.5590 kept their respective heap block/allocation
counts unchanged between the initial and final snapshots. Every interval kept
28 pipelines, with no creation after sampling started. Image descriptors had
0–5 updates, zero retirements and 397–401 resident sets at the end. Each frame
recorded one executor submission, four target changes, nine rendering scopes
and 34 barrier calls, with about 4.6 MiB copied into mapped rings.

These are diagnostic operation counts, not passing pacing evidence. Foreground
ownership was never observed, another client overlapped two runs, and this first
harness selected GPU-dependent first-run presets. The subsequent harness pins
the preset. The original reports remain in `.tmp/vk-roadmap/presentation-ac/`;
see [the presentation failures](vulkan-frame-pacing.md#visible-window-diagnostics-3-november-2026)
before using any timing value.

The corrected balanced-preset pair in `.tmp/vk-roadmap/presentation-balanced/`
has the same per-frame submission/target/scope/barrier counts, five image
descriptor updates per run, zero retirements and 401 resident sets. Both runs
also retain their heap/allocation and 28-pipeline plateaus. This confirms the
operation-count observation with the profile fixed; focus and presentation
limitations still prevent performance qualification.

Decisions for this workload:

- Keep the descriptor and pipeline caches: the recorded steady intervals have
  negligible descriptor churn and no pipeline churn. They do not justify a new
  cache or broader pipeline warm-up; the earlier cold-cache decision remains.
- Defer indirect drawing/Hi-Z and parallel recording until a dense or
  recording-limited workload identifies their critical path. The large
  presented-frame tail here is not explained by the short retained GPU timing
  window. These measurements cannot show that either mechanism would fix it.
- Defer transfer/upload overlap until an upload-heavy workload measures waits
  and potential overlap. Mapped-ring byte counts alone establish neither a
  transfer-queue bottleneck nor an overlap benefit.

No optimization is promoted from these short intervals. Long-session plateaus,
matching-output comparisons and improved frame-time tails remain required for
any future resource/submission change under VK-06.

## Restart residency failure (4 November 2026)

A stock map tour on the clean `3866569f` build reproduced image-table exhaustion
at the fifth map's video restart, after airdefense1, hangar1, convoy1 and hub1
followed by medlabs. The restart reloaded file handles already purged from
earlier maps; the 4096-entry table filled and logged 2,023 repeated warnings.
The owned failing run was stopped and the queued Intel baseline was cancelled.
Its raw logs, captures, configuration and unchanged runtime identity remain in
`.tmp/vk-roadmap/soak-nov4/aborted-report.json`; this is failed, incomplete soak
evidence.

Ordinary reloads now leave purged, unreferenced file images lazy. Loaded images,
active map/UI references and generated targets still reload. Checked settings
recovery retains its explicit residency snapshot as the authority. Table-full
warnings are limited to one per continuous exhaustion episode, and
`rendererVulkanMemoryInfo` now includes the resident image count and capacity.
Native controls cover a 12,000-handle catalogue with only 2,401 referenced
images, checked-recovery selection, all 4096 slots and warning recovery after a
slot becomes available. These controls pass on Windows and Linux (WSL). The
existing checked-recovery mutation controls and all 533 metadata checks also
pass on Windows.

The corrected clean `da0080eb` package completes two cycles of all eight maps
on each local adapter, with 600 frames per stop and the same fifth-map restart.
Both runs produce all 16 drawn engine captures, all 32 labelled residency
samples, zero Vulkan warnings and zero validation messages. The staged package
hashes are unchanged after testing.

| Adapter | Duration | Peak image slots | Largest same-map allocation growth | Largest same-map process-memory growth |
| --- | ---: | ---: | ---: | ---: |
| RTX 4060 Laptop, 591.74 | 690.5 s | 2,372 / 4,096 | 7.22% | 6.70% |
| Iris Xe, 101.5590 | 699.8 s | 2,372 / 4,096 | 7.22% | 15.73% |

The failing restart now changes medlabs residency from 2,370 to 2,369 instead
of filling the table. Every map's counts match between adapters. Counts increase
between the first and second visits (airdefense1: 2,023 to 2,170), so this does
not establish a long-session resource plateau. The shorter stops target the
reproduced restart defect; concurrent functional testing and the offscreen
workload provide no pacing or optimization claim.

The qualification report and hash-checked `residency-proof.json` are retained
under `.tmp/vk-roadmap/residency-fixed-tour/`. Both runs pass the bounded tour
checks and retain 117 stock-content warnings (47 unique, identical normalized
digest). These include unknown look joints, invalid reverb areas, uncached
declarations and script/entity warnings. The missing `textures/proto/cap_lg`
image is also present in the retained failed baseline. No warnings were removed
or converted into general clean-startup qualification. The exact measured
package is `runtime-da0080eb.zip`, SHA-256
`daf52c9e2804fe3536233cf34ebdf6e33a7298b4804dcd2e182df0dc9d23467e`.
