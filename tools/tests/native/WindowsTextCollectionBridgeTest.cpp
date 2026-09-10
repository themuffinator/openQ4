// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Actual Windows SDK store/model plus counted SDL provider functions. No HWND,
// COM activation, SDL initialization, message pumping or user input is performed.
#include "sys/sdl3/WindowsTextCollectionBridge.h"
#include <SDL3/SDL_init.h>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <thread>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
using namespace openq4::sys;
using namespace openq4::ui;
static unsigned checks=0;
static bool denyAllocations=false;
static int failAllocation=-1;
void* operator new(std::size_t n){if(denyAllocations || failAllocation==0)throw std::bad_alloc();if(failAllocation>0)--failAllocation;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);std::exit(1);}}while(false)
#define OK(x) CHECK((x)==S_OK)
static const auto engineThread=std::this_thread::get_id();
static bool enabled=false,providerHealthy=true;
static Uint64 generation=0;
static unsigned marks=0,providerCalls=0;
static std::optional<OQ4_NativeCollectionContext> providerContext;
static std::function<bool()> markAction;
static WindowsTextStore* tripStore=nullptr;
static NativeTextIdentity tripIdentity;
static std::uint64_t tripRevision=0;
extern "C" bool SDLCALL SDL_IsMainThread(){++providerCalls;return std::this_thread::get_id()==engineThread;}
extern "C" bool SDLCALL OQ4_WindowsNativeFenceHealthy(){
    ++providerCalls;
    if(tripStore){WindowsTextLifecycle life;if(tripStore->QueryLifecycle(tripIdentity,life)==S_OK && life.engineRevision==tripRevision)denyAllocations=true;}
    return enabled && providerHealthy;
}
extern "C" Uint64 SDLCALL OQ4_WindowsNativeFenceQueueGeneration(){++providerCalls;return enabled && providerHealthy?generation:0;}
extern "C" bool SDLCALL OQ4_WindowsNativeFenceMarkActivity(const OQ4_NativeCollectionContext* c){
    ++providerCalls;++marks;
    if(!c || !providerContext || !enabled || !providerHealthy || c->version!=1 || c->generation!=generation ||
        c->kind!=providerContext->kind || c->dispatch!=providerContext->dispatch)return false;
    return markAction?markAction():true;
}
struct Sink final:ITextStoreACPSink {
    ULONG refs=1;unsigned calls=0,notices=0;
    std::function<HRESULT(DWORD)> lock;
    std::function<HRESULT()> notice;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(ITextStoreACPSink))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{CHECK(refs>1);return --refs;}
    HRESULT STDMETHODCALLTYPE OnLockGranted(DWORD flags)override{++calls;return lock?lock(flags):S_OK;}
    HRESULT STDMETHODCALLTYPE OnTextChange(DWORD,const TS_TEXTCHANGE*)override{++notices;return notice?notice():S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange()override{++notices;return notice?notice():S_OK;}
    HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode,TsViewCookie)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnAttrsChange(LONG,LONG,ULONG,const TS_ATTRID*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStartEditTransaction()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnEndEditTransaction()override{return S_OK;}
};
template<class T> struct Counted : T {
	ULONG refs=1;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
		if(!out) return E_POINTER;*out=nullptr;
		if(iid!=__uuidof(IUnknown) && iid!=__uuidof(T)) return E_NOINTERFACE;
		*out=static_cast<T*>(this);AddRef();return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
	ULONG STDMETHODCALLTYPE Release() override{CHECK(refs>1);return --refs;}
};
struct Context : Counted<ITfContext> {
	HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId,ITfEditSession*,DWORD,HRESULT*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE InWriteSession(TfClientId,BOOL*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetSelection(TfEditCookie,ULONG,ULONG,TF_SELECTION*,ULONG*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE SetSelection(TfEditCookie,ULONG,const TF_SELECTION*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetStart(TfEditCookie,ITfRange**) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetEnd(TfEditCookie,ITfRange**) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetActiveView(ITfContextView**) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE EnumViews(IEnumTfContextViews**) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetStatus(TF_STATUS*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetProperty(REFGUID,ITfProperty**) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetAppProperty(REFGUID,ITfReadOnlyProperty**) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE TrackProperties(const GUID**,ULONG,const GUID**,ULONG,ITfReadOnlyProperty**) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE EnumProperties(IEnumTfProperties**) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr**) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE CreateRangeBackup(TfEditCookie,ITfRange*,ITfRangeBackup**) override{return E_NOTIMPL;}
};
struct EditRecord : Counted<ITfEditRecord> {
	HRESULT STDMETHODCALLTYPE GetSelectionStatus(BOOL*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetTextAndPropertyUpdates(DWORD,const GUID**,ULONG,IEnumTfRanges**) override{return E_NOTIMPL;}
};
struct Range : Counted<ITfRangeACP> {
	Context* context=nullptr; LONG start=0,length=0;
	std::function<void()> reenter;
	std::function<void()> releasing;
	unsigned extentCalls=0;HRESULT extentResult=S_OK;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
		if(iid==__uuidof(ITfRange)) {if(!out)return E_POINTER;*out=static_cast<ITfRange*>(this);AddRef();return S_OK;}
		return Counted::QueryInterface(iid,out);
	}
	ULONG STDMETHODCALLTYPE Release() override{const auto result=Counted::Release();if(releasing)releasing();return result;}
	HRESULT STDMETHODCALLTYPE GetExtent(LONG* first,LONG* count) override{++extentCalls;if(reenter) reenter();*first=start;*count=length;return extentResult;}
	HRESULT STDMETHODCALLTYPE SetExtent(LONG first,LONG count) override{start=first;length=count;return S_OK;}
	HRESULT STDMETHODCALLTYPE GetContext(ITfContext** out) override{if(!context)return E_FAIL;context->AddRef();*out=context;return S_OK;}
	HRESULT STDMETHODCALLTYPE GetText(TfEditCookie,DWORD,WCHAR*,ULONG,ULONG*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE SetText(TfEditCookie,DWORD,const WCHAR*,LONG) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetFormattedText(TfEditCookie,IDataObject**) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetEmbedded(TfEditCookie,REFGUID,REFIID,IUnknown**) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE InsertEmbedded(TfEditCookie,DWORD,IDataObject*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE ShiftStart(TfEditCookie,LONG,LONG*,const TF_HALTCOND*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE ShiftEnd(TfEditCookie,LONG,LONG*,const TF_HALTCOND*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE ShiftStartToRange(TfEditCookie,ITfRange*,TfAnchor) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE ShiftEndToRange(TfEditCookie,ITfRange*,TfAnchor) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE ShiftStartRegion(TfEditCookie,TfShiftDir,BOOL*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE ShiftEndRegion(TfEditCookie,TfShiftDir,BOOL*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE IsEmpty(TfEditCookie,BOOL*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE Collapse(TfEditCookie,TfAnchor) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE IsEqualStart(TfEditCookie,ITfRange*,TfAnchor,BOOL*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE IsEqualEnd(TfEditCookie,ITfRange*,TfAnchor,BOOL*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE CompareStart(TfEditCookie,ITfRange*,TfAnchor,LONG*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE CompareEnd(TfEditCookie,ITfRange*,TfAnchor,LONG*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE AdjustForInsert(TfEditCookie,ULONG,BOOL*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetGravity(TfGravity*,TfGravity*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE SetGravity(TfEditCookie,TfGravity,TfGravity) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE Clone(ITfRange**) override{return E_NOTIMPL;}
};
struct Composition final : Counted<ITfCompositionView> {
	Range range;
	unsigned rangeCalls=0;HRESULT rangeResult=S_OK;
	HRESULT STDMETHODCALLTYPE GetOwnerClsid(CLSID*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetRange(ITfRange** out) override{++rangeCalls;if(FAILED(rangeResult))return rangeResult;range.AddRef();*out=&range;return S_OK;}
};
struct Fixture {
    NativeTextEditorBarrier current{{101,202},{11,22,33,44,55,66,10,std::string(180,'n')},0,1,0,{},false};
    WindowsTextStore* store=nullptr;
    Sink sink;
    std::unique_ptr<WindowsTextCollectionBridge> bridge;
    OQ4_NativeCollectionHooks hooks{};
    OQ4_NativeCollectionContext context{};
    std::string error;
    Fixture(bool bind=true){
        enabled=false;providerHealthy=true;generation=0;marks=0;providerCalls=0;providerContext.reset();markAction={};
        OK(WindowsTextStore::Create(current.native,10,"1",1,1,reinterpret_cast<HWND>(1),&store));
        OK(store->AdviseSink(__uuidof(ITextStoreACPSink),&sink,TS_AS_ALL_SINKS));
        CHECK(WindowsTextCollectionBridge::Create(*store,current,9,bridge,error));hooks=bridge->Hooks();CHECK(hooks.version==1&&hooks.userdata==bridge.get());
        if(bind)Bind();
    }
    void Bind(){enabled=true;generation=71;CHECK(bridge->BindProviderGeneration(9,71,error));}
    ~Fixture(){denyAllocations=false;failAllocation=-1;tripStore=nullptr;markAction={};providerContext.reset();enabled=false;
        bridge.reset();OK(store->UnadviseSink(&sink));CHECK(store->Release()==0);CHECK(sink.refs==1);}
    bool Begin(Uint64 dispatch=1,Uint32 kind=OQ4_COLLECTION_PUMP){context={1,kind,generation,dispatch};providerContext=context;return hooks.Prepare(hooks.userdata,&context);}
    bool End(bool aborted=false){const bool result=hooks.Finish(hooks.userdata,&context,aborted);providerContext.reset();return result;}
    WindowsTextLifecycle Life(){WindowsTextLifecycle l;OK(store->QueryLifecycle(current.native,l));return l;}
    void Edit(const wchar_t* value,ULONG count){
        sink.lock=[&](DWORD flags){CHECK((flags&TS_LF_READWRITE)==TS_LF_READWRITE);LONG last;OK(store->GetEndACP(&last));TS_TEXTCHANGE change;return store->SetText(0,0,last,value,count,&change);};
        HRESULT session=E_FAIL;OK(store->RequestLock(TS_LF_READWRITE|TS_LF_SYNC,&session));OK(session);sink.lock={};
    }
    void Read(){sink.lock=[&](DWORD){LONG end;OK(store->GetEndACP(&end));return S_OK;};HRESULT result=E_FAIL;OK(store->RequestLock(TS_LF_READ|TS_LF_SYNC,&result));OK(result);sink.lock={};}
    NativeClosedTextCollection Seal(){NativeClosedTextCollection seal;CHECK(bridge->Closed(current,context.dispatch,seal,error));return seal;}
    NativeTextOffer Offer(){NativeTextOffer offer;CHECK(bridge->Peek(current.native,offer,error));return offer;}
    NativeTextEditorReceipt Ack(const NativeTextOffer& offer){NativeTextEditorReceipt r;r.before=current;r.after=current;r.after.sequence=offer.transaction.sequence;r.after.shadowRevision=offer.transaction.shadowAfter;++r.after.editor.revision;
        CHECK(bridge->Acknowledge(r,error));current=r.after;return r;}
};
static void RegistrationAndEmpty(){
    {Fixture f(false);CHECK(!f.bridge->Healthy());CHECK(!f.bridge->BindProviderGeneration(9,71,f.error));f.Bind();CHECK(!f.bridge->BindProviderGeneration(9,71,f.error));CHECK(f.bridge->Healthy());
        CHECK(f.Begin());CHECK(f.Life().collectionOpen);CHECK(f.End());const auto seal=f.Seal();CHECK(seal.pending.count==0&&seal.admittedCallbacks==0&&marks==0);CHECK(f.bridge->StillClosed(seal));
        WindowsTextCollectionReceipt copy;CHECK(f.bridge->CopyClosed(copy));CHECK(copy.collection.dispatch==1);denyAllocations=true;CHECK(f.bridge->StillClosed(seal));CHECK(f.bridge->CopyClosed(copy));denyAllocations=false;
        CHECK(f.Begin(2));CHECK(!f.bridge->StillClosed(seal));f.Read();CHECK(f.End());CHECK(marks==1);const auto second=f.Seal();CHECK(second.admittedCallbacks==1&&second.pending.count==0);CHECK(!f.bridge->StillClosed(seal));
        NativeTextPendingSnapshot pending;CHECK(f.bridge->Pending(f.current,2,pending,f.error));CHECK(pending.count==0&&pending.lastSequence==0);}
    {Fixture f(false);enabled=true;generation=71;CHECK(!f.bridge->BindProviderGeneration(8,71,f.error));CHECK(!f.bridge->BindProviderGeneration(9,72,f.error));CHECK(f.bridge->BindProviderGeneration(9,71,f.error));}
    {Fixture f(false);generation=71;enabled=true;CHECK(!f.Begin());CHECK(!f.End(true));CHECK(!f.bridge->Healthy());CHECK(f.store->Healthy());}
    {Fixture f;for(int field=0;field<5;++field){auto stale=f.current;std::unique_ptr<WindowsTextCollectionBridge> other;
        if(field==0)stale.group=1;if(field==1)stale.collection.dispatch=1;if(field==2)stale.sequence=1;if(field==3)stale.shadowRevision=2;if(field==4)stale.collectionOpen=true;
        CHECK(!WindowsTextCollectionBridge::Create(*f.store,stale,9,other,f.error));CHECK(!other&&f.store->Healthy());}
        CHECK(f.Begin());CHECK(f.End());std::unique_ptr<WindowsTextCollectionBridge> other;CHECK(!WindowsTextCollectionBridge::Create(*f.store,f.current,9,other,f.error));CHECK(!other&&f.store->Healthy());}
}
static void ExactContextsAndCleanup(){
    for(int field=0;field<4;++field){Fixture f;f.context={1,OQ4_COLLECTION_PUMP,71,1};if(field==0)f.context.version=2;if(field==1)f.context.kind=99;if(field==2)f.context.generation=72;if(field==3)f.context.dispatch=0;
        providerContext=f.context;CHECK(!f.hooks.Prepare(f.hooks.userdata,&f.context));CHECK(!f.End(true));CHECK(!f.Life().collectionOpen);}
    {Fixture f;WindowsTextCollection previous;OK(f.store->OpenCollection(f.current.native,10,0,1,8,WindowsTextCollectionKind::Pump,previous));
        CHECK(!f.Begin(9));CHECK(!f.End(true));const auto life=f.Life();CHECK(life.healthy&&life.collectionOpen&&life.collection==previous);OK(f.store->AbortCollection(previous));}
    {Fixture f;CHECK(f.Begin());const auto original=f.context;auto newer=original;++newer.dispatch;CHECK(!f.hooks.Prepare(f.hooks.userdata,&newer));
        CHECK((f.Life().collection==WindowsTextCollection{f.current.native,1,1,WindowsTextCollectionKind::Pump}));
        CHECK(!f.hooks.Finish(f.hooks.userdata,&newer,true));CHECK(f.Life().collectionOpen);CHECK(!f.End(true));CHECK(!f.Life().healthy);}
    {Fixture f;CHECK(f.Begin());auto bad=f.context;++bad.generation;CHECK(!f.hooks.Finish(f.hooks.userdata,&bad,false));CHECK(f.Life().collectionOpen);CHECK(!f.End());CHECK(!f.store->Healthy());}
    {Fixture f;CHECK(f.Begin());CHECK(!f.End(true));CHECK(!f.bridge->Healthy()&&!f.store->Healthy());}
    {Fixture f;CHECK(f.Begin());f.bridge->RetireExact({999,999});CHECK(f.store->Healthy());denyAllocations=true;f.bridge->RetireExact(f.current.native);CHECK(!f.End(true));denyAllocations=false;CHECK(!f.store->Healthy());}
    {Fixture f;CHECK(f.Begin());++generation;CHECK(!f.End());CHECK(!f.store->Healthy());}
    {Fixture f;CHECK(f.Begin());CHECK(f.End());CHECK(!f.Begin(1));CHECK(!f.End(true));}
}
static void ActivityAndReentry(){
    for(int mode=0;mode<3;++mode){Fixture f;CHECK(f.Begin());f.Read();markAction=[&]{
            if(mode==0)return false;
            if(mode==1){auto context=f.context;++context.dispatch;CHECK(!f.hooks.Prepare(f.hooks.userdata,&context));return true;}
            f.bridge->RetireExact(f.current.native);return true;};
        CHECK(!f.End());CHECK(marks==1&&!f.store->Healthy());WindowsTextCollectionReceipt out;out.admittedCallbacks=77;CHECK(!f.bridge->CopyClosed(out));CHECK(out.admittedCallbacks==77);}
    {Fixture f;CHECK(f.Begin());f.sink.lock=[&](DWORD){WindowsTextCollectionReceipt out;CHECK(!f.bridge->CopyClosed(out));CHECK(!f.hooks.Prepare(f.hooks.userdata,&f.context));return S_OK;};
        HRESULT session;OK(f.store->RequestLock(TS_LF_READ|TS_LF_SYNC,&session));OK(session);CHECK(!f.End());CHECK(!f.store->Healthy());}
    {Fixture f;CHECK(f.Begin());CHECK(f.End());auto seal=f.Seal();bool success=true;const auto calls=providerCalls;
        std::thread worker([&]{success=f.bridge->StillClosed(seal)||f.bridge->Healthy();NativeTextPendingSnapshot p;CHECK(!f.bridge->Pending(f.current,1,p,f.error));f.bridge->RetireExact(f.current.native);});worker.join();CHECK(!success&&providerCalls==calls);CHECK(f.bridge->StillClosed(seal));}
}
static void FifoAndSettlement(){
    Fixture f;CHECK(f.Begin());f.Edit(L"15",2);f.Edit(L"150",3);CHECK(f.End());const auto original=f.Seal();CHECK(original.pending.count==2&&original.admittedCallbacks==2);
    auto first=f.Offer();CHECK(first.transaction.after.text=="15"&&first.expectedEngineRevision==10);auto copy=first;
    NativeTextEditorReceipt wrong;wrong.before=f.current;wrong.after=f.current;wrong.after.sequence=1;wrong.after.shadowRevision=2;wrong.after.editor.revision=11;wrong.after.editor.control="other";
    CHECK(!f.bridge->Acknowledge(wrong,f.error));CHECK(f.Life().pending==2);f.Ack(first);CHECK(f.bridge->StillClosed(original));CHECK(copy==first);
    NativeTextPendingSnapshot pending;CHECK(f.bridge->Pending(f.current,1,pending,f.error));CHECK(pending.count==1&&pending.engineRevision==11);
    auto second=f.Offer();CHECK(second.transaction.after.text=="150"&&second.expectedEngineRevision==11);f.Ack(second);CHECK(f.bridge->StillClosed(original));
    NativeTextEditorReceipt settle;settle.effect=NativeTextEditorEffect::SyncEngine;settle.before=f.current;settle.after=f.current;++settle.after.editor.revision;
    tripStore=f.store;tripIdentity=f.current.native;tripRevision=settle.after.editor.revision;
    CHECK(f.bridge->Sync(settle,second.transaction.after,f.error));CHECK(denyAllocations);CHECK(f.bridge->StillClosed(original));denyAllocations=false;tripStore=nullptr;f.current=settle.after;
    CHECK(f.bridge->Pending(f.current,1,pending,f.error));CHECK(pending.count==0&&pending.engineRevision==13&&pending.shadowRevision==3);CHECK(f.sink.notices==0);
    auto stale=f.current;--stale.editor.revision;auto saved=pending;CHECK(!f.bridge->Pending(stale,1,pending,f.error));CHECK(pending==saved);
    CHECK(f.Begin(2));CHECK(f.End());CHECK(f.Seal().pending.engineRevision==13);
}
static void LifecycleAndSyncFaults(){
    {Fixture f;CHECK(f.Begin(1,OQ4_COLLECTION_LIFECYCLE));OK(f.store->SyncEngine(f.current.native,10,1,11,"1",1,1));CHECK(!f.End());CHECK(!f.store->Healthy());}
    {Fixture f;CHECK(f.Begin(1,OQ4_COLLECTION_LIFECYCLE));f.sink.notice=[&]{HRESULT write=0;OK(f.store->RequestLock(TS_LF_READWRITE|TS_LF_SYNC,&write));CHECK(write==TS_E_SYNCHRONOUS);return S_OK;};
        OK(f.store->SyncEngine(f.current.native,10,1,11,"22",2,2));CHECK(!f.End());CHECK(!f.bridge->Healthy()&&!f.store->Healthy());CHECK(f.current.editor.revision==10&&f.current.shadowRevision==1);CHECK(f.sink.notices==2);}
    {Fixture f;CHECK(f.Begin(1,OQ4_COLLECTION_LIFECYCLE));f.Read();CHECK(f.End());const auto seal=f.Seal();CHECK(seal.kind==NativeClosedCollectionKind::Lifecycle&&seal.pending.count==0&&seal.admittedCallbacks==1);}
    {Fixture f;CHECK(f.Begin());CHECK(f.End());auto seal=f.Seal();NativeTextEditorReceipt sync;sync.effect=NativeTextEditorEffect::SyncEngine;sync.before=f.current;sync.after=f.current;++sync.after.editor.revision;
        NativeTextSnapshot wrong{"22",2,2,{}};f.sink.notice=[&]{NativeTextPendingSnapshot p;CHECK(!f.bridge->Pending(f.current,1,p,f.error));return S_OK;};
        CHECK(!f.bridge->Sync(sync,wrong,f.error));CHECK(!f.store->Healthy());CHECK(!f.bridge->StillClosed(seal));}
    {Fixture f;CHECK(f.Begin());CHECK(f.End());NativeTextEditorReceipt sync;sync.effect=NativeTextEditorEffect::SyncEngine;sync.before=f.current;sync.after=f.current;++sync.after.editor.revision;
        NativeTextSnapshot wrong{"22",2,2,{}};CHECK(!f.bridge->Sync(sync,wrong,f.error));CHECK(!f.store->Healthy());}
}
static void AllocationFailures(){
    for(int fail=0;fail<4;++fail){Fixture f;std::unique_ptr<WindowsTextCollectionBridge> out;failAllocation=fail;
        std::fprintf(stderr,"Create fail index %d\n",fail);
        const bool created=WindowsTextCollectionBridge::Create(*f.store,f.current,9,out,f.error);failAllocation=-1;
        if(!created){CHECK(!out);CHECK(f.store->Healthy());}else out.reset();}
    {Fixture f;CHECK(f.Begin());f.Edit(L"12",2);CHECK(f.End());NativeTextOffer out;out.expectedEngineRevision=999;std::fprintf(stderr,"Peek fail\n");denyAllocations=true;CHECK(!f.bridge->Peek(f.current.native,out,f.error));denyAllocations=false;CHECK(out.expectedEngineRevision==999);CHECK(!f.store->Healthy());}
    {Fixture f;CHECK(f.Begin());std::fprintf(stderr,"Destroy noalloc\n");denyAllocations=true;f.bridge.reset();denyAllocations=false;CHECK(!f.store->Healthy());}
}
static void MetadataAndStaleSeals(){
    {Context context;Composition composition;Fixture f;OK(f.store->BindContext(f.current.native,&context));composition.range.context=&context;composition.range.length=1;
        CHECK(f.Begin());f.Edit(L"12",2);BOOL accepted=FALSE;OK(f.store->OnStartComposition(&composition,&accepted));CHECK(accepted);
        Range updated;updated.context=&context;updated.length=2;OK(f.store->OnUpdateComposition(&composition,&updated));
        const auto rangeCalls=composition.rangeCalls;OK(f.store->OnEndComposition(&composition));CHECK(composition.rangeCalls==rangeCalls);
        f.Edit(L"123",3);CHECK(f.End());const auto original=f.Seal();CHECK(original.pending.count==5&&original.admittedCallbacks==5&&marks==1);
        for(unsigned i=0;i<5;++i){const auto offer=f.Offer();CHECK(offer.transaction.sequence==i+1&&offer.transaction.nativeDispatch==1);
            CHECK(offer.transaction.classification==((i==0||i==4)?NativeTextClassification::Unclassified:NativeTextClassification::CompositionRelated));f.Ack(offer);CHECK(f.bridge->StillClosed(original));}
        CHECK(f.Life().retainedCompositions==1&&f.Life().liveCompositions==0);}
    {Context context;Composition first,second;Fixture f;OK(f.store->BindContext(f.current.native,&context));first.range.context=second.range.context=&context;
        first.range.length=1;second.range.start=1;second.range.length=0;CHECK(f.Begin());BOOL accepted=FALSE;
        OK(f.store->OnStartComposition(&first,&accepted));CHECK(accepted);OK(f.store->OnStartComposition(&second,&accepted));CHECK(accepted);CHECK(f.End());
        auto one=f.Offer();CHECK(one.transaction.after.compositions.size()==1);f.Ack(one);auto two=f.Offer();CHECK(two.transaction.after.compositions.size()==2);f.Ack(two);
        CHECK(f.Begin(2));OK(f.store->OnEndComposition(&first));OK(f.store->OnEndComposition(&second));CHECK(f.End());CHECK(f.Seal().pending.count==2);}
    {Fixture f;CHECK(f.Begin());CHECK(f.End());const auto original=f.Seal();
        for(int field=0;field<7;++field){auto bad=original;if(field==0)++bad.identity.document;if(field==1)++bad.serial;if(field==2)++bad.dispatch;
            if(field==3)bad.kind=NativeClosedCollectionKind::Lifecycle;if(field==4)++bad.pending.engineRevision;if(field==5)++bad.pending.count;if(field==6)++bad.admittedCallbacks;
            CHECK(!f.bridge->StillClosed(bad));CHECK(f.bridge->StillClosed(original));}
        WindowsTextCollection other;WindowsTextCollectionReceipt closed;OK(f.store->OpenCollection(f.current.native,10,0,1,2,WindowsTextCollectionKind::Pump,other));
        CHECK(!f.bridge->StillClosed(original));OK(f.store->CloseCollection(other,closed));CHECK(!f.bridge->StillClosed(original));}
    {Fixture f;CHECK(f.Begin());f.Read();markAction=[] {++generation;return true;};CHECK(!f.End());CHECK(!f.store->Healthy());}
}
int main(){
#if defined(_MSC_VER)
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
#if !defined(USE_SDL3) || !defined(OPENQ4_SDL3_CHECKED_NATIVE_QUEUE) || !OPENQ4_SDL3_CHECKED_NATIVE_QUEUE
    WindowsTextStore* store=nullptr;NativeTextEditorBarrier current{{101,202},{11,22,33,44,55,66,10,"number"},0,1,0,{},false};
    OK(WindowsTextStore::Create(current.native,10,"1",1,1,reinterpret_cast<HWND>(1),&store));
    std::unique_ptr<WindowsTextCollectionBridge> out;std::string error;
    CHECK(!WindowsTextCollectionBridge::Create(*store,current,9,out,error));CHECK(!out&&providerCalls==0&&store->Healthy());CHECK(store->Release()==0);
#else
    std::fprintf(stderr,"RegistrationAndEmpty\n");RegistrationAndEmpty();
    std::fprintf(stderr,"ExactContextsAndCleanup\n");ExactContextsAndCleanup();
    std::fprintf(stderr,"ActivityAndReentry\n");ActivityAndReentry();
    std::fprintf(stderr,"FifoAndSettlement\n");FifoAndSettlement();
    std::fprintf(stderr,"LifecycleAndSyncFaults\n");LifecycleAndSyncFaults();
    std::fprintf(stderr,"AllocationFailures\n");AllocationFailures();
    std::fprintf(stderr,"MetadataAndStaleSeals\n");MetadataAndStaleSeals();
#endif
    std::printf("PASS %u checks\n",checks);}
