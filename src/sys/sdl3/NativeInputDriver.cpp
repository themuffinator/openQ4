// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../../idlib/precompiled.h"
#include "NativeInputDriver.h"
#include "../../framework/EventLoop.h"
#include <atomic>

namespace {
openq4::NativeInputDriver* nativeDriver=nullptr;
std::atomic<bool> nativeDriverOrphaned{false},nativeDriverOwned{false};
std::uint64_t nativeInputBudgetFrame=0; // Bound engine thread only, never reset.
openq4::NativeInputDriver* NativeDriverOnThread() noexcept {
    return Sys_EventDispositionBoundThread()?nativeDriver:nullptr;
}
}
namespace openq4 {
struct NativeInputDriver::Guard {NativeInputDriver& d;~Guard(){d.calling=false;}};
NativeInputDriver::NativeInputDriver(NativeInputRoute& r,std::uint64_t id,const NativeInputBinding& b,
    NativeInputEmissionInventory& i,NativeInputTransfers& t,NativeEventDispositionLedger& l,
    NativeQueueIngress& in,NativeQueueSource& s,const NativeQueueBatch& q,
    ui::NativeTextCollectionCoordinator& c,ui::NativeTextCollectionOwner& o,ui::NativeTextCollectionStore& st,
    ui::NativeTextCollectionFence& f,NativeInputDriverLifecycle& life,idEventLoop& e):
    route(r),routeId(id),original(b),inventory(i),transfers(t),ledger(l),ingress(in),source(s),batch(q),
    coordinator(c),owner(o),store(st),fence(f),lifecycle(life),loop(e){}
NativeInputDriver::~NativeInputDriver(){
    // Misuse cannot expose a dangling callback or silently reopen legacy pumps.
    if(nativeDriver==this){nativeDriverOrphaned.store(true);nativeDriver=nullptr;}
}
bool NativeInputDriver::Fail() noexcept {fault=true;interrupted=true;(void)ledger.Retire();(void)route.Revoke(routeId);return false;}
bool NativeInputDriver::Enter() noexcept {
    if(std::this_thread::get_id()!=thread)return false;
    if(calling)return Fail();interrupted=false;calling=true;return true;
}
bool NativeInputDriver::Current() noexcept {
    if(fault || released || detached)return false;
    if(!route.MatchesOriginalBinding(routeId,original))return Fail();
    NativeInputSelection selected;
    if(!route.Probe(selected) || selected.route!=routeId || selected.current!=original.outer ||
        selected.allocation!=original.editor.allocation || selected.window!=original.window.window ||
        !owner.Current(barrier) || fault)return Fail();
    if(!ingress.Validate(source,batch.Receipt(),error) || fault || !route.Probe(selected) ||
        selected.route!=routeId || !owner.Current(barrier) || fault)return Fail();
    return true;
}
bool NativeInputDriver::Install(const ui::NativeTextCollectionCompletion& exact,std::string& outError) noexcept {
    if(!Enter())return false;Guard guard{*this};
    try {
        NativeDispositionProgress progress;
        NativeInputDriver* previous=nativeDriver;
        if(installed || (previous && (!previous->detached || previous->calling || previous->fault ||
            &previous->route!=&route || previous->routeId!=routeId || previous->finishedAtFrame>=nativeInputBudgetFrame)) || nativeDriverOrphaned.load() || loop.JournalLevel()!=0 ||
            com_asyncInput.GetBool() || !inventory.MatchesBinding(route,routeId,original) ||
            inventory.transfers!=&transfers || !ledger.QueryProgress(progress) || progress.inFlight ||
            progress.receipt.batch!=batch.Receipt() || exact.batch!=batch.Receipt() ||
            !coordinator.CompletionCurrent(exact) || !coordinator.QueryBarrier(barrier,outError))return false;
        completion=exact;
        if(!Current() || !coordinator.CompletionCurrent(exact))return false;
        if(nativeDriver!=previous || (previous && !previous->detached))return false;
        budgetFrame=nativeInputBudgetFrame;
        nativeDriver=this;nativeDriverOwned.store(true);installed=true;if(previous)previous->installed=false;else Usercmd_NativeInputChanged();
        if(fault || !Current())return false;outError.clear();return true;
    } catch(...) {Fail();return false;}
}
bool NativeInputDriver::CanPrepareCollection() noexcept {
    if(!Enter())return false;Guard guard{*this};
    if(nativeDriver!=this || !installed || !detached || fault || released ||
        finishedAtFrame>=nativeInputBudgetFrame || inFlight || mouseOpen || keyboardOpen)return false;
    NativeInputSelection current;
    return route.MatchesOriginalBinding(routeId,original) && route.Probe(current) &&
        current.route==routeId && owner.Current(barrier) && !fault;
}
bool NativeInputDriver::Uninstall() noexcept {
    if(!Enter())return false;Guard guard{*this};
    if(!installed || nativeDriver!=this || inFlight || mouseOpen || keyboardOpen || !released)return false;
    nativeDriver=nullptr;nativeDriverOwned.store(false);installed=false;Usercmd_NativeInputChanged();return true;
}
bool NativeInputDriver::Pass(NativeDispositionPass pass) noexcept {
    NativeDispositionProgress p;
    return ledger.QueryProgress(p) && !p.complete && !p.inFlight && p.pass==pass;
}
nativeInputSessionResult_t NativeInputDriver::TakeSession(sysEvent_t& out,bool deferredOnly) noexcept {
    if(!Enter())return nativeInputSessionResult_t::Stop;Guard guard{*this};
    if(inFlight){Fail();return nativeInputSessionResult_t::Stop;}
    if(!installed || fault || detached || work>=WorkPerFrame)return nativeInputSessionResult_t::Stop;
    if(!Current() || !coordinator.CompletionCurrent(completion))return nativeInputSessionResult_t::Stop;
    NativeDispositionProgress p;if(!ledger.QueryProgress(p)){Fail();return nativeInputSessionResult_t::Stop;}
    const bool sessionPass=!p.complete && (p.pass==NativeDispositionPass::SessionInitial || p.pass==NativeDispositionPass::SessionDeferred);
    if(deferredOnly && (!sessionPass || p.pass!=NativeDispositionPass::SessionDeferred))return nativeInputSessionResult_t::Stop;
    sysEventDispositionTag_t head;
    const auto peek=loop.PeekEventDispositionTag(head);
    if(peek==sysEventTransfer_t::Refused){Fail();return nativeInputSessionResult_t::Stop;}
    if(peek==sysEventTransfer_t::Ready && head.Empty()) {
        // The existing EventLoop owns legacy prefixes. Native continuation must
        // not skip or consume them, nor run command buffers on their behalf.
        return deferredOnly || sessionNativeSeen?nativeInputSessionResult_t::Stop:nativeInputSessionResult_t::Legacy;
    }
    if(!sessionPass)return nativeInputSessionResult_t::Stop;
    if(!p.next.emission) {
        if(!ledger.CompletePass(p.pass,error))Fail();
        return nativeInputSessionResult_t::Stop;
    }
    if(peek!=sysEventTransfer_t::Ready)return nativeInputSessionResult_t::Stop;
    if(head.ledger!=p.next.record.receipt.ledger || head.ledgerSerial!=p.next.record.receipt.serial ||
        head.emission!=p.next.emission || head.ingress!=batch.Receipt().ingress || head.batchSerial!=batch.Receipt().serial){Fail();return nativeInputSessionResult_t::Stop;}
    sysEvent_t owned{};NativeTranslatedEmission emitted;
    const auto status=transfers.BeginSessionDelivery(owned,emitted,error);
    if(status==NativeInputDeliveryStatus::Unchanged){Fail();return nativeInputSessionResult_t::Stop;}
    delivery=emitted;sink=NativeInputSink::Session;legacy=false;inFlight=true;sessionNativeSeen=true;++work;
    if(status!=NativeInputDeliveryStatus::OwnedDelivery || !Current()) {
        Fail();try{if(owned.evPtr)Mem_Free(owned.evPtr);DisposedRetired();}catch(...){/* unresolved ownership */}
        return nativeInputSessionResult_t::Stop;
    }
    sessionScope=true;out=owned;return nativeInputSessionResult_t::Owned;
}
bool NativeInputDriver::SessionCurrent() noexcept {
    if(std::this_thread::get_id()!=thread)return false;
    if(!sessionScope)return true; // Programmatic UI work has its own ownership.
    if(!Enter())return false;Guard guard{*this};return Current();
}
void NativeInputDriver::BeginLegacySession() noexcept {
    if(!Enter())return;Guard guard{*this};
    if(sessionScope || inFlight){Fail();return;}
    sessionScope=true;(void)Current();
}
void NativeInputDriver::EndLegacySession() noexcept {
    if(!Enter())return;Guard guard{*this};
    if(!sessionScope || inFlight){Fail();return;}
    sessionScope=false;if(!fault)(void)Current();
}
bool NativeInputDriver::DisposedRetired() noexcept {
    // Only the actual driver sink reaches here AFTER payload destruction or
    // scalar disposal. No caller-supplied success/receipt can enter this path.
    if(!inFlight || legacy)return false;
    auto* entry=const_cast<NativeInputEmissionInventory::Entry*>(inventory.Find(delivery.tag));
    if(!entry || entry->emission.ticket!=delivery.ticket || entry->emission.value!=delivery.value ||
        entry->ownership!=NativeInputEmissionInventory::Ownership::TakenForDelivery)return false;
    (void)ledger.Retire();
    if(!ledger.DisposeTakenAfterRetirement(delivery.ticket))return false;
    entry->ownership=NativeInputEmissionInventory::Ownership::Cancelled;
    inFlight=false;delivery={};return true;
}
bool NativeInputDriver::FinishDelivery(NativeInputSink expected) noexcept {
    if(!inFlight || sink!=expected)return Fail();
    if(legacy){inFlight=false;legacy=false;return Current();}
    if(fault || !Current() || !ledger.CompleteDelivery(delivery.ticket,
        expected==NativeInputSink::Session?NativeDeliveryDisposition::Delivered:NativeDeliveryDisposition::AuditedDiscard,error)) {
        Fail();(void)DisposedRetired();return false;
    }
    inFlight=false;delivery={};return true;
}
bool NativeInputDriver::CompleteSession() noexcept {
    if(!Enter())return false;Guard guard{*this};
    const bool complete=FinishDelivery(NativeInputSink::Session);sessionScope=false;return complete;
}
bool NativeInputDriver::BeginPoll(bool keyboard) noexcept {
    if(!Enter())return true;Guard guard{*this};
    if(!installed)return false;
    if(inFlight){Fail();return true;}
    if(fault || detached || work>=WorkPerFrame)return true;
    auto& opened=keyboard?keyboardOpen:mouseOpen;
    if(opened)return true; // Resume this exact budget-limited slice.
    if(!Current())return true;
    const auto pass=keyboard?NativeDispositionPass::KeyboardPoll:NativeDispositionPass::MousePoll;
    if(!Pass(pass))return true; // Repeated/later natural poll does not advance.
    auto& slice=keyboard?keyboardSlice:mouseSlice;
    if(!(keyboard?Sys_PollKeyboardInputWithDisposition(slice):Sys_PollMouseInputWithDisposition(slice))){Fail();return true;}
    opened=true;return true;
}
bool NativeInputDriver::NextPoll(bool keyboard,int& a,int& b) noexcept {
    if(!Enter())return false;Guard guard{*this};
    auto& opened=keyboard?keyboardOpen:mouseOpen;
    if(!opened || inFlight || fault || work>=WorkPerFrame)return false;
    if(!Current())return false;
    sysInputDispositionSlot_t slot;sysKeyboardInputDisposition_t key;sysMouseInputDisposition_t mouse;
    const auto status=keyboard?Sys_PeekKeyboardInputWithDisposition(keyboardSlice,slot,key):Sys_PeekMouseInputWithDisposition(mouseSlice,slot,mouse);
    if(status==sysEventTransfer_t::Empty)return false;
    if(status!=sysEventTransfer_t::Ready)return Fail();
    const auto tag=keyboard?key.disposition.parent:mouse.disposition;
    sink=keyboard?NativeInputSink::Keyboard:NativeInputSink::Mouse;legacy=tag.Empty();
    if(legacy) {
        if(keyboard?keyboardNativeSeen:mouseNativeSeen)return false;
        // Natural sink and original eligibility only; no ticket is invented.
        if((keyboard?Sys_TakeKeyboardInputWithDisposition(slot,key):Sys_TakeMouseInputWithDisposition(slot,mouse))!=sysEventTransfer_t::Ready)return Fail();
    } else {
        (keyboard?keyboardNativeSeen:mouseNativeSeen)=true;
        const auto result=keyboard?transfers.BeginKeyboardDelivery(slot,key,delivery,error):transfers.BeginMouseDelivery(slot,mouse,delivery,error);
        if(result==NativeInputDeliveryStatus::Unchanged)return Fail();
        inFlight=true;
        if(result!=NativeInputDeliveryStatus::OwnedDelivery || !Current()){Fail();(void)DisposedRetired();return false;}
        if(!ObserveHeld(delivery,sink)){Fail();(void)DisposedRetired();return false;}
        if(keyboard && key.disposition.deferredEmission) {
            auto childTag=delivery.tag;childTag.emission=key.disposition.deferredEmission;
            const auto* child=inventory.Find(childTag);NativeTranslatedEmission emitted;
            if(!child || transfers.Deferred({delivery,child->emission},emitted,error)!=NativeInputTransferStatus::StoredAdmitted){Fail();(void)DisposedRetired();return false;}
        }
    }
    inFlight=true;++work;
    if(keyboard){a=key.key;b=key.down?1:0;}else{a=mouse.action;b=mouse.value;}
    return true;
}
bool NativeInputDriver::HeldSourceCurrent(std::uint64_t exactRoute,std::uint64_t exactWindow) const noexcept {
    return std::this_thread::get_id()==thread && nativeDriver==this && installed && calling &&
        !interrupted && !released && routeId==exactRoute && original.window.lifetime==exactWindow &&
        route.MatchesOriginalBinding(routeId,original);
}
bool NativeInputDriver::ObserveHeld(const NativeTranslatedEmission& emitted,NativeInputSink inputSink) noexcept {
    const auto index=emitted.ticket.record.index;
    if(index>=batch.Events().size())return false;
    const auto& event=batch.Events()[index].header;
    if(inputSink==NativeInputSink::Keyboard) {
        if((event.type!=SDL_EVENT_KEY_DOWN && event.type!=SDL_EVENT_KEY_UP) || !event.key.scancode ||
            event.key.windowID!=original.window.window || event.key.down!=(emitted.value.value2!=0))return false;
        Usercmd_NativeInputSource(routeId,original.window.lifetime,event.key.which,
            static_cast<unsigned>(event.key.scancode),emitted.value.value,event.key.down);
    } else if(inputSink==NativeInputSink::Mouse && emitted.value.value>=M_ACTION1 && emitted.value.value<=M_ACTION8) {
        if((event.type!=SDL_EVENT_MOUSE_BUTTON_DOWN && event.type!=SDL_EVENT_MOUSE_BUTTON_UP) || !event.button.button ||
            event.button.windowID!=original.window.window || event.button.down!=(emitted.value.value2!=0))return false;
        Usercmd_NativeInputSource(routeId,original.window.lifetime,event.button.which,
            0x10000u+event.button.button,K_MOUSE1+(emitted.value.value-M_ACTION1),event.button.down);
    }
    return true;
}
bool NativeInputDriver::NextMouse(int& a,int& b) noexcept{return NextPoll(false,a,b);}
bool NativeInputDriver::NextKeyboard(int& key,bool& down) noexcept {int a=0,b=0;if(!NextPoll(true,a,b))return false;key=a;down=b!=0;return true;}
bool NativeInputDriver::CompletePoll(bool keyboard) noexcept {
    if(!Enter())return false;Guard guard{*this};return FinishDelivery(keyboard?NativeInputSink::Keyboard:NativeInputSink::Mouse);
}
void NativeInputDriver::EndPoll(bool keyboard) noexcept {
    if(!Enter())return;Guard guard{*this};
    auto& opened=keyboard?keyboardOpen:mouseOpen;if(!opened || inFlight || fault)return;
    // A budget-limited slice retains its cursor; End is never an implicit clear.
    sysInputDispositionSlot_t slot;sysKeyboardInputDisposition_t key;sysMouseInputDisposition_t mouse;
    const auto status=keyboard?Sys_PeekKeyboardInputWithDisposition(keyboardSlice,slot,key):Sys_PeekMouseInputWithDisposition(mouseSlice,slot,mouse);
    if(status==sysEventTransfer_t::Ready)return;
    if(status!=sysEventTransfer_t::Empty || !(keyboard?Sys_EndKeyboardInputWithDisposition(keyboardSlice):Sys_EndMouseInputWithDisposition(mouseSlice))){Fail();return;}
    opened=false;
    if(!ledger.CompletePass(keyboard?NativeDispositionPass::KeyboardPoll:NativeDispositionPass::MousePoll,error))Fail();
}
class NativeInputDriver::CheckedFence final:public ui::NativeTextCollectionFence {
public:
    explicit CheckedFence(NativeInputDriver& d):driver(d){}
    bool Acknowledge(const NativeQueueStatus& status,std::string& error) override {
        if(!driver.ledger.Current(driver.ordinary) || !driver.Current() || driver.fault)return false;
        return driver.fence.Acknowledge(status,error);
    }
private:NativeInputDriver& driver;
};
bool NativeInputDriver::FinishFence() noexcept {
    if(fault || inFlight || mouseOpen || keyboardOpen || fenceFinished)return false;
    NativeDispositionProgress p;if(!ledger.QueryProgress(p) || !p.complete)return false;
    if(!Current() || !ledger.Seal(ordinary,error) || !coordinator.CompletionCurrent(completion))return Fail();
    CheckedFence checked(*this);
    try {
        if(!coordinator.CompleteFence(ingress,source,completion,owner,store,checked,error) || fault ||
            !ledger.Current(ordinary))return Fail();
        fenceFinished=true;finishedAtFrame=budgetFrame;
        if(!lifecycle.DetachCompleted(*this) || fault || !detached)return Fail();
        return true;
    }catch(...){return Fail();}
}
bool NativeInputDriver::DetachCompletedInventory(NativeInputEmissionInventory& exact) noexcept {
    if(std::this_thread::get_id()!=thread || !calling || fault || !fenceFinished || detached ||
        &exact!=&inventory || inFlight || mouseOpen || keyboardOpen || inventory.transfers!=&transfers ||
        transfers.calling || inventory.calling || !ledger.Current(ordinary))return false;
    // Successful fence proof is private to this exact driver. Retire the old
    // ledger without revoking the still-live original route.
    inventory.retired=true;(void)ledger.Retire();
    if(!inventory.Terminal())return Fail();
    inventory.transfers=nullptr;inventory.transferAuthorized=false;inventory.bound=false;
    inventory.route=nullptr;inventory.ledger=nullptr;inventory.batch=nullptr;
    inventory.original.reset();inventory.entries.clear();
    transfers.claimed=false;transfers.released=true;detached=true;return true;
}
void NativeInputDriver::Cleanup() noexcept {
    if(!fault || inFlight || released)return;
    if(!retired){if(!detached)(void)inventory.Retire();retired=true;}
    lifecycle.RetirePreservingEvents();if(interrupted)return;
    if(detached){if(lifecycle.ReleaseRetired(routeId))released=true;return;}
    // Fresh attempts retain original identity even after callback invalidation.
    NativeInputHead head;
    while(work<WorkPerFrame && transfers.CancelSession(head,error)==sysEventTransfer_t::Ready){++work;if(interrupted)return;}
    if(interrupted)return;
    while(work<WorkPerFrame && transfers.CancelMouse(head,error)==sysEventTransfer_t::Ready){
        ++work;const auto* entry=inventory.Find(head.tag);if(entry)(void)ObserveHeld(entry->emission,NativeInputSink::Mouse);
        if(interrupted)return;
    }
    if(interrupted)return;
    if(mouseOpen && Sys_EndMouseInputForRetirement(mouseSlice))mouseOpen=false;
    while(work<WorkPerFrame && transfers.CancelKeyboard(head,error)==sysEventTransfer_t::Ready){
        ++work;const auto* entry=inventory.Find(head.tag);if(entry)(void)ObserveHeld(entry->emission,NativeInputSink::Keyboard);
        if(interrupted)return;
    }
    if(interrupted)return;
    if(keyboardOpen && Sys_EndKeyboardInputForRetirement(keyboardSlice))keyboardOpen=false;
    NativeInputDisposalReceipt receipt;
    if(!mouseOpen && !keyboardOpen && inventory.SealDisposal(receipt,error) && !interrupted &&
        lifecycle.ReleaseRetired(routeId) && transfers.Release())released=true;
}
void NativeInputDriver::Checkpoint() noexcept {
    if(!Enter())return;Guard guard{*this};
    if(detached && !fault){NativeInputSelection selected;if(!route.Probe(selected) || !owner.Current(barrier))Fail();}
    if(installed && !detached && !fault)(void)Current();
    if(fault)Cleanup();else (void)FinishFence();
}
void NativeInputDriver::Abort() noexcept {if(std::this_thread::get_id()==thread)Fail();}
bool NativeInputDriver::FatalRetire() noexcept {
    if(std::this_thread::get_id()!=thread || calling){Abort();return false;}
    Abort();Checkpoint();
    if(!Enter())return false;Guard guard{*this};
    return lifecycle.PumpsRetired() && !interrupted;
}
void NativeInputDriver::BeginFrame(std::uint64_t presentation) noexcept {
    if(!Enter())return;Guard guard{*this};
    if(!presentation || presentation<=budgetFrame)return;
    budgetFrame=presentation;work=0;
}
void NativeInputDriver::ContinueDeferred(idEventLoop& expected) noexcept {
    if(&expected!=&loop || std::this_thread::get_id()!=thread){Abort();return;}
    try{loop.ContinueNativeInput();}catch(...){Abort();}Checkpoint();
}
} // namespace openq4

