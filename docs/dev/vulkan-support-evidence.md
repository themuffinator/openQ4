# Vulkan qualification evidence

Roadmap VK-01 owns additional hardware/platform qualification; VK-02 owns
visible presentation pacing. Windows x64 NVIDIA/Intel support does not qualify
AMD, Linux hardware or MoltenVK. Software Vulkan results remain regression
coverage. This document describes the existing runner, not a support promotion.

`tools/tests/renderer_vulkan_qualification.py` runs the staged package against
the installed retail assets. It records the engine's source/version identity,
package hashes before and after testing, GPU/driver identity, loaded PK4
checksums, launch commands, logs and engine-generated captures. A dirty or
unresolvable engine version, changed package, missing capture, unexpected
renderer warning or incomplete run fails the report. Run `self-test` to check
the parser and synthetic controls without starting the game.

| Suite | Evidence | Limit |
| --- | --- | --- |
| `sp-sweep` | Opening gameplay views on all 29 stock SP maps; warnings compared with GL | Does not play each campaign through |
| `mp-sweep` | Stock MP bot matches, loopback only; warnings compared with GL | Does not establish WAN/cross-machine parity |
| `soak` | Repeated map tours, a video restart, process and Vulkan heap growth | Duration and selected map ring remain part of the result |
| `benchmark` | Repeated synchronous render timings, validation disabled | Device-idle benchmark, not display pacing or input latency |

All four suites use hidden, windowed, input-disabled runs. **Hidden Vulkan
renders offscreen.** Their report's `settings.evidenceScope` explicitly excludes
display latency and automatic support promotion. Device/preset variations may
use `--config`, but it cannot override the runner's window, input, filesystem,
logging or validation invariants. Visible VK-02 runs must retain actual
presentation timing separately, with the workload, foreground conditions,
adapter route, power state and unchanged acceptance budgets.

Example (PowerShell, from the repository root):

```powershell
python tools/tests/renderer_vulkan_qualification.py run --suite sp-sweep `
  --runtime-dir .install --output-dir .tmp/vk-qualification/sp `
  --basepath 'E:\SteamLibrary\steamapps\common\Quake 4' `
  --config vk=vulkan --config gl=gl
```

`--maps` limits a run and must remain visible in the evidence; a subset is not
the full sweep. `--resume` reuses intact passing runs only with the same runtime
and settings. Keep failed reports as evidence; do not loosen image or timing
thresholds to turn them into passes. Platform sign-off also requires VK-01's
recovery, resize, captures, long-session and final-package budget checks, plus
the material/HDR/capture contracts linked from the roadmap.
