// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#if defined(_WIN32)
#include "WindowsTextCollectionBridge.h"

namespace openq4::sys {
struct WindowsTextSessionWindow {
    HWND hwnd = nullptr;
    std::uint64_t window = 0, lifetime = 0, moduleEpoch = 0, association = 0, registration = 0;
    bool operator==(const WindowsTextSessionWindow&) const = default;
};
// Engine-owned, callback-free, allocation-free probe of the immutable native
// window AND exclusive association claim. HWND/SDL IDs alone are not leases.
// No GUI lookup, native call, user code or input dispatch is allowed here.
class WindowsTextSessionWindowProbe {
public:
    virtual ~WindowsTextSessionWindowProbe() = default;
    virtual bool Current(const WindowsTextSessionWindow&) const noexcept = 0;
    // Independent immutable module/exclusive hook-registration claim. This must
    // remain true through disabled unregister; HWND/focus loss alone does not
    // invalidate it. A new module or registration must never revive this claim.
    virtual bool ProviderCurrent(const WindowsTextSessionWindow&) const noexcept = 0;
};
class WindowsTextSessionPlatform {
public:
    virtual ~WindowsTextSessionPlatform() = default;
    virtual HRESULT InitializeSta() noexcept = 0;
    virtual HRESULT CreateThreadManager(ITfThreadMgr**) noexcept = 0;
    virtual void UninitializeSta() noexcept = 0;
};
// Production SDK factory. Tests inject counted SDK objects; they never call it.
class WindowsTextSessionSystemPlatform final : public WindowsTextSessionPlatform {
public:
    HRESULT InitializeSta() noexcept override;
    HRESULT CreateThreadManager(ITfThreadMgr**) noexcept override;
    void UninitializeSta() noexcept override;
};
struct WindowsTextEventDisposition {
    NativeQueueReceipt batch;
    std::uint64_t ledger = 0, serial = 0;
    bool operator==(const WindowsTextEventDisposition&) const = default;
};
// Engine-owned copied receipt, minted only after EVERY record in this exact
// batch (Outside/Sentinel included) has received its ordinary disposition.
// Current is a no-callback/no-allocation ledger check, not an unbound bool.
class WindowsTextSessionDisposition {
public:
    virtual ~WindowsTextSessionDisposition() = default;
    virtual bool Current(const WindowsTextEventDisposition&) const noexcept = 0;
};
struct WindowsTextSessionCleanup {
    std::uint64_t session = 0;
    ui::NativeClosedTextCollection closed;
    bool operator==(const WindowsTextSessionCleanup&) const = default;
};
struct WindowsTextSessionRelease {
    bool providerRetired = false, hooksRemoved = false, nativeReleased = false;
    bool associationRestored = false, graceful = false;
};
struct WindowsTextSessionRetirement {
    std::uint64_t session = 0, generation = 0;
    ui::NativeTextIdentity native;
    WindowsTextSessionWindow window;
    bool storeRetired = false, providerRetired = false, hooksRemoved = false, nativeReleased = false;
};

// One non-renewable native/editor/window/module lease on the creating STA.
// Create is pure store/bridge construction. Register occurs while SDL is
// disabled; the engine enables the checked provider externally and then Bind
// observes its real generation/continuity. This class NEVER enables, pumps,
// polls, flushes, dispatches ordinary input, or invents character provenance.
// All native construction and graceful cleanup run inside checked Lifecycle
// scopes. The native callbacks operate on the original immutable document only.
//
// Activate returns a CLOSED seal. The caller uses ReconcileLifecycle and then
// FinishActivation with the exact native completion and ordinary-event receipt.
// Quiesce first retires the old engine adoption lease, then terminates native
// composition directly from an idle scope (TSF obtains its synchronous lock),
// restores the guarded association, unadvises, Pops and Deactivates. Cleanup
// edits are NEVER settled: FinishQuiesce proves the whole ordered batch, retires
// the store, ACKs/finishes the exact fence, THEN disables/unregisters/releases.
//
// A failure requires FaultRetirePreservingEvents. It performs no queue removal
// or ACK, restores the stable draft of only the original editor and reports
// incomplete native/association cleanup. Retired-store callbacks cannot write.
// The emergency path allocates no engine data, but foreign COM final Release
// (and other SDK implementation behavior) cannot be guaranteed allocation-free.
// Objects/hooks remain alive until successful disabled unregister. Destroy is
// checked; it refuses busy/wrong-thread or incompletely released sessions.
// Even a never-registered session must first retire its attached engine lease.
// Lost provider/module claims refuse even Retire/unregister of a replacement;
// the host must retain this object and its old module until ownership is resolved.
// All collaborators must outlive the session and their calls must return.
// Inputs/outputs/errors must not alias. Outputs are unchanged on failure.
// Live COM activation, SDL IMM exclusion, ordinary-character association,
// renewal, candidate layout and real TIP qualification remain separate work.
class WindowsTextSession final {
public:
    enum class Phase { Created, Registered, Bound, ActivationHeld, Active, CleanupHeld, Fault, Released };
    static bool Create(const ui::NativeTextEditorView&, const WindowsTextSessionWindow&,
        WindowsTextSessionPlatform&, WindowsTextSessionWindowProbe&, ui::NativeTextCollectionOwner&,
        WindowsTextSession*& out, std::string& error);
    static bool Destroy(WindowsTextSession*& session) noexcept;
    WindowsTextSession(const WindowsTextSession&) = delete;
    WindowsTextSession& operator=(const WindowsTextSession&) = delete;
    bool Register(std::string& error);
    bool Bind(NativeQueueSource&, std::string& error);
    bool Activate(NativeQueueSource&, ui::NativeClosedTextCollection& out, std::string& error);
    bool FinishActivation(ui::NativeTextCollectionCoordinator&, const ui::NativeTextCollectionCompletion&,
        NativeQueueIngress&, NativeQueueSource&, const NativeQueueBatch&,
        const WindowsTextEventDisposition&, WindowsTextSessionDisposition&, ui::NativeTextCollectionFence&,
        std::string& error);
    bool Quiesce(ui::NativeTextCollectionCoordinator&, NativeQueueSource&, WindowsTextSessionCleanup& out, std::string& error);
    bool FinishQuiesce(const WindowsTextSessionCleanup&, NativeQueueIngress&, NativeQueueSource&,
        const NativeQueueBatch&, const WindowsTextEventDisposition&, WindowsTextSessionDisposition&,
        ui::NativeTextCollectionFence&, WindowsTextSessionRelease& out, std::string& error);
    WindowsTextSessionRelease FaultRetirePreservingEvents() noexcept;
    Phase State() const noexcept;
    // Copied original-controller facts only, no owner/probe/SDL/COM callback.
    // A store query is an engine-owned callback-free method, never a GUI query.
    // Busy/wrong-thread/mismatched lease preserves out. UI retirement is NOT
    // inferred from ownerRetired, and provider cleanup requires its actual result.
    bool QueryRetirement(ui::NativeTextIdentity,const ui::TextEditorIdentity&,
        const WindowsTextSessionWindow&,WindowsTextSessionRetirement& out) const noexcept;
    // Borrow only during a live engine call; never retain through Destroy.
    ui::NativeTextCollectionStore& Collections() noexcept;
private:
    struct Impl;
    explicit WindowsTextSession(std::unique_ptr<Impl>);
    ~WindowsTextSession();
    std::unique_ptr<Impl> impl;
    static bool SDLCALL Work(void*, const OQ4_NativeCollectionContext*) noexcept;
};
} // namespace openq4::sys
#endif
