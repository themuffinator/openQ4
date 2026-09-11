// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../../idlib/precompiled.h"
#include "NativeInputTransfers.h"
#include "InputDisposition.h"
#include "../../framework/EventLoop.h"

namespace openq4 {
namespace {
bool TransferError(std::string& error,const char* text) noexcept {try{error=text;}catch(...){}return false;}
NativeInputValue EventValue(const sysEvent_t& event) noexcept {
    return Sys_EventRetirementHead(NativeInputLane::Platform,0,0,event,{}).value;
}
}
struct NativeInputTransfers::Guard {
    NativeInputTransfers& owner;
    explicit Guard(NativeInputTransfers& o):owner(o){owner.inventory.transferBusy=true;}
    ~Guard(){owner.inventory.transferBusy=false;owner.calling=false;}
};
struct NativeInputTransfers::Authorization {
    NativeInputEmissionInventory& inventory;
    explicit Authorization(NativeInputEmissionInventory& i):inventory(i){inventory.transferAuthorized=true;}
    ~Authorization(){inventory.transferAuthorized=false;}
};
NativeInputTransfers::NativeInputTransfers(NativeInputEmissionInventory& i,NativeInputRoute& r,idEventLoop& e) noexcept:
    inventory(i),route(r),loop(e),thread(std::this_thread::get_id()){}
bool NativeInputTransfers::Fail(std::string& error,const char* text) noexcept {
    revoked=true;interrupted=true;if(mutation!=UINT64_MAX)++mutation;
    if(claimed)(void)inventory.Retire();
    return TransferError(error,text);
}
bool NativeInputTransfers::Enter(bool cleanup,std::string& error) noexcept {
    if(std::this_thread::get_id()!=thread)return TransferError(error,"Native transfer requires its original thread");
    if(calling)return Fail(error,"Reentrant native transfer/disposal");
    if(!claimed || released)return TransferError(error,"Native transfer has no retained inventory");
    if(!cleanup && (revoked || inventory.retired))return TransferError(error,"Native transfer is retired");
    interrupted=false;calling=true;return true;
}
bool NativeInputTransfers::Claim(std::string& error) noexcept {
    if(std::this_thread::get_id()!=thread)return TransferError(error,"Native transfer requires its original thread");
    if(calling)return Fail(error,"Reentrant native transfer claim");
    if(claimed || released || revoked ||
        !inventory.original || inventory.route!=&route ||
        !inventory.MatchesBinding(route,inventory.base.route,*inventory.original) || !inventory.ClaimTransfers(*this))
        return TransferError(error,"Native transfer cannot claim this exact empty inventory");
    claimed=true;error.clear();return true;
}
NativeInputTransferStatus NativeInputTransfers::AdmitStored(const NativeTranslatedEmission& emitted,std::uint64_t before,std::string& error) noexcept {
    auto* entry=const_cast<NativeInputEmissionInventory::Entry*>(inventory.Find(emitted.tag));
    if(!entry){Fail(error,"Stored native emission lost its inventory");return NativeInputTransferStatus::StoredRetired;}
    entry->ownership=NativeInputEmissionInventory::Ownership::Stored;
    NativeDispositionAdmission admission;
    Authorization authorization{inventory};
    if(mutation!=before || interrupted || revoked || !inventory.Admit(emitted,admission,error) || mutation!=before || interrupted) {
        Fail(error,"Native storage owns an emission whose admission retired");return NativeInputTransferStatus::StoredRetired;
    }
    return NativeInputTransferStatus::StoredAdmitted;
}
NativeInputTransferStatus NativeInputTransfers::SessionKey(NativeDispositionRecord record,sysEvent_t& event,NativeInputSessionQueue queue,
    NativeTranslatedEmission& out,std::string& error) noexcept {return Session(record,event,queue,NativeInputKind::RouteKey,out,error);}
NativeInputTransferStatus NativeInputTransfers::SessionCharacter(NativeDispositionRecord record,sysEvent_t& event,NativeInputSessionQueue queue,
    NativeTranslatedEmission& out,std::string& error) noexcept {return Session(record,event,queue,NativeInputKind::RouteText,out,error);}
NativeInputTransferStatus NativeInputTransfers::SessionMouse(NativeDispositionRecord record,sysEvent_t& event,NativeInputSessionQueue queue,
    NativeTranslatedEmission& out,std::string& error) noexcept {return Session(record,event,queue,NativeInputKind::RouteMouse,out,error);}
NativeInputTransferStatus NativeInputTransfers::Session(NativeDispositionRecord record,sysEvent_t& event,NativeInputSessionQueue queue,
    NativeInputKind kind,NativeTranslatedEmission& out,std::string& error) noexcept {
    if(!Enter(false,error))return NativeInputTransferStatus::Unchanged;Guard guard{*this};
    const auto before=mutation;const sysEvent_t frozen=event;const auto footprint=EventValue(frozen);
    if((queue!=NativeInputSessionQueue::Platform && queue!=NativeInputSessionQueue::Pushed) ||
        (kind==NativeInputKind::RouteKey && (footprint.type!=SE_KEY || (frozen.evValue2!=0 && frozen.evValue2!=1))) ||
        (kind==NativeInputKind::RouteText && (footprint.type!=SE_CHAR || frozen.evValue2 || frozen.evPtrLength || frozen.evPtr)) ||
        (kind==NativeInputKind::RouteMouse && (footprint.type!=SE_MOUSE || frozen.evPtrLength || frozen.evPtr))) {
        Fail(error,"Typed native Session emission has the wrong event shape");return NativeInputTransferStatus::Unchanged;
    }
    NativeTranslatedEmission emitted;bool issued=false;
    {
    Authorization authorization{inventory};
    if(kind==NativeInputKind::RouteKey)issued=inventory.SessionKey(record,frozen.evValue,frozen.evValue2!=0,frozen.evPtrLength,
        reinterpret_cast<std::uintptr_t>(frozen.evPtr),emitted,error);
    else if(kind==NativeInputKind::RouteText)issued=inventory.SessionCharacter(record,frozen.evValue,emitted,error);
    else if(kind==NativeInputKind::RouteMouse)issued=inventory.SessionMouse(record,frozen.evValue,frozen.evValue2,emitted,error);
    }
    if(!issued || mutation!=before || interrupted || EventValue(event)!=footprint || !inventory.ledger->CanAdmit(emitted.ticket)) {
        Fail(error,"Native Session issuance changed before actual transfer");return NativeInputTransferStatus::Unchanged;
    }
    auto value=frozen;auto tag=emitted.tag;
    const bool accepted=queue==NativeInputSessionQueue::Platform?Sys_QueTrackedEvent(value,tag):loop.PushEventWithDisposition(value,tag);
    if(!accepted){Fail(error,"Native Session queue refused ownership");return NativeInputTransferStatus::Unchanged;}
    // Storage owns the frozen input. A replacement caller value introduced by
    // reentry remains caller-owned; do not zero somebody else's payload.
    const bool unchanged=EventValue(event)==footprint;
    if(unchanged)event={};else Fail(error,"Caller changed during native ownership transfer");
    const auto result=AdmitStored(emitted,before,error);out=emitted;
    return result;
}
NativeInputTransferStatus NativeInputTransfers::Keyboard(NativeDispositionRecord record,int key,bool down,int time,
    NativeTranslatedKeyboard& out,std::string& error) noexcept {
    if(!Enter(false,error))return NativeInputTransferStatus::Unchanged;Guard guard{*this};const auto before=mutation;
    NativeTranslatedKeyboard emitted;
    bool issued=false;{Authorization authorization{inventory};issued=inventory.Keyboard(record,key,down,time,emitted,error);}
    if(!issued || mutation!=before || interrupted || !inventory.ledger->CanAdmit(emitted.parent.ticket)) {
        Fail(error,"Native Keyboard issuance changed before actual transfer");return NativeInputTransferStatus::Unchanged;
    }
    sysKeyboardInputDisposition_t value{key,down,time,{emitted.parent.tag,emitted.parent.value.deferredEmission}};
    if(!Sys_QueKeyboardInputWithDisposition(value)){Fail(error,"Native Keyboard queue refused ownership");return NativeInputTransferStatus::Unchanged;}
    const auto result=AdmitStored(emitted.parent,before,error);out=emitted;return result;
}
NativeInputTransferStatus NativeInputTransfers::Mouse(NativeDispositionRecord record,int action,int value,int time,
    NativeTranslatedEmission& out,std::string& error) noexcept {
    if(!Enter(false,error))return NativeInputTransferStatus::Unchanged;Guard guard{*this};const auto before=mutation;
    NativeTranslatedEmission emitted;
    bool issued=false;{Authorization authorization{inventory};issued=inventory.Mouse(record,action,value,time,emitted,error);}
    if(!issued || mutation!=before || interrupted || !inventory.ledger->CanAdmit(emitted.ticket)) {
        Fail(error,"Native Mouse issuance changed before actual transfer");return NativeInputTransferStatus::Unchanged;
    }
    sysMouseInputDisposition_t queued{action,value,time,emitted.tag};
    if(!Sys_QueMouseInputWithDisposition(queued)){Fail(error,"Native Mouse queue refused ownership");return NativeInputTransferStatus::Unchanged;}
    const auto result=AdmitStored(emitted,before,error);out=emitted;return result;
}
NativeInputTransferStatus NativeInputTransfers::Deferred(const NativeTranslatedKeyboard& requested,NativeTranslatedEmission& out,std::string& error) noexcept {
    if(!Enter(false,error))return NativeInputTransferStatus::Unchanged;Guard guard{*this};const auto before=mutation;const auto frozen=requested;
    const auto* parent=inventory.Find(frozen.parent.tag);const auto* child=inventory.Find(frozen.deferred.tag);
    if(!parent || !child || parent->emission.ticket!=frozen.parent.ticket || parent->emission.value!=frozen.parent.value ||
        child->emission.ticket!=frozen.deferred.ticket || child->emission.value!=frozen.deferred.value ||
        parent->ownership!=NativeInputEmissionInventory::Ownership::TakenForDelivery ||
        child->ownership!=NativeInputEmissionInventory::Ownership::NeverTransferred || child->plan.trigger!=parent->emission.ticket ||
        frozen.parent.value.deferredEmission!=frozen.deferred.ticket.emission ||
        !inventory.Current() || mutation!=before || interrupted || !inventory.ledger->CanAdmit(frozen.deferred.ticket)) {
        Fail(error,"Deferred native emission has no exact active Keyboard parent");return NativeInputTransferStatus::Unchanged;
    }
    sysEvent_t event{};event.evType=SE_KEY;event.evValue=frozen.deferred.value.value;event.evValue2=frozen.deferred.value.value2;
    auto tag=frozen.deferred.tag;
    if(!Sys_QueTrackedEvent(event,tag)){Fail(error,"Deferred Session queue refused ownership");return NativeInputTransferStatus::Unchanged;}
    const auto result=AdmitStored(frozen.deferred,before,error);out=frozen.deferred;return result;
}
NativeInputDeliveryStatus NativeInputTransfers::BeginSessionDelivery(sysEvent_t& event,NativeTranslatedEmission& emission,std::string& error) noexcept {
    sysKeyboardInputDisposition_t keyboard;sysMouseInputDisposition_t mouse;
    return Take(NativeInputSink::Session,nullptr,event,keyboard,mouse,emission,error);
}
NativeInputDeliveryStatus NativeInputTransfers::BeginKeyboardDelivery(const sysInputDispositionSlot_t& slot,
    sysKeyboardInputDisposition_t& keyboard,NativeTranslatedEmission& emission,std::string& error) noexcept {
    sysEvent_t event{};sysMouseInputDisposition_t mouse;
    return Take(NativeInputSink::Keyboard,&slot,event,keyboard,mouse,emission,error);
}
NativeInputDeliveryStatus NativeInputTransfers::BeginMouseDelivery(const sysInputDispositionSlot_t& slot,
    sysMouseInputDisposition_t& mouse,NativeTranslatedEmission& emission,std::string& error) noexcept {
    sysEvent_t event{};sysKeyboardInputDisposition_t keyboard;
    return Take(NativeInputSink::Mouse,&slot,event,keyboard,mouse,emission,error);
}
NativeInputDeliveryStatus NativeInputTransfers::Take(NativeInputSink sink,const sysInputDispositionSlot_t* requested,
    sysEvent_t& outEvent,sysKeyboardInputDisposition_t& outKey,sysMouseInputDisposition_t& outMouse,
    NativeTranslatedEmission& outEmission,std::string& error) noexcept {
    if(!Enter(false,error))return NativeInputDeliveryStatus::Unchanged;Guard guard{*this};const auto before=mutation;
    const auto slot=requested?*requested:sysInputDispositionSlot_t{};
    NativeInputHead head;const auto peek=sink==NativeInputSink::Session?loop.PeekEventForRetirement(head):
        sink==NativeInputSink::Keyboard?Sys_PeekKeyboardInputForRetirement(head):Sys_PeekMouseInputForRetirement(head);
    auto* entry=peek==sysEventTransfer_t::Ready?const_cast<NativeInputEmissionInventory::Entry*>(inventory.Find(head.tag)):nullptr;
    if(!entry || entry->sink!=sink || entry->emission.value!=head.value ||
        entry->ownership!=NativeInputEmissionInventory::Ownership::Stored || !inventory.ledger->CanBeginDelivery(entry->emission.ticket) ||
        !inventory.Current() || mutation!=before || interrupted || !inventory.ledger->CanBeginDelivery(entry->emission.ticket)) {
        Fail(error,"Native delivery has no exact current stored ticket");return NativeInputDeliveryStatus::Unchanged;
    }
    // Current may cross the copied Source boundary. Re-read the exact storage
    // identity afterward; Session's legacy aggregate Take has no expected-head
    // parameter and must never consume a prefix inserted by that observation.
    NativeInputHead fresh;const auto freshStatus=sink==NativeInputSink::Session?loop.PeekEventForRetirement(fresh):
        sink==NativeInputSink::Keyboard?Sys_PeekKeyboardInputForRetirement(fresh):Sys_PeekMouseInputForRetirement(fresh);
    if(freshStatus!=sysEventTransfer_t::Ready || fresh!=head || mutation!=before || interrupted) {
        Fail(error,"Native delivery head changed during current-owner observation");return NativeInputDeliveryStatus::Unchanged;
    }
    const auto emission=entry->emission;
    sysEvent_t event{};sysEventDispositionTag_t tag{};sysKeyboardInputDisposition_t keyboard;sysMouseInputDisposition_t mouse;
    NativeInputValue actual;sysEventTransfer_t taken=sysEventTransfer_t::Refused;
    if(sink==NativeInputSink::Session){taken=loop.TakeEventWithDisposition(event,tag);if(taken==sysEventTransfer_t::Ready)actual=EventValue(event);}
    else if(sink==NativeInputSink::Keyboard){taken=Sys_TakeKeyboardInputWithDisposition(slot,keyboard);
        if(taken==sysEventTransfer_t::Ready){tag=keyboard.disposition.parent;actual={SE_KEY,keyboard.key,keyboard.down?1:0,keyboard.time,0,0,keyboard.disposition.deferredEmission};}}
    else{taken=Sys_TakeMouseInputWithDisposition(slot,mouse);
        if(taken==sysEventTransfer_t::Ready){tag=mouse.disposition;actual={SE_MOUSE,mouse.action,mouse.value,mouse.time,0,0,0};}}
    if(taken!=sysEventTransfer_t::Ready){Fail(error,"Native delivery storage refused its original head");return NativeInputDeliveryStatus::Unchanged;}
    // Actual Take occurred. Never hide ownership behind a later false result.
    if(sink==NativeInputSink::Session)outEvent=event;else if(sink==NativeInputSink::Keyboard)outKey=keyboard;else outMouse=mouse;
    outEmission=emission;entry->ownership=NativeInputEmissionInventory::Ownership::Indeterminate;
    if(tag!=head.tag || actual!=head.value){Fail(error,"Native delivery storage changed its owned head");return NativeInputDeliveryStatus::OwnedRetired;}
    entry->ownership=NativeInputEmissionInventory::Ownership::TakenForDelivery;
    if(mutation!=before || interrupted || !inventory.ledger->BeginDelivery(emission.ticket,error) ||
        mutation!=before || interrupted || !inventory.Current() || mutation!=before || interrupted) {
        Fail(error,"Native delivery retired after Take; caller retains its input");return NativeInputDeliveryStatus::OwnedRetired;
    }
    error.clear();return NativeInputDeliveryStatus::OwnedDelivery;
}
sysEventTransfer_t NativeInputTransfers::CancelSession(NativeInputHead& out,std::string& error) noexcept {return Cancel(NativeInputSink::Session,out,error);}
sysEventTransfer_t NativeInputTransfers::CancelKeyboard(NativeInputHead& out,std::string& error) noexcept {return Cancel(NativeInputSink::Keyboard,out,error);}
sysEventTransfer_t NativeInputTransfers::CancelMouse(NativeInputHead& out,std::string& error) noexcept {return Cancel(NativeInputSink::Mouse,out,error);}
sysEventTransfer_t NativeInputTransfers::Cancel(NativeInputSink sink,NativeInputHead& out,std::string& error) noexcept {
    if(!Enter(true,error))return sysEventTransfer_t::Refused;Guard guard{*this};const auto before=mutation;
    if(!inventory.retired)(void)inventory.Retire();
    if(route.State()!=NativeInputRoute::Phase::DrainOnly && !route.MarkDrainOnly(inventory.base.route))
        return sysEventTransfer_t::Refused;
    if(mutation!=before || interrupted)return sysEventTransfer_t::Refused;
    NativeInputHead head;const auto peek=sink==NativeInputSink::Session?loop.PeekEventForRetirement(head):
        sink==NativeInputSink::Keyboard?Sys_PeekKeyboardInputForRetirement(head):Sys_PeekMouseInputForRetirement(head);
    if(peek!=sysEventTransfer_t::Ready)return peek;
    auto* entry=const_cast<NativeInputEmissionInventory::Entry*>(inventory.Find(head.tag));
    if(!entry || entry->sink!=sink || entry->emission.value!=head.value ||
        entry->ownership!=NativeInputEmissionInventory::Ownership::Stored) {
        Fail(error,"Retired head does not belong to this stored emission");return sysEventTransfer_t::Refused;
    }
    NativeInputRoute::CancellationPermit permit;
    if(!route.PrepareCancellation(head,permit) || mutation!=before || interrupted){Fail(error,"Retired head lost original disposal authority");return sysEventTransfer_t::Refused;}
    sysEventTransfer_t taken=sysEventTransfer_t::Refused;NativeInputValue actual;
    sysEvent_t event{};sysEventDispositionTag_t tag{};
    if(sink==NativeInputSink::Session) {
        taken=loop.TakeEventForRetirement(route,permit,event,tag);
        if(taken==sysEventTransfer_t::Ready)actual=EventValue(event);
    } else if(sink==NativeInputSink::Keyboard) {
        sysKeyboardInputDisposition_t value;taken=Sys_TakeKeyboardInputForRetirement(route,permit,value);
        if(taken==sysEventTransfer_t::Ready){tag=value.disposition.parent;actual={SE_KEY,value.key,value.down?1:0,value.time,0,0,value.disposition.deferredEmission};}
    } else {
        sysMouseInputDisposition_t value;taken=Sys_TakeMouseInputForRetirement(route,permit,value);
        if(taken==sysEventTransfer_t::Ready){tag=value.disposition;actual={SE_MOUSE,value.action,value.value,value.time,0,0,0};}
    }
    if(taken!=sysEventTransfer_t::Ready)return taken;
    entry->ownership=NativeInputEmissionInventory::Ownership::Indeterminate;
    if(tag!=head.tag || actual!=head.value){Fail(error,"Retirement storage returned a different owned event");return sysEventTransfer_t::Refused;}
    // Payload ownership transferred exactly once. Destruction is outside every
    // store lock and cannot enter a GUI. An exceptional allocator return leaves
    // an explicit indeterminate obligation; never retry an uncertain free.
    try {if(event.evPtr)Mem_Free(event.evPtr);} catch(...) {
        Fail(error,"Retired payload destruction did not complete");return sysEventTransfer_t::Refused;
    }
    entry->ownership=NativeInputEmissionInventory::Ownership::Cancelled;
    if(mutation!=before || interrupted){Fail(error,"Native disposal completed during reentry; fresh cleanup required");return sysEventTransfer_t::Refused;}
    out=head;error.clear();return sysEventTransfer_t::Ready;
}
bool NativeInputTransfers::Release() noexcept {
    if(std::this_thread::get_id()!=thread || calling || !claimed || released || !inventory.ReleaseTransfers(*this))return false;
    claimed=false;released=true;return true;
}
} // namespace openq4
