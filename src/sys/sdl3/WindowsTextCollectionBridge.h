// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#if defined(_WIN32)
#include "WindowsTextStore.h"
#include "../../ui/application/NativeTextCollectionCoordinator.h"

namespace openq4::sys {
// One engine-owned native/editor lease within one verified SDL module lifetime.
// Creation does not register hooks or enable input. Register Hooks() while SDL
// is disabled; after enabling, bind its externally checked generation BEFORE
// pumping. No callback is accepted until that one-shot bind succeeds.
// Create requires a fresh reconciler/store: sequence 0/shadow 1, no current or
// prior collection, group, retained or live composition identities. Lifecycle
// scopes do not authorize untracked application revision changes; an explicit
// checked application transition API would be needed before permitting those.
//
// SDL copies userdata: this object must remain alive until successful disabled
// unregister and all callbacks have unwound. Final destruction/Store Release
// belongs to the creating apartment. Foreign COM final Release behavior remains
// the caller's lifetime responsibility, not an allocation-free SDK guarantee.
// No method invokes a GUI, renderer, HWND, TSF activation, pump, poll or ACK fence.
class WindowsTextCollectionBridge final : public ui::NativeTextCollectionStore {
public:
    static bool Create(WindowsTextStore&, const ui::NativeTextEditorBarrier&,
        std::uint64_t checkedModuleEpoch, std::unique_ptr<WindowsTextCollectionBridge>& out,
        std::string& error);
    ~WindowsTextCollectionBridge();
    WindowsTextCollectionBridge(const WindowsTextCollectionBridge&) = delete;
    WindowsTextCollectionBridge& operator=(const WindowsTextCollectionBridge&) = delete;
    OQ4_NativeCollectionHooks Hooks() noexcept;
    bool BindProviderGeneration(std::uint64_t checkedModuleEpoch, std::uint64_t checkedGeneration, std::string& error);
    bool CopyClosed(WindowsTextCollectionReceipt& out) const noexcept;
    bool Healthy() const noexcept;
    bool Closed(const ui::NativeTextEditorBarrier&, std::uint64_t dispatch,
        ui::NativeClosedTextCollection&, std::string&) override;
    bool StillClosed(const ui::NativeClosedTextCollection&) const noexcept override;
    bool Pending(const ui::NativeTextEditorBarrier&, std::uint64_t dispatch,
        ui::NativeTextPendingSnapshot&, std::string&) override;
    bool Peek(const ui::NativeTextIdentity&, ui::NativeTextOffer&, std::string&) override;
    bool Acknowledge(const ui::NativeTextEditorReceipt&, std::string&) override;
    bool Sync(const ui::NativeTextEditorReceipt&, const ui::NativeTextSnapshot&, std::string&) override;
    void RetireExact(const ui::NativeTextIdentity&) noexcept override;
private:
    struct Impl;
    explicit WindowsTextCollectionBridge(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
    static bool SDLCALL Prepare(void*, const OQ4_NativeCollectionContext*) noexcept;
    static bool SDLCALL Finish(void*, const OQ4_NativeCollectionContext*, bool) noexcept;
};
} // namespace openq4::sys
#endif
