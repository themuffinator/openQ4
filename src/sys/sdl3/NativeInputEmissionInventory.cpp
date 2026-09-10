// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../../idlib/precompiled.h"
#include "NativeInputEmissionInventory.h"
#include "../../framework/KeyInput.h"

namespace openq4 {
namespace {
bool InventoryError(std::string& error,const char* text) noexcept {try{error=text;}catch(...){}return false;}
bool DeferredKey(int key) noexcept {return key==K_CTRL || key==K_ALT || key==K_RIGHT_ALT || key==K_PRINT_SCR;}
bool ValidMouse(int action,int value) noexcept {
    if (action>=M_ACTION1 && action<=M_ACTION8) return value==0 || value==1;
    return (action==M_DELTAX || action==M_DELTAY || action==M_DELTAZ) && value!=0;
}
}
struct NativeInputEmissionInventory::Guard {
    NativeInputEmissionInventory& owner;
    ~Guard(){owner.calling=false;}
};
NativeInputEmissionInventory::NativeInputEmissionInventory():thread(std::this_thread::get_id()),entries(0){}
bool NativeInputEmissionInventory::Fail(std::string& error,const char* text) noexcept {
    retired=true;if(route)(void)route->Revoke(base.route);if(ledger)(void)ledger->Retire();
    return InventoryError(error,text);
}
bool NativeInputEmissionInventory::Enter(std::string& error) noexcept {
    if(std::this_thread::get_id()!=thread)return InventoryError(error,"Native emission inventory requires its original thread");
    if(calling)return Fail(error,"Reentrant native emission inventory");
    calling=true;return true;
}
bool NativeInputEmissionInventory::Current() noexcept {
    if(!bound || retired || !route || !ledger || !batch)return false;
    NativeInputSelection selected;
    return route->Probe(selected) && !retired && selected.route==base.route && selected.window==base.window;
}
bool NativeInputEmissionInventory::Bind(NativeInputRoute& r,std::uint64_t id,const NativeInputBinding& binding,
    NativeEventDispositionLedger& l,const NativeQueueBatch& b,std::string& error) noexcept {
    if(!Enter(error))return false;Guard guard{*this};
    if(bound || retired || !id || !binding.dispatchEpoch || !binding.streamToken || !binding.window.module ||
        b.Status().providerEpoch!=binding.window.module || b.Status().engineToken!=binding.streamToken ||
        !b.Receipt().ingress || !b.Receipt().serial)return Fail(error,"Native emission inventory binding is invalid");
    try{
        auto frozen=std::make_unique<const NativeInputBinding>(binding);
        std::vector<Entry> prepared(0);prepared.reserve(NativeEventDispositionLedger::MaxEmissions);
        NativeInputSelection selected;
        if(!r.MatchesOriginalBinding(id,*frozen) || !r.Probe(selected) ||
            !r.MatchesOriginalBinding(id,*frozen) || retired || selected.route!=id || selected.current!=frozen->outer ||
            selected.allocation!=frozen->editor.allocation || selected.window!=frozen->window.window)
            return Fail(error,"Native emission inventory owner changed during preparation");
        base.dispatchEpoch=frozen->dispatchEpoch;base.streamToken=frozen->streamToken;base.providerEpoch=frozen->window.module;
        base.route=id;base.window=frozen->window.window;base.windowLifetime=frozen->window.lifetime;
        base.ingress=b.Receipt().ingress;base.batchSerial=b.Receipt().serial;
        route=&r;ledger=&l;batch=&b;original=std::move(frozen);entries.swap(prepared);bound=true;error.clear();return true;
    }catch(...){return Fail(error,"Native emission inventory allocation failed");}
}
bool NativeInputEmissionInventory::Issue(NativeDispositionRecord record,NativeEmissionPlan plan,
    NativeInputSink sink,NativeInputKind kind,NativeInputValue value,NativeTranslatedEmission& out,std::string& error) noexcept {
    if(!Current() || record.receipt.batch!=batch->Receipt() || record.index>=batch->Events().size() ||
        record.queueSequence!=batch->Events()[record.index].record.queue_sequence || !record.receipt.ledger || !record.receipt.serial ||
        entries.size()==NativeEventDispositionLedger::MaxEmissions)
        return Fail(error,"Native emission has no exact live record/owner");
    // This pre-reserved trivial slot exists before real Issue can call Source.
    entries.push_back({{},plan,sink,kind});
    const auto index=entries.size()-1;
    entries[index].emission.value=value;
    NativeDispositionTicket ticket;
    if(!ledger->Issue(record,plan,ticket,error)){entries.pop_back();return Fail(error,"Native ledger refused translator issue");}
    auto& entry=entries[index];entry.emission.ticket=ticket;entry.emission.tag=base;
    auto& tag=entry.emission.tag;tag.ledger=ticket.record.receipt.ledger;tag.ledgerSerial=ticket.record.receipt.serial;
    tag.queueSequence=ticket.record.queueSequence;tag.recordIndex=ticket.record.index;tag.emission=ticket.emission;
    if(!tag.ShapeValid() || !Current())return Fail(error,"Native owner changed after issuing its retained ticket");
    out=entry.emission;error.clear();return true;
}
bool NativeInputEmissionInventory::SessionKey(NativeDispositionRecord record,int key,bool down,int length,
    std::uintptr_t payload,NativeTranslatedEmission& out,std::string& error) noexcept {
    if(!Enter(error))return false;Guard guard{*this};
    if(key<=0 || key>=K_LAST_KEY || DeferredKey(key) || length<0 || length>1024*1024 || ((length!=0)!=(payload!=0)))
        return Fail(error,"Invalid direct Session key emission");
    return Issue(record,{NativeDispositionPass::SessionInitial,{}},NativeInputSink::Session,NativeInputKind::RouteKey,
        {SE_KEY,key,down?1:0,0,length,payload,0},out,error);
}
bool NativeInputEmissionInventory::SessionCharacter(NativeDispositionRecord record,int character,NativeTranslatedEmission& out,std::string& error) noexcept {
    if(!Enter(error))return false;Guard guard{*this};
    if(character<=0 || character>255)return Fail(error,"Legacy Session character exceeds its existing byte contract");
    return Issue(record,{NativeDispositionPass::SessionInitial,{}},NativeInputSink::Session,NativeInputKind::RouteText,
        {SE_CHAR,character,0,0,0,0,0},out,error);
}
bool NativeInputEmissionInventory::SessionMouse(NativeDispositionRecord record,int dx,int dy,NativeTranslatedEmission& out,std::string& error) noexcept {
    if(!Enter(error))return false;Guard guard{*this};
    return Issue(record,{NativeDispositionPass::SessionInitial,{}},NativeInputSink::Session,NativeInputKind::RouteMouse,
        {SE_MOUSE,dx,dy,0,0,0,0},out,error);
}
bool NativeInputEmissionInventory::Keyboard(NativeDispositionRecord record,int key,bool down,int time,
    NativeTranslatedKeyboard& out,std::string& error) noexcept {
    if(!Enter(error))return false;Guard guard{*this};
    if(key<=0 || key>=K_LAST_KEY || time<0)return Fail(error,"Invalid Keyboard emission");
    NativeTranslatedKeyboard candidate;
    if(!Issue(record,{NativeDispositionPass::KeyboardPoll,{}},NativeInputSink::Keyboard,NativeInputKind::RouteKey,
        {SE_KEY,key,down?1:0,time,0,0,0},candidate.parent,error))return false;
    if(DeferredKey(key)){
        if(!Issue(record,{NativeDispositionPass::SessionDeferred,candidate.parent.ticket},NativeInputSink::Session,NativeInputKind::RouteKey,
            {SE_KEY,key,down?1:0,0,0,0,0},candidate.deferred,error))return false;
        candidate.parent.value.deferredEmission=candidate.deferred.ticket.emission;
        // No parent output/head existed before the reservation was complete.
        entries[entries.size()-2].emission.value.deferredEmission=candidate.deferred.ticket.emission;
    }
    out=candidate;error.clear();return true;
}
bool NativeInputEmissionInventory::Mouse(NativeDispositionRecord record,int action,int value,int time,
    NativeTranslatedEmission& out,std::string& error) noexcept {
    if(!Enter(error))return false;Guard guard{*this};
    if(!ValidMouse(action,value) || time<0)return Fail(error,"Invalid Mouse emission");
    return Issue(record,{NativeDispositionPass::MousePoll,{}},NativeInputSink::Mouse,NativeInputKind::RouteMouse,
        {SE_MOUSE,action,value,time,0,0,0},out,error);
}
const NativeInputEmissionInventory::Entry* NativeInputEmissionInventory::Find(const sysEventDispositionTag_t& tag) const noexcept {
    for(const auto& entry:entries)if(entry.emission.tag==tag)return &entry;return nullptr;
}
bool NativeInputEmissionInventory::Admit(const NativeTranslatedEmission& input,NativeDispositionAdmission& out,std::string& error) noexcept {
    if(!Enter(error))return false;Guard guard{*this};
    const auto frozen=input;const auto* entry=Find(frozen.tag);
    if(!Current() || !entry || entry->emission.ticket!=frozen.ticket || entry->emission.value!=frozen.value)
        return Fail(error,"Native admission does not match its retained translator output");
    NativeDispositionAdmission candidate;
    if(!ledger->Admit(frozen.ticket,candidate,error) || !Current())return Fail(error,"Native admission lost ownership");
    out=candidate;error.clear();return true;
}
bool NativeInputEmissionInventory::Retire() noexcept {
    if(std::this_thread::get_id()!=thread)return false;
    retired=true;if(route)(void)route->Revoke(base.route);return !ledger || ledger->Retire();
}
bool NativeInputEmissionInventory::Inspect(std::uint64_t id,const NativeInputBinding& b,const sysEventDispositionTag_t& tag,
    NativeInputIssued& out) const noexcept {
    if(std::this_thread::get_id()!=thread || calling || !bound || !ledger || !original || !tag.ShapeValid() || id!=base.route ||
        b.outer!=original->outer || b.sessionTransition!=original->sessionTransition || b.editor!=original->editor ||
        b.native!=original->native || b.window!=original->window ||
        tag.route!=id || tag.dispatchEpoch!=b.dispatchEpoch || tag.streamToken!=b.streamToken ||
        tag.providerEpoch!=b.window.module || tag.window!=b.window.window || tag.windowLifetime!=b.window.lifetime)return false;
    const auto* entry=Find(tag);if(!entry)return false;
    NativeIssuedEmission actual;
    if(!ledger->InspectIssuedForRetirement(entry->emission.ticket,actual) || actual.ticket!=entry->emission.ticket ||
        actual.pass!=entry->plan.pass || actual.trigger!=entry->plan.trigger)return false;
    if(entry->emission.value.deferredEmission){
        const Entry* child=nullptr;
        for(const auto& candidate:entries)if(candidate.emission.ticket.emission==entry->emission.value.deferredEmission)child=&candidate;
        NativeIssuedEmission childActual;
        if(!child || child->plan.pass!=NativeDispositionPass::SessionDeferred || child->plan.trigger!=entry->emission.ticket ||
            !ledger->InspectIssuedForRetirement(child->emission.ticket,childActual) || childActual.trigger!=entry->emission.ticket ||
            childActual.pass!=NativeDispositionPass::SessionDeferred)return false;
    }
    out={tag,entry->sink,entry->kind,entry->emission.value,true,actual.terminal,actual.inFlight};return true;
}
} // namespace openq4
