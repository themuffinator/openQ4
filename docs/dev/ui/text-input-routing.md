# Native text delivery

Status: implementation in progress. The portable broker, wire codec, SDL record
decoder and reviewed Windows observer are implemented. The observer remains
disabled until the engine owner boundary, ordered event delivery and native
dispatch fence are connected and qualified. This does not complete the
[UI product plan](../plans/idtech5-ui.md) or its international text requirements.

## Ownership and ordering

Native callbacks collect bounded data and never call Session, GUI or renderer
code. The engine resolves the actual editor after earlier commands and actions
have completed. Ownership includes the managed allocation, backend/document and
modal lifetimes, native window/session, control, and current edit session/revision.
Paths, numeric SDL window IDs and pointer addresses are insufficient identities.

The broker binds a composition once, preserves that association through empty
preedit, and advances it only after an exact successful delivery receipt. A
focus change, external revision or rejected delivery retires the association.
Old retained text never falls through to a newly focused legacy field. Native
Result records are metadata; only a validated admitted Commit can insert text.
Unknown origin never becomes proven input for the current retained editor.

The 80-byte versioned wire header encodes integers explicitly in little endian.
It carries bounded NUL-free UTF-8 plus explicit optional preedit byte offsets,
not raw C++ objects, editor leases or callback addresses. The decoder validates
every enum, reserved field, length and payload shape before replacing its output.
Only Unknown composition metadata may have no native text session: it advances
the ordered sequence and is discarded without adopting an editor.

The SDL decoder validates the actual private provider record and converts UTF-16
preedit offsets to strict UTF-8 boundaries. A bounded ledger associates each
public SDL text echo with its exact canonical Commit, once. It never compares
strings to guess duplicate input. Failed, missing, reused or overflowing marker
associations latch failure. Engine queue loss/flush and native provider failure
must also retire delivery before any editor mutation; the ledger cannot observe
a loss inside the existing void event-queue API by itself.

The engine now exposes a process-local queue continuity token. Both platform
queues invalidate it before overflow discards an event and before every explicit
clear, including an empty clear after the final event was removed. The pushed
event queue and event-loop initialization/shutdown invalidate the same token.
Ordinary enqueue/dequeue preserves it. Exhaustion produces permanent zero;
neither zero nor a recorded token can authorize native input. Native integration
must capture the token before collection and check it before each mutation and
acknowledgement, alongside provider health and the exact native fence. This signal
does not make the existing queues safe for concurrent producers.

## Windows provider and remaining integration

The bundled SDL patch observes native composition scope and exact admission of
the resulting public SDL text event. Event filters cannot lend that scope to a
nested synthetic event, mutate a private marker into another event, or strand its
storage silently. Build support requires the private reviewed package capability;
a matching system SDL version does not enable the decoder. Unsupported builds
use a stub that reports unavailable without calling SDL.

IMM messages do not provide a durable composition object identity. After an
ambiguous cancellation, ending or overlap, the current observer conservatively
taints the surviving window. Repeated production IME editing therefore requires
the planned TSF text-store provider, with immutable document/composition identity,
atomic range transactions, checked locks and separate native/engine revisions.
The current observer is not production IME qualification.

The portable native text-document model is now implemented. It keeps an immutable
document/editor lease, separate native-shadow and accepted engine revisions,
checked read/write callback scopes, strict UTF-8/UTF-16 ACP boundaries and bounded
FIFO range transactions. A failed operation prevents partial publication, and a
rejected front transaction retires its dependent work. Composition identity is
transient and never restored from an editor save. Classification occurs at the
complete transaction boundary, including insertion before a composition begins
inside that boundary. The model does not prove that all TSF callbacks share one lock.

The Windows-only text store now implements the actual SDK `ITextStoreACP`,
composition-sink and edit-sink interfaces. It checks apartment ownership,
callback-scoped locks, context identity, deferred write upgrades, copied range
layout revisions and transaction acknowledgements. It is not activated or
connected to a field. Composition changes currently require an active write
callback; arbitrary multi-lock composition sessions and production candidate
placement remain required. A failed
callback retires the document because a text service may already have accepted
the synchronous edit. Counted SDK callback tests do not qualify an installed TIP.

Application-origin snapshots use a separate exact-revision `SyncEngine` call.
It refuses pending native edits, locks or composition, updates the document
atomically and then reports actual text/selection changes outside the native
lock. A revision-only synchronization produces no false text change or native
shadow increment. Notification callbacks can obtain read locks; synchronous
write requests are refused and asynchronous writes are served once after the
whole batch. Application mutations and sink replacement cannot reenter that
batch. A failing callback retires the store and requires retirement of the
paired engine binding. Native edits and acknowledgements never emit these
application-origin notifications. A fresh copied layout emits its own layout
notification after installation. These rules follow Microsoft's
[OnTextChange](https://learn.microsoft.com/en-us/windows/win32/api/textstor/nf-textstor-itextstoreacpsink-ontextchange),
[RequestLock](https://learn.microsoft.com/en-us/windows/win32/api/textstor/nf-textstor-itextstoreacp-requestlock)
and [OnLayoutChange](https://learn.microsoft.com/en-us/windows/win32/api/textstor/nf-textstor-itextstoreacpsink-onlayoutchange)
contracts; the engine-to-native receipt path remains to be connected.

The adapter follows the SDK's range-query and insertion contracts, including
query-only insertion and unavailable stale layout. See Microsoft's
[ITextStoreACP interface](https://learn.microsoft.com/en-us/windows/win32/api/textstor/nn-textstor-itextstoreacp),
[InsertTextAtSelection](https://learn.microsoft.com/en-us/windows/win32/api/textstor/nf-textstor-itextstoreacp-inserttextatselection)
and [GetTextExt](https://learn.microsoft.com/en-us/windows/win32/api/textstor/nf-textstor-itextstoreacp-gettextext).

SDL can collect several native messages before Session handles the resulting
events. A private dispatch fence must hold collection after a native group until
the engine completes those events and their effects, updates the actual editor,
and acknowledges that exact fence. One group includes the synchronous callbacks
inside message collection and dispatch. A later pump must not bypass the hold;
teardown must retire ownership before draining its native messages.

Activation must establish a newly observed text session: enabling the observer
while SDL text input is already active emits no retroactive SessionBegin.
Likewise, native Reset clears its active session without emitting SessionEnd,
while broker Reset preserves the old-session barrier. Integration must drain a
real ordered End before resetting or introduce an explicitly checked reset
protocol. It must not invent lifecycle records or silently adopt old input.

Candidate placement needs a current range-layout snapshot tied to the exact
document and text revision. Current scalar LTR numeric caret geometry does not
qualify shaping, graphemes, bidi affinity, CJK composition or general text layout.
Native process IDs also confer no journal replay authority; live editor delivery
must reject recorded native origins until a separate replay design is qualified.

## Validation boundary

Pure tests exercise production broker/codec/decoder source, explicit echo
association, stale owners, UTF-16 surrogate boundaries, result-versus-commit
separation, queue bounds, and unsupported compilation. Windows and Linux
sanitizer runs cover those value contracts. The observer tests compile the
actual pinned SDL headers and patched Windows translation units and reject
behavioral mutations. These tests do not operate a physical device or establish
native TSF/IMM coexistence, candidate key consumption, monitor/DPI movement,
clipboard field behavior, or full menu/editor completion.
