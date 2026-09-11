// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#if defined(_WIN32)
#include "WindowsTextSession.h"
#include "NativeInputEmissionInventory.h"
#include "NativeInputDriver.h"
#include "../../framework/NativeInputPublications.h"

namespace openq4::sys {
// Uses copied engine window/provider publications, never HWND/SDL queries.
class WindowsNativeInputWindowProbe final : public WindowsTextSessionWindowProbe {
public:
    bool Current(const WindowsTextSessionWindow&) const noexcept override;
    bool ProviderCurrent(const WindowsTextSessionWindow&) const noexcept override;
};

// Production join for one original route and one retained issuance inventory.
// The engine driver owns this source, the exact controller, route and inventory
// until their checked release. No collaborator may be destroyed/reused meanwhile.
// This class does not enable the provider, attach an editor, pump, remove input,
// finish a fence or infer native text origin from collection membership.
//
// Bootstrap: construct/attach the pure native store using the copied engine
// window probe and manager owner, SetOwner before route.Prepare, then BindRoute.
// BindInventory occurs once before translating that ledger. Subsequent batch
// replacement needs a checked driver disposition seam, not a pointer overwrite.
// BacklogDisposed requires the inventory's sealed exact terminal proof. No
// missing-head/failed-admission inference or successful native ACK is made.
class WindowsNativeInputRouteSource final : public NativeInputRouteSource, public NativeInputDriverLifecycle {
public:
    explicit WindowsNativeInputRouteSource(WindowsTextSession&) noexcept;
    WindowsNativeInputRouteSource(const WindowsNativeInputRouteSource&) = delete;
    WindowsNativeInputRouteSource& operator=(const WindowsNativeInputRouteSource&) = delete;
    bool SetOwner(const NativeInputBinding&,std::string& error) noexcept;
    bool BindRoute(NativeInputRoute&,std::uint64_t) noexcept;
    bool BindInventory(NativeInputEmissionInventory&) noexcept;
    // Actual successful route Release and publication unbind precede dropping
    // collaborators. A failed step retains them for exact original cleanup.
    bool ReleaseRoute(std::uint64_t exactRoute) noexcept;
    void RetirePreservingEvents() noexcept override;
    bool DetachCompleted(NativeInputDriver&) noexcept override;
    bool ReleaseRetired(std::uint64_t exactRoute) noexcept override {return ReleaseRoute(exactRoute);}
    bool PumpsRetired() const noexcept override;
    bool Observe(NativeInputObservation&) const noexcept override;
    bool Retirement(std::uint64_t,const NativeInputBinding&,NativeInputRetirement&) const noexcept override;
    bool InspectIssued(std::uint64_t,const NativeInputBinding&,const sysEventDispositionTag_t&,NativeInputIssued&) const noexcept override;
private:
    bool Original(std::uint64_t,const NativeInputBinding&) const noexcept;
    const std::thread::id thread;
    WindowsTextSession& controller;
    std::unique_ptr<const NativeInputBinding> binding;
    NativeInputRoute* route = nullptr;
    NativeInputEmissionInventory* inventory = nullptr;
    std::uint64_t routeId = 0, controllerId = 0;
    bool preparing = false, poisoned = false, releasing = false, finished = false, completedInventory = false;
};
} // namespace openq4::sys
#endif
