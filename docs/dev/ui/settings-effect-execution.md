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
