// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "NativeInputPublications.h"
#include "NativeInputRoute.h"
#include "../sys/EventDisposition.h"
#include <mutex>

namespace openq4 {
namespace {
struct Publications {
    std::mutex mutex;
    NativeInputRoute* owner = nullptr;
    std::uint64_t route = 0, allocation = 0, backend = 0, transition = 1;
    bool exhausted = false, wrongThread = false, pendingRevocation = false;
};
Publications& Facts() noexcept { static Publications value; return value; }
void Revoke(Publications& p) noexcept {
    if (!p.owner) return;
    if (!Sys_EventDispositionBoundThread()) { p.wrongThread = p.pendingRevocation = true; return; }
    (void)p.owner->Revoke(p.route); // Zero Source/foreign calls, including reentry.
}
void ObservePending(Publications& p) noexcept {
    if (p.pendingRevocation && Sys_EventDispositionBoundThread()) {
        p.pendingRevocation=false;
        if (p.owner) (void)p.owner->Revoke(p.route);
    }
}
}
bool NativeInputBindPublications(NativeInputRoute& owner, std::uint64_t route, const NativeInputBinding& b) noexcept {
    if (!Sys_EventDispositionBoundThread()) return false;
    const auto expectedRoute=route,expectedAllocation=b.editor.allocation,expectedBackend=b.editor.backend;
    const auto expectedTransition=b.sessionTransition,expectedWindow=b.window.window;
    const auto expectedOuter=b.outer;
    NativeInputSelection selected;
    // Probe may observe Source; it must happen before the publication mutex.
    if (!owner.MatchesOriginalBinding(expectedRoute,b) || !owner.Probe(selected) ||
        !owner.MatchesOriginalBinding(expectedRoute,b) || selected.route!=expectedRoute || selected.current!=expectedOuter ||
        selected.allocation!=expectedAllocation || selected.window!=expectedWindow) return false;
    auto& p=Facts();std::lock_guard<std::mutex> lock(p.mutex);
    if (!Sys_EventDispositionBoundThread() || p.owner || !route || p.exhausted || p.wrongThread ||
        !expectedAllocation || !expectedBackend || expectedTransition!=p.transition ||
        owner.State()!=NativeInputRoute::Phase::Bound) return false;
    p.owner=&owner;p.route=expectedRoute;p.allocation=expectedAllocation;p.backend=expectedBackend;return true;
}
bool NativeInputUnbindPublications(NativeInputRoute& owner, std::uint64_t route) noexcept {
    auto& p=Facts();std::lock_guard<std::mutex> lock(p.mutex);
    if (!Sys_EventDispositionBoundThread() || p.owner!=&owner || p.route!=route ||
        owner.State()!=NativeInputRoute::Phase::Empty) return false;
    p.owner=nullptr;p.route=p.allocation=p.backend=0;return true;
}
bool NativeInputPublicationsCurrent(NativeInputRoute& owner,std::uint64_t route) noexcept {
    auto& p=Facts();std::lock_guard<std::mutex> lock(p.mutex);
    ObservePending(p);
    return Sys_EventDispositionBoundThread() && p.owner==&owner && p.route==route;
}
bool NativeInputPublicationSlotEmpty() noexcept {
    auto& p=Facts();std::lock_guard<std::mutex> lock(p.mutex);
    return Sys_EventDispositionBoundThread() && !p.exhausted && !p.wrongThread && !p.owner;
}
std::uint64_t NativeInputSessionTransition() noexcept {
    auto& p=Facts();std::lock_guard<std::mutex> lock(p.mutex);
    ObservePending(p);
    return !p.exhausted && !p.wrongThread ? p.transition : 0;
}
void NativeInputBeforeSessionChange() noexcept {
    auto& p=Facts();std::lock_guard<std::mutex> lock(p.mutex);
    Revoke(p);
    if (p.transition==UINT64_MAX) p.exhausted=true;else ++p.transition;
}
void NativeInputBeforeUiChange(std::uint64_t allocation,std::uint64_t backend) noexcept {
    auto& p=Facts();std::lock_guard<std::mutex> lock(p.mutex);
    if (p.owner && ((allocation && allocation==p.allocation) || (backend && backend==p.backend))) Revoke(p);
}
void NativeInputBeforeWindowChange() noexcept {
    auto& p=Facts();std::lock_guard<std::mutex> lock(p.mutex);Revoke(p);
}
void NativeInputBeforeInputBlockerChange() noexcept { NativeInputBeforeSessionChange(); }
} // namespace openq4
