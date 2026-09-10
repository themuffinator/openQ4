# openQ4 SDL3 package adjustments

The Meson package builds [SDL 3.4.10](https://github.com/libsdl-org/SDL/tree/release-3.4.10),
copyright Sam Lantinga and the SDL contributors, under its zlib license. That
license permits the modifications below and is compatible with openQ4's GPLv3
distribution. SDL's original license notice remains in the source distribution.

`clipboard-status.patch` is an openQ4-authored modification, not an upstream SDL
release change. It makes Windows clipboard retry, clear, allocation, lock and
ownership-transfer failures observable to the caller, preserving the primary
error and freeing storage whose ownership did not transfer. The shared text
setter also checks its copy allocation before publishing callback data. The
Windows read fallback tolerates failure to allocate its empty result.

Text reads always request Unicode, including when only ANSI or OEM text was
advertised, and report failed retrieval/conversion. This follows Windows'
[synthesized clipboard formats](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats#synthesized-clipboard-formats)
and [clipboard locale conversion](https://learn.microsoft.com/en-us/windows/win32/dataxchg/standard-clipboard-formats)
contracts. The patch removes raw legacy-byte guesses; Windows owns the conversion
using the clipboard's locale rather than an assumed process code page.

This supports checked UTF-8 clipboard primitives for future retained text fields.
An unsuccessful write may already have cleared the native clipboard; the patch
does not promise clipboard rollback. Native platform newline conversion remains.
It does not enable a live text field, input route, IME, shaping or editor feature.
Only the bundled Windows provider verified to contain this patch enables the private
`OPENQ4_SDL3_CHECKED_CLIPBOARD=1` build capability; external/system SDL with the
same version is not assumed patched. X11, Wayland and Cocoa native failure
paths still require qualification. Running the counted Windows/SDL tests on
Linux does not qualify those native providers. Without the capability the new checked
primitives report unsupported and make no SDL calls. Legacy clipboard routes
remain available with their existing behavior.

`text-provenance.patch` and the adjacent `SDL_openq4_text_provenance` sources are
also openQ4-authored zlib-licensed modifications. They provide a private Windows
text observer, advertised only by this bundled provider through
`OPENQ4_SDL3_TEXT_PROVENANCE=1`. They do not change public SDL event layouts or
the existing native IME UI hint. The Windows dispatcher still runs its original
IME handler and default window procedure with their original return behavior.

The observer puts bounded, payload-free markers in the same SDL event queue.
Each actual admitted text commit has a private immutable UTF-8 record and an
explicit token in the public text event's reserved field. Association occurs
after event filters/watchers and before queue admission, so a filter rejection
or transformation is respected. Native scope is claimed once for the exact
`SDL_SendKeyboardText` event before those callbacks. Reentrant public pushes,
including the same event pointer, and nested `SDL_SendKeyboardText` calls receive
Unknown origin; they cannot inherit the outer native admission. A public queue-admission failure removes its
private commit. `Result` notifications describe IMM state and never insert
characters independently; the actual commit is the only insertion record.
Synthetic or off-thread text has no native composition affinity. Consumers must
copy and release markers, suppress only their explicitly associated public
duplicates, and reserve unassociated/failure text for validated legacy fallback.

The copy/release API is main-thread-only. It bounds storage to 256 records,
1 MiB of text including terminators, and 65,536 UTF-8 bytes per record. Allocation,
encoding, admission, and counter exhaustion failures poison retained affinity.
The independently queried health flag does not depend on delivery of a failure
marker. Filters that change a marker's type, window, token, reserved field or
data pointers are rejected before queue insertion and poison the observation;
SDL-owned timestamp changes remain permitted. Flushes/filters can strand only bounded slots, released by explicit reset,
disable, or video shutdown. Reset never reuses tokens and does not restore trust
in existing native windows; callers must reconcile their own queues. Arbitrary
filters that remove or mutate already queued markers/public events cannot be
assumed lossless. Token exhaustion fails closed without pointer truncation.

This is deliberately a partial IMM observation contract. SDL 3.4.10 has no active
TSF text store or composition sink. A native Begin establishes an observed
interval; preedit clearing and synchronous default-result character generation
retain that interval. Unmarked `WM_CHAR` is identified as a native character
path, **not** as proven physical-key causation. Missing/overlapping Begin,
cancellation, context changes, and asynchronous result characters are unknown.
After End or cancellation, the observer conservatively keeps that native window
lifetime tainted: a later Begin cannot prove that a late `GCS_RESULTSTR` belongs
to it. Thus this hook cannot qualify repeated production IME editing. Native
legacy characters remain available; an ordered retained broker must reject
unknown origins rather than adopt them into the currently focused field.

Microsoft documents the underlying [default IME result translation](https://learn.microsoft.com/en-us/windows/win32/intl/status--composition--and-candidates-windows),
[IMM message parameters](https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-composition),
and separate [TSF composition identity and sink](https://learn.microsoft.com/en-us/windows/win32/tsf/compositions).
The counted native-method tests validate observer, queue, and ownership logic;
they do not qualify an installed IME, native candidate positioning, TSF,
retained composition rendering, cross-platform input, or a completed text field.

`dispatch-fence.patch` and `SDL_openq4_native_fence` are separate openQ4-authored
zlib-licensed modifications, advertised by the bundled Windows provider through
`OPENQ4_SDL3_NATIVE_FENCE=1`. They do not alter public SDL event structures. The
extension is disabled by default and has no GUI, Session, COM or TSF callbacks.
It provides an ordering boundary for a future ordered engine/native-document
bridge; adding the capability does not activate that bridge.

A strict collection consists of the existing Windows pump's input housekeeping,
one `PeekMessage(PM_REMOVE)` and the optional translation/dispatch of its returned
message, including all synchronous callbacks. [Microsoft documents that PeekMessage
also dispatches incoming sent messages](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-peekmessagew).
Thus one collection can include several WindowProc callbacks and SDL events. It
is not one key, one native callback or one rendered frame. A future TSF document
must retain its immutable editor lease throughout this whole collection; it must
not adopt the GUI that might be selected when those queued keys are later handled.

After that complete step, a bounded payload-free marker holds subsequent native
pumps and waits until the engine copies the matching published fence and explicitly
acknowledges its exact dispatch/sequence after checked consumption. Empty
held waits return promptly, including SDL periodic-poll and fallback paths, without
another native Peek. A synchronous nested pump cannot start a second collection.
Filters, failed queue insertion, mutated/forged fence admissions, unsolicited
callbacks outside a collection and counter/event-budget exhaustion invalidate
health. Engine queue continuity and native-provider health remain separate checks
required before every delivery and acknowledgement.

`queue-consumer.patch` is the additional openQ4-authored checked consumption guard,
advertised only by this bundled Windows provider through
`OPENQ4_SDL3_CHECKED_NATIVE_QUEUE=1`. It tags private SDL queue entries at actual
admission, including direct `SDL_PeepEvents` additions, without changing public
event layouts. `OQ4_WindowsNativeFencePoll` removes one actual FIFO entry without
pumping. It returns 1 for a record, 0 for an empty queue and -1 when unavailable;
both outputs remain unchanged on 0/-1. Dynamic payloads retain SDL's ordinary
borrowed lifetime and must be copied immediately by the engine.

Records distinguish preexisting/worker/outside events, poll sentinels, native
collection entries and the fence. `event_count` counts actual collection entries;
each has an ordered ordinal. Copy/Ack require the actual checked fence receipt
after every collection ordinal, so matching fabricated marker bytes cannot
authorize acknowledgement. The engine must buffer the full ordered group through
that receipt before native mutations, preserving interleaved outside records.
Sentinel maintenance is harmless and excluded from group counts; it can create
gaps in the otherwise increasing queue sequence. Total queue counts or sequence
contiguity must not be used to infer collection membership.

While enabled, another consumer's non-sentinel GET, flush, filter removal or
coalescing discard faults health immediately, including outside/prefix events.
In-place filters that mutate an already queued event union also fault. Ordinary
pre-admission watchers retain their existing SDL/observer transformation rules;
rejected native admissions fault the collection. The guard does not deep-copy
caller-owned payload memory. Queue state is protected by SDL's event-queue lock;
workers observe only atomic provider health and cannot adopt main-thread native
scope. Stopping the event subsystem retires the queue lease before freeing it;
starting it again cannot revive the old lease. Disabled ordinary queue behavior
and its failure diagnostics remain unchanged.

The API is main-thread-only. Failed Pending/Copy calls preserve output, and merely
reading Pending does not authorize acknowledgement. Successful checked Poll alone
also does not replace the matching Copy before Ack. Dispatch and sequence use
non-reused 64-bit counters; marker codes use a non-reused positive 31-bit counter.
A collection accepts at most 4,096 queued SDL events before health fails closed.
No native payload is allocated by this fence. A provider reload requires a separate
engine epoch; process tokens and live compositions must never be serialized.
Creation/destruction drains retire before native work rather than waiting on engine
acknowledgement. Retirement clears pending authority, preserves highwaters and
prevents re-enabling inside a still-unwinding collection. Engine reconciliation is
required before re-enabling. The existing legacy dispatcher continues running.

MSVC-compatible Windows builds restore the observer scope and fault/clean up a
collection or marker emission on abnormal SEH unwinding. On other C toolchains,
nonlocal unwinding through SDL callbacks remains outside the supported C callback
contract; a callback must return normally. SDL queue allocators, filters and logging
callbacks retain that normal-return contract on every toolchain; arbitrary nonlocal
unwinding through SDL's locked queue internals is not made safe by this extension.
Tests compile the five production C translation units against the pinned headers
and execute complete pump, admission, wait and fence methods plus SDL's actual
Add/Cut/Peep/Flush/Filter/Start/Stop bodies with native doubles, real worker threads
and synthetic SEH failures at the guarded Windows seams. They
do not qualify a real OS message queue, installed IME, TSF document, engine owner
handoff or completed retained text editing.

`queue-generation.patch` adds a main-thread, queue-locked read of the healthy
active generation (zero when unavailable). It allows the engine's owned ingress
to bind the enable lifetime before its first checked Poll and to recheck it after
copying each borrowed event. It does not enable, pump, consume or acknowledge the
queue. Module epochs remain engine-owned and cannot be invented by constructing
another ingress object. The owned batch preserves ordering only; neither a queue
collection nor a verified fence proves physical-key or text-composition origin.
