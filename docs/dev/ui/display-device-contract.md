# Display device request and observation contract

Status: renderer foundation, 9 September 2026. API 14 provides strict window
requests, nonfatal device restart attempts and observed presentation results.
The full SYSTEM settings flow remains blocked on the integration gates below.
This checkpoint accepts none of the 271 GUI migration entries or the complete
[UI product](../plans/ui-product-completion.md).

## Private services and ownership

[RenderModuleAPI.h](../../../src/renderer/RenderModuleAPI.h) adds
`QueryWindowState` and `ApplyScreenParmsStrict` to the engine window services,
and `TryDeviceRestart` and `GetDisplayPresentation` to renderer exports. Engine
and renderer modules must use the same ABI version. These are private engine
services, not console-command forwarding or a game-module interface.

`RetainVideoSystem`/`ReleaseVideoSystem` balance a reference to the already
active SDL video subsystem. The engine retains it before device teardown, then
releases it after successful reconstruction or a refusal that leaves the old
device running. A failed attempt with no device keeps the reference through an
explicit restore (or engine renderer shutdown). Otherwise SDL shutdown/reinit
invalidates the captured display IDs before they can be used for restoration.
This lease preserves identities only in the current process/video lifetime;
a recovery journal must re-resolve monitors after process restart.

[RendererModule.h](../../../src/renderer/RendererModule.h) exposes:

| Operation | Contract |
| --- | --- |
| `R_RendererModule_QueryDisplay` | Reads renderer status and actual SDL window state. Failure leaves output unchanged. An unavailable device can still be observed successfully; inspect `rendererReady`, `windowValid` and presentation `available`. |
| `R_RendererModule_TryDeviceRestart` | Attempts the immutable request on the active renderer. It neither switches renderer modules nor selects a fallback mode. The caller owns recovery and must submit queued frontend commands first. |

Call these services on the main/video thread, serialized with module loading and
frame submission. The query samples renderer generation before and after the SDL
query; it rejects a changed generation or module epoch instead of combining
observations from different device lifetimes.

Pair renderer-local `generation` with the engine-owned `moduleEpoch`. Local
counters disappear when a module unloads; a generation or frame count alone is
not a cross-module identity. Bind future asynchronous results to the request
token and settings owner as well as this pair. The current services do not
provide that transaction scheduler.

## SDL request and readback rules

`renderWindowRequest_t` carries window parameters, display ID/index, desktop
fullscreen/span policy, swap interval and explicit restoration placement and
maximized state. A nonzero SDL `displayId` is authoritative. If it disappears,
the request fails; the index is not substituted. With ID zero, an explicit index
is resolved from the current enumeration. Index `-1` selects the current window
display, with primary-display selection only when no usable current display
exists. Display IDs are runtime identities, not durable monitor identifiers.

Windowed width/height use logical window units. Exclusive fullscreen requests
use pixels: the selected enumerated mode must match pixel dimensions and the
integer refresh bucket exactly; 59.94 Hz belongs to 60 Hz. Refresh zero allows
the highest matching mode. Desktop fullscreen validates the desktop pixel mode.
Borderless/span dimensions derive from display bounds. Spanned desktop
fullscreen is represented by a borderless window covering those bounds.

`renderWindowState_t` reports current flags, display identity, logical and pixel
sizes, density/scale, viewport and current display mode. It reads
`SDL_GetCurrentDisplayMode`, not an unapplied requested fullscreen mode.
`currentModeValid` and `positionValid` distinguish unavailable observations.
The query does not update archived CVars or publish window geometry.

Explicit `restorePlacement` requires usable absolute coordinates; unsupported
placement and spanning fail on positionless compositors. Otherwise a decorated
window keeps its observed position on the same display or centers on the chosen
display. Strict application synchronizes SDL, then compares actual flags,
dimensions, identity, applicable placement and mode with the request. Every
failure preserves the output object, but may leave a partially changed window.
**Failure is not rollback.** The caller must explicitly restore captured state.

Transition suppression covers the synchronous SDL apply and avoids publishing
partial state from that call. It does not yet qualify suppression/reconciliation
of later visible-window geometry events and CVar persistence over an entire
settings transaction. Maximized state can be requested and observed, but the
POD does not capture the compositor's hidden normal restore rectangle. The
normal placement cache is not replaced by observed maximized geometry.

## Device restart and presentation

The recoverable restart rejects nested calls, pending frontend commands and
invalid requests before teardown. Once teardown begins, a failed attempt leaves
the renderer unavailable and permits a subsequent explicit restore attempt.
Expected initialization, context, shader and resource refusals return failure
and diagnostics; allocation exhaustion and corruption are not claimed recoverable.
Legacy startup/fallback policy remains a separate route.

Frontend success is published only after device, images, fonts and world
dependencies are reconstructed. `videoRestartCount` advances on that successful
completion, including a successful restore, and not on a failed attempt. It is
a resource refresh signal, not proof of presentation.

