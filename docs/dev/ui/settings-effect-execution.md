# Completing SYSTEM effect execution

The complete SYSTEM page needs one durable settings operation covering every
changed domain. The existing display coordinator already owns a complete typed
transaction, recovery journal, configuration guard and Apply/Keep/Revert
sequencing. Extend that owner; independent renderer and audio controllers must
not each commit the same draft. Until the required executors are available,
reject an unsupported batch before any live write.

This document records the implementation direction from the source audit. It
does not claim that the following work is implemented or qualified. Preset
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
after the current settings-startup entry, so mixed recovery needs a later sound
readiness hook. Process pointers, module epochs and request tokens are not
portable persisted device identities.

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
