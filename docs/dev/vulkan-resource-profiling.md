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
