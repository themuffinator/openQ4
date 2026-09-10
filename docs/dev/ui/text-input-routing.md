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
connected to a field. Composition metadata outside a write callback uses a
copied revision/sequence observation and revalidates it after foreign range
queries. Metadata within a write callback stays in its atomic transaction.
Update uses the supplied range; End does not query a terminated composition's
range or imply acceptance/cancellation. A failed
callback retires the document because a text service may already have accepted
the synchronous edit. Counted SDK callback tests do not qualify an installed TIP.

Application-origin snapshots use a separate exact-revision `SyncEngine` call.
It refuses pending native edits, locks or composition, updates the document
atomically and then reports actual text/selection changes outside the native
lock. A revision-only synchronization produces no false text change or native
shadow increment. Notification callbacks can obtain read locks. Outside an
explicit collection, all writes are refused; inside a lifecycle collection,
asynchronous writes are served once after the whole notification batch.
Application mutations and sink replacement cannot reenter that
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

The bundled SDL queue now associates each admitted entry with its exact provider
generation, queue sequence and native group ordinal. A checked poll consumes
one entry; copying or acknowledging a fence requires that exact consumed fence.
Another consumer, filtering or flushing the held group retires its validity.
Ordinary poll-sentinel maintenance can leave sequence gaps without inventing
missing group members. Provider enablement remains off by default.

The portable `NativeQueueIngress` owns copied event values and bounded text or
candidate payloads before a complete native collection becomes available to
Session. It verifies the group and copied fence before publication and exposes
`Validate` for the caller to recheck provider generation and engine queue
continuity before each effect. This caller integration remains pending. It requires
the exact external acknowledgement before finishing a collection. A provider
reset requires an observed disabled generation followed by a fresh generation.
Queue membership alone does not prove physical input or native text origin.
Connecting this ingress to Session and qualifying SDL's actual temporary-payload
lifetime are still required; counted queue tests cannot establish either.

The private SDL provider also supports copied Prepare/Finish collection hooks.
Registration requires a disabled, idle main-thread provider; caller-owned hook
storage remains alive through successful unregister. Prepare precedes native
processing, and Finish pairs with every attempted Prepare, including failure,
retirement and supported abnormal unwind. An exact context can mark native
activity even without SDL events. Controlled lifecycle work produces a held
fence without pumping or acknowledging it. These hooks remain disabled until
the engine supplies the complete ownership and provider integration.

The Windows store separately opens and closes explicit Pump or Lifecycle
collections. A dispatch number alone grants no write or composition authority.
Close removes callback admission and returns a copied final queue watermark;
it performs no acknowledgement or editor settlement. Exact-scope abort retires
without allocating, including with MSVC debug iterators. Queries distinguish
native quiescence from the separate editor/provider proof needed for renewal.

The portable `NativeTextEditor` reconciler keeps stable local text/history
separate from a complete native snapshot, including directional selection and
all concurrent composition ranges. It validates every ordered ACP operation
against the full editor/document/revision barrier. A checked collection spans
all offers up to its explicitly observed final transaction sequence, so an
insertion before Begin and later composition metadata share one undo group.
Stable history remains frozen until the collection is complete and composition
has ended; an explicit settlement makes the group one local edit. Clone/swap
publication checks the originating barrier again, and retirement restores the
stable draft.

Number interaction now owns the reconciler and exposes immutable full native
presentation independently from stable local text/history. Local commands,
clipboard authority and setting commit require native retirement first, even
when a native group has settled. Save-only copies carry no native mutation
authority; checked adoption must succeed before Runtime state or motion can
change. The Number view paints every concurrent nonempty composition range
using bounded copies of the authored vector underline and invalidates cached
geometry on exact native snapshot changes. Native provider activation and
platform qualification remain in development.

`NativeTextCollectionCoordinator` joins a checked owned event batch, an exact
closed Pump receipt and the original editor lease. It validates again after
foreign observations, applies and acknowledges the complete FIFO, and prepares
all stable-text/history allocations before native synchronization. Final local
publication checks the original authority and allocates nothing. Fence
completion is separate: Session must account for every ordinary event first.
Failure retires only the original lease and never consumes another owner's
input. Runtime exposes the matching checked methods; its allocation-free exact
retirement preserves stable draft/history. An explicit lifecycle entry requires
the exact copied closed receipt from the caller's checked application operation;
the ordinary entry still accepts Pump scopes only. Lifecycle callbacks retain
the same owner, revision, FIFO, settlement and fence checks. Cleanup of an
ineligible editor cannot use this path to publish edits.
An idle coordinator can return an owned copy of its last complete barrier.
The query refuses busy, retired and wrong-thread calls, preserves output on
allocation failure, and performs no owner or provider callbacks. The caller
must still establish current route eligibility before using that snapshot.

`WindowsTextCollectionBridge` connects the copied SDL hook table to the actual
SDK store. Fresh attachment requires no prior collection or composition lease;
the externally checked module epoch and provider generation are bound before
pumping. Prepare opens an exact scope, and Finish closes only its own successful
Open. Callback activity preserves a fence even without SDL text events. The
bridge validates the immutable closed watermark through acknowledgements and
native synchronization, including allocation-free terminal checks. Lifecycle
scopes cannot silently adopt a changed application revision. Destruction and
final COM release remain on the creating apartment, after disabled hook removal.
`ManagedNativeTextOwner` supplies the coordinator's private GUI endpoints. Each
call re-observes an engine-owned, callback-free route probe and resolves the
original allocation/backend/document again. Resource preparation returns before
the manager resolves the backend for the next effect. Native calls interlock
with ordinary text and clipboard boundaries; exact retirement preserves the
stable draft even after route eligibility changes. Current-owner checks, prepared
publication and exact retirement allocate nothing. Unloaded deferred GUIs and
dedicated builds refuse native entry without loading a retained backend.
Session routing, the route probe implementation and native activation remain open.

The Windows activation audit confirms that this SDL version owns IMM, with no
active SDL TSF thread manager to deactivate. The engine provider still requires
an exclusive per-window handoff, collection scope established before Peek,
activity marking for callbacks that produce no SDL events, and controlled
lifecycle collections. Ordinary character association, native candidate
geometry, graceful termination before store retirement, and fresh-context
renewal of the bounded composition identity budget remain explicit dependencies.

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
