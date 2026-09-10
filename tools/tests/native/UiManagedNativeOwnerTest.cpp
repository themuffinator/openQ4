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
int main() {
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_WARN,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_WARN,_CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ERROR,_CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    Happy(false);Happy(true);Gates();Replacement();Reentry();AtomicPublication();
    CHECK(uiManagerLocal.allocations.Num()==0);
    std::printf("Managed native owner: %u checks passed; actual manager/deferred/retained/Runtime methods and editor models; counted host, no SDL/COM/Session activation.\n",checks);
}
