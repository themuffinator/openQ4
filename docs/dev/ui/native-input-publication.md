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

The source deliberately reports incomplete backlog disposal. The next driver
increment must connect exact storage transfer, successful delivery or terminal
cancellation, held-source inhibition, fence acknowledgement and checked release.
Untracked or in-flight events must prevent release. The full SP/MP sink schedules,
deferred modifiers, native character provenance, loading/console/fatal pumps,
IME behavior and platform/input acceptance remain required. A process-linked
SDL claim does not qualify unloading and replacing that module.
