# Native input sink driver

The driver connects an already reconciled, fully translated owned batch to the
actual EventLoop, Session and natural Usercmd sinks. The live engine does not
construct it yet. Native character entry, the SDL translator, provider bootstrap
and complete platform activation remain in development.

## Admission and delivery

`NativeInputDriver::Install` checks the exact collection completion, original
GUI/editor/window binding, owned batch, emission inventory, transfers and ledger.
The driver and every referenced collaborator must survive through checked
`Uninstall`, including a failed Install that already acquired pump ownership.
Callbacks cannot destroy these objects. Failed owner checks revoke the original
route and start retirement; incomplete-batch checkpoints also observe revocation.

The driver follows the ledger schedule established at the actual Session or
Mouse-poll entry. It does not infer ordering from single-player or multiplayer
mode. Deferred keyboard emissions enter the real Session queue before the
corresponding Usercmd key call. The usercmd boundary continues only the owned
deferred Session pass, without pumping events, executing command buffers,
simulating another tick or drawing a frame.

Each ordinary completion follows the actual storage transfer and payload
destruction or scalar disposition. Session payloads are also freed on callback
exceptions. Ownership is rechecked between foreign console, Escape and menu
callbacks. A private exact path disposes a taken event after its route retires;
it cannot invent a successful delivery. Legacy prefixes retain their natural
eligible sink. Uncertain suffixes obstruct progress and cannot reach a replacement
GUI merely because they lack a typed tag.

All passes and issued emissions need actual terminal results before the ledger
can seal. A checked fence revalidates the ordinary receipt, original route,
owner and queue continuity immediately before acknowledgement; post-ACK checks
remain mandatory. Completed inventory can detach while collection pumps stay
owned. A successor needs a fresh presentation frame and exact reconciliation
and installation checks. `CanPrepareCollection` is a fresh observation, not a
reusable permission. Work is bounded to 512 sink/disposal operations per frame;
exhaustion preserves the current cursor and ownership.

## Held input and retirement

Usercmd inhibition preserves the existing Session inhibit state. Key and mouse
button source tuples come from actual owned SDL records joined to their typed
emissions. Known and unknown holds stay quarantined. Only a known final release
from the original route/window/device/source, with no other hold for that logical
key, can clear preliminary key state. Collection membership does not certify
native text provenance.

The original Windows source and controller provide the exact native store,
provider and hook retirement proofs. Raw Windows and SDL event pumps are gated
while a driver owns collection. Fatal Windows handling first copies its bounded
diagnostic, then attempts retirement before opening or pumping a console window.
If retirement cannot be proved, it writes the diagnostic to stderr and terminates
the current process without calling native input hooks.

SDL clients select the real driver; dedicated and other platform backends select
the C++17 disabled facade. They never link both. With no installed driver, the
ordinary engine event and usercmd behavior remains active.

## Verification and remaining work

`tools/tests/native_input_driver.py` compiles actual driver, queue, ledger,
coordinator, Interaction, EventLoop, Session and Usercmd code against counted
external boundaries. Windows/POSIX projections exercise both schedules, held
input, failure, disposal, revocation and callback replacement. Compiled mutations
test the critical ordering checks. Windows SDK tests exercise the real store and
controller interfaces; portable sanitizer and complete engine-unit compilation
are separate evidence. These tests do not establish installed IME behavior.

Live activation still requires the exact translator/bootstrap, immutable provider,
window and document lifetimes, character provenance and association, queue-group
collection, and lifecycle renewal. Asynchronous usercmd input is refused. Loading
getters and Pacifier, SDL shutdown/window replacement, cursor/grab housekeeping,
unknown held-source renewal and independent legacy/system-event disposition must
join the lifecycle before enablement. The non-SDL GL fake-window pump needs its
separate applicability gate. All product requirements and final acceptance gates
remain governed by the [completion plan](../plans/ui-product-completion.md).
