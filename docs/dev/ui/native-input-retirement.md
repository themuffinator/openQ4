# Native input retirement

This increment supports the replacement UI's native text route. It does not
enable that route or accept the complete text, input or platform requirements.

`NativeInputRoute` owns one immutable original GUI/editor/window binding on its
constructing thread. A route identity never repeats. The route cannot be replaced
until its native controller and hooks are released and its old input is disposed.
Session, UI and window publishers must revoke it before changing the facts that
allowed input; observing the old pointer again cannot restore live eligibility.
The original window and provider claims remain independently queryable for cleanup.

Cleanup requires separate exact proofs for the original UI lease, native store
and provider. A failed eligibility query is not proof of retirement. The original
UI can remain bound while hidden, during resource reset or after its adapter has
advanced its document generation. Missing or busy proof keeps cleanup pending.

The manager's `UI_NativeTextPresence` query now distinguishes PresentExact,
AbsentOriginal and BusyOrUnknown on the manager's constructing thread. It scans
the original allocation and inspects the existing Runtime/Interaction barrier
without preparing a deferred backend, copying string-bearing views or consulting
current focus/visibility. A missing original allocation can prove absence;
an unavailable Runtime remains unknown. Advancing an adapter generation during
resource reset does not hide a native model that is still bound.

`NativeEventDispositionLedger::InspectIssuedForRetirement` reads an exact issued
ticket after the planned ledger retires. It returns the original pass, deferred
parent, admission serial and terminal/in-flight state without consulting the
provider or completing any event. Legacy serial ledgers cannot provide this
inventory. Admission success is not required: a queue can own an event even when
the later admission acknowledgement fails.

The route combines that issued-ticket observation with the translator's pinned
event classification and exact storage head. Preparation runs outside queue
locks and creates a copied cancellation permit. The predicate used by storage
performs no source lookup, allocation, native call or GUI dispatch. It compares
the nonreused head identity, lane, slot, complete sidecar and scalar/payload
header. Storage must still transfer that precise head once and preserve payload
ownership. A permit is neither an event-delivery receipt nor a native batch ACK.

Only audited old-route key, mouse, text, native echo and held-release records are
eligible. Window and process intents need their separate ordered disposition.
An old route cannot replay its input into a replacement GUI. Every new permit
preparation or exact revoke/release attempt invalidates an earlier permit.

A protocol fault permanently closes live eligibility but permits fresh exact
cleanup. Blocking all cleanup after a fault would strand the route's own backlog.
Reentry during a cleanup proof still invalidates that proof and its permits.
After complete exact release, a new route receives a different identity.

The planned ledger passes 117,524 checks, including 50 compiled mutations on
Windows Clang and sanitized Linux GCC, with positive MSVC debug qualification.
The route helper passes 4,203 checks and 33 mutations on Windows Clang and
sanitized Linux; MSVC debug passes 4,193 checks with its documented string-copy
allocation-sweep limitation. The tests bind production helpers to counted facts.
They do not establish production publishers, terminal queue cancellation,
held-source inhibition, native character provenance or installed IME behavior.
Those remaining seams and the complete native activation matrix are required.
The managed presence query passes 1,234 actual-method/model checks on all three
compilers and 43 compiled mutations on Windows Clang, including hidden and
generation-invalidated original models, replacement, allocation denial, busy
boundaries and independently constructed manager threads.

The import receipts and qualification records are preserved under
`.tmp/ui/native-retirement-integration/`. Actual storage retirement and original
UI presence observations remain separate proofs; native activation stays
disabled until the complete driver and lifecycle contracts are implemented.

## Actual storage transfer

The platform, pushed, SDL keyboard and SDL mouse stores now expose exact
retirement Peek/Take operations. A retained checked SDL slice precedes its ring;
pushed events precede platform events. Untagged heads and legacy slices obstruct
cleanup rather than being skipped. A nonreused storage serial prevents a reused
ring slot from satisfying an old permit. The original bound event thread remains
required after its active dispatch epoch has retired.

Permit construction stays outside storage locks. The actual Take compares the
current head under SDL's existing storage lock, or the platform/pushed store's
existing serialized thread ownership, and transfers that head's payload and
complete sidecar once. Keyboard retirement preserves its reserved deferred-child
identity without emitting that child. A separate End releases only an exactly
identified, already empty checked slice. Journal mode does not prevent cleanup;
these operations neither read nor write journal records and do not deliver input.

The actual store methods pass 2,221 checks on Windows Clang, MSVC debug and
sanitized Linux, with 30 compiled mutations on Clang and Linux. Existing queue,
SDL poll, journal and legacy ownership suites also pass. Full engine integration
found and corrected a shared-header dependency: the opaque cancellation permit
is forward declared, preserving the game modules' C++17 boundary. The resulting
engine/modules build and all 57 integrated UI suites pass. Terminal disposition,
native ACKs, held-source inhibition and production route activation remain
separate required integration work.

An unrelated legacy issue remains: platform and pushed head/tail counters are
signed integers that can eventually overflow without a queue Clear. The new
retirement serials do not change that ordinary queue arithmetic.
