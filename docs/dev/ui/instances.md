# Independent retained document instances

Development checkpoint on `idtech5-ui`. Each `Runtime` now owns an independent
RmlUi context and canonical document. This removes the single-document runtime
restriction needed for the editor, menus, HUDs and world interfaces. Normal
`idUserInterfaceManager` routing and world-surface integration remain pending;
this checkpoint does not accept any production GUI replacement.

## Ownership

All live runtimes share one `Host`. The host supplies the engine VFS, localized
text, CVar reads, font metrics and rendering. It must outlive every runtime
using it. Calls run sequentially on the engine thread; host callbacks must not
reenter the runtime. A second host cannot take over RmlUi while contexts are
alive and receives an explicit diagnostic.

RmlUi file/font/system services and element/decorator factories remain alive
until the last context closes. The runtime that initially created those
services may close before any of its peers. Each view has its own:

- Document/node namespace, layout dimensions and density.
- Application state, binding revision and source-error suppression.
- Playback clock, timelines and presented properties.
- Focus, modal stack, pointer state, activation pairing and action queue.
- Render backend, geometry accounting and composition leases.

Duplicate document and node IDs therefore remain valid across views. Loading
an invalid replacement preserves the existing document in that view and does
not change any peer. Closing a view releases its geometry and input state;
survivors retain their state and geometry.

## Service and backend lifetime

RmlUi keeps a render manager after its final context is removed. Its global
`ReleaseRenderManagers` operation also dirties font faces and calls `Update`
on every remaining context. Using that operation during a local close would
evaluate surviving views on the closing view's clock.

The integration instead releases textures and compiled geometry for the
departed view's renderer. The services retain and reuse idle render backends,
so their count is bounded by peak concurrent views rather than total document
loads. At final service shutdown, RmlUi's render managers are destroyed while
their backend objects and counters are still alive.

RmlUi callbacks observe a scoped view clock. Canonical playback maintains its
own monotonic time per view, so a context running at time 100 cannot advance a
neighbor whose next frame is time 1.5. Closing or reloading a neighbor does not
force a survivor's font geometry to rebuild.

`Runtime::Statistics()` reports per-view rendering counters, plus shared
`activeContexts` and `residentBackends` counts. These shared residency fields
are available to C++ consumers; they are not GPU timing measurements.

## Composition target identity

The engine host has one pool of numbered render targets. A saved mask owns
its lease until its filter is released; stack pops and other views cannot
reassign that lease. All contexts allocate through the same host-owned pool.

Target reuse also respects deferred rendering. `Host::RenderFrame()` identifies
the current host submission frame for every view. The engine uses its existing
frame-latched presentation number combined with the video-restart generation.
A differently sized target cannot replace an image already referenced by an
earlier view's queued commands in that frame. Allocation prefers:

1. Free targets with the requested dimensions.
2. Previously unused slots.
3. Free targets last used in an earlier host frame.

The host retains these identity records even if the last runtime closes and a
new runtime opens in the same submission frame. A constant host token is safe
but conservatively prevents recycling differently sized targets. Hosts with
deferred rendering must advance the token at their submission-frame boundary,
not once per document.

The engine resizes only the requested unleased slot. It no longer clears other
targets when a view has different dimensions. The 256 MiB RGBA8 target budget
uses the sum of allocated target sizes, including cached targets. A sparse
slot ID does not multiply the requested allocation's byte cost. The existing
48-slot limit remains; exhausted or failed allocations suppress the affected
frame and restore its base target with an explicit diagnostic.

This is a bound on the host's tracked render-target storage. Cropped targets,
GPU cache trimming and optimized multi-view performance remain to be qualified.
The engine owner must coordinate all views before resetting host image/font
resources; the existing preview still owns only one engine-facing runtime.

## Qualification scope

The native retained suite executes the production RmlUi core and adapters. Its
instance scenarios cover:

