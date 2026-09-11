# Owned image recovery preparation

This increment connects the portable DDS and observed-bimage descriptors to the
existing checked image restart. It does **not** enable mixed SYSTEM Apply or
claim that a retail renderer inventory is wholly reconstructible. The existing
unprepared restart remains available when no preparation is held.

## Ordering and ownership

1. The engine owner calls `R_RendererModule_PrepareImageRecovery` with its complete
   owner/request identity, durable attempt, and requested policy before changing
   CVars or the device. Preparation captures the complete current image and
   material inventory and inherited loader dependencies. It completes already
   submitted image work through the existing checked bulk boundary.
2. Preparation reads and validates source bytes, reconstructs both CPU directions,
   and retains both immutable CPU sets. It returns a process-local lease bound
   to the module, resource lifetime, owner and request. The lease is not serialized.
3. `R_RendererModule_CaptureImageRecovery` copies each canonical direction. The
   caller must pack both through `PackSettingsImageRecovery`, atomically persist
   the complete settings journal, and retain its original actual display baseline
   before changing policy. The renderer lease does not certify that persistence
   happened and does not replace service, UI, input or material authorization.
4. `R_RendererModule_TryImagePolicyRestart` accepts the exact lease and direction.
   It validates current logical/source inventory and policy before teardown. The
   loader borrows the retained CPU set; it does not reread VFS content or clone
   pixel data after policy changes. Every required load and native upload remains
   subject to the existing checked restart and completion observations.
5. A failed target retains both sets and the original census. Restoring direction
   1 can be retried even when source files have changed or disappeared since
   preparation. A new cold preparation instead rereads and verifies the exact
   recorded qpath, complete owned file digest and output digest.
6. Cancel is exact and untouched-only: matching lease, resource epoch and original
   current policy, with no teardown started. After mutation, releasing preparation
   requires the exact successful direction/result and still-current device,
   failure sequence, policy and resource lifetime. A failed or stale receipt
   cannot discard recovery. Unloading a renderer DLL destroys its module statics.
   The built-in GL fallback has no quiescent disposal/reset hook yet: after an
   invalidating mutation, ordinary renderer shutdown/reinit does not clear its
   retained CPU sets or sticky refusal. Those can remain until process exit.
   A checked owner shutdown/disposal protocol is required before full Apply
   activation; an epoch mismatch or empty inventory is not disposal authority.

All false-result outputs remain unchanged, including after partial native work.
Error buffers are separate diagnostic storage and must not alias inputs or
outputs. No pointer borrowed from a prepared CPU set may escape the synchronous
loader attempt. Source or allocation failure during preparation publishes no lease.

The engine wrapper pins renderer module code for each exported image operation.
Reentrant unload, shutdown, boot or reselection is refused before dependent module
state changes and latches failure on the active call. A failed retention callback
prevents the export from running. Release-callback invalidation prevents result
publication. Public cancel/completed-release execute their callback-free backend
cleanup only after video release passes, preserving the held lease on refusal.
This pin proves synchronous main/video-thread code lifetime, not arbitrary native
driver reentry safety or asynchronous renderer access.

## Durable representation and limits

`RendererImageRecovery` writes explicit little-endian integers, length-prefixed
ASCII strings, all source/output digest bytes, resolved DDS dimensions, complete
declared image keys, and complete material source records. No C++ object layout,
native pointer, process lifetime or device handle enters the file. Entries are
ordered and unique; unsupported, missing or default entries are not omitted.

The `openq4.image-recovery.1` envelope uses direction/attempt-bound canonical
base64 chunks of 4,096 characters. Each key `chunk.0000` has a matching `0000:`
value prefix, including for the last chunk. Missing, duplicated, reordered,
malformed, noncanonical or oversized chunks fail. The framing checksum detects
corruption; it is not an authorization or source authenticity signature. The
journal validates the complete envelope rather than its discriminator alone.
Renderer semantic decode and actual cold-source validation remain separate.

Only versioned image directions receive the larger limit: 1.5 MiB of base64 per
direction, or 1,179,648 raw bytes. Other domains and legacy image maps retain their
64 KiB limits. The complete serialized journal, including JSON escaping, chunk
metadata, both directions, typed settings and every other domain, must fit the
existing 4 MiB atomic envelope. No truncation or sidecar protocol is introduced.

Actual parent-owned runtime census observed 1,903 total/loaded images in SP
airdefense1 after restart and 1,429 in MP q4dm1. These are inventory counts, not
proof that each image or material is supported. Synthetic complete journals with
1,903 images **and** 1,903 materials per direction occupy 2,843,527 bytes; 2,500
of each occupy 3,569,116 bytes. Both measurements include eight other domain maps
near their existing limits and real typed/placement metadata. Longer real names
or material inventories can still exceed the budget and must be refused.

CPU preparation limits retained pixel payload across both directions to 512 MiB,
plus independently bounded inventory/container metadata. A source read is limited
to 64 MiB; temporary decoding/source buffers can coexist with retained sets.
These are explicit resource limits, not estimates of all retail working sets.

## Remaining product and executor work

- Direct DDS can reconstruct a changed downsize policy only when the actual
  authored mip chain proves the exact requested result. Source bytes must match.
- Observed bimage is explicitly **cache pixels only**. Exact cache-byte replay
  cannot authorize a changed original source policy or reconstruct absent cache
  content. Cache names and digests do not imply original-source provenance.
- Decoded files, image programs, multi-source cubes, procedural, scratch,
  persistent/font and default image cohorts remain unsupported in this portable
  path. Any such entry refuses the whole preparation. Existing runtime lifecycle
  participation is not replaced by selective capture.
- Default materials, missing/unobserved retained source, historical material
  quality differing from current policy, shader reconstruction, and changes to
  source-selection/material-quality policy remain refused.
- Cold preparation currently requires an initialized complete live image/material
  inventory. Pre-first-device startup, portable all-cohort construction, content
  mounts and the unified durable host/controller still need integration.
- Upload-ring/path policy, audio, all-resource rollback, current owner presentation
  and actual GPU/driver qualification remain separate. There is no new SYSTEM
  activation, no claim of usable full-preset recovery, and no change to the current
  unsupported-effect Apply gate.

Focused tests execute the real coordinator, loader entry, module wrappers,
codec/journal and CPU cold loaders with counted boundaries and synthetic bytes.
They include retained opposite-direction recovery, source loss after preparation,
allocation failures, stale owners, teardown callbacks and malformed records.
Full MSVC translation units provide compile evidence; final module linkage and
windowed engine/GPU qualification are separate integration checks.
