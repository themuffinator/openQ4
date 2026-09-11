// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "NativeInputTransfers.h"
#include "../../framework/NativeInputDispatch.h"
#include "../../ui/application/NativeTextCollectionCoordinator.h"

namespace openq4 {
class NativeInputDriver;
// Production Windows implementation uses the exact retained controller/source.
// No callback may destroy/rebind these collaborators during a driver call.
class NativeInputDriverLifecycle {
public:
    virtual ~NativeInputDriverLifecycle() = default;
    virtual void RetirePreservingEvents() noexcept = 0;
    virtual bool DetachCompleted(NativeInputDriver&) noexcept = 0;
    virtual bool ReleaseRetired(std::uint64_t route) noexcept = 0;
    virtual bool PumpsRetired() const noexcept = 0;
};

// One already-reconciled and fully translated batch, consumed by real engine
// sinks. No translation, provider activation, pumping or simulation occurs.
// The caller owns every reference through checked Uninstall. Faults retain the
// slot and original payload obligations; they cannot fall back to legacy input.
class NativeInputDriver final {
public:
    static constexpr unsigned WorkPerFrame = 512;
    NativeInputDriver(NativeInputRoute&,std::uint64_t,const NativeInputBinding&,
        NativeInputEmissionInventory&,NativeInputTransfers&,NativeEventDispositionLedger&,
        NativeQueueIngress&,NativeQueueSource&,const NativeQueueBatch&,
        ui::NativeTextCollectionCoordinator&,ui::NativeTextCollectionOwner&,
        ui::NativeTextCollectionStore&,ui::NativeTextCollectionFence&,NativeInputDriverLifecycle&,idEventLoop&);
    ~NativeInputDriver();
    NativeInputDriver(const NativeInputDriver&)=delete;
    NativeInputDriver& operator=(const NativeInputDriver&)=delete;
    bool Install(const ui::NativeTextCollectionCompletion&,std::string&) noexcept;
    bool Uninstall() noexcept;
    bool CanPrepareCollection() noexcept;
    // Called only by exact Source.DetachCompleted; proves the successful fence
    // and privately terminal taken/delivered census before dropping old refs.
    bool DetachCompletedInventory(NativeInputEmissionInventory&) noexcept;
    nativeInputSessionResult_t TakeSession(sysEvent_t&,bool deferredOnly=false) noexcept;
    bool CompleteSession() noexcept;
    bool BeginPoll(bool keyboard) noexcept;
    bool NextMouse(int&,int&) noexcept;
    bool NextKeyboard(int&,bool&) noexcept;
    bool CompletePoll(bool keyboard) noexcept;
    void EndPoll(bool keyboard) noexcept;
    bool SessionCurrent() noexcept;
    void BeginLegacySession() noexcept;
    void EndLegacySession() noexcept;
    void ContinueDeferred(idEventLoop&) noexcept;
    void BeginFrame(std::uint64_t presentation) noexcept;
    void Abort() noexcept;
    bool FatalRetire() noexcept;
    void Checkpoint() noexcept;
    bool HeldSourceCurrent(std::uint64_t exactRoute,std::uint64_t exactWindow) const noexcept;
    bool Faulted() const noexcept {return fault;}
    bool Finished() const noexcept {return detached;}
    bool TypedPollDelivery() const noexcept {return std::this_thread::get_id()==thread && !fault && inFlight && !legacy && sink!=NativeInputSink::Session;}
private:
    struct Guard;
    class CheckedFence;
    bool Enter() noexcept;
    bool Current() noexcept;
    bool Fail() noexcept;
    bool Pass(NativeDispositionPass) noexcept;
    bool FinishFence() noexcept;
    bool FinishDelivery(NativeInputSink) noexcept;
    bool DisposedRetired() noexcept;
    bool ObserveHeld(const NativeTranslatedEmission&,NativeInputSink) noexcept;
    bool NextPoll(bool,int&,int&) noexcept;
    void Cleanup() noexcept;
    NativeInputRoute& route;
    const std::uint64_t routeId;
    const NativeInputBinding original;
    NativeInputEmissionInventory& inventory;
    NativeInputTransfers& transfers;
    NativeEventDispositionLedger& ledger;
    NativeQueueIngress& ingress;
    NativeQueueSource& source;
    const NativeQueueBatch& batch;
    ui::NativeTextCollectionCoordinator& coordinator;
    ui::NativeTextCollectionOwner& owner;
    ui::NativeTextCollectionStore& store;
    ui::NativeTextCollectionFence& fence;
    NativeInputDriverLifecycle& lifecycle;
    idEventLoop& loop;
    const std::thread::id thread=std::this_thread::get_id();
    ui::NativeTextEditorBarrier barrier;
    ui::NativeTextCollectionCompletion completion{};
    NativeDispositionReceipt ordinary{};
    sysInputDispositionSlice_t mouseSlice{},keyboardSlice{};
    NativeTranslatedEmission delivery{};
    std::string error;
    NativeInputSink sink=NativeInputSink::Session;
    unsigned work=0;
    std::uint64_t budgetFrame=0,finishedAtFrame=0;
    bool installed=false,calling=false,fault=false,inFlight=false,legacy=false;
    bool mouseOpen=false,keyboardOpen=false,fenceFinished=false,detached=false,retired=false,released=false;
    bool interrupted=false;
    bool sessionScope=false;
    bool sessionNativeSeen=false,mouseNativeSeen=false,keyboardNativeSeen=false;
};
} // namespace openq4
