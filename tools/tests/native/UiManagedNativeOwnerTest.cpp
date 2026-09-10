// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Included after actual manager/deferred/retained/native Runtime method bodies.
// Host/resource preparation is counted; editor, transactions and history are real.
#include "src/ui/application/ManagedNativeTextOwner.h"
#include <thread>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
static bool denyAllocation=false;
void* operator new(std::size_t size) {
    if(denyAllocation){std::fputs("FAIL allocation in native publication/current/retirement\n",stderr);std::exit(1);}
    if(void* result=std::malloc(size?size:1))return result;throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete[](void* value) noexcept {std::free(value);}
void operator delete(void* value,std::size_t) noexcept {std::free(value);}
void operator delete[](void* value,std::size_t) noexcept {std::free(value);}
using namespace openq4::ui;
static unsigned checks;
#define CHECK(value) do {++checks;if(!(value)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value);std::exit(1);}}while(false)
static uiNativeTextRoute_t Probe(void* value) noexcept {return *static_cast<uiNativeTextRoute_t*>(value);}
struct Fixture {
    idUserInterfaceManaged* outer=nullptr;
    idUserInterfaceRetained* gui=nullptr;
    std::shared_ptr<Runtime> runtime;
    uiNativeTextRoute_t route;
    ManagedNativeTextOwner owner{Probe,&route};
    TextEditorIdentity editor;
    NativeTextIdentity nativeId{123,456};
    NativeTextDocument native;
    NativeTextEditorBarrier barrier;
    std::string error;
    explicit Fixture(bool deferred=false) {
        if(deferred){auto* value=new idUserInterfaceDeferred;outer=value;CHECK(value->InitFromFile("number.q4ui"));gui=dynamic_cast<idUserInterfaceRetained*>(value->backend);}
        else {gui=new idUserInterfaceRetained;outer=gui;}
        CHECK(gui);runtime=gui->impl->runtime;route={outer,17,true};
        gui->impl->active=true;gui->impl->interactive=true;
        const auto state=View();editor={outer->allocationId,gui->impl->textBackend,gui->impl->textDocument,runtime->impl->interaction.ModalToken(),route.window,state.identity.session,state.identity.revision,"number-with-a-long-control-id"};
    }
    ~Fixture(){delete outer;}
    NumberEditView View() const {auto value=runtime->impl->interaction.Widget("number-with-a-long-control-id");CHECK(value && value->number);return *value->number;}
    void Attach(){const auto state=View();CHECK(owner.Attach(editor,nativeId,barrier,error));CHECK(native.Open(nativeId,editor.revision,state.state.text,state.state.anchor,state.state.caret,error));}
    NativeTextOffer Change(const std::string& text="1.75") {
        NativeTextLockScope lock;CHECK(native.RequestLock(nativeId,NativeTextAccess::ReadWrite,true,lock,error)==NativeTextLockResult::Granted);
        NativeTextSnapshot before;CHECK(native.Read(lock,before,error));
        CHECK(native.ReplaceACP(lock,0,before.text.size(),text,error));
        CHECK(native.SelectACP(lock,text.size(),0,error));
        std::uint64_t sequence=0;CHECK(native.FinishLock(lock,31,sequence,error));
        NativeTextOffer offer;CHECK(native.PeekOffer(nativeId,offer,error));return offer;
    }
    std::unique_ptr<Interaction::NativeSettlement> Prepare() {
        const auto offer=Change();NativeTextEditorBarrier next;
        const NativeTextCollection collection{31,32,offer.transaction.sequence};
        CHECK(owner.Begin(barrier,collection,next,error));barrier=next;
        NativeTextEditorReceipt receipt;CHECK(owner.Apply(barrier,offer,receipt,error));
        CHECK(native.Acknowledge(nativeId,receipt.after.sequence,receipt.after.shadowRevision,receipt.before.editor.revision,receipt.after.editor.revision,error));barrier=receipt.after;
        CHECK(View().state.text=="1");CHECK(View().nativePresentation->text=="1.75");
        CHECK(owner.Complete(barrier,collection,next,error));barrier=next;
        auto prepared=owner.PrepareSettlement(barrier,error);CHECK(prepared);return prepared;
    }
};
static void Happy(bool deferred) {
    Fixture f(deferred);f.Attach();CHECK(f.owner.Current(f.barrier));
    NativeTextEditorView observed;CHECK(f.owner.Refresh(f.barrier,observed,f.error));CHECK(observed.barrier==f.barrier);
    const int preparedCalls=f.gui->impl->prepares,hostCalls=f.runtime->impl->prepares;
    denyAllocation=true;CHECK(f.owner.Current(f.barrier));denyAllocation=false;
    CHECK(f.gui->impl->prepares==preparedCalls && f.runtime->impl->prepares==hostCalls);
    auto prepared=f.Prepare();const auto receipt=prepared->Receipt();const auto presentation=prepared->Presentation();
    NativeTextEditorReceipt published;published.after.editor.control="unchanged";
    CHECK(f.native.SyncEngine(f.nativeId,receipt.before.editor.revision,receipt.before.shadowRevision,receipt.after.editor.revision,presentation.text,presentation.anchor,presentation.caret,f.error));
    denyAllocation=true;
    CHECK(f.owner.Current(f.barrier));CHECK(f.owner.PublishSettlement(*prepared,published));
    CHECK(f.owner.Current(published.after));CHECK(!f.owner.PublishSettlement(*prepared,published));
    denyAllocation=false;
    CHECK(published.after==receipt.after && f.View().state.text=="1.75");CHECK(f.View().state.anchor==4 && f.View().state.caret==0);
    const auto oldOwner=f.editor;f.route.inputAllowed=false;f.gui->impl->active=false;f.gui->impl->suspended=true;
    denyAllocation=true;CHECK(UI_NativeTextRetireExact(f.nativeId,oldOwner));CHECK(!UI_NativeTextRetireExact(f.nativeId,oldOwner));denyAllocation=false;
    CHECK(f.View().state.text=="1.75" && !f.View().nativePresentation);
    f.gui->impl->active=true;f.gui->impl->suspended=false;
    auto& input=f.runtime->impl->interaction;
    CHECK(input.UndoNumberEdit(f.editor.control,f.View().identity,false,f.error));CHECK(f.View().state.text=="1");
    CHECK(!input.UndoNumberEdit(f.editor.control,f.View().identity,false,f.error));
}
static void Gates() {
    for(int field=0;field<8;++field) {
        Fixture f;auto stale=f.editor;
        switch(field){case 0:++stale.allocation;break;case 1:++stale.backend;break;case 2:++stale.document;break;case 3:++stale.modal;break;case 4:++stale.window;break;case 5:++stale.session;break;case 6:++stale.revision;break;case 7:stale.control="other";break;}
        NativeTextEditorBarrier out;out.editor.control="untouched";
        CHECK(!f.owner.Attach(stale,f.nativeId,out,f.error));CHECK(out.editor.control=="untouched" && !f.View().nativePresentation);
    }
    for(bool deferred:{false,true}) {
        Fixture f(deferred);f.Attach();const auto original=f.barrier;
        for(int field=0;field<15;++field) {
            auto stale=original;
            switch(field){case 0:++stale.editor.allocation;break;case 1:++stale.editor.backend;break;case 2:++stale.editor.document;break;case 3:++stale.editor.modal;break;case 4:++stale.editor.window;break;case 5:++stale.editor.session;break;case 6:++stale.editor.revision;break;case 7:stale.editor.control="other";break;case 8:++stale.native.editorLease;break;case 9:++stale.native.document;break;case 10:++stale.sequence;break;case 11:++stale.shadowRevision;break;case 12:++stale.group;break;case 13:stale.collectionOpen=true;break;case 14:++stale.collection.dispatch;break;}
            NativeTextEditorView out;out.presentation.text="untouched";
            CHECK(!f.owner.Current(stale));CHECK(!f.owner.Refresh(stale,out,f.error));CHECK(out.presentation.text=="untouched");
            NativeTextEditorBarrier next;next.editor.control="untouched";
            CHECK(!f.owner.Begin(stale,{1,2,3},next,f.error));CHECK(next.editor.control=="untouched");
            NativeTextOffer offer;NativeTextEditorReceipt receipt;receipt.after.editor.control="untouched";
            CHECK(!f.owner.Apply(stale,offer,receipt,f.error));CHECK(receipt.after.editor.control=="untouched");
            CHECK(!f.owner.Complete(stale,{1,2,3},next,f.error));CHECK(!f.owner.PrepareSettlement(stale,f.error));
        }
        for(int flag=0;flag<11;++flag) {
            auto old=f.route;auto& p=*f.gui->impl;
            switch(flag){case 0:f.route.current=nullptr;break;case 1:f.route.current=reinterpret_cast<idUserInterface*>(0x1234);break;case 2:++f.route.window;break;case 3:f.route.inputAllowed=false;break;case 4:p.initialized=false;break;case 5:p.active=false;break;case 6:p.interactive=false;break;case 7:p.suspended=true;break;case 8:p.unavailable=true;break;case 9:p.close=true;break;case 10:f.runtime->impl->document=false;break;}
            denyAllocation=true;CHECK(!f.owner.Current(original));denyAllocation=false;
            f.route=old;p.initialized=true;p.active=true;p.interactive=true;p.suspended=false;p.unavailable=false;p.close=false;f.runtime->impl->document=true;
            CHECK(f.owner.Current(original));
        }
        // AcceptInput is allowed only in refreshing operations; route probes are
        // responsible for the same current engine focus/console policy thereafter.
        windowFocused=false;NativeTextEditorView out;CHECK(!f.owner.Refresh(original,out,f.error));windowFocused=true;
        CHECK(f.gui->impl->quarantines==1);f.gui->impl->suspended=false;
    }
}
static void Replacement() {
    for(bool deferred:{false,true}) for(bool host:{false,true}) for(int kind=0;kind<7;++kind) {
        Fixture f(deferred);f.Attach();NativeTextEditorView out;out.presentation.text="untouched";
        auto callback=[&]{
            switch(kind){case 0:f.route.inputAllowed=false;break;case 1:++f.route.window;break;case 2:f.gui->impl->textDocument=UI_NextTextLifetime();break;case 3:f.gui->impl->textBackend=UI_NextTextLifetime();break;case 4:f.gui->impl->close=true;break;
            case 5:uiManagerLocal.UnregisterGui(f.outer);break;
            case 6:delete f.outer;f.outer=nullptr;f.gui=nullptr;break;}
        };
        if(host)f.runtime->impl->callback=callback;else f.gui->impl->callback=callback;
        CHECK(!f.owner.Refresh(f.barrier,out,f.error));CHECK(out.presentation.text=="untouched");
        CHECK(f.runtime->impl->prepares==(host?1:0)+1); // Attach already queried once.
        f.runtime->impl->callback={};
    }
    Fixture f(true);f.Attach();auto* deferred=static_cast<idUserInterfaceDeferred*>(f.outer);
    auto old=f.barrier;auto oldRuntime=f.runtime;
    f.gui->impl->callback=[&]{CHECK(deferred->InitFromFile("new-number.q4ui"));};
    NativeTextEditorView out;CHECK(!f.owner.Refresh(old,out,f.error));CHECK(oldRuntime->impl->prepares==1);
    CHECK(!f.owner.Current(old));denyAllocation=true;CHECK(!UI_NativeTextRetireExact(old.native,old.editor));denyAllocation=false;
    f.gui=dynamic_cast<idUserInterfaceRetained*>(deferred->backend);CHECK(f.gui);
    // No managed allocation or address is a sufficient native lease by itself.
    Fixture address;address.Attach();auto* oldAddress=address.gui;const auto stale=address.barrier;
    recycleAllocation=true;delete address.outer;address.outer=new idUserInterfaceRetained;address.gui=static_cast<idUserInterfaceRetained*>(address.outer);
    CHECK(address.gui==oldAddress);address.route.current=address.outer;
    CHECK(address.outer->allocationId!=stale.editor.allocation && !address.owner.Current(stale));
    denyAllocation=true;CHECK(!UI_NativeTextRetireExact(stale.native,stale.editor));denyAllocation=false;
    recycleAllocation=false;
}
static void Reentry() {
    for(bool host:{false,true})for(int kind=0;kind<5;++kind) {
        Fixture f;f.Attach();auto callback=[&]{
            switch(kind){case 0:CHECK(!f.owner.Current(f.barrier));break;
            case 1:CHECK(!UI_QueryTextContext(f.outer,17,19).editor);break;
            case 2:{TextBrokerContext context;TextBrokerDelivery delivery;CHECK(UI_DeliverTextInput(f.outer,17,19,context,delivery).outcome==TextDeliveryOutcome::Rejected);break;}
            case 3:{bool close=true;CHECK(UI_DispatchApplicationActions(f.outer,"retained-pending",close) && !close);break;}
            case 4:CHECK(UI_NativeTextRetireExact(f.nativeId,f.editor));break;}
        };
        if(host)f.runtime->impl->callback=callback;else f.gui->impl->callback=callback;
        NativeTextEditorView out;out.presentation.text="untouched";CHECK(!f.owner.Refresh(f.barrier,out,f.error));CHECK(out.presentation.text=="untouched");
        CHECK(!uiManagerLocal.nativeBoundaryActive && !uiManagerLocal.textBoundaryActive && !uiManagerLocal.clipboardBoundaryActive);
        f.gui->impl->callback={};f.runtime->impl->callback={};CHECK(f.owner.Current(f.barrier)==(kind!=4));
    }
    Fixture f;f.Attach();
    f.gui->onTextQuery=[&]{CHECK(!f.owner.Current(f.barrier));};CHECK(!UI_QueryTextContext(f.outer,17,19).editor);f.gui->onTextQuery={};
    f.gui->onDispatch=[&]{CHECK(!f.owner.Current(f.barrier));};f.gui->pendingActions.push_back("dismiss");bool close=true;
    CHECK(UI_DispatchApplicationActions(f.outer,"retained-pending",close) && !close);f.gui->onDispatch={};CHECK(f.owner.Current(f.barrier));
    // Wrong-thread adapter calls stop before touching manager/GUI state.
    const auto prepares=f.gui->impl->prepares;bool refused=false;
    std::thread worker([&]{NativeTextEditorView out;std::string error;refused=!f.owner.Current(f.barrier)&&!f.owner.Refresh(f.barrier,out,error);f.owner.RetireExact(f.nativeId,f.editor);});worker.join();
    CHECK(refused && f.gui->impl->prepares==prepares && f.owner.Current(f.barrier));
}
static void AtomicPublication() {
    for(int kind=0;kind<8;++kind) {
        Fixture f(true);f.Attach();auto prepared=f.Prepare();NativeTextEditorReceipt out;out.after.editor.control="untouched";
        switch(kind){case 0:f.route.inputAllowed=false;break;case 1:f.gui->impl->active=false;break;case 2:++f.gui->impl->textDocument;break;case 3:++f.gui->impl->textBackend;break;case 4:++f.route.window;break;case 5:f.gui->impl->close=true;break;
        case 6:CHECK(UI_NativeTextRetireExact(f.nativeId,f.editor));break;
        case 7:uiManagerLocal.UnregisterGui(f.outer);break;}
        denyAllocation=true;CHECK(!f.owner.PublishSettlement(*prepared,out));denyAllocation=false;
        CHECK(out.after.editor.control=="untouched" && f.View().state.text=="1");
    }
    // Inputs are copied before preparation can mutate the caller's aliases.
    Fixture f;auto original=f.editor;f.gui->impl->callback=[&]{f.editor.control="tampered";f.nativeId={77,88};};
    NativeTextEditorBarrier out;CHECK(f.owner.Attach(f.editor,f.nativeId,out,f.error));CHECK(out.editor==original && out.native==NativeTextIdentity({123,456}));
    f.gui->impl->callback={};
    Fixture unavailable;unavailable.Attach();unavailable.runtime->impl->available=false;
    NativeTextEditorView view;view.presentation.text="untouched";CHECK(!unavailable.owner.Refresh(unavailable.barrier,view,unavailable.error));CHECK(view.presentation.text=="untouched");
    unavailable.runtime->impl->available=true;unavailable.gui->impl->callback=[] {throw std::runtime_error("prepare exception");};
    CHECK(!unavailable.owner.Refresh(unavailable.barrier,view,unavailable.error));CHECK(view.presentation.text=="untouched" && !uiManagerLocal.nativeBoundaryActive);
    unavailable.gui->impl->callback={};CHECK(unavailable.owner.Current(unavailable.barrier));
}

static NativeTextPresence Presence(const Fixture& f) noexcept {return UI_NativeTextPresence(f.nativeId,f.editor);}
static void PresenceQueries() {
    using P=NativeTextPresence;
    for(bool deferred:{false,true}) {
        Fixture f(deferred);
        denyAllocation=true;CHECK(Presence(f)==P::AbsentOriginal);denyAllocation=false;
        f.Attach();const auto stable=f.View();const auto original=f.barrier;
        const auto preparations=f.gui->impl->prepares,host=f.runtime->impl->prepares;
        f.gui->impl->callback=[] {CHECK(false);};f.runtime->impl->callback=[] {CHECK(false);};
        denyAllocation=true;CHECK(Presence(f)==P::PresentExact);denyAllocation=false;
        CHECK(f.gui->impl->prepares==preparations && f.runtime->impl->prepares==host);
        f.gui->impl->callback={};f.runtime->impl->callback={};
        // The presence query has no current-route, geometry or eligibility gate.
        for(int flag=0;flag<15;++flag) {
            auto& p=*f.gui->impl;auto route=f.route;
            switch(flag){case 0:f.route.current=nullptr;break;case 1:++f.route.window;break;case 2:f.route.inputAllowed=false;break;
            case 3:p.initialized=false;break;case 4:p.active=false;break;case 5:p.interactive=false;break;
            case 6:p.suspended=true;break;case 7:p.unavailable=true;break;case 8:p.close=true;break;
            case 9:f.runtime->impl->canonical=false;break;case 10:f.runtime->impl->document=false;break;
            case 11:f.runtime->impl->available=false;break;
            // Exact production BeforeResourceReset ordering advances this scalar
            // while Quarantine(cancelRuntime=false) leaves the old model alive.
            case 12:p.textDocument=UI_NextTextLifetime();break;
            case 13:p.textBackend=UI_NextTextLifetime();break;
            case 14:windowFocused=false;break;}
            denyAllocation=true;CHECK(Presence(f)==P::PresentExact);denyAllocation=false;
            if(flag==12 || flag==13) CHECK(!UI_NativeTextRetireExact(f.nativeId,f.editor));
            f.route=route;p.initialized=true;p.active=true;p.interactive=true;p.suspended=false;p.unavailable=false;p.close=false;
            p.textDocument=f.editor.document;p.textBackend=f.editor.backend;
            f.runtime->impl->canonical=true;f.runtime->impl->document=true;f.runtime->impl->available=true;windowFocused=true;
        }
        // Every immutable identity component matters; editor revision is allowed
        // to progress independently. A changed control cannot conceal the actual
        // stored native model by selecting an empty item first.
        for(int field=0;field<10;++field) {
            auto owner=f.editor;auto native=f.nativeId;
            switch(field){case 0:++owner.backend;break;case 1:++owner.document;break;case 2:++owner.modal;break;
            case 3:++owner.window;break;case 4:++owner.session;break;case 5:owner.control="other";break;
            case 6:++native.document;break;case 7:++native.editorLease;break;case 8:++owner.revision;break;case 9:owner.revision=1;break;}
            denyAllocation=true;CHECK(UI_NativeTextPresence(native,owner)==(field>=8?P::PresentExact:P::AbsentOriginal));denyAllocation=false;
        }
        auto foreignAllocation=f.editor;++foreignAllocation.allocation;
        denyAllocation=true;CHECK(f.runtime->QueryNumberNativePresence(f.nativeId,foreignAllocation)==P::AbsentOriginal);denyAllocation=false;
        auto copy=f.runtime->impl->interaction;
        denyAllocation=true;CHECK(copy.QueryNumberNativePresence(f.nativeId,f.editor)==P::BusyOrUnknown);denyAllocation=false;
        auto savedRuntime=f.gui->impl->runtime;f.gui->impl->runtime.reset();
        denyAllocation=true;CHECK(Presence(f)==P::BusyOrUnknown);denyAllocation=false;
        f.gui->impl->runtime=savedRuntime;
        auto savedImpl=f.runtime->impl;f.runtime->impl.reset();
        denyAllocation=true;CHECK(Presence(f)==P::BusyOrUnknown);denyAllocation=false;
        f.runtime->impl=savedImpl;
        auto prepared=f.Prepare();CHECK(f.barrier.editor.revision!=original.editor.revision);
        denyAllocation=true;CHECK(Presence(f)==P::PresentExact);denyAllocation=false;
        // Presence neither settles an open native group nor changes draft/history.
        CHECK(f.View().state.text==stable.state.text && f.View().nativePresentation->text=="1.75");
        const auto receipt=prepared->Receipt();const auto presentation=prepared->Presentation();
        CHECK(f.native.SyncEngine(f.nativeId,receipt.before.editor.revision,receipt.before.shadowRevision,receipt.after.editor.revision,presentation.text,presentation.anchor,presentation.caret,f.error));
        NativeTextEditorReceipt published;
        denyAllocation=true;CHECK(f.owner.PublishSettlement(*prepared,published));CHECK(Presence(f)==P::PresentExact);
        CHECK(UI_NativeTextRetireExact(f.nativeId,f.editor));CHECK(Presence(f)==P::AbsentOriginal);denyAllocation=false;
        // A fresh native binding does not resurrect the original lease.
        auto current=f.View();auto editor=f.editor;editor.session=current.identity.session;editor.revision=current.identity.revision;
        const NativeTextIdentity replacement{f.nativeId.document+10,f.nativeId.editorLease+10};NativeTextEditorBarrier attached;
        CHECK(f.owner.Attach(editor,replacement,attached,f.error));
        denyAllocation=true;CHECK(Presence(f)==P::AbsentOriginal);CHECK(UI_NativeTextPresence(replacement,editor)==P::PresentExact);denyAllocation=false;
    }
}
static void PresenceBoundaryAndLifetime() {
    using P=NativeTextPresence;
    Fixture f;f.Attach();
    Interaction empty;Interaction emptyCandidate(empty);Interaction moved(std::move(empty));
    denyAllocation=true;CHECK(emptyCandidate.QueryNumberNativePresence(f.nativeId,f.editor)==P::BusyOrUnknown);
    CHECK(empty.QueryNumberNativePresence(f.nativeId,f.editor)==P::BusyOrUnknown);
    CHECK(moved.QueryNumberNativePresence(f.nativeId,f.editor)==P::AbsentOriginal);denyAllocation=false;
    auto* legacy=new idUserInterfaceLocal;auto legacyOwner=f.editor;legacyOwner.allocation=legacy->allocationId;
    denyAllocation=true;CHECK(UI_NativeTextPresence(f.nativeId,legacyOwner)==P::BusyOrUnknown);denyAllocation=false;delete legacy;
    for(int flag=0;flag<4;++flag) {
        switch(flag){case 0:uiManagerLocal.nativeBoundaryActive=true;break;case 1:uiManagerLocal.textBoundaryActive=true;break;
        case 2:uiManagerLocal.clipboardBoundaryActive=true;break;case 3:++uiManagerLocal.applicationPumpDepth;break;}
        denyAllocation=true;CHECK(Presence(f)==P::BusyOrUnknown);denyAllocation=false;
        uiManagerLocal.nativeBoundaryActive=false;uiManagerLocal.textBoundaryActive=false;uiManagerLocal.clipboardBoundaryActive=false;uiManagerLocal.applicationPumpDepth=0;
    }
    // Querying from real native prepare callbacks observes Busy without invoking
    // a probe/host or poisoning the permitted outer operation.
    for(bool host:{false,true}) {
        auto callback=[&]{denyAllocation=true;CHECK(Presence(f)==P::BusyOrUnknown);denyAllocation=false;};
        if(host)f.runtime->impl->callback=callback;else f.gui->impl->callback=callback;
        NativeTextEditorView out;CHECK(f.owner.Refresh(f.barrier,out,f.error));
        f.gui->impl->callback={};f.runtime->impl->callback={};CHECK(Presence(f)==P::PresentExact);
    }
    f.gui->onTextQuery=[&]{denyAllocation=true;CHECK(Presence(f)==P::BusyOrUnknown);denyAllocation=false;};
    CHECK(UI_QueryTextContext(f.outer,17,19).editor);f.gui->onTextQuery={};
    f.gui->onDispatch=[&]{denyAllocation=true;CHECK(Presence(f)==P::BusyOrUnknown);denyAllocation=false;};
    f.gui->pendingActions.push_back("dismiss");bool close=false;CHECK(UI_DispatchApplicationActions(f.outer,"retained-pending",close));f.gui->onDispatch={};
    NativeTextPresence worker=P::PresentExact;std::thread thread([&]{worker=Presence(f);});thread.join();CHECK(worker==P::BusyOrUnknown);
    // An independent manager owns its constructing thread, not a namespace
    // initializer or the global manager's thread. No GUI/backend is installed.
    std::unique_ptr<idUserInterfaceManagerLocal> other;P ownThread=P::BusyOrUnknown;
    std::thread constructor([&]{other=std::make_unique<idUserInterfaceManagerLocal>();other->nextAllocationId=f.editor.allocation;
        ownThread=other->NativeTextPresence(f.nativeId,f.editor);});constructor.join();
    CHECK(ownThread==P::AbsentOriginal);
    denyAllocation=true;CHECK(other->NativeTextPresence(f.nativeId,f.editor)==P::BusyOrUnknown);denyAllocation=false;

    for(int field=0;field<13;++field) {
        auto owner=f.editor;auto native=f.nativeId;
        switch(field){case 0:owner.allocation=0;break;case 1:owner.allocation=uiManagerLocal.nextAllocationId+1;break;
        case 2:owner.backend=0;break;case 3:owner.document=0;break;case 4:owner.modal=0;break;case 5:owner.window=0;break;
        case 6:owner.session=0;break;case 7:owner.revision=0;break;case 8:owner.control.clear();break;
        case 9:owner.control.assign(129,'x');break;case 10:owner.control.assign("a\0b",3);break;case 11:native.document=0;break;case 12:native.editorLease=0;break;}
        denyAllocation=true;CHECK(UI_NativeTextPresence(native,owner)==P::BusyOrUnknown);denyAllocation=false;
    }
    auto* unloaded=new idUserInterfaceDeferred;auto unloadedOwner=f.editor;unloadedOwner.allocation=unloaded->allocationId;
    denyAllocation=true;CHECK(UI_NativeTextPresence(f.nativeId,unloadedOwner)==P::AbsentOriginal);denyAllocation=false;CHECK(!unloaded->backend);delete unloaded;
    Fixture old;old.Attach();auto native=old.nativeId;auto owner=old.editor;
    auto* address=old.outer;recycleAllocation=true;delete old.outer;old.outer=new idUserInterfaceRetained;
    CHECK(old.outer==address && old.outer->allocationId!=owner.allocation);old.gui=nullptr;
    denyAllocation=true;CHECK(UI_NativeTextPresence(native,owner)==P::AbsentOriginal);denyAllocation=false;recycleAllocation=false;
    // A loaded deferred backend replacement destroys its original binding. The
    // new empty Runtime proves absence; no original current-route test is used.
    Fixture replaced(true);replaced.Attach();auto* deferred=static_cast<idUserInterfaceDeferred*>(replaced.outer);
    unsigned changingCallbacks=0;
    onBackendDestroy=[&]{
        ++changingCallbacks;
        CHECK(deferred->nativeInputChanging);
        CHECK(Presence(replaced)==P::BusyOrUnknown);
        CHECK(!replaced.owner.Current(replaced.barrier));
        CHECK(!UI_NativeTextRetireExact(replaced.nativeId,replaced.editor));
    };
    CHECK(deferred->InitFromFile("replacement.q4ui"));
    CHECK(changingCallbacks==1 && !deferred->nativeInputChanging);
    Fixture closing;closing.Attach();closing.gui->MarkNativeInputClosing();
    CHECK(Presence(closing)==P::BusyOrUnknown);
    CHECK(!UI_NativeTextRetireExact(closing.nativeId,closing.editor));
    CHECK(closing.View().nativePresentation.has_value());
    denyAllocation=true;CHECK(Presence(replaced)==P::AbsentOriginal);denyAllocation=false;
}
int main() {
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_WARN,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_WARN,_CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ERROR,_CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    Happy(false);Happy(true);Gates();Replacement();Reentry();AtomicPublication();PresenceQueries();PresenceBoundaryAndLifetime();
    CHECK(uiManagerLocal.allocations.Num()==0);
    std::printf("Managed native owner: %u checks passed; actual manager/deferred/retained/Runtime methods and editor models; counted host, no SDL/COM/Session activation.\n",checks);
}