GL strict initialization forbids MSAA degradation/CVar substitution and verifies
actual samples. Requested swap interval is set and read back even when its CVar
was not marked modified. Vulkan currently supports only single-sample targets;
strict multisampling and stereo requests are rejected. Vulkan records the
selected present mode and effective interval; strict interval requests fail when
no supported corresponding mode exists. Inspect `parametersValid` before using
sample, interval or present-mode fields.

An accepted strict interval remains the device policy after the restart returns,
including loading-state notifications and Vulkan swapchain recreation. The
backend remembers the contemporaneous raw `r_swapInterval` value and relinquishes
that policy when the value changes. A dirty flag alone does not count as a new
setting; identical CVar writes are no-ops. Device shutdown clears the policy and
a typed restore installs its own interval. GL default-framebuffer and scene-target
MSAA decisions use the observed context sample count rather than an unapplied
CVar. These rules prevent a reported successful request from reverting before
its first normal frame.
The legacy `gfxInfo` effective-AA estimate still derives from preferences; it
must not be used as proof of a typed request's actual default-framebuffer or
scene-target samples. The private observation is the authority for context
samples. Complete scene/AA telemetry remains part of graphics-quality integration.

[DisplayPresentation.h](../../../src/renderer/DisplayPresentation.h) defines a
coherent POD observation with device/display generation, cumulative submitted,
presented and failure sequences, availability, outcome and native error.
Initialization/recreation starts a generation in `RDP_PENDING`; it does not count
a frame. Failed operations preserve a failure sequence even if later work
succeeds. GL counts a successful actual swap. Vulkan counts successful graphics
submission separately from `VK_SUCCESS`/`VK_SUBOPTIMAL_KHR` presentation.
Screenshot-only submissions do not increment `presentedSequence`. An
out-of-date present is a failure; suboptimal acceptance followed by recreation
starts a new pending generation that still needs its own successful present.

These results mean **API acceptance, not physical scanout**, visible pixels,
correct image composition, HDR/color parity or user confirmation. A future
first-present gate must observe the expected generation, a newer presented
sequence, available/ready resources and the matching window tuple, while also
checking owner readiness and intervening failures.

Vulkan submission, recording, synchronization and upload-dependency failures
latch presentation off until a full device restart. Subsequent frame paths stop
before slot waits, acquisition or reuse; failed readback resources are deferred
for retirement. Replacing an unsignaled fence with a synthetic signaled fence
would leave the acquired binary semaphore unsafe to reuse. On submission OOM,
Vulkan leaves referenced synchronization unchanged; device loss does not provide
that guarantee. See the primary
[vkQueueSubmit2 contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit2.html).
Presentation failure also requires care because an allocation failure may leave
its semaphore wait unqueued; see
[vkQueuePresentKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html).

## Reproduction and evidence

From the repository root, run the five production-method native harnesses:

```text
python tools/tests/sdl3_strict_window.py
python tools/tests/renderer_vid_restart_route.py
python tools/tests/renderer_display_services.py
python tools/tests/renderer_display_presentation.py
python tools/tests/renderer_gl_display_policy.py
```

These compile actual production functions against controlled SDL/renderer API
stand-ins. They cover strict observation/placement and partial failures;
restart ordering, refusal and explicit restore; built-in/module-only/dedicated
service routing and epoch isolation; and actual tracking with GL swaps, Vulkan
scene/capture submissions, failed presents/recreation and blocked synchronization
reuse. A C++ compiler is required. The presentation harness also needs Vulkan
headers from `VULKAN_SDK`, `/usr/include` or a configured SDL3 subproject. The CI
script-smoke jobs install `libvulkan-dev`. No window, input or game
is opened by these tests.

Adjacent native Meson suites are `openq4-ui-settings-transaction`,
`openq4-ui-behavior`, `openq4-ui-presentation`,
`openq4-ui-presentation-runtime`, `openq4-retained-ui`, `openq4-ui-document` and
`openq4-ui-state`. Run them from a configured `builddir` using
`tools/build/meson_setup.ps1 test -C builddir --no-rebuild` with those names on
Windows. These validate the existing model/runtime/settings foundations, not
actual device confirmation. `ui_settings_service.py` and
`ui_system_settings_host.py` cover the current service/CVar boundary;
`sdl3_multidisplay_windowing.py` retains legacy windowing source contracts.

The engine diagnostic `rendererDisplayProbe` supports `report`, `save`,
`apply width height [interval] [samples]`, `restore` and `missing-display`. The
optional diagnostic interval accepts only 0 or 1; samples accept 0, 2, 4, 8 or 16.
Mutation is restricted to
an already hidden, windowed device with a saved actual baseline. Its saved state
is rejected after a module-epoch change. This diagnostic is not a production
settings action. [capture_display_device.py](../../../tools/ui/capture_display_device.py)
drives it through the appropriate SP/MP gameplay profile, disables host input,
forces hidden/windowed rendering and uses registered engine screenshots. Example:

Quote the hyphenated argument in an engine script:
`rendererDisplayProbe "missing-display"`.

```text
python tools/ui/capture_display_device.py --mode sp --renderer gl --assets "C:/Program Files (x86)/Steam/steamapps/common/Quake 4" --output .tmp/ui/display-device/sp-gl
python tools/ui/capture_display_device.py --mode mp --renderer vulkan --assets "C:/Program Files (x86)/Steam/steamapps/common/Quake 4" --output .tmp/ui/display-device/mp-vulkan
```

