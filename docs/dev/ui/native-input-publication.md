# Native input ownership publications

The engine now publishes copied Session, managed GUI and SDL window ownership
facts for the native input route. This connects the earlier route model to its
production owners. It does not activate native text or change ordinary event
delivery. The original route and all borrowed collaborators must remain alive
until checked retirement and release succeed.

Session changes, console/diagnostic blockers, relevant CVar changes, GUI
replacement and window changes revoke the old route before mutating their
published identity. Changing a value and restoring it cannot revive that route.
Closing or changing managed backends refuse native queries and retirement
forwarding while their destructor or replacement callback is running.

Binding checks compare the complete immutable original identity before and
after observing live eligibility. A publication fault blocks new input while
retaining the original identity needed for cleanup. Wrong-thread publication
latches revocation for the original event thread. Publication locks never call
the GUI, native provider or queue storage; observation and cancellation permit
construction remain outside storage locks.

The typed emission inventory retains the actual translator branch, batch,
ledger ticket, event footprint and reserved deferred child. It reserves storage
before issuing a real ticket and retains issued ownership if later admission
fails. A dequeued event is never classified retrospectively to manufacture
authority. The controller supplies separate copied store, provider and native
release observations.

## Qualification and remaining integration

Independent reviews corrected backend deletion reentry and incomplete binding
comparison before integration. Portable Windows/Linux tests cover the actual
publication, route, inventory and ledger code; extracted engine methods cover
pre-mutation ordering. The Windows SDK tests use counted provider objects and
the real controller/store/bridge methods, without native activation or OS input.
The integration records live under `.tmp/ui/native-publication-integration/`.

The first full build also found the publication declarations were guarded out
of the dedicated GUI translation unit. The declarations now remain available
to both client and dedicated builds. Native queue inventory and Windows source
implementation remain client dependencies.

## Exact transfer and terminal disposal

`NativeInputTransfers` now connects the typed inventory to the actual platform,
pushed, keyboard and mouse stores. A private claim precedes ticket issuance.
Successful storage owns the frozen event even if later admission fails; the
result distinguishes unchanged input, stored admission and stored retirement.
A callback that replaces the caller's value cannot lose its replacement payload.

Delivery takes the exact stored head before entering the ledger's delivery state.
It rechecks the complete head after observing the live source, since that
observation can prepend an unrelated event. Both owned delivery results transfer
the actual value to the sink; a later refusal cannot conceal that ownership.
Deferred modifier children require their exact keyboard parent's active delivery.
The sink must complete delivery only after its actual effect or audited discard
and payload disposal.

Cancellation observes the actual head, obtains an original-route permit outside
storage locks, takes that same head and disposes its payload outside the locks.
An uncertain destruction remains indeterminate and cannot be retried as a free.
The source's disposal receipt requires every issued slot to be accounted for by
private transfer/disposal state and the ledger. Merely completing a ledger ticket
cannot dispose an event still in a queue. Unknown, external, stored or in-flight
obligations prevent release; checked route release and unbind precede dropping
the transfer owner's borrowed collaborators.

The terminal suite executes both actual Windows and POSIX queue/store bodies
with counted source, allocator and provider boundaries. Meson and both CI
workflows register it; portable runs provision only the pinned SDL public-header
closure with its existing verified source helper. The integration records live
under `.tmp/ui/terminal-content-integration/`. These tests exercise ownership and
disposal, without native activation or physical input.

## Remaining production dispatch

The next driver increment must connect the normal production sinks, held-source
inhibition, ordinary fence acknowledgement and successful batch turnover. The
full SP/MP sink schedules,
deferred modifiers, native character provenance, loading/console/fatal pumps,
IME behavior and platform/input acceptance remain required. A process-linked
SDL claim does not qualify unloading and replacing that module.
