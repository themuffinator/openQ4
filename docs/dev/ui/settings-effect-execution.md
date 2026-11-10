# Completing SYSTEM effect execution

The complete SYSTEM page needs one durable settings operation covering every
changed domain. The existing display coordinator already owns a complete typed
transaction, recovery journal, configuration guard and Apply/Keep/Revert
sequencing. Extend that owner; independent renderer and audio controllers must
not each commit the same draft. Until the required executors are available,
reject an unsupported batch before any live write.

This document records the implementation direction from the source audit and
the separately qualified foundations below. Since 10 November 2026 a batch
that mixes effects, such as a Performance Preset, applies through the
[combined executor](#combined-executor), which restarts the devices as the
stock page did and does not use the checked image and audio foundations.
Preset draft expansion is described separately in
[performance presets](performance-presets.md).

## Actual effects

| Domain | Required result |
| --- | --- |
| Display | Requested mode and actual window/device agree; the owning confirmation view has presented before Keep becomes available. |
| Images and materials | Affected resident images have completed the requested load policy, with no new unexplained defaults; high-quality material policy has been reparsed and rebound where necessary. |
| Renderer resources | Actual renderer path and allocated upload resources agree with the request or an explicitly reported supported fallback. |
| Audio | Actual output mode, device readiness, EFX routing and a subsequent normal audio update agree with the owned request. |
| Deferred loading | Committed desired policy, currently effective policy and its pending load boundary are reported separately. |

The GL upload manager now checks native buffer creation, allocated byte sizes
and persistent mappings before publishing its storage as ready. Partial failures
release every owned buffer; a supported persistent-to-streaming fallback updates
the allocator, synchronization and reported path together. Failed frame orphaning
disables the dynamic stream and lets ordinary uploads use their legacy fallback.
It preserves existing static geometry. No per-frame GPU completion wait is added.

`R_RendererUpload_QueryStorage` returns copied actual allocation metadata on the
renderer thread, with a nonreused manager generation and the separately captured
request. It calls no native getter and does not infer allocation from requested
statistics. This is a GL storage observation, not upload completion, portable
recovery, renderer-path acceptance or settings Apply authorization. Vulkan's
separate staging allocator refuses this GL-specific query. The unified resource
executor still needs its own complete requested/actual policy and recovery path.

The audit found several reasons why a CVar write followed by a console restart
is insufficient:

- `image_ignoreHighQuality` is consumed when materials are parsed. Regenerating
  world references does not reparse those material declarations.
- Image reload includes persistent runtime images and font atlases. A lighter
  reload path must preserve them or perform the existing complete UI/font
  resource lifecycle.
- Requested upload-budget statistics do not establish successful allocations,
  and Vulkan does not implement every GL upload-ring option.
- Legacy audio initialization can use a default device, ignore requested output
  attributes, disable EFX or enter silent retry mode. Its void restart method
  does not return a checked settings result.
- `s_maxEmitterChannels` is consumed during normal sound-world updates;
  `image_writeGeneratedImages` controls future cache writes. Neither is proved
  by a fictional next-map receipt.
- `s_maxSoundsPerShader` is consumed while sound declarations are parsed.
  Declarations retained outside the normal purge need explicit handling before
  claiming that a new level applied the policy to every sound.

## Renderer and audio adapters

Begin with the existing checked display restart for a coalesced renderer/image
rebuild, using captured actual display and placement even for a resource-only
request. Add bounded image/material outcomes and actual allocation/path readback.
Expected load/allocation failures must return a result suitable for recovery.
Do not accept merely requested statistics or an unchecked default image as proof
that the requested resource quality is active.

For capable OpenAL providers, the first checked audio backend can reset output
properties on the existing context and explicitly update surviving source EFX
routing. OpenAL's completed extension permits this without replacing existing
contexts, but a successful reset can ignore requested attributes; query the
actual output afterward. [HRTF/reset specification](https://openal-soft.org/openal-extensions/SOFT_HRTF.txt),
[output-mode specification](https://openal-soft.org/openal-extensions/SOFT_output_mode.txt).

The audio adapter must capture requested policy separately from actual baseline
output. It holds exact owner/request and device/source lifetimes, checks external
dependencies, records successful filter/source bindings where getters do not
exist, and verifies one later normal audio update. A lost device or conflicting
external change invalidates the result. Unsupported providers refuse before
mutation; a later checked recreation path must handle providers that cannot use
the in-place backend. API readiness does not establish audible speaker placement.

## One journal and recovery sequence

1. Freeze the complete typed baseline, target and changed-key patch. Preflight
   every participating domain and capture its actual baseline and dependencies.
2. Acquire the existing process/configuration ownership and window-placement
   lease where needed. Durably write Pending before any live mutation.
3. Execute at full Common frames in a fixed order: coalesced renderer work,
   checked audio work, then observed presentation/update completion. Nested
   GUI/loading frames must not start another effect operation.
4. Keep deferred effective loading/cache policy at the captured baseline while
   display confirmation is pending. A level-load boundary must resolve or
   cancel the pending attempt before consuming unconfirmed policy.
5. On failure or cancellation, restore only the safe owned CVar patch and rebuild
   every touched actual domain. Divergent external values remain conflicts.
   Unproved restoration stays RecoveryRequired with its journal and guard held.
6. On accepted completion, durably write Confirmed, persist checked configuration,
   publish committed deferred policy, finish the transaction, remove the exact
   journal and release ownership. Keep intent remains monotonic after an
   uncertain durability failure.

The current schema-1 journal means display-only recovery and must retain that
meaning. A versioned schema-2 record must describe participating domains and
their portable restore/target policy. Startup must verify each domain and its
normal readiness boundary before deleting recovery evidence. Sound initializes
after the current settings-startup entry, and `idSoundSystemLocal::Init` already
creates hardware and stream buffers. Mixed recovery must select its checked
audio policy before that initialization and then obtain a later normal-update
receipt. The old Common comment describing this as hardware-free initialization
must not determine the recovery order. Process pointers, module epochs and request tokens are not
portable persisted device identities.

### Checked in-place audio foundation

`SoundSettings` now captures requested audio policy separately from actual
OpenAL output and holds an exact owner/request, device/context lifetime, source
lifetimes and device-event watermark. Policy and lease arguments are immutable
POD copies taken before any foreign callback. Supported transitions reset output
on the existing context, verify actual output/EFX state and record successful
source-routing setters. A subsequent ordinary sound Render/hardware Update must
complete on the same generation before completion can be accepted. Mute remains
the current external state and is never restored from a stale settings baseline.

Failed transitions retain the lease and block legacy automatic restart. Exact
original-context revalidation may renew a changed device-event watermark only
after all other dependencies and the original requested baseline are proved.
It invalidates old update receipts and allows restoration only; it cannot
convert a failure into a fresh target Apply.

`CancelCaptured` releases an untouched preparation only while its actual
baseline, requested dependencies and ownership still match. It calls no native
setter. `CheckCompletion` verifies completion while retaining the lease and
legacy request caches through durable publication. `Finish` repeats the actual
checks before adopting those caches and releasing ownership. A prior completion
observation does not authorize release after a device or source change.

The production API, checked source routing and normal Render/Update bodies pass
35,443 checks and 34 compiled behavioral mutations on Windows Clang, MSVC debug
STL and Linux GCC with ASan/UBSan. Unsupported provider, non-SDL and dedicated
stubs refuse without changing outputs. These tests use counted OpenAL/engine
boundaries. They do not qualify audibility, hardware error timing or cold audio
reconstruction. The shared durable host still needs portable actual audio
descriptors, checked cold recovery and lease integration before SYSTEM can use
this API. Existing environmental reverb parameter selection is also unfinished;
verifying an effect type alone does not establish that acoustic feature.

Resource-only operations need a checked automatic completion path; they must
neither invent user Keep input nor require a display confirmation for an
unchanged display. Delayed policies need localized status explaining when they
become effective. `r_displayRefresh`, `r_skipSky`, `s_maxEmitterChannels` and
`image_writeGeneratedImages` now declare their archive policy explicitly. These
local preferences are eligible for normal configuration serialization and typed
settings edits from initial registration. This also prevents the emitter limit
from being misclassified as a cheat and rejecting lower preset drafts.
The host still rejects explicit protected, initialization-only, private and
network-synchronized settings; it does not add flags while applying a draft.

### Automatic completion foundation

The existing transaction/controller now accepts an explicit `SettingsCompletion`
policy. Its default remains user confirmation. Automatic requests are refused
when the host classifies the change as needing display confirmation; the check
runs at preparation, before writing, and before acceptance. They remain Applying
until actual effect readiness and a fresh presentation permit a full frame to
prepare their commit. They never enter Confirming, adopt a confirmation-view
draw, expose Keep, or synthesize an input action.

The observation's `effectsReady` flag lets a ready device wait for participating
domains to finish their normal updates. Loss of that proof before acceptance
queues restoration. Restoration also waits for real effects and presentation.
Nested/loading frames can observe or queue work but cannot start effects or
persist configuration. Closing the owning page during commit preparation
prevents persistence for both automatic and manual operations.

Acceptance renews the transaction request and makes commit intent monotonic
before durable publication. An uncertain publication keeps ownership and
requires explicit forward finalization; it cannot silently restore an accepted
target. Fresh exact setting readback is still required after persistence.

`tools/tests/ui_settings_automatic.py` runs the actual transaction/controller,
the existing transaction suite and exact-value regressions. Windows Clang,
MSVC debug STL and Linux GCC with ASan/UBSan pass 873 controller checks, 1,168
exact-value checks and 13 compiled behavioral mutations. The hosts are counted
test boundaries: this qualifies sequencing, not real mixed effects or native
file durability. The SYSTEM page uses it for the deferred light-grid preload
(below); every other effect stays refused until the same durable host, portable
journal and its own executor are connected.

## Versioned journal envelope

`SettingsEffectPlan` computes a versioned effect mask from the complete typed
53-key catalog and exact baseline/target difference. Its read-only projection
is checked against the production catalog in tests. Display changes require
confirmation; image, display and renderer-resource work use one coalesced
renderer strategy. A preset-name change records draft metadata and never
instructs recovery to expand a profile again.

Schema 2 uses the same recovery file and canonical checksum envelope, with
explicit dispatch through `SettingsJournalRecord`. The schema-1 encoder/decoder
and its bytes remain unchanged; its decoder still rejects schema 2. The new
record owns an immutable variant and publishes through a nonallocating pointer
swap. Unknown fields, versions, contradictory masks, inexact patches, missing
or surplus domain pairs, invalid placement and excessive metadata are rejected.
Each new domain map has a 512-entry, 64 KiB and 96-byte-key bound, inside the
existing 4 MiB file and 4,096 flattened-field limits.

These domain maps are typed envelopes, not portable hardware descriptions or
readiness evidence. The host must validate every domain before acting on a
decoded record. `ValidateDisplayPreserveActualPair` separately handles automatic
resource work: it requires unchanged display catalog keys and identical valid
captured display plans. Archived window dimensions may differ from actual
window dimensions; this helper grants no authority to overwrite the former
with the latter during automatic completion.

The standalone codec suite passes on Windows Clang, MSVC debug STL and sanitized
Linux GCC, including the existing schema-1 suite, 2,528 exact-value checks and
18 compiled mutations per compiler. Normal debug-STL decoding and allocation
refusal in the owned encoder pass. Exhaustive decoder allocation refusal is
qualified only with release STL: upstream JsonCpp can terminate during a
`noexcept` debug-string construction. The new APIs preserve outputs when they
return false; they do not promise recovery from process termination. The shared
typed parser uses an equivalent throwing construction for its own empty BOM
buffer, without modifying JsonCpp.

Production uses schema 2 only for the deferred next-map domain (see
[Deferred light-grid executor](#deferred-light-grid-executor)); display attempts
stay on schema 1, and every other schema-2 domain waits for its own executor.
This increment created no second journal or independently committing controller.

## Checked image and material restart

The renderer now exposes one checked image-policy restart through module API 16.
It copies the requested policy, coalesces device and image work through the full
video restart, reparses supported quality-sensitive retained material sources,
and checks allocation, mip/layer uploads, backend completion and default outcomes.
Its result identifies the module epoch, attempt and device generation. This is
resource evidence; later UI/font drawing and a fresh present remain necessary.

A failed attempt retains its original private inventory. Nonreused image and
material instance IDs prevent address reuse from inheriting old default
allowances. Destruction, manager lifecycle changes and unowned content changes
invalidate that inventory permanently for the renderer lifetime. Metadata-only
paths participate too: deferred downsize permission, scratch-image usage,
samplers, persistent options, dimensions and copy formats cannot silently change
the recorded resources. Guards act at method entry and receipt publication;
arbitrary concurrent destruction inside an executing resource method is unsupported.

The full engine and both renderer modules build, all 49 integrated UI suites
pass, and focused production-method tests cover metadata, native boundaries,
instance construction and the full restart route. Windowed SP/OpenGL at 125%
and MP/Vulkan at 200% pass gameplay, ordinary video restart and SYSTEM opening,
with four reviewed engine render-target images. These runs exercise ordinary
restart compatibility, not the checked image-policy Apply operation. SP logs no
warnings. MP retains the earlier 93 warnings and additionally reports a vertex
cache virtual-memory warning during restart; that warning remains under review.

The source-bound record is
`.tmp/ui/renderer-effects-integration/validation-evidence.json`. The current
policy request is not a record of policy historically consumed by resident
resources. The later increments below add consumed-policy observations and exact
DDS reduction checks; portable content descriptors and cold reconstruction remain required.
SYSTEM mixed effects stay disabled until the durable host owns those guarantees.

## Portable audio record increment

The audio executor now captures immutable requested and observed recovery
records separately. The bounded version-1 grammar records the OpenAL provider,
logical output device and HRTF specifier, actual output mode, requested speaker
and emitter policy, and the supported no-effect state. Runtime handles, process
tokens, HRTF indices and mute state never enter the portable record. Resolution
requires unique current device/specifier matches and produces a fresh HRTF index.
The default device at capture is diagnostic: changing the system default does
not invalidate an explicitly selected original device that is still available.

Capture keeps the live executor lease and checks its dependencies around native
queries. It refuses actual effect, slot or filter resources, including filters
owned by idle voices, and refuses enabling new effects. A completed in-place
transition can be observed without releasing its recovery ownership. These are
portable logical descriptors, not physical endpoint identities or permission
to recreate a device from a copied enumeration.

The codec/capture suite and the existing checked audio suite pass on Windows
Clang, MSVC debug and Linux GCC with sanitizers. Eleven compiled mutations cover
the new grammar/capture; the original 34 audio mutations remain covered. Full
effect parameters, cold initialization, live audibility and integration with the
shared durable host remain required. At this checkpoint startup and live
persistence still used schema 1. The new runner's repository-default path was corrected after the
integrated Meson run exposed its snapshot-only assumption; the failing run and
successful rerun are retained under `.tmp/ui/native-retirement-integration/`.

## Consumed image-policy observations

Image loads and material parses now retain the policy values they actually
used. The loader freezes its downsize/picmip inputs once for cache naming and
CPU reduction; material parsing similarly freezes quality/no-mip inputs. Each
observation belongs to a nonreused resource instance and load/parse revision.
Nested parses, exceptions, mutation and replacement cannot publish an unfinished
or stale observation. Early renderer initialization establishes its owning
thread before resource callbacks; genuinely earlier parses remain unobserved.

Images remain pending until allocation, complete mip/layer uploads and backend
completion are observed. OpenGL uses an explicit bulk completion boundary;
Vulkan uses the existing upload batch and successful fence retirement, with
nonreused batch identities. Getters do not call a graphics API. No per-image
wait is added, and a failed Vulkan submission retains staging ownership.

These records describe consumed policy and admitted output, not source-content
identity. Generated-cache classification does not prove how the cached bytes
were produced. The exact reduction increment below adds direct DDS mip reachability
and atomic cube reduction. Portable content reconstruction and the durable host
still need completion. Default,
unobserved and unsupported paths refuse capture. SYSTEM gains no mixed Apply
capability from these internal observations alone.

The frozen suites cover actual resolvers, publication boundaries and batch
methods on Windows Clang, MSVC debug and sanitized Linux GCC. New observer and
batch tests reject 23 compiled mutations on Windows/Linux; the adjacent
coordinator, metadata and boundary tests retain their 41 mutation checks.
Twenty-six translation units compile against production headers. Main engine
linking and runtime qualification are recorded separately in the integration
folder; these isolated tests do not establish driver or hardware behavior.

## Exact source-image reduction

DDS loading resolves the requested size once from the original image dimensions.
The loader reports the selected authored mip and whether that chain reaches the
requested target. Ordinary stock loading can retain the best available mip when
the chain is too short, while an exact consumed-policy observation refuses that
fallback. An impossible authored mip count cannot become successful evidence.
File buffers and mip views transfer ownership only after setup succeeds.

Decoded cube reduction stages all six faces before replacing any original face
or the common size. The shared RGBA resampler returns the exact requested extent
or refuses it; it no longer silently clamps output axes to 4096. Its explicit
limits are 32768 pixels per axis and 256 MiB for each input or output buffer.
Both 2D and cube binary assemblers check allocations and release partial work
before failure. Image loading, generated images and lightgrid writing check their
results before native upload or payload writing. Odd compressed cube sizes use
the existing edge padding while preserving logical mip dimensions.

The generated cache revision changes for the corrected reduction and cube
assembly. Failed reduction cannot populate a cache under the requested target.
Generated cache records still have no original source dimensions or content
identity and therefore clear source-reduction evidence. The observation continues
to require full native mip/layer coverage and backend completion.

The frozen Windows Clang, MSVC debug and sanitized Linux GCC suites pass
3,528,082 CPU checks, 601 OpenGL/material observation checks and 31 Vulkan
observation checks, plus the existing block-mip and picmip suites. Windows and
Linux reject all 32 compiled mutations; thirteen full production translation
units compile across GL, GLES, Vulkan and dedicated configurations. This is CPU
loading and process-local observation qualification; source-content identity,
portable reconstruction, native-driver qualification and mixed SYSTEM Apply
remain separate requirements. The frozen handoff is
`.tmp/ui-image-reduction-exact/.tmp/handoff.json`.

## Renderer settings reports

Render API 20 gives the settings service the renderer's own evidence for the
two SYSTEM settings that are neither display modes nor immediate values. It
changes nothing a player sees.

- **Renderer selection.** Each time the renderer resolves `r_renderer` it
  reports what it was asked for (cut to 31 bytes), the back end it selected,
  the back end `best` picks on this device, and how a fallback arose. A
  request is either met, unavailable with the automatic pick running, or a
  retired legacy name with the automatic pick running. The report also says
  whether the development auto-promotion of the modern path is active, and
  what OpenGL upload storage the device allocated (path and persistent
  fallback; Vulkan reports none). Device initialization always resolves the
  back end, so every checked restart produces a fresh report. The
  pre-capability placeholder in early initialization reports nothing.
- **Light-grid loads.** A level load asks the settings service once for its
  preload policy and a fresh, never-reused token. The service answers with the
  committed choice: an open attempt's baseline until its Confirmed record
  and configuration are durable, otherwise the live catalog value. A load
  during an Apply
  therefore never consumes a value that could still be undone. Without the
  service, as on a dedicated server, the load uses `r_lightGridPreload`. The
  preload itself no longer reads the CVar. Every exit of the light-grid setup
  reports what the load did: no baked assets, no device, streamed, fully
  preloaded or partly preloaded. It also reports the usable and resident area
  counts, and the policy and token it used. The developer log gains one
  receipt line, and the existing residency lines are unchanged.
- **Engine stores.** The engine keeps the latest report of each kind, stamped
  with its own serial and the module epoch it came from. Queries refuse a
  report from an earlier module. Module builds forward the three calls
  through the services table; static builds call the engine directly.
- **Unload.** The session marks each map unload, after closing SYSTEM, so the
  service can tell a receipt that describes the level on screen from an older
  one.

`tools/tests/renderer_settings_reports.py` pins the ABI, the services table
and forwarders, the epochs, the report beside the existing fallback warnings,
the single policy request with a receipt at every exit, and the provider rule.
The settings service harness compiles the provider against its catalog double:
the live choice outside an attempt, the baseline while one is open, fresh
tokens, and no answer when the setting cannot be read. The deferred
executor below relies on that rule, the SYSTEM page's
[Preload Light Grids status](system-render-options.md) reads the light-grid
receipts, and the selection report waits for the renderer executor.

The reports were qualified by the contract test, the settings service harness
and the full Meson suite, and by SP map loads on OpenGL and Vulkan that logged
the selection report and the light-grid receipt beside unchanged residency
lines. Evidence: `.tmp/ui/renderer-settings-reports/validation-evidence.json`,
SHA-256 `240dffae83f06f6a459fdae2473ab1277718e2671ebf7a210a2fd05b3b9c7a74`.

## Deferred light-grid executor

`r_lightGridPreload` is the first SYSTEM setting outside display modes that
Apply can complete. It takes effect at the next map load, so its attempt
completes automatically: there is nothing for the player to see before the
next load, and no Keep/Revert question to answer.

- **Admission.** Apply runs one executor. `SystemSettingsHost::ApplyClassOf`
  classes a draft by what its changed keys need: immediate, display,
  renderer, deferred, mixed or unsupported. Immediate keys ride along with
  any class. A display change keeps its Keep/Revert confirmation. A deferred
  change needs an owning view and a renderer that is presenting now
  (`ReadyForAutomatic`). At this increment a draft that combined display,
  renderer and light-grid changes was refused before any work, as were the
  renderer fallback, image, audio and preset effects. Such drafts now take
  the [combined executor](#combined-executor).
- **Attempt.** Apply journals a schema-2 Pending record with only the
  next-map domain (mask 8): exact snapshots, the patch, and the portable
  policy in each direction (`deferredRestore`/`deferredTarget`
  `lightGridPreload`). There is no placement lease and no device restart;
  the host only observes that the same renderer module keeps presenting. A
  later presented frame lets a full frame commit: the Confirmed record is
  written, then the configuration, then the journal is removed. Loading
  frames observe but never start work or persist. A failed observation or
  closing the owning view restores the baseline instead and never archives
  the candidate.
- **Retry.** A save that did not finish leaves the attempt in recovery with
  Retry. Retry re-reads the presenting renderer and renews the commit: the
  keys the attempt wrote must still hold the choice, and the rest of the
  catalog is taken as it is now, so a resized window or a recreated swapchain
  no longer fails every retry.
- **Level loads.** A load during the attempt takes the committed baseline.
  Once the Confirmed record and the configuration are durable (`Persisted`),
  loads take the new choice, even while journal cleanup still waits for
  Retry. A failed save keeps the baseline until Retry persists it.
- **Status.** Automatic attempts report Applying settings (`#str_230073`),
  Restoring previous settings (`#str_230074`) and Saving settings
  (`#str_230075`) instead of the display wording. The confirmation panel
  opens for one only when it needs recovery (a failed preparation or restore,
  or a save that did not finish); its title then reads Settings need
  attention (`#str_230077`) rather than the Keep question.
  `tools/ui/retained_system.py` owns that title binding.
- **Apply and Exit.** Exit completes only when the attempt committed its
  target (`LastCommitted`); a restore cancels it.
- **Startup.** A schema-2 deferred record replays its owned keys before the
  renderer starts: Pending restores the baseline, Confirmed keeps the target.
  Display initialization is untouched. Renderer and window startup may
  settle keys: on the first full frame, keys the record does not own keep
  what startup chose (the window size, an MSAA fallback), owned keys go back
  to the recovered choice (a platform frame-cap default, for instance), and
  the configuration is committed before the record is removed. Machine-spec
  detection skips a launch with any pending recovery and runs on the next
  launch without one. A record whose policy contradicts its snapshots, or
  whose owned keys changed outside the attempt, blocks before any write; a
  failed save keeps the record and is not retried blindly. Schema-1 records
  keep the display route and still refuse any batch with a non-display
  effect.

`tools/tests/ui_settings_service.py` drives the production service through
deferred Apply, the loading-frame rule, the policy before, during and after
persistence, Retry after a failed save (also after a recreated swapchain and
a change to a key the attempt did not write), owner close, Apply and Exit
with commit and restore, the mixed and unready refusals, and an audio change
with no executor. `tools/tests/ui_settings_display_service.py` runs the real
host and codec: readiness, class/completion agreement before any lease, the
Pending record's contents, observe-only restarts, Confirmed before the
configuration, restore, module replacement, cold startup in both directions,
startup settling unowned keys and changing owned ones, configuration and
journal-removal failures with no blind retry, exact subnormal values and
tampered records.
`tools/tests/ui_system_settings_host.py` classes every key of the real
catalog: only `r_lightGridPreload` is deferred and only `r_renderer` needs the
renderer. The controller test checks `Completion`, `Persisted`,
`LastCommitted` and the renewed commit. The checked mutation lists gain three
automatic-completion and three exact-value mutants, all rejected. Of fifteen
further compiled mutations of the new branches, fourteen are rejected; the
survivor, an exit witness forced true, is equivalent today because every
restore passes through a stage that cancels exit first.

The harness also emits production-encoded Pending and Confirmed journals
(`--emit-deferred-journals`). Cold starts of the staged client with each
journal in an isolated savepath logged `UI_SETTINGS startup_recovery=complete`,
loaded `game/airdefense2` with the recovered policy (streamed with 0 of 29
areas resident, or preloaded with 29 of 29), removed the journal and archived
`r_lightGridPreload` as 0 and 1.

The executor was qualified by the settings service, host, catalog and controller
tests, the mutation pass, the full Meson suite and the cold-recovery probe.
Evidence: `.tmp/ui/deferred-light-grid-executor/validation-evidence.json`,
SHA-256 `4909d049af40f5c7a8a8436bba2e85caffe6168b9ca676cb64ce213e90664598`.

## Renderer fallback executor

`r_renderer` (Renderer Fallback) applies through its own executor: a checked
device restart on the display the renderer has now, proved by the renderer's
own selection report. Like the deferred preload it completes automatically,
with no Keep/Revert question. The SYSTEM page drafts it from its
[Renderer Fallback row](system-render-options.md#renderer-fallback), which is
available exactly where the admission below passes. Like the display
executor, Prepare can still refuse a window the recovery record cannot
describe (a borderless window on Wayland), before anything is written.

- **Admission.** A draft whose only non-immediate change is `r_renderer`
  needs an owning view and a renderer that can report its selection: a ready,
  presenting OpenGL renderer that has reported one for its current module.
  Vulkan has one back end, so it cannot. With display, preload or any other
  effect it joins the [combined executor](#combined-executor), which keeps
  the same requirement.
- **Attempt.** Apply journals a schema-2 Pending record with the renderer
  domain (mask 16): exact snapshots, both display directions capturing the
  same actual display, the leased window placement for both directions, and
  the request in each direction (`resourceRestore`/`resourceTarget`
  `request`). It then writes `r_renderer` and restarts the device on the
  captured display. The new device resolves `r_renderer` and reports it; the
  attempt continues only if that report is new and agrees.
- **Agreement.** The report names the requested renderer, and it either
  selected it or reports a fallback (unavailable, or a retired name) to the
  automatic pick. `best` takes the automatic pick, and an explicit name never
  promotes the modern path. A later presented frame on a full frame commits:
  the Confirmed record, then the configuration, then journal removal, with
  the report unchanged and still agreeing. A report the restart did not
  produce, a failed observation or closing the owner restores the previous
  request with the same restart and checks.
- **Startup.** A renderer record replays its request before the renderer
  starts. Its saved display is structural evidence only: a renderer change
  never moved the display, so the engine starts the normal way on whatever
  display it finds, and a missing monitor or a Vulkan launch cannot fail the
  recovery. On the first full frame, where `r_renderer` chooses a back end
  (OpenGL), the started device's report must agree before the configuration
  is committed and the record removed; disagreement blocks, keeps the record
  and is not retried blindly. On Vulkan the recovered request commits as is.

`tools/tests/ui_settings_display_service.py` runs the real host and codec:
support (OpenGL with a report; not Vulkan, an unready renderer or a missing
report), Prepare's refusals, the Pending record, one restart on the same
display, a re-selection refused, Confirmed before the configuration, refusals
for a report that ignores the request, publishes nothing new or promotes past
an explicit name, an unavailable fallback accepted, restore, startup in both
directions with no display initialization or geometry lease, a disagreeing
startup, a missing captured monitor, a Vulkan launch, a contradicting record
and an exact subnormal rider. Startup completion is traced with the executor
that recovered, read before finishing clears it.
`tools/tests/ui_settings_service.py` drives the service
route: mixed drafts refused, no report refused, and an automatic apply that
restarts, commits and finishes. The exact-value list gains two renderer replay
mutants.

The executor was qualified by the host, service and exact-value harnesses, a
mutation pass and the full Meson suite. Evidence:
`.tmp/ui/renderer-fallback-executor/validation-evidence.json`, SHA-256 `7213f4abf2a342f6665306862afa3a19139c09e32e835795985a14866cbea819`.

## Combined executor

A Performance Preset writes 55 settings across every effect: MSAA (a display
restart), nine image settings, three renderer-resource settings, three audio
settings, two load-policy settings and its own marker. Until 10 November 2026
`ApplyClassOf` classed such a batch Unsupported, or Mixed when it only
combined classes that each had an executor, and Apply stayed disabled. The
batch is now `SystemApplyClass::Combined` and one executor in
`EngineSettingsDisplayHost` applies it, as the stock page's
`applyPerformancePreset` followed by its APPLY (`vid_restart`) did. A preset
or Auto-Detect chosen on the retained SYSTEM page therefore applies there, and
the page [joined the `ui_retained` gate](system-resolution-rows.md#the-gate).

- **Class.** Immediate settings, saved engine-restart preferences and the
  preset marker ride along with any class. `com_performancePreset` only names
  the profile a draft was filled from; its targets carry the effects. A
  display change alone, `r_renderer` alone and `r_lightGridPreload` alone keep
  their own executors and records. Every other batch is Combined: any image,
  audio, renderer-resource or other load-policy change, and any two classes
  together.
- **What Apply does with each setting.**

  | Effect | Settings | In this session |
  | --- | --- | --- |
  | Display | `r_multiSamples` and the display rows | One device restart on the target display, then Keep or Revert. |
  | Image | `image_downSize*`, `image_picmip`, `image_usePrecompressedTextures` | The same device restart reloads every image the menus and the level hold, under the new values. |
  | Image | `image_ignoreHighQuality` | Written and saved. A material reads it when it is parsed, so materials already parsed keep the old value until the game next starts. |
  | Renderer resources | `r_rendererUploadMegs`, `r_rendererUploadFrameBuffers` | The restarted device allocates its upload storage from them. |
  | Renderer resources | `r_rendererBenchmarkPreset` | Read on each use. |
  | Renderer resources | `r_renderer`, when the batch holds it | The restarted renderer must report that it resolved the request (OpenGL). |
  | Audio | `s_numberOfSpeakers`, `s_useEAXReverb` | The sound device closes and reopens on the next sound frame, as `s_restart` does, and reloads the samples in use. The attempt waits for it. |
  | Audio | `s_maxEmitterChannels` | Read by every sound update. |
  | Next load | `r_lightGridPreload` | The next map load, through the committed policy. |
  | Next load | `image_writeGeneratedImages` | Later cache writes. |
  | Next load | `s_maxSoundsPerShader` | Written and saved. A sound shader reads it when it is parsed, so shaders already parsed keep the old value until the game next starts. |

  The stock page behaved the same way for the two settings that wait for the
  next start. The retained page says so: while the draft holds a change to
  either, its status line reads `#str_230076` (Some texture and sound detail
  changes finish when the game next starts). The notice does not block Apply.
- **Admission.** A Combined draft needs an owning view and a renderer that is
  presenting now. With `r_renderer` in the batch it also needs the renderer's
  selection report, so such a batch cannot apply on Vulkan.
- **Attempt.** Apply journals a schema-2 Pending record:
  - the plan with every changed domain, exact snapshots and the patch;
  - each participating domain's settings in both directions, under their
    catalog names (the image, audio, next-load and renderer-resource maps);
  - when the display, image or renderer-resource domain takes part, both
    captured displays and the leased window placement.

  It then writes the patch and restarts the device once: on the target
  display when the display changed, otherwise on the display the renderer has
  now. A speaker layout or EFX change asks the sound device to reopen. The
  attempt reports its effects not ready until the device has reopened, and a
  device that does not come back fails the attempt. A batch of audio and
  load-policy settings alone restarts no renderer and leases no placement.
- **Completion.** A batch that changes the display shows Keep or Revert with
  the countdown, as a display change alone does. Any other batch completes
  automatically after ready effects and a later presented frame. Either way
  the Confirmed record is written first, then the configuration, then the
  journal is removed.
- **Failure.** A failed restart, a failed observation, a sound device that
  did not reopen, Revert, the countdown and closing the page all restore the
  baseline settings with the same restarts and never save the candidate. A
  save that did not finish keeps the record and offers Retry.
- **Startup.** A combined record replays its owned settings before the
  renderer and the sound system start: Pending restores the baseline and
  Confirmed keeps the target. Both systems read their settings as they open,
  so the ordinary start recovers every domain.
  - Without a display change the engine starts the normal way, and the first
    full frame commits the configuration and removes the record. An
    `r_renderer` in the batch must agree with the started OpenGL renderer's
    report first.
  - With a display change the record takes the display route: the saved
    display is initialized with its placement, and the recovery commits after
    it presents.
  - A record whose domain maps contradict its snapshots, or whose owned
    settings changed outside the attempt, is refused before any write.

### What the combined executor does not prove

It restarts the devices as `vid_restart` and `s_restart` do. It does not use
the checked image-policy restart or the checked in-place audio transition
described above, because neither can run a preset on stock content:

- `R_PrepareImagePolicyRecovery` refuses a session that holds any default,
  procedural or scratch image, and any change to `image_ignoreHighQuality` or
  `image_usePrecompressedTextures`.
- `SoundSettings_Begin` refuses while sound is off or a restart is pending,
  and its portable record refuses a device with EFX resources.

The executor's evidence is therefore exact CVar readback, a new device
generation that matches the planned display, the renderer's selection report
where `r_renderer` changed, the sound device's state after its restart, and a
later presented frame. It does not read back image dimensions, upload
allocations or the speaker layout the device chose. Those foundations remain
for a stricter executor.

The restart is the only image reload. Until the follow-up of the same day the
frame after it reloaded the file-backed images a second time, as the stock
path always had: the image manager's per-frame check
(`idImageManager::CheckCvars`) still saw the changed reduction settings as
modified. A device start now swallows that state once its own reload is done
(`R_InitRendererDevice` calls `PrimeCvars`). That also spares the second
reload after a `vid_restart` that follows a texture setting change, and at a
launch whose configuration sets a reduction. A change made without a restart
still reloads through the per-frame check.

### Combined executor qualification

- `tools/tests/ui_settings_display_service.py` runs the real host and codec
  (1,336 checks in all):
  - support on OpenGL and Vulkan, with and without a renderer change, and the
    completion each batch must ask for;
  - the Pending record's plan, both displays, the placement and every domain
    map;
  - one device restart and one sound restart, the wait for the sound device,
    Confirmed before the configuration, and Finish;
  - a sound device that fails, a restore while its restart is pending, sound
    and load policy alone, the emitter limit alone and sound that is off;
  - a display change with Keep and the committed placement;
  - a renderer change that agrees, disagrees or is reported again later;
  - cold starts in both directions with and without a display change, a
    disagreeing renderer, each domain map contradicting its snapshots and a
    divergent owned setting.
- `tools/tests/ui_settings_service.py` drives the service route (117
  scenarios): a combined batch with and without an owning view or a
  presenting renderer, the wait for effects, the automatic commit, the
  confirmation and restore for a batch with a display change, a combined
  renderer change without a selection report, and the next-start notice.
- `tools/tests/ui_system_settings_host.py` classes every key of the real
  catalog: the marker rides along, `r_lightGridPreload` is deferred,
  `r_renderer` needs the renderer, and every other effect is Combined.
- `tools/tests/ui_retained_gate.py` mirrors `ApplyClassOf` and the combined
  route statement by statement ([the gate](system-resolution-rows.md#the-gate)).
- Engine runs on Windows 11 with an RTX 4060 Laptop GPU: a hidden 1280x720
  window at the title menu, installed stock assets, console verbs only
  (`openq4_retainedGui`, `openq4_system report`, `screenshot`), no gate
  setting on the command line.
  - **The default.** `OPENQ4_RETAINED ... system=1`, and SETTINGS > DISPLAY
    opens `guis/menu/settings/system.q4ui` as the SETTINGS page's child.
  - **Presets.** Starting from Enhanced, each run steps the Performance
    Preset row through all nine presets and applies each, then applies
    Performance and Auto-Detect. After every Apply the 55 preset settings are
    printed, the console command of the same name is run
    (`applyPerformancePreset`, `autoDetectPerformancePreset`) and they are
    printed again: the two lists are identical for every preset and for
    Auto-Detect, on OpenGL and on Vulkan. Each run made eleven applies with
    ten device restarts and six Keeps (the batches that changed MSAA).
  - **Sound.** The OpenGL run had sound on at volume 0 with a stereo layout.
    The first preset returned the layout to the device's own, the sound
    device reopened (`4 sound samples reloaded`) and the attempt completed
    after it. The Vulkan run had sound off and asked for no restart.
  - **Restart.** A second launch on the same save path prints the same 55
    values as the last Apply, on both renderers.
  - **Interrupted Apply.** Enhanced to Quality changes MSAA. The process was
    killed while Keep or Revert was showing, with the Quality values live and
    a schema-2 Pending record on disk. The next launch logged
    `UI_SETTINGS startup_recovery=complete`, printed the 55 Enhanced values
    and removed the record.
  - **Revert and expiry.** In an earlier Vulkan run the script activated
    Revert before Keep was enabled, and once let the countdown run out. Both
    restored the previous preset with one device restart, and the 55 values
    printed afterwards were the previous preset's.
  - **The notice.** With Minimum drafted from Maximum the status line reads
    the next-start notice on two lines beside an enabled APPLY CHANGES
    (engine screenshot reviewed).
  - **One reload.** Before the follow-up each preset run logged four second
    reloads (`Texture reduction changed, reloading images`) and one more at
    the relaunch. After it both runs, repeated on both renderers with the
    same results as above, log none. In `game/airdefense1`,
    `image_downSizeLimit 64` followed at once by `vid_restart` shrank 432
    of 477 world textures with no reload after the restart, and a later
    `image_downSizeLimit 128` without a restart reloaded once and grew the
    same 432, on OpenGL and on Vulkan.
- Not covered: a preset was not applied inside a map (the in-map check used
  the console); a failed device restart was exercised in the harness only;
  the window was hidden, so no visible exclusive-fullscreen Apply ran. The
  interrupted Apply and the Revert ran before the follow-up and were not
  repeated.

Evidence: `.tmp/ui/retained-display-default/validation-evidence.json`,
SHA-256 `82734e2d995324a16e00f03540e64af51a51c5fbbd57d83e5c49dd63d5e0c9c0`.

## Qualification

Extend the existing transaction, controller, journal, persistence, renderer and
audio harnesses with actual production-method tests. Cover mixed-domain failure,
reverse work, stale owner/request/dependencies, uncertain durable writes,
unsupported capabilities, resource fallback, retained font restoration, hotplug
and subsequent load/update boundaries. Compiled behavioral mutations should
demonstrate that critical result and ownership checks are effective.

Final qualification also requires windowed SP/MP gameplay, actual resource and
audio readbacks, failed Apply/restoration, restart recovery and localized page
states. Use engine render-target screenshots for visual checks. Complete SYSTEM,
editor support, every GUI migration and all product gates remain open.