Use a new output directory for each run. Staged binaries and installed retail
assets are required. Review source/binary hashes, logs, readbacks, presentation
sequences and engine-generated images together.

The final Windows x64 captures qualify this private foundation against the
working source derived from engine `962b179c0e4214df2ebde38e98cb3e090b75fdd6`
and unchanged companion `1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`:

| Capture under `.tmp/ui/display-review/` | Gameplay | Density override | Result |
| --- | --- | --- | --- |
| `sp-gl-policy-qualified/capture.json` | SP `airdefense1`, OpenGL | 125% | Passed; exited 0 |
| `mp-vulkan-policy-qualified/capture.json` | MP `q4dm1`, Vulkan | 200% | Passed; exited 0 |

Each run enters gameplay before opening the retained diagnostic, then observes
eight ordered device operations and 32 settings readbacks. Three successful
reconstructions preserve the owned draft, first resize from 1280×720 to 960×540,
then restore the original dimensions and actual placement. A deliberately
unavailable display identity fails initialization without advancing the restart
count; explicit restoration subsequently succeeds. Each successful device has
six newer accepted presents before its report. Four render-target screenshots
per run show the applied, restored and recovered menu, followed by the world,
weapon and HUD after closing the diagnostic. All eight images were reviewed.
The responsive diagnostic panel retains its text and vector framing at both
dimensions; this does not accept the production SYSTEM page or its artwork.

Both cases apply interval 0 while the stored CVar remains 1, verify it through
the new device's accepted presents, then restore interval 1. OpenGL additionally
applies and observes four samples against a stored zero before restoring zero.
The changed policy is therefore exercised independently of preference values.
An additional SP/OpenGL run,
`sp-gl-msaa-gameplay-qualified/capture.json`, keeps the world visible throughout:
five ordered observations verify samples 0→4→0 and interval 1→0→1, two successful
restarts and six newer presents after each. It exits 0 without diagnostics. Two
reviewed engine screenshots show gameplay at 960×540 and after restoration to
1280×720. The game's separate post-processing target remains single-sample under
its unchanged CVar policy; this proves gameplay with a four-sample default
framebuffer, not that every render target or depth-copy branch uses four samples.

The full engine/module build, staging, seven native Meson UI suites and five
production-method native harnesses passed. The capture oracle passes 23 tests,
rejecting 281 corrupted traces; it requires interleaved operation/readback
ordering, actual window and presentation results, draft continuity and the
post-recovery gameplay image. `gfxInfo` safely reports an unavailable graphics
device between the deliberate failure and restoration, without accessing
driver-owned strings from a destroyed context.

The final local record is `.tmp/ui/display-review/capture-policy-evidence.json`.
Its SHA-256 is `02ba863b2d420ff0b9271e42e0da60e9b40ac188595afc977b8b9ddf3c76ae15`.
It binds the working source, staged binaries, build options, tests, logs, images
and review findings. Earlier failed attempts remain recorded: teardown initially
invalidated display IDs, which required the engine-owned SDL video reference;
the original fixed-width diagnostic clipped at the smaller 200% layout and was
replaced with a responsive fixture. Neither rejected attempt is final evidence.
The earlier `capture-evidence.json` remains immutable. Its captures used matching
requested/stored intervals; subsequent review exposed the policy drift and
preflight side effects described above. The final record binds their fixes and
the stronger captures instead of overwriting that earlier evidence.

Both final logs contain zero errors. SP has the expected missing-display
warning. MP preserves the previous 86 warning classes: compared with its
94-warning baseline, the additional restarts produce two more existing vertex
array warnings and the deliberate failure adds two expected Vulkan warnings
(98 total). Existing MP content warnings remain an unrelated qualification
backlog. Two stale test assertions for game ABI 47 were corrected to the existing
ABI 48; this checkpoint changes only the private renderer ABI to 14.

These are hidden windowed diagnostics with host input disabled. Density values
are overrides, not observed operating-system DPI changes. No physical input,
visible compositor transition, fullscreen, monitor hotplug, physical scanout,
driver device-loss recovery or non-Windows platform qualification is claimed.

## Gate before enabling production display Apply

The [SYSTEM settings contract](system-settings-contract.md) still requires:

- A durable recovery journal and frame-scheduled asynchronous `Applying` and
  `Restoring` phases with stale-result rejection by owner/request identity.
- Owner-ready reconstruction and first-present verification before starting
  Keep/Revert confirmation, including verification of the restored device.
- Suppression and reconciliation of visible geometry CVar persistence across
  the whole operation, not only its synchronous SDL mutation.
- Conflict recovery that preserves unrelated external changes and reports an
  unresolved restore without silently accepting or overwriting it.
- Qualification of maximized/hidden normal restore geometry, monitor changes,
  compositor positioning, focus and supported platform/backend combinations.

Until these are implemented and evidenced, non-immediate settings batches remain
blocked. The complete production page, editor workflows, visual quality and all
271 GUI migrations remain pending.