nativeInputSessionResult_t NativeInput_TakeSession(sysEvent_t& out) noexcept {
    return NativeDriverOnThread()?NativeDriverOnThread()->TakeSession(out):(nativeDriverOwned.load() || nativeDriverOrphaned.load())?nativeInputSessionResult_t::Stop:nativeInputSessionResult_t::Legacy;
}
nativeInputSessionResult_t NativeInput_TakeDeferredSession(sysEvent_t& out) noexcept {
    return NativeDriverOnThread()?NativeDriverOnThread()->TakeSession(out,true):nativeInputSessionResult_t::Stop;
}
bool NativeInput_CompleteSession() noexcept{return NativeDriverOnThread() && NativeDriverOnThread()->CompleteSession();}
void NativeInput_AbortDelivery() noexcept {if(NativeDriverOnThread())NativeDriverOnThread()->Abort();}
bool NativeInput_SessionCurrent() noexcept{return NativeDriverOnThread()?NativeDriverOnThread()->SessionCurrent():!(nativeDriverOwned.load() || nativeDriverOrphaned.load());}
void NativeInput_BeginLegacySession() noexcept{if(NativeDriverOnThread())NativeDriverOnThread()->BeginLegacySession();}
void NativeInput_EndLegacySession() noexcept{if(NativeDriverOnThread())NativeDriverOnThread()->EndLegacySession();}
bool NativeInput_BeginMouse() noexcept{return NativeDriverOnThread()?NativeDriverOnThread()->BeginPoll(false):(nativeDriverOwned.load() || nativeDriverOrphaned.load());}
bool NativeInput_NextMouse(int& a,int& b) noexcept{return NativeDriverOnThread() && NativeDriverOnThread()->NextMouse(a,b);}
bool NativeInput_CompleteMouse() noexcept{return NativeDriverOnThread() && NativeDriverOnThread()->CompletePoll(false);}
void NativeInput_EndMouse() noexcept{if(NativeDriverOnThread())NativeDriverOnThread()->EndPoll(false);}
bool NativeInput_BeginKeyboard() noexcept{return NativeDriverOnThread()?NativeDriverOnThread()->BeginPoll(true):(nativeDriverOwned.load() || nativeDriverOrphaned.load());}
bool NativeInput_NextKeyboard(int& key,bool& down) noexcept{return NativeDriverOnThread() && NativeDriverOnThread()->NextKeyboard(key,down);}
bool NativeInput_CompleteKeyboard() noexcept{return NativeDriverOnThread() && NativeDriverOnThread()->CompletePoll(true);}
void NativeInput_EndKeyboard() noexcept{if(NativeDriverOnThread())NativeDriverOnThread()->EndPoll(true);}
void NativeInput_ContinueDeferred(idEventLoop& loop) noexcept {if(NativeDriverOnThread())NativeDriverOnThread()->ContinueDeferred(loop);}
void NativeInput_BeginFrame(std::uint64_t presentation) noexcept {
    if(!Sys_EventDispositionBoundThread() || !presentation || presentation<=nativeInputBudgetFrame)return;
    nativeInputBudgetFrame=presentation;
    if(NativeDriverOnThread()){NativeDriverOnThread()->BeginFrame(presentation);NativeDriverOnThread()->Checkpoint();}
}
bool NativeInput_OwnsPump() noexcept{return nativeDriverOwned.load() || nativeDriverOrphaned.load();}
bool NativeInput_CanPrepareCollection() noexcept{return NativeDriverOnThread() && NativeDriverOnThread()->CanPrepareCollection();}
bool NativeInput_FatalRetire() noexcept {
    return NativeDriverOnThread()?NativeDriverOnThread()->FatalRetire():!(nativeDriverOwned.load() || nativeDriverOrphaned.load());
}
bool NativeInput_Inhibited() noexcept{return nativeDriverOwned.load() || nativeDriverOrphaned.load();}

bool NativeInput_TypedPollDelivery() noexcept{return NativeDriverOnThread() && NativeDriverOnThread()->TypedPollDelivery();}
bool NativeInput_HeldSourceCurrent(std::uint64_t route,std::uint64_t window) noexcept {return NativeDriverOnThread() && NativeDriverOnThread()->HeldSourceCurrent(route,window);}
