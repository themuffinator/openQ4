# Completing SYSTEM effect execution

The complete SYSTEM page needs one durable settings operation covering every
changed domain. The existing display coordinator already owns a complete typed
transaction, recovery journal, configuration guard and Apply/Keep/Revert
sequencing. Extend that owner; independent renderer and audio controllers must
not each commit the same draft. Until the required executors are available,
reject an unsupported batch before any live write.

This document records the implementation direction from the source audit and
the separately qualified foundations below. Mixed Apply is not enabled. Preset
draft expansion is described separately in [performance presets](performance-presets.md).

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
file durability. The production SYSTEM effect gate remains closed until the
same durable host, portable journal and all participating executors are connected.

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

Production startup and live persistence remain on schema 1 until the existing
host can validate, execute, observe and recover every schema-2 domain. This
increment creates no second journal or independently committing controller.

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
shared durable host remain required. Startup and live persistence still use
schema 1. The new runner's repository-default path was corrected after the
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
