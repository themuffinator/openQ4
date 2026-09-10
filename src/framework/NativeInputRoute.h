// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "../sys/EventDisposition.h"
#include "../ui/retained/TextInputBroker.h"
#include "../ui/retained/NativeTextDocument.h"
#include <memory>
#include <thread>
#include <type_traits>

namespace openq4 {
struct NativeInputWindow {
    std::uintptr_t handle = 0; // Comparison only; never dereferenced or called.
    std::uint64_t window = 0, lifetime = 0, module = 0, association = 0, registration = 0;
    bool operator==(const NativeInputWindow&) const = default;
};
struct NativeInputBinding {
    std::uintptr_t outer = 0; // Original managed allocation address, comparison only.
    std::uint64_t sessionTransition = 0, dispatchEpoch = 0, streamToken = 0;
    ui::TextEditorIdentity editor;
    ui::NativeTextIdentity native;
    NativeInputWindow window;
};
struct NativeInputObservation {
    std::uintptr_t current = 0;
    std::uint64_t allocation = 0, sessionTransition = 0, dispatchEpoch = 0, streamToken = 0;
    NativeInputWindow window;
    bool boundThread = false, inputAllowed = false, windowAllowed = false, providerOwned = false;
};
struct NativeInputSelection {
    std::uintptr_t current = 0;
    std::uint64_t route = 0, allocation = 0, window = 0;
    bool operator==(const NativeInputSelection&) const = default;
};
enum class NativeInputUiRetirement { Unknown, Busy, RetiredExact, AbsentOriginal };
enum class NativeInputNativeRetirement { Unknown, Busy, RetiredExact, ClaimLost };
struct NativeInputRetirement {
    std::uint64_t route = 0;
    ui::NativeTextIdentity native;
    NativeInputWindow window;
    NativeInputUiRetirement ui = NativeInputUiRetirement::Unknown;
    NativeInputNativeRetirement store = NativeInputNativeRetirement::Unknown;
    NativeInputNativeRetirement provider = NativeInputNativeRetirement::Unknown;
    bool hooksRemoved = false, controllerReleased = false, backlogDisposed = false;
};
enum class NativeInputLane { Platform, Pushed, Keyboard, Mouse };
enum class NativeInputSink { Session, Keyboard, Mouse };
enum class NativeInputKind { Unknown, RouteKey, RouteMouse, RouteText, NativeEcho, HeldRelease, WindowIntent, ProcessIntent };
// Exact scalar/header footprint of an actual storage entry, copied under its
// owning lock. Payload is an address for identity comparison only, never read.
// A translator must retain this exact value when issuing its ticket. Kind alone
// is not reconstructed from these raw numbers or from collection membership.
struct NativeInputValue {
    int type = 0, value = 0, value2 = 0, time = 0, payloadLength = 0;
    std::uintptr_t payload = 0;
    std::uint64_t deferredEmission = 0;
    bool operator==(const NativeInputValue&) const = default;
};
struct NativeInputHead {
    NativeInputLane lane = NativeInputLane::Platform;
    // Sequence is a non-reused storage head/slice identity, not just a ring index.
    std::uint64_t sequence = 0;
    unsigned slot = 0;
    sysEventDispositionTag_t tag;
    NativeInputValue value;
    bool operator==(const NativeInputHead&) const = default;
};
struct NativeInputIssued {
    sysEventDispositionTag_t tag;
    NativeInputSink sink = NativeInputSink::Session;
    NativeInputKind kind = NativeInputKind::Unknown;
    NativeInputValue value;
    bool issued = false, terminal = false, inFlight = false;
    // Deliberately no Admit success flag: actual transfer may precede failed Admit.
};

// Engine-owned read-only publishers/inspectors. These are REQUIRED production
// proofs, not established by this helper. All calls run on the constructing
// thread OUTSIDE storage locks, allocate nothing and invoke no GUI/native code.
// Return false or Unknown/Busy when an exact fact cannot be proved. Outputs must
// be copied values; never retain binding references after a call.
class NativeInputRouteSource {
public:
    virtual ~NativeInputRouteSource() = default;
    virtual bool Observe(NativeInputObservation& out) const noexcept = 0;
    // Match the FULL original binding (including control, allocation/backend/doc,
    // modal/editor-session/native lease), allowing checked editing revision progress.
    // UI !Current or WindowsTextSession::ownerRetired alone proves nothing here.
    // Store/provider retirement is independent of UI absence and current GUI.
    virtual bool Retirement(std::uint64_t route, const NativeInputBinding&, NativeInputRetirement& out) const noexcept = 0;
    // Join InspectIssuedForRetirement's exact retired planned ticket/pass/trigger
    // with the pinned translator kind/value and original route. Match the full
    // ticket and parent reservation; a legacy serial ledger cannot prove this.
    // No completion, Source polling, queue mutation or implicit native authority.
    virtual bool InspectIssued(std::uint64_t route, const NativeInputBinding&,
        const sysEventDispositionTag_t&, NativeInputIssued& out) const noexcept = 0;
};

// One stable, noncopyable/nonmovable slot. Neither Prepare nor any probe activates
// a provider, attaches an editor, retires native state, removes input or ACKs.
// Root's driver owns every referenced source, controller, batch and ledger until
// Release succeeds. This object's address also outlives all probe/permit calls.
// Kept at namespace scope so shared engine/game declarations can forward-declare
// this opaque type without importing retained UI's C++20 implementation headers.
// Only PrepareCancellation can mint a permit. Storage must still compare and
// transfer its exact current head once. Copies are not disposition receipts.
// New preparation or exact revoke/release attempts invalidate previous permits.
class NativeInputCancellationPermit {
public:
    NativeInputCancellationPermit() = default;
private:
    friend class NativeInputRoute;
    std::uint64_t route = 0, serial = 0;
    NativeInputHead head;
};

class NativeInputRoute final {
public:
    enum class Phase { Empty, Bound, Revoked, DrainOnly, Unavailable };
    using CancellationPermit = NativeInputCancellationPermit;
    explicit NativeInputRoute(NativeInputRouteSource&) noexcept;
    ~NativeInputRoute();
    NativeInputRoute(const NativeInputRoute&) = delete;
    NativeInputRoute& operator=(const NativeInputRoute&) = delete;
    NativeInputRoute(NativeInputRoute&&) = delete;
    NativeInputRoute& operator=(NativeInputRoute&&) = delete;
    // Copies all string-bearing data before publication. Bound does not mean the
    // WindowsTextSession is Active. The driver checks its separate native phase.
    bool Prepare(const NativeInputBinding&, std::uint64_t& route, std::string& error) noexcept;
    // Route eligibility only. The managed native-owner endpoint must separately
    // validate full editor/native lease and current revision before any effect.
    bool Probe(NativeInputSelection& out) noexcept;
    // Original identity proofs remain available for cleanup after a route fault.
    // They never grant live input eligibility or reactivate a revoked route.
    bool WindowCurrent(const NativeInputWindow&) noexcept;
    bool ProviderCurrent(const NativeInputWindow&) noexcept;
    bool Revoke(std::uint64_t exactRoute) noexcept;
    // An earlier protocol/reentry fault does not prevent exact old-head cleanup.
    // Every fresh proof rejects any new reentry/revocation during that read.
    bool MarkDrainOnly(std::uint64_t exactRoute) noexcept;
    bool PrepareCancellation(const NativeInputHead&, CancellationPermit& out) noexcept;
    // ZERO Source calls, allocation or foreign calls. Safe under storage locks;
    // lock order never requires a manager/native lookup. Exact old storage epoch
    // and stream identity stay in the permit even after those epochs retire.
    bool AllowsCancellation(const CancellationPermit&, const NativeInputHead& actual) noexcept;
    // Requires separate exact native release + hooks removed + backlog disposal
    // facts. Unknown/busy or loss cannot be upgraded to a successful release.
    bool Release(std::uint64_t exactRoute) noexcept;
    Phase State() const noexcept;
private:
    struct Entry;
    struct Guard;
    bool OnThread() const noexcept;
    bool Enter() noexcept;
    void Poison() noexcept;
    bool Observe(NativeInputObservation&) noexcept;
    bool OriginalWindow(const NativeInputObservation&) const noexcept;
    bool OriginalProvider(const NativeInputObservation&) const noexcept;
    bool ReadRetirement(NativeInputRetirement&) noexcept;
    NativeInputRouteSource& source;
    const std::thread::id thread;
    std::unique_ptr<Entry> entry;
    Phase phase = Phase::Empty;
    bool calling = false, poisoned = false, exhausted = false;
    std::uint64_t mutation = 0, permitSerial = 0, permitHighwater = 0;
};
static_assert(std::is_trivially_copyable<NativeInputObservation>::value &&
    std::is_trivially_copyable<NativeInputHead>::value &&
    std::is_trivially_copyable<NativeInputRoute::CancellationPermit>::value,
    "Native route probes and permits must be copied POD data");
} // namespace openq4
