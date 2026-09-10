// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "NativeInputRoute.h"
#include <atomic>
#include <limits>

namespace openq4 {
namespace {
std::atomic<std::uint64_t> nativeInputRouteHighwater{0};
std::uint64_t NextRoute() noexcept {
    auto previous = nativeInputRouteHighwater.load();
    do {
        if (previous == (std::numeric_limits<std::uint64_t>::max)()) return 0;
    } while (!nativeInputRouteHighwater.compare_exchange_weak(previous, previous + 1));
    return previous + 1;
}
bool WindowShape(const NativeInputWindow& w) noexcept {
    return w.handle && w.window && w.lifetime && w.module && w.association && w.registration;
}
bool BindingShape(const NativeInputBinding& b) noexcept {
    const auto& e = b.editor;
    return b.outer && b.sessionTransition && b.dispatchEpoch && b.streamToken && WindowShape(b.window) &&
        e.allocation && e.backend && e.document && e.modal && e.window == b.window.window && e.session && e.revision &&
        !e.control.empty() && e.control.size() <= 128 && e.control.find('\0') == std::string::npos &&
        b.native.document && b.native.editorLease;
}
bool ValueShape(const NativeInputValue& v) noexcept {
    return v.payloadLength >= 0 && v.payloadLength <= 1024*1024 &&
        ((v.payloadLength != 0) == (v.payload != 0));
}
bool HeadShape(const NativeInputHead& h) noexcept {
    const bool poll = h.lane == NativeInputLane::Keyboard || h.lane == NativeInputLane::Mouse;
    const bool session = h.lane == NativeInputLane::Platform || h.lane == NativeInputLane::Pushed;
    return (poll || session) && h.sequence && h.slot < (poll ? 512u : 8192u) && h.tag.ShapeValid() && ValueShape(h.value) &&
        (!poll || !h.value.payload) &&
        (!h.value.deferredEmission || (h.lane == NativeInputLane::Keyboard && h.value.deferredEmission != h.tag.emission));
}
bool KindMatches(const NativeInputIssued& issued, NativeInputLane lane) noexcept {
    if (issued.kind == NativeInputKind::Unknown || issued.kind == NativeInputKind::WindowIntent || issued.kind == NativeInputKind::ProcessIntent)
        return false;
    if (lane == NativeInputLane::Keyboard)
        return issued.sink == NativeInputSink::Keyboard &&
            (issued.kind == NativeInputKind::RouteKey || issued.kind == NativeInputKind::HeldRelease);
    if (lane == NativeInputLane::Mouse)
        return issued.sink == NativeInputSink::Mouse &&
            (issued.kind == NativeInputKind::RouteMouse || issued.kind == NativeInputKind::HeldRelease);
    return issued.sink == NativeInputSink::Session &&
        (issued.kind == NativeInputKind::RouteKey || issued.kind == NativeInputKind::RouteMouse ||
         issued.kind == NativeInputKind::RouteText || issued.kind == NativeInputKind::NativeEcho || issued.kind == NativeInputKind::HeldRelease);
}
bool Retired(const NativeInputRetirement& facts) noexcept {
    return (facts.ui == NativeInputUiRetirement::RetiredExact || facts.ui == NativeInputUiRetirement::AbsentOriginal) &&
        facts.store == NativeInputNativeRetirement::RetiredExact && facts.provider == NativeInputNativeRetirement::RetiredExact;
}
bool Error(std::string& error, const char* value) noexcept {
    try { error = value; } catch (...) { error.clear(); }
    return false;
}
}
struct NativeInputRoute::Entry {
    const NativeInputBinding binding;
    const std::uint64_t route;
    Entry(const NativeInputBinding& b, std::uint64_t id) : binding(b), route(id) {}
};
struct NativeInputRoute::Guard {
    NativeInputRoute& owner;
    explicit Guard(NativeInputRoute& o) : owner(o) {}
    ~Guard() { owner.calling = false; }
};
NativeInputRoute::NativeInputRoute(NativeInputRouteSource& s) noexcept : source(s), thread(std::this_thread::get_id()) {}
NativeInputRoute::~NativeInputRoute() = default;
bool NativeInputRoute::OnThread() const noexcept { return thread == std::this_thread::get_id(); }
bool NativeInputRoute::MatchesOriginalBinding(std::uint64_t route, const NativeInputBinding& b) const noexcept {
    if (!OnThread() || calling || !entry || entry->route != route) return false;
    const auto& original = entry->binding;
    return original.outer == b.outer && original.sessionTransition == b.sessionTransition &&
        original.dispatchEpoch == b.dispatchEpoch && original.streamToken == b.streamToken &&
        original.editor == b.editor && original.native == b.native && original.window == b.window;
}
void NativeInputRoute::Poison() noexcept {
    poisoned = true; permitSerial = 0;
    // Even repeated reentry after an earlier fault must invalidate an outer
    // cleanup proof. Neither this barrier nor permit identities ever wrap.
    if (mutation == (std::numeric_limits<std::uint64_t>::max)()) exhausted = true;
    else ++mutation;
    if (phase == Phase::Bound) phase = Phase::Revoked;
}
bool NativeInputRoute::Enter() noexcept {
    if (!OnThread() || exhausted) return false;
    if (calling) { Poison(); return false; }
    calling = true; return true;
}
bool NativeInputRoute::Observe(NativeInputObservation& out) noexcept {
    const auto before = mutation;
    return source.Observe(out) && !exhausted && mutation == before && out.boundThread;
}
bool NativeInputRoute::OriginalProvider(const NativeInputObservation& o) const noexcept {
    return entry && o.providerOwned && o.window.module == entry->binding.window.module &&
        o.window.registration == entry->binding.window.registration;
}
bool NativeInputRoute::OriginalWindow(const NativeInputObservation& o) const noexcept {
    return OriginalProvider(o) && o.windowAllowed && o.window == entry->binding.window;
}
bool NativeInputRoute::Prepare(const NativeInputBinding& requested, std::uint64_t& out, std::string& error) noexcept {
    if (!Enter()) return Error(error, "Native route is reentrant or on another thread");
    Guard guard(*this);
    if (entry || phase != Phase::Empty || poisoned || permitHighwater == (std::numeric_limits<std::uint64_t>::max)() ||
        !BindingShape(requested)) return Error(error, "Native route slot or binding is unavailable");
    const auto route = NextRoute();
    if (!route) return Error(error, "Native route identities exhausted");
    try {
        // Freeze all borrowed/string-bearing fields before any Source observation.
        auto candidate = std::make_unique<Entry>(requested, route);
        NativeInputObservation observed;
        if (!Observe(observed)) return Error(error, "Native route source is unavailable");
        const auto& b = candidate->binding;
        if (!observed.inputAllowed || !observed.windowAllowed || !observed.providerOwned ||
            observed.current != b.outer || observed.allocation != b.editor.allocation ||
            observed.sessionTransition != b.sessionTransition || observed.dispatchEpoch != b.dispatchEpoch ||
            observed.streamToken != b.streamToken || observed.window != b.window)
            return Error(error, "Native route no longer matches its original owner or window");
        entry = std::move(candidate); phase = Phase::Bound; out = route; error.clear(); return true;
    } catch (...) { return Error(error, "Native route allocation failed"); }
}
bool NativeInputRoute::Probe(NativeInputSelection& out) noexcept {
    if (!Enter()) return false;
    Guard guard(*this);
    if (!entry || phase != Phase::Bound || poisoned) return false;
    NativeInputObservation observed;
    if (!Observe(observed)) { phase = Phase::Revoked; return false; }
    const auto& b = entry->binding;
    if (!OriginalWindow(observed) || !observed.inputAllowed || observed.current != b.outer ||
        observed.allocation != b.editor.allocation || observed.sessionTransition != b.sessionTransition ||
        observed.dispatchEpoch != b.dispatchEpoch || observed.streamToken != b.streamToken) {
        phase = Phase::Revoked; return false;
    }
    out = {b.outer, entry->route, b.editor.allocation, b.window.window}; return true;
}
bool NativeInputRoute::WindowCurrent(const NativeInputWindow& expected) noexcept {
    if (!Enter()) return false;
    Guard guard(*this);
    if (!entry || expected != entry->binding.window) return false;
    NativeInputObservation observed;
    return Observe(observed) && OriginalWindow(observed);
}
bool NativeInputRoute::ProviderCurrent(const NativeInputWindow& expected) noexcept {
    if (!Enter()) return false;
    Guard guard(*this);
    if (!entry || expected != entry->binding.window) return false;
    NativeInputObservation observed;
    return Observe(observed) && OriginalProvider(observed);
}
bool NativeInputRoute::Revoke(std::uint64_t expected) noexcept {
    // Safe during a Source/outer callback: do not enter another foreign boundary.
    if (!OnThread() || !entry || expected != entry->route) return false;
    if (mutation == (std::numeric_limits<std::uint64_t>::max)()) { Poison(); return false; }
    ++mutation; permitSerial = 0;
    if (phase == Phase::Bound) phase = Phase::Revoked;
    return true;
}
bool NativeInputRoute::ReadRetirement(NativeInputRetirement& out) noexcept {
    if (!entry || exhausted) return false;
    const auto before = mutation;
    NativeInputObservation observed;
    if (!source.Observe(observed) || exhausted || !observed.boundThread || mutation != before) return false;
    // Retirement keeps the original binding; a new GUI/provider observation is
    // neither permission to touch it nor grounds to relabel it as the original.
    return source.Retirement(entry->route, entry->binding, out) && !exhausted && mutation == before &&
        out.route == entry->route && out.native == entry->binding.native && out.window == entry->binding.window;
}
bool NativeInputRoute::MarkDrainOnly(std::uint64_t expected) noexcept {
    if (!Enter()) return false;
    Guard guard(*this);
    if (!entry || expected != entry->route || phase != Phase::Revoked) return false;
    NativeInputRetirement facts;
    if (!ReadRetirement(facts) || !Retired(facts)) return false;
    phase = Phase::DrainOnly; return true;
}
bool NativeInputRoute::PrepareCancellation(const NativeInputHead& requested, CancellationPermit& out) noexcept {
    if (!Enter()) return false;
    Guard guard(*this);
    permitSerial = 0;
    if (!entry || phase != Phase::DrainOnly || !HeadShape(requested)) return false;
    const auto head = requested; // Source cannot change the caller's head mid-call.
    const auto& b = entry->binding;
    if (head.tag.route != entry->route || head.tag.dispatchEpoch != b.dispatchEpoch ||
        head.tag.streamToken != b.streamToken || head.tag.providerEpoch != b.window.module ||
        head.tag.window != b.window.window || head.tag.windowLifetime != b.window.lifetime) return false;
    NativeInputRetirement facts;
    NativeInputIssued issued;
    const auto before = mutation;
    if (!ReadRetirement(facts) || !Retired(facts) ||
        !source.InspectIssued(entry->route, b, head.tag, issued) || exhausted || mutation != before ||
        !issued.issued || issued.terminal || issued.inFlight || issued.tag != head.tag || issued.value != head.value || !KindMatches(issued,head.lane)) return false;
    // Recheck copied retirement facts after the separate issued-ticket inspector.
    if (!ReadRetirement(facts) || !Retired(facts) || mutation != before) return false;
    if (permitHighwater == (std::numeric_limits<std::uint64_t>::max)()) { Poison(); return false; }
    CancellationPermit candidate;
    candidate.route = entry->route; candidate.serial = ++permitHighwater; candidate.head = head;
    permitSerial = candidate.serial;
    out = candidate; return true;
}
bool NativeInputRoute::AllowsCancellation(const CancellationPermit& permit, const NativeInputHead& actual) noexcept {
    if (!OnThread()) return false;
    if (calling) { Poison(); return false; }
    return entry && !exhausted && phase == Phase::DrainOnly && permit.serial && permit.serial == permitSerial &&
        permit.route == entry->route && permit.head == actual;
}
bool NativeInputRoute::Release(std::uint64_t expected) noexcept {
    if (!Enter()) return false;
    Guard guard(*this);
    if (!entry || expected != entry->route || phase == Phase::Bound) return false;
    permitSerial = 0;
    NativeInputRetirement facts;
    // Even a poisoned boundary may release its own fully retired/disposed slot.
    // Guard against reentry/revocation during these external proof reads.
    const auto before = mutation;
    const bool poisonBefore = poisoned;
    if (!ReadRetirement(facts) || mutation != before || poisoned != poisonBefore || !Retired(facts) ||
        !facts.hooksRemoved || !facts.controllerReleased || !facts.backlogDisposed) return false;
    entry.reset(); phase = Phase::Empty; poisoned = false; permitSerial = 0; return true;
}
NativeInputRoute::Phase NativeInputRoute::State() const noexcept {
    if (!OnThread()) return Phase::Unavailable;
    return phase;
}
} // namespace openq4
