// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <cstdint>

class idUserInterface;
namespace openq4 {
class NativeInputRoute;
struct NativeInputBinding;
// Engine-private POD only: this header may be included by the C++17 game PCH.
struct NativeSessionPublication {
    std::uintptr_t current = 0;
    std::uint64_t transition = 0;
    bool inputAllowed = false;
};
struct NativeWindowPublication {
    std::uintptr_t handle = 0;
    std::uint64_t window = 0, lifetime = 0, module = 0, association = 0, registration = 0;
    bool windowAllowed = false, providerOwned = false;
};
// One original-route observer. No native attachment, provider enablement or
// delivery is performed. The bound route must outlive its checked unbind.
bool NativeInputBindPublications(NativeInputRoute&, std::uint64_t route,
    const NativeInputBinding&) noexcept;
bool NativeInputUnbindPublications(NativeInputRoute&, std::uint64_t route) noexcept;
// Original observer identity, usable for exact cleanup after a publication
// fault. It is NOT live eligibility. Faults keep Transition zero and refuse
// fresh binding even after the original observer is successfully released.
bool NativeInputPublicationsCurrent(NativeInputRoute&, std::uint64_t route) noexcept;
bool NativeInputPublicationSlotEmpty() noexcept;
std::uint64_t NativeInputSessionTransition() noexcept;

// Call BEFORE changing fields or invoking callbacks that invalidate their
// identity. These functions only revoke a local route and advance scalar facts.
// They never enter GUI/native code, Source observation or storage. Callers hold
// no input-store lock. A relevant invalidation remains sticky through an ABA.
void NativeInputBeforeSessionChange() noexcept;
void NativeInputBeforeUiChange(std::uint64_t allocation, std::uint64_t backend = 0) noexcept;
void NativeInputBeforeWindowChange() noexcept;
void NativeInputBeforeInputBlockerChange() noexcept;

// Implemented beside the actual owning fields. Output-preserving, constructing
// thread only, no virtual GUI/backend call, preparation or OS/SDL getter.
bool Session_QueryNativeInputPublication(NativeSessionPublication&) noexcept;
bool UI_QueryNativeInputAllocation(std::uintptr_t current, std::uint64_t& out) noexcept;
bool Console_BlocksNativeInput() noexcept;
bool Sys_QueryNativeInputWindow(NativeWindowPublication&) noexcept;
// Reserve a single exclusive engine claim from the current cached real window.
// This is ownership of a future registration, not native provider activation.
bool Sys_AcquireNativeInputWindowClaim(NativeWindowPublication&) noexcept;
bool Sys_ReleaseNativeInputWindowClaim(const NativeWindowPublication&) noexcept;
} // namespace openq4
