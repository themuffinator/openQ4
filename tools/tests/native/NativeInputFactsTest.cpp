// Actual publication hub, route, inventory, ingress and retired planned ledger.
// Native/GUI/translator integration stays disabled. Source facts below are
// counted external owners; the separate lifecycle suite executes producer bodies.
#define main ExistingDispositionMain
#include "NativeEventDispositionTest.cpp"
#undef main
#include "src/framework/NativeInputPublications.h"
#include "src/framework/NativeInputRoute.h"
#include "src/sys/sdl3/NativeInputEmissionInventory.h"
#include "engine_enums.inc"

struct RouteFacts final : NativeInputRouteSource {
    NativeInputBinding binding;
    std::uint64_t id=0;
    bool allow=true,retired=true,disposed=true,useHub=false;
    mutable std::function<void()> callback;
    mutable unsigned observations=0,retirements=0;
    const NativeInputEmissionInventory* inventory=nullptr;
    bool Observe(NativeInputObservation& out) const noexcept override {
        ++observations;auto f=std::move(callback);if(f)f();
        const auto transition=useHub?NativeInputSessionTransition():binding.sessionTransition;
        out={binding.outer,binding.editor.allocation,transition,binding.dispatchEpoch,binding.streamToken,
            binding.window,true,allow&&transition!=0,true,true};return true;
    }
    bool Retirement(std::uint64_t route,const NativeInputBinding& b,NativeInputRetirement& out) const noexcept override {
        ++retirements;
        if(route!=id || b.native!=binding.native || b.editor!=binding.editor)return false;
        out={id,binding.native,binding.window,
            retired?NativeInputUiRetirement::AbsentOriginal:NativeInputUiRetirement::Busy,
            retired?NativeInputNativeRetirement::RetiredExact:NativeInputNativeRetirement::Busy,
            retired?NativeInputNativeRetirement::RetiredExact:NativeInputNativeRetirement::Busy,
            retired,retired,disposed};return true;
    }
    bool InspectIssued(std::uint64_t route,const NativeInputBinding& b,const sysEventDispositionTag_t& tag,NativeInputIssued& out) const noexcept override {
        return inventory && inventory->Inspect(route,b,tag,out);
    }
};
static NativeInputBinding Binding(std::uint64_t transition=2) {
    NativeInputBinding b;b.outer=0x1000;b.sessionTransition=transition;b.dispatchEpoch=Sys_EventDispositionEpoch();
    b.streamToken=Sys_EventQueueToken();b.window={0x2000,5,6,7,8,9};
    b.editor={10,11,12,13,5,14,15,"system.gamma_precise"};b.native={16,17};return b;
}
struct InventoryFixture : Fixture {
    RouteFacts facts;
    NativeInputRoute route{facts};
    NativeInputEmissionInventory inventory;
    std::uint64_t id=0;
    InventoryFixture() {
        facts.binding=Binding();facts.inventory=&inventory;
        CHECK(route.Prepare(facts.binding,id,error));facts.id=id;
        CHECK(ledger.BeginPlanned(ingress,source,*batch,error));
        CHECK(inventory.Bind(route,id,facts.binding,ledger,*batch,error));
    }
    NativeInputIssued Inspect(const NativeTranslatedEmission& value) {
        NativeInputIssued out;CHECK(inventory.Inspect(id,facts.binding,value.tag,out));
        CHECK(out.issued && out.tag==value.tag && out.value==value.value);return out;
    }
};
static void IssuedOwnership() {
    for(int key:std::array<int,5>{'a',K_CTRL,K_ALT,K_RIGHT_ALT,K_PRINT_SCR}) {
        InventoryFixture f;auto record=f.Open();NativeTranslatedKeyboard out;
        CHECK(f.inventory.Keyboard(record,key,true,123,out,f.error));
        CHECK(out.parent.value.type==SE_KEY && out.parent.value.value==key && out.parent.value.value2==1 && out.parent.value.time==123);
        NativeDispositionAdmission admitted;CHECK(f.inventory.Admit(out.parent,admitted,f.error));
        CHECK(admitted.ticket==out.parent.ticket && admitted.serial);
        CHECK(f.ledger.SealRecord(record,NativeRecordDisposition::Emitted,f.error));CHECK(f.ledger.SealTranslation(f.error));
        CHECK(f.ledger.CompletePass(NativeDispositionPass::SessionInitial,f.error));
        CHECK(f.ledger.CompletePass(NativeDispositionPass::MousePoll,f.error));
        CHECK(f.ledger.BeginDelivery(out.parent.ticket,f.error));
        if(out.deferred.ticket.emission) {
            CHECK(out.parent.value.deferredEmission==out.deferred.ticket.emission);
            CHECK(out.deferred.value.value==key && out.deferred.value.time==0);
            CHECK(f.inventory.Admit(out.deferred,admitted,f.error));
        } else CHECK(key=='a' && !out.parent.value.deferredEmission);
        CHECK(f.ledger.CompleteDelivery(out.parent.ticket,NativeDeliveryDisposition::Delivered,f.error));
        CHECK(f.inventory.Retire());
        CHECK(f.Inspect(out.parent).terminal);
        if(out.deferred.ticket.emission) {
            auto child=f.Inspect(out.deferred);CHECK(child.sink==NativeInputSink::Session && !child.terminal);
        }
    }
    for(unsigned kind=0;kind<4;++kind) {
        InventoryFixture f;auto record=f.Open();NativeTranslatedEmission emitted;
        if(kind==0) CHECK(f.inventory.SessionKey(record,'a',false,32,0xabc,emitted,f.error));
        if(kind==1) CHECK(f.inventory.SessionCharacter(record,255,emitted,f.error));
        if(kind==2) CHECK(f.inventory.SessionMouse(record,-11,37,emitted,f.error));
        if(kind==3) CHECK(f.inventory.Mouse(record,M_DELTAZ,-4,88,emitted,f.error));
        CHECK(f.inventory.Retire()); // No Admit: the actual storage transfer may already own this exact slot.
        const auto value=f.Inspect(emitted);CHECK(!value.terminal && !value.inFlight);
        CHECK(value.kind==(kind==1?NativeInputKind::RouteText:kind<1?NativeInputKind::RouteKey:NativeInputKind::RouteMouse));
        CHECK(value.sink==(kind==3?NativeInputSink::Mouse:NativeInputSink::Session));
        if(kind==0)CHECK(value.value.payloadLength==32 && value.value.payload==0xabc);
        NativeInputHead head{kind==3?NativeInputLane::Mouse:NativeInputLane::Platform,991,0,emitted.tag,emitted.value};
        CHECK(f.route.MarkDrainOnly(f.id));NativeInputRoute::CancellationPermit permit;
        CHECK(f.route.PrepareCancellation(head,permit));CHECK(f.route.AllowsCancellation(permit,head));
        auto changed=head;++changed.value.value;CHECK(!f.route.AllowsCancellation(permit,changed));
        CHECK(f.route.Release(f.id));
    }
}
static void InventoryFailures() {
    for(unsigned which=0;which<13;++which) {
        InventoryFixture f;auto record=f.Open();NativeTranslatedEmission out;out.ticket.emission=999;
        const auto old=out;
        bool result=true;
        switch(which) {
        case 0:result=f.inventory.SessionKey(record,K_CTRL,true,0,0,out,f.error);break;
        case 1:result=f.inventory.SessionKey(record,0,true,0,0,out,f.error);break;
        case 2:result=f.inventory.SessionKey(record,'a',true,1,0,out,f.error);break;
        case 3:result=f.inventory.SessionCharacter(record,256,out,f.error);break;
        case 4:result=f.inventory.Mouse(record,M_DELTAX,0,1,out,f.error);break;
        case 5:result=f.inventory.Mouse(record,M_ACTION1,2,1,out,f.error);break;
        case 6:++record.queueSequence;result=f.inventory.SessionMouse(record,1,2,out,f.error);break;
        case 7:++record.receipt.batch.serial;result=f.inventory.SessionMouse(record,1,2,out,f.error);break;
        case 8:++record.index;result=f.inventory.SessionMouse(record,1,2,out,f.error);break;
        case 9:f.facts.allow=false;result=f.inventory.SessionMouse(record,1,2,out,f.error);break;
        case 10:f.source.observe=[&]{f.inventory.Retire();};result=f.inventory.SessionMouse(record,1,2,out,f.error);break;
        case 11:f.facts.callback=[&]{NativeTranslatedEmission nested;CHECK(!f.inventory.SessionMouse(record,1,2,nested,f.error));};
            result=f.inventory.SessionMouse(record,1,2,out,f.error);break;
        case 12:f.source.observe=[&]{f.route.Revoke(f.id);};result=f.inventory.SessionMouse(record,1,2,out,f.error);break;
        }
        CHECK(!result && out.ticket==old.ticket && out.tag==old.tag && out.value==old.value);
        CHECK(f.ledger.NeedsRetirement());
    }
    for(unsigned which=0;which<9;++which) {
        InventoryFixture f;auto record=f.Open();NativeTranslatedEmission first,second;
        CHECK(f.inventory.SessionMouse(record,3,4,first,f.error));CHECK(f.inventory.SessionMouse(record,5,6,second,f.error));
        NativeDispositionAdmission out;out.serial=901;const auto before=out;
        auto changed=first;
        switch(which) {
        case 0:++changed.value.value;break;
        case 1:++changed.tag.route;break;
        case 2:++changed.ticket.emission;break;
        case 3:changed=second;break; // exact issued sibling, wrong actual admission order
        case 4:f.source.observe=[&]{f.inventory.Retire();};break;
        case 5:f.facts.allow=false;break;
        case 6:f.facts.callback=[&]{NativeDispositionAdmission nested;CHECK(!f.inventory.Admit(first,nested,f.error));};break;
        case 7:f.source.continuity.Invalidate();break;
        case 8:f.source.observe=[&]{f.facts.allow=false;};break;
        }
        CHECK(!f.inventory.Admit(changed,out,f.error));CHECK(out==before);
        CHECK(f.inventory.Retire());CHECK(!f.Inspect(first).terminal);CHECK(!f.Inspect(second).terminal);
    }
    InventoryFixture f;auto record=f.Open();NativeTranslatedEmission value;
    std::thread worker([&]{std::string error;CHECK(!f.inventory.SessionMouse(record,3,4,value,error));CHECK(!f.inventory.Retire());});worker.join();
    CHECK(!f.ledger.NeedsRetirement());CHECK(f.inventory.SessionMouse(record,3,4,value,f.error));CHECK(f.inventory.Retire());
    NativeInputIssued result;result.value.value=432;const auto old=result;
    auto wrong=f.facts.binding;
    for(unsigned which=0;which<6;++which) {
        wrong=f.facts.binding;
        if(which==0)++wrong.editor.revision;if(which==1)++wrong.native.editorLease;if(which==2)++wrong.window.lifetime;
        if(which==3)++wrong.dispatchEpoch;if(which==4)++wrong.streamToken;if(which==5)++wrong.outer;
        CHECK(!f.inventory.Inspect(f.id,wrong,value.tag,result));CHECK(result.value==old.value);
    }
    std::thread inspect([&]{CHECK(!f.inventory.Inspect(f.id,f.facts.binding,value.tag,result));});inspect.join();
}
static void OriginalBindingChecks() {
    for(unsigned field=0;field<20;++field) {
        Fixture f;RouteFacts facts;facts.binding=Binding(NativeInputSessionTransition());
        NativeInputRoute route(facts);CHECK(route.Prepare(facts.binding,facts.id,f.error));
        auto changed=facts.binding;
        switch(field) {
        case 0:++changed.outer;break;case 1:++changed.sessionTransition;break;
        case 2:++changed.dispatchEpoch;break;case 3:++changed.streamToken;break;
        case 4:++changed.editor.allocation;break;case 5:++changed.editor.backend;break;
        case 6:++changed.editor.document;break;case 7:++changed.editor.modal;break;
        case 8:++changed.editor.window;break;case 9:++changed.editor.session;break;
        case 10:++changed.editor.revision;break;case 11:changed.editor.control="other";break;
        case 12:++changed.native.document;break;case 13:++changed.native.editorLease;break;
        case 14:++changed.window.handle;break;case 15:++changed.window.window;break;
        case 16:++changed.window.lifetime;break;case 17:++changed.window.module;break;
        case 18:++changed.window.association;break;case 19:++changed.window.registration;break;
        }
        const auto observations=facts.observations;
        denyAllocation=true;
        CHECK(route.MatchesOriginalBinding(facts.id,facts.binding));
        CHECK(!route.MatchesOriginalBinding(facts.id,changed));
        CHECK(!route.MatchesOriginalBinding(facts.id+1,facts.binding));
        denyAllocation=false;CHECK(facts.observations==observations);
        std::thread worker([&]{CHECK(!route.MatchesOriginalBinding(facts.id,facts.binding));});worker.join();
        CHECK(!NativeInputBindPublications(route,facts.id,changed));CHECK(NativeInputPublicationSlotEmpty());
        CHECK(f.ledger.BeginPlanned(f.ingress,f.source,*f.batch,f.error));
        NativeInputEmissionInventory invalid;
        CHECK(!invalid.Bind(route,facts.id,changed,f.ledger,*f.batch,f.error));
        CHECK(route.Revoke(facts.id));
        denyAllocation=true;CHECK(route.MatchesOriginalBinding(facts.id,facts.binding));denyAllocation=false;
        CHECK(route.MarkDrainOnly(facts.id));CHECK(route.Release(facts.id));
        CHECK(!route.MatchesOriginalBinding(facts.id,facts.binding));
    }
    RouteFacts facts;facts.binding=Binding(NativeInputSessionTransition());NativeInputRoute route(facts);std::string error;
    CHECK(route.Prepare(facts.binding,facts.id,error));
    facts.callback=[&]{CHECK(!route.MatchesOriginalBinding(facts.id,facts.binding));route.Revoke(facts.id);};
    CHECK(!NativeInputBindPublications(route,facts.id,facts.binding));CHECK(NativeInputPublicationSlotEmpty());
    CHECK(route.MatchesOriginalBinding(facts.id,facts.binding));
}
static void Publications(bool exhaust) {
    for(unsigned which=0;which<(exhaust?1u:6u);++which) {
        RouteFacts facts;facts.useHub=true;facts.binding=Binding(NativeInputSessionTransition());
        NativeInputRoute route(facts);std::string error;
        CHECK(route.Prepare(facts.binding,facts.id,error));CHECK(NativeInputBindPublications(route,facts.id,facts.binding));
        NativeInputSelection selection;CHECK(route.Probe(selection));
        CHECK(!NativeInputPublicationSlotEmpty());CHECK(!NativeInputUnbindPublications(route,facts.id));
        NativeInputBeforeUiChange(facts.binding.editor.allocation+1,facts.binding.editor.backend+1);CHECK(route.Probe(selection));
        if(exhaust) {NativeInputBeforeSessionChange();NativeInputBeforeSessionChange();CHECK(!NativeInputSessionTransition());}
        else if(which==0)NativeInputBeforeSessionChange();
        else if(which==1)NativeInputBeforeUiChange(facts.binding.editor.allocation);
        else if(which==2)NativeInputBeforeUiChange(0,facts.binding.editor.backend);
        else if(which==3)NativeInputBeforeWindowChange();
        else if(which==4)NativeInputBeforeInputBlockerChange();
        else {std::thread worker([]{NativeInputBeforeSessionChange();});worker.join();CHECK(!NativeInputSessionTransition());}
        CHECK(!route.Probe(selection));CHECK(NativeInputPublicationsCurrent(route,facts.id));
        // Wrong thread/exhaustion closes live input permanently, while original
        // retirement remains observable and can release the pinned publication.
        CHECK(route.MarkDrainOnly(facts.id));CHECK(route.Release(facts.id));
        CHECK(NativeInputUnbindPublications(route,facts.id));
        if(exhaust || which==5)CHECK(!NativeInputPublicationSlotEmpty());
        else CHECK(NativeInputPublicationSlotEmpty());
    }
}
int main(int argc,char**) {
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    CHECK(Sys_BindEventDispositionThread()!=0);
    if(argc>1)Publications(true);else {OriginalBindingChecks();IssuedOwnership();InventoryFailures();Publications(false);}
    std::printf("NativeInputFactsTest PASS %u checks\n",checks);return 0;
}