- Identical IDs with separate 20% and 80% live gauges at different densities.
- Independent focus, disabled state and completed action queues.
- Invalid-load isolation and both context destruction orders.
- Survival of the initial service creator's destruction without flushing a
  peer's geometry.
- Twenty-four temporary context lifetimes while backend residency stays at two.
- Independently timed masked views at different viewport sizes, including
  multiple views within the same host submission frame.
- Target-dimension stability for queued draws and last-context shutdown.
- Safe exhaustion when 60 differently sized submissions use one host
  frame, followed by target recycling and correct pixels in the next frame.

The production engine target allocator is also compiled and exercised by
`tools/tests/ui_retained_layer_pool.py` with a counted renderer. It checks
mixed-size target preservation, exact aggregate budget accounting, sparse IDs,
and failure isolation. Both script smoke workflows run this check.

Native multi-view tests and engine allocator tests establish these ownership
and scheduling contracts. The Windows build and all four native suites
(`openq4-retained-ui`, `openq4-ui-document`, `openq4-ui-state`, `openq4-ui-vector`)
pass for this checkpoint, including exhaustion and recovery. Production layer,
input, CVar-source and presentation-boundary script checks also pass.

On 8 September 2026, hidden, windowed engine captures after actual gameplay
additionally qualified the existing preview and renderer restart. Host mouse
and controller input were disabled. The fixture was
`tools/ui/fixtures/mask-smoke.q4ui`; the independent mask pixel oracle and visual
review passed. These captures are not evidence of multiple world surfaces,
multiple native editor panes, a functional settings screen or full GUI parity.

| Evidence under `.tmp/ui/contexts-review/` | Configuration | Result |
| --- | --- | --- |
| `sp-gl-125` | SP `airdefense1`, OpenGL, 1280x720, 125% density; full windowed video restart | Exit 0; no warnings/errors; 157,080 mask pixels checked; maximum channel error 3.413/255; no hidden-pixel leaks |
| `mp-vulkan-200` | MP `q4dm1`, Vulkan, 1920x1080, 200% density; explicit auto-join | Exit 0; no errors/retained diagnostics; 401,472 mask pixels checked; maximum channel error 2.602/255; no hidden-pixel leaks |

The 93 MP content warnings (86 unique) match the preceding presentation
checkpoint's warning multiset exactly. They remain unrelated open issues.
Both launches requested the shared GUI path; its ownership counters still
reported zero owned views. This is evidence for the active fallback draw path.

Both runs used client SHA-256
`2d3cdae09de128de6492fa47d5bf2d872f8a34f9db587fff14a358d03f5464a3`.
The companion revision is `1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`.
Screenshot SHA-256 values, in table order:

```text
3fa6175804123531970249c933e797afc87d5b79dd0eab70b8ba135ed922833a
d5d042156c8fae1d7c2017df102d919cca13b99ad9b7559a31a165cff52a1b24
```

Each capture contains 60-frame CPU profiles. SP p95 was 10.071 ms before
restart and 10.724 ms afterward; MP p95 was 16.938 ms. These are debug,
optimization-0 engine submission measurements, including GUI flushes and
first-frame work, not GPU execution or shipping-build performance. They leave
optimized performance and frame pacing unqualified. Raw logs, source/binary
hashes, pixel reports and review images are retained with the capture metadata.

Reproduce with `capture_legacy_baseline.py --retained-document
tools/ui/fixtures/mask-smoke.q4ui --profile-frames 60 --shared-gui`, supplying
the installed assets path, a fresh output directory, mode/backend and the
dimensions/density above; SP additionally uses `--video-restart`. Run
`verify_mask_capture.py <engine-tga> --density <scale>` and visually inspect
that engine-render-target image.

No external implementation code is incorporated. Game API 48 and renderer API
13 are unchanged. The GUI manager adapter, complete state/action dispatch,
world surfaces and save/restore, all-GUI translation, text backend and full
visual editor remain required by the original goal.
