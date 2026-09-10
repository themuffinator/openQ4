// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Actual SDK controller/store/bridge/coordinator/Interaction and owned ingress.
// Counted COM factory and SDL provider only: no activation, windows, or input.
#include "sys/sdl3/WindowsTextSession.h"
#include <SDL3/SDL_init.h>
#include <array>
#include <deque>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <new>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
using namespace openq4;
using namespace openq4::ui;
using namespace openq4::sys;
static unsigned checks=0;
static unsigned requestEditSessions=0;
static bool denyAllocations=false;
void* operator new(std::size_t n){if(denyAllocations){std::fputs("FAIL emergency engine allocation\n",stderr);std::exit(1);}if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);std::exit(1);}}while(false)
#define OK(x) CHECK((x)==S_OK)
static const auto engineThread=std::this_thread::get_id();
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
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override;
	HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId,ITfEditSession*,DWORD,HRESULT*) override{++requestEditSessions;return E_NOTIMPL;}
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

struct Editor {
	DocumentModel model; Interaction input; std::string error;
	std::map<std::string,ControlReadback> values; std::map<std::string,ControlBounds> bounds;
	NativeTextIdentity nativeId{100,200}; NativeTextDocument native;
	NativeTextEditorBarrier barrier; std::uint64_t dispatch=10,fence=20;
	Editor() {
		model.id="numbers";model.root.id="root";
		for(const auto* id:{"n0","n1"}) {
			Node n;n.id=id;Control c;c.role=ControlRole::Number;c.action="edit";Expression e;e.type=0;c.value=e;
			NumberSpec spec;spec.minimum=.5;spec.maximum=2;spec.maxBytes=2048;c.widget=spec;
			for(auto s:{ControlState::Default,ControlState::Hover,ControlState::Focus,ControlState::Pressed,ControlState::Disabled})c.states[s]="feedback";
			n.control=c;model.root.children.push_back(n);values[id]={1.0,false,{}};bounds[id]={0,0,100,40,true};
		}
		input.Reset(model);CHECK(input.SetReadbacks(values,error));input.SetBounds(bounds);
		CHECK(input.Focus("n0") && input.BeginNumberEdit("n0",error));
	}
	NumberEditView View() const { const auto v=input.Widget("n0");CHECK(v && v->number);return *v->number; }
	NumberDraftSummary Drafts() const {NumberDraftSummary d;std::string e;CHECK(input.QueryNumberDrafts(d,e));return d;}
	void Text(const std::string& text) {CHECK(input.ReplaceNumberSelection("n0",View().identity,text,error));}
	void Attach() {
		const auto v=View();TextEditorIdentity owner{1,2,3,input.ModalToken(),5,v.identity.session,v.identity.revision,"n0"};
		CHECK(input.AttachNumberNative("n0",v.identity,owner,nativeId,barrier,error));
		CHECK(native.Open(nativeId,owner.revision,v.state.text,v.state.anchor,v.state.caret,error));
	}
	NativeTextOffer Offer(const std::function<void(const NativeTextLockScope&)>& operations) {
		NativeTextLockScope lock;CHECK(native.RequestLock(nativeId,NativeTextAccess::ReadWrite,true,lock,error)==NativeTextLockResult::Granted);
		operations(lock);std::uint64_t seq=0;CHECK(native.FinishLock(lock,dispatch,seq,error) && seq);
		NativeTextOffer offer;CHECK(native.PeekOffer(nativeId,offer,error));return offer;
	}
	void Begin(std::uint64_t last) {
		NativeTextEditorBarrier out;CHECK(input.BeginNumberNativeCollection(barrier,{dispatch,fence,last},out,error));barrier=out;
	}
	void Receipt(const NativeTextEditorReceipt& out) {
		if(out.effect==NativeTextEditorEffect::Acknowledge)
			CHECK(native.Acknowledge(nativeId,out.after.sequence,out.after.shadowRevision,out.before.editor.revision,out.after.editor.revision,error));
		else if(out.effect==NativeTextEditorEffect::SyncEngine) {
			const auto p=*View().nativePresentation;
			CHECK(native.SyncEngine(nativeId,out.before.editor.revision,out.before.shadowRevision,out.after.editor.revision,p.text,p.anchor,p.caret,error));
		} else CHECK(native.Retire(nativeId,error));
		barrier=out.after;
	}
	void Apply(const NativeTextOffer& offer) { NativeTextEditorReceipt out;CHECK(input.ApplyNumberNative(barrier,offer,out,error));Receipt(out); }
	void Complete() {NativeTextEditorBarrier out;CHECK(input.CompleteNumberNativeCollection(barrier,barrier.collection,out,error));barrier=out;}
	void Settle() {NativeTextEditorReceipt out;CHECK(input.SettleNumberNative(barrier,out,error));CHECK(out.effect==NativeTextEditorEffect::SyncEngine);Receipt(out);}
	void Retire() {NativeTextEditorReceipt out;CHECK(input.RetireNumberNative(barrier,out,error));CHECK(out.effect==NativeTextEditorEffect::Retire);Receipt(out);}
	ValueWidgetSnapshot Save() const {ValueWidgetSnapshot s;std::string e;CHECK(input.CaptureWidgets(s,e));return s;}
};

enum Stage { Init,MakeManager,ActivateManager,MakeDocument,MakeContext,BindIdentity,SourceQI,Advise,
    CompositionQI,Push,Associate,FocusStage,Terminate,RestoreAssociation,Unadvise,Pop,Deactivate,Uninitialize,StageCount };
static std::array<unsigned,StageCount> calls{};
static int failStage=-1;
static HRESULT failCode=E_FAIL,apartmentResult=S_OK;
static bool returnFailedOutput=false,windowCurrent=true,enabled=false,healthy=true,unregisterFails=false;
static bool inCleanup=false;
static unsigned unregisters=0,retireCalls=0,ackCalls=0,markCalls=0;
static std::function<void(Stage)> onStage;
static std::function<void()> onObserve,onAck;
static std::optional<OQ4_NativeCollectionContext> liveContext;
static OQ4_NativeCollectionHooks registered{};
static WindowsTextStore* gStore=nullptr;
static NativeTextIdentity gNative{100,200};
static std::uint64_t generation=71;
static std::uint64_t moduleEpoch=9,registrationClaim=13;
static unsigned providerCalls=0;
static ITfSource* gSource=nullptr;
static ITfContextOwnerCompositionServices* gComposition=nullptr;
static HRESULT Step(Stage stage) {
    ++calls[stage];
    if(stage!=Uninitialize){
        if(enabled && healthy)CHECK(liveContext && liveContext->kind==OQ4_COLLECTION_LIFECYCLE);
        if(gStore && gStore->Healthy() && stage!=BindIdentity){
            WindowsTextLifecycle state;OK(gStore->QueryLifecycle(gNative,state));CHECK(state.collectionOpen);
        }
    }
    if(onStage)onStage(stage);
    return failStage==stage?failCode:S_OK;
}
HRESULT Context::QueryInterface(REFIID iid,void** out) {
    if(!out)return E_POINTER;*out=nullptr;
    if(iid==__uuidof(ITfSource) || iid==__uuidof(ITfContextOwnerCompositionServices)){
        const auto hr=Step(iid==__uuidof(ITfSource)?SourceQI:CompositionQI);
        IUnknown* value=iid==__uuidof(ITfSource)?static_cast<IUnknown*>(gSource):static_cast<IUnknown*>(gComposition);
        if(SUCCEEDED(hr)||returnFailedOutput){value->AddRef();*out=iid==__uuidof(ITfSource)?static_cast<void*>(gSource):static_cast<void*>(gComposition);}
        return hr;
    }
    const auto hr=Step(BindIdentity);if(FAILED(hr))return hr;
    return Counted::QueryInterface(iid,out);
}
struct EditSource final:Counted<ITfSource> {
    ITfTextEditSink* sink=nullptr;
    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID iid,IUnknown* target,DWORD* cookie) override {
        CHECK(iid==__uuidof(ITfTextEditSink) && !sink);
        const auto hr=Step(Advise);
        if(SUCCEEDED(hr)||returnFailedOutput){OK(target->QueryInterface(iid,reinterpret_cast<void**>(&sink)));*cookie=29;}
        return hr;
    }
    HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD cookie) override {
        CHECK(cookie==29 && sink);const auto hr=Step(Unadvise);
        if(SUCCEEDED(hr)){auto* old=sink;sink=nullptr;old->Release();}return hr;
    }
};
struct CompositionServices final:Counted<ITfContextOwnerCompositionServices> {
    Sink* sink=nullptr;
    Composition* live=nullptr;
    HRESULT STDMETHODCALLTYPE StartComposition(TfEditCookie,ITfRange*,ITfCompositionSink*,ITfComposition**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumCompositions(IEnumITfCompositionView**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE FindComposition(TfEditCookie,ITfRange*,IEnumITfCompositionView**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TakeOwnership(TfEditCookie,ITfCompositionView*,ITfCompositionSink*,ITfComposition**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TerminateComposition(ITfCompositionView* specific)override {
        CHECK(!specific);const auto hr=Step(Terminate);if(FAILED(hr))return hr;
        if(live){
            // Model TSF itself requesting a synchronous write lock, then End.
            sink->lock=[&](DWORD flags){CHECK((flags&TS_LF_READWRITE)==TS_LF_READWRITE);return gStore->OnEndComposition(live);};
            HRESULT locked=E_FAIL;const auto result=gStore->RequestLock(TS_LF_SYNC|TS_LF_READWRITE,&locked);sink->lock={};
            if(FAILED(result)||FAILED(locked))return TF_E_NOLOCK;live=nullptr;
        }
        return S_OK;
    }
};
struct TestDocument final:Counted<ITfDocumentMgr> {
    Context context;Sink sink;WindowsTextStore* store=nullptr;
    bool stacked=false;
    void Drop() {
        if(store){OK(store->UnadviseSink(&sink));auto* old=store;store=nullptr;old->Release();gStore=nullptr;}
    }
    ULONG STDMETHODCALLTYPE Release()override {const auto n=Counted::Release();if(n==1)Drop();return n;}
    HRESULT STDMETHODCALLTYPE CreateContext(TfClientId id,DWORD flags,IUnknown* owner,ITfContext** out,TfEditCookie* cookie)override {
        CHECK(id==7 && !flags);const auto hr=Step(MakeContext);
        if(SUCCEEDED(hr)||returnFailedOutput){
            ITextStoreACP* acp=nullptr;OK(owner->QueryInterface(__uuidof(ITextStoreACP),reinterpret_cast<void**>(&acp)));
            store=static_cast<WindowsTextStore*>(acp);gStore=store;
            ITfContextOwnerCompositionSink* sinkInterface=nullptr;
            OK(owner->QueryInterface(__uuidof(ITfContextOwnerCompositionSink),reinterpret_cast<void**>(&sinkInterface)));sinkInterface->Release();
            OK(store->AdviseSink(__uuidof(ITextStoreACPSink),&sink,TS_AS_ALL_SINKS));
            context.AddRef();*out=&context;*cookie=13;
        }return hr;
    }
    HRESULT STDMETHODCALLTYPE Push(ITfContext* c)override {CHECK(c==&context);const auto hr=Step(::Push);if(SUCCEEDED(hr))stacked=true;return hr;}
    HRESULT STDMETHODCALLTYPE Pop(DWORD flags)override {CHECK(flags==TF_POPF_ALL);const auto hr=Step(::Pop);if(SUCCEEDED(hr)){stacked=false;Drop();}return hr;}
    HRESULT STDMETHODCALLTYPE GetTop(ITfContext**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetBase(ITfContext**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumContexts(IEnumTfContexts**)override{return E_NOTIMPL;}
};
struct ThreadManager final:Counted<ITfThreadMgr> {
    TestDocument document,previous;
    ITfDocumentMgr* association=&previous;
    bool active=false;
    HRESULT STDMETHODCALLTYPE Activate(TfClientId* client)override {const auto hr=Step(ActivateManager);if(SUCCEEDED(hr)){active=true;*client=7;}return hr;}
    HRESULT STDMETHODCALLTYPE Deactivate()override {const auto hr=Step(::Deactivate);if(SUCCEEDED(hr))active=false;return hr;}
    HRESULT STDMETHODCALLTYPE CreateDocumentMgr(ITfDocumentMgr** out)override {const auto hr=Step(MakeDocument);if(SUCCEEDED(hr)||returnFailedOutput){document.AddRef();*out=&document;}return hr;}
    HRESULT STDMETHODCALLTYPE EnumDocumentMgrs(IEnumTfDocumentMgrs**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetFocus(ITfDocumentMgr**)override{CHECK(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetFocus(ITfDocumentMgr* doc)override {CHECK(doc==&document);return Step(FocusStage);}
    HRESULT STDMETHODCALLTYPE AssociateFocus(HWND hwnd,ITfDocumentMgr* doc,ITfDocumentMgr** prior)override {
        CHECK(hwnd==reinterpret_cast<HWND>(77) && windowCurrent);
        const auto hr=Step(doc==&document?Associate:RestoreAssociation);
        if(SUCCEEDED(hr)||returnFailedOutput){if(association)association->AddRef();*prior=association;}
        if(SUCCEEDED(hr))association=doc;return hr;
    }
    HRESULT STDMETHODCALLTYPE IsThreadFocus(BOOL*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetFunctionProvider(REFCLSID,ITfFunctionProvider**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumFunctionProviders(IEnumTfFunctionProviders**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetGlobalCompartment(ITfCompartmentMgr**)override{return E_NOTIMPL;}
};
struct Platform final:WindowsTextSessionPlatform {
    ThreadManager manager;EditSource source;CompositionServices composition;
    unsigned apartmentRefs=0;
    Platform(){gSource=&source;gComposition=&composition;composition.sink=&manager.document.sink;}
    HRESULT InitializeSta()noexcept override {const auto hr=Step(Init);if(FAILED(hr))return hr;if(SUCCEEDED(apartmentResult))++apartmentRefs;return apartmentResult;}
    HRESULT CreateThreadManager(ITfThreadMgr** out)noexcept override {const auto hr=Step(MakeManager);if(SUCCEEDED(hr)||returnFailedOutput){manager.AddRef();*out=&manager;}return hr;}
    void UninitializeSta()noexcept override {Step(Uninitialize);CHECK(apartmentRefs);--apartmentRefs;}
    void Balanced() {CHECK(!apartmentRefs && manager.refs==1 && !manager.active && manager.document.refs==1 && manager.previous.refs==1 &&
        manager.document.context.refs==1 && manager.document.sink.refs==1 && source.refs==1 && !source.sink && composition.refs==1 && !manager.document.stacked);}
};
struct Source final:NativeQueueSource {
    NativeQueueStatus status{true,true,9,71,5,0x9000,{}};
    std::deque<std::pair<SDL_Event,OQ4_NativeQueueRecord>> events;
    std::uint64_t seq=0,dispatch=0,fence=0;unsigned polls=0;
    bool Observe(NativeQueueStatus& out,std::string&)override {
        if(onObserve)onObserve();out=status;out.mainThread=std::this_thread::get_id()==engineThread;
        out.healthy=enabled&&healthy;out.generation=enabled&&healthy?generation:0;return true;
    }
    int Poll(SDL_Event& e,OQ4_NativeQueueRecord& r)override {++polls;if(events.empty())return 0;e=events.front().first;r=events.front().second;events.pop_front();return 1;}
    bool CopyFence(const SDL_Event&,OQ4_NativeFence& out)override{if(!status.pending)return false;out=*status.pending;return true;}
    void Add(Uint32 type,Uint32 kind=OQ4_QUEUE_OUTSIDE,Uint32 ordinal=0){
        SDL_Event event{};event.type=type;
        OQ4_NativeQueueRecord r{1,kind,ordinal,0,generation,++seq,0,0};
        if(kind==OQ4_QUEUE_COLLECTION||kind==OQ4_QUEUE_FENCE)r.dispatch=dispatch;
        if(kind==OQ4_QUEUE_FENCE){r.fence_sequence=fence;event.user.code=11;}
        events.emplace_back(event,r);
    }
    void Seal(){
        status.pending=OQ4_NativeFence{1,1,dispatch,++fence};
        Add(SDL_EVENT_KEY_UP);Add(SDL_EVENT_POLL_SENTINEL,OQ4_QUEUE_SENTINEL);
        Add(SDL_EVENT_CLIPBOARD_UPDATE,OQ4_QUEUE_COLLECTION,1);Add(status.fenceEventType,OQ4_QUEUE_FENCE,1);
    }
};
static Source* queue=nullptr;
extern "C" bool SDLCALL SDL_IsMainThread(){++providerCalls;return std::this_thread::get_id()==engineThread;}
extern "C" bool SDLCALL OQ4_WindowsNativeFenceHealthy(){++providerCalls;return enabled&&healthy;}
extern "C" Uint64 SDLCALL OQ4_WindowsNativeFenceQueueGeneration(){++providerCalls;return enabled&&healthy?generation:0;}
extern "C" bool SDLCALL OQ4_WindowsNativeFenceRegisterHooks(const OQ4_NativeCollectionHooks* hooks){
    ++providerCalls;
    if(enabled||liveContext)return false;
    if(!hooks){++unregisters;if(unregisterFails)return false;registered={};return true;}
    CHECK(!registered.userdata);registered=*hooks;return true;
}
extern "C" bool SDLCALL OQ4_WindowsNativeFenceEnable(bool value){
    ++providerCalls;
    CHECK(!value);if(liveContext||queue->status.pending)return false;enabled=false;return true;
}
extern "C" bool SDLCALL OQ4_WindowsNativeFenceRetire(){++providerCalls;++retireCalls;if(liveContext)return false;enabled=false;healthy=false;return true;}
extern "C" bool SDLCALL OQ4_WindowsNativeFenceMarkActivity(const OQ4_NativeCollectionContext* c){++markCalls;return liveContext&&c&&c->generation==liveContext->generation&&c->dispatch==liveContext->dispatch;}
extern "C" bool SDLCALL OQ4_WindowsNativeFenceRunLifecycle(OQ4_NativeCollectionWork work,void* data){
    if(!enabled||!healthy||liveContext||queue->status.pending||!registered.userdata)return false;
    liveContext=OQ4_NativeCollectionContext{1,OQ4_COLLECTION_LIFECYCLE,generation,++queue->dispatch};
    bool okay=registered.Prepare(registered.userdata,&*liveContext);
    if(okay)okay=work(data,&*liveContext);
    const bool finished=registered.Finish(registered.userdata,&*liveContext,!okay);liveContext.reset();
    if(!okay||!finished){healthy=false;return false;}queue->Seal();return true;
}
struct Probe final:WindowsTextSessionWindowProbe {
    WindowsTextSessionWindow original{reinterpret_cast<HWND>(77),5,6,9,11,13};
    bool Current(const WindowsTextSessionWindow& w)const noexcept override{return windowCurrent&&w==original;}
    bool ProviderCurrent(const WindowsTextSessionWindow& w)const noexcept override{return w.moduleEpoch==moduleEpoch&&w.registration==registrationClaim;}
};
struct Disposition final:WindowsTextSessionDisposition {
    WindowsTextEventDisposition expected;
    bool valid=true;
    bool Current(const WindowsTextEventDisposition& r)const noexcept override{return valid&&r==expected;}
};
struct Fence final:NativeTextCollectionFence {
    bool okay=true,removeOnFailure=false;
    bool Acknowledge(const NativeQueueStatus& s,std::string&)override {
        ++ackCalls;if(onAck)onAck();if(!okay){if(removeOnFailure)queue->status.pending.reset();return false;}
        if(!queue->status.pending||!s.pending||s.pending->dispatch!=queue->status.pending->dispatch||s.pending->sequence!=queue->status.pending->sequence)return false;
        queue->status.pending.reset();return true;
    }
};
struct Owner final:NativeTextCollectionOwner {
    Editor& f;bool replaced=false;unsigned retired=0,applied=0,published=0;
    explicit Owner(Editor& e):f(e){}
    bool Refresh(const NativeTextEditorBarrier& b,NativeTextEditorView& out,std::string& error)override{return !replaced&&f.input.QueryNumberNative(b.editor.control,b.editor,out,error);}
    bool Current(const NativeTextEditorBarrier& b)const noexcept override{return !replaced&&f.input.IsNumberNativeCurrent(b);}
    bool Begin(const NativeTextEditorBarrier& b,const NativeTextCollection& c,NativeTextEditorBarrier& out,std::string& error)override{return !replaced&&f.input.BeginNumberNativeCollection(b,c,out,error);}
    bool Apply(const NativeTextEditorBarrier& b,const NativeTextOffer& offer,NativeTextEditorReceipt& out,std::string& error)override{++applied;return !replaced&&f.input.ApplyNumberNative(b,offer,out,error);}
    bool Complete(const NativeTextEditorBarrier& b,const NativeTextCollection& c,NativeTextEditorBarrier& out,std::string& error)override{return !replaced&&f.input.CompleteNumberNativeCollection(b,c,out,error);}
    std::unique_ptr<Interaction::NativeSettlement> PrepareSettlement(const NativeTextEditorBarrier& b,std::string& error)override{return replaced?nullptr:f.input.PrepareNumberNativeSettlement(b,error);}
    bool PublishSettlement(Interaction::NativeSettlement& p,NativeTextEditorReceipt& out)noexcept override{++published;return !replaced&&f.input.PublishNumberNativeSettlement(p,out);}
    void RetireExact(const NativeTextIdentity& n,const TextEditorIdentity& e)noexcept override{++retired;CHECK(!liveContext);if(!replaced)(void)f.input.RetireNumberNativeExact(n,e);}
};
struct Session {
    Editor editor;Owner owner{editor};Source source;Probe probe;Platform platform;
    WindowsTextSession* value=nullptr;
    WindowsTextStore* observedStore=nullptr; // Borrow valid until controller release.
    std::unique_ptr<NativeTextCollectionCoordinator> coordinator;
    NativeQueueIngress ingress;Fence fence;Disposition disposition;NativeClosedTextCollection closed;
    NativeTextCollectionResult result;WindowsTextSessionCleanup cleanup;
    std::unique_ptr<const NativeQueueBatch> batch;
    std::string error;
    Session(){
        calls.fill(0);failStage=-1;failCode=E_FAIL;apartmentResult=S_OK;returnFailedOutput=false;windowCurrent=true;
        enabled=false;healthy=true;unregisterFails=false;inCleanup=false;unregisters=retireCalls=ackCalls=markCalls=requestEditSessions=0;
        onStage={};onObserve={};onAck={};liveContext.reset();registered={};generation=71;moduleEpoch=9;registrationClaim=13;gStore=nullptr;queue=&source;
        editor.Attach();NativeTextEditorView view;CHECK(owner.Refresh(editor.barrier,view,error));
        CHECK(WindowsTextSession::Create(view,probe.original,platform,probe,owner,value,error));
        coordinator=std::make_unique<NativeTextCollectionCoordinator>(editor.barrier);
    }
    void Register(){CHECK(value->Register(error));enabled=true;CHECK(value->Bind(source,error));}
    void Read(){CHECK(ingress.Read(source,batch,error)==NativeQueueRead::Ready);CHECK(batch->Events().size()==4);disposition.expected={batch->Receipt(),31,41};}
    void Start(){Register();CHECK(value->Activate(source,closed,error));observedStore=gStore;CHECK(!calls[Uninitialize]&&registered.userdata&&source.status.pending);Read();
        CHECK(coordinator->ReconcileLifecycle(closed,ingress,source,*batch,owner,value->Collections(),result,error));
        CHECK(value->FinishActivation(*coordinator,result.completion,ingress,source,*batch,disposition.expected,disposition,fence,error));
        CHECK(value->State()==WindowsTextSession::Phase::Active&&registered.userdata&&!source.status.pending);}
    void Quiesce(){inCleanup=true;CHECK(value->Quiesce(*coordinator,source,cleanup,error));CHECK(value->State()==WindowsTextSession::Phase::CleanupHeld);
        CHECK(registered.userdata&&source.status.pending&&calls[Terminate]==1&&!calls[Uninitialize]);Read();}
    void Finish(){WindowsTextSessionRelease out;CHECK(value->FinishQuiesce(cleanup,ingress,source,*batch,disposition.expected,disposition,fence,out,error));
        CHECK(out.graceful&&out.associationRestored&&out.nativeReleased&&out.hooksRemoved);CHECK(!registered.userdata&&!enabled&&!source.status.pending);}
    ~Session(){
        denyAllocations=false;onStage={};onObserve={};onAck={};failStage=-1;unregisterFails=false;returnFailedOutput=false;
        // Restore the counted fixture's registration only to release stack test
        // objects. This is not module renewal or an executable host recovery API.
        moduleEpoch=9;registrationClaim=13;generation=71;
        if(value&&value->State()!=WindowsTextSession::Phase::Released){auto r=value->FaultRetirePreservingEvents();CHECK(r.hooksRemoved&&r.nativeReleased);}
        CHECK(WindowsTextSession::Destroy(value)&&!value);platform.Balanced();gStore=nullptr;
    }
};

static void SuccessAndApartment() {
    {Session f;CHECK(!WindowsTextSession::Destroy(f.value)&&f.value&&f.owner.retired==0);
        CHECK(f.value->FaultRetirePreservingEvents().nativeReleased&&f.owner.retired==1);
        CHECK(!f.editor.View().nativePresentation);}
    for(const auto apartment:{S_OK,S_FALSE}){
        Session f;apartmentResult=apartment;f.Start();
        CHECK(calls[Init]==1&&f.platform.apartmentRefs==1&&calls[Push]==1&&calls[Advise]==1);
        CHECK(f.platform.manager.association==&f.platform.manager.document);
        f.Quiesce();CHECK(f.owner.retired==1&&f.editor.View().state.text=="1");
        CHECK(calls[RestoreAssociation]==1&&calls[Unadvise]==1&&calls[Pop]==1&&calls[Deactivate]==1);
        CHECK(f.platform.manager.association==&f.platform.manager.previous&&!calls[Uninitialize]);
        f.Finish();CHECK(calls[Uninitialize]==1&&ackCalls==2&&requestEditSessions==0);f.platform.Balanced();
    }
    {Session f;f.Register();apartmentResult=RPC_E_CHANGED_MODE;CHECK(!f.value->Activate(f.source,f.closed,f.error));
        CHECK(calls[Init]==1&&!calls[MakeManager]&&!f.platform.apartmentRefs&&!calls[Uninitialize]);}
}
static void StageFailures() {
    for(const auto stage:{Init,MakeManager,ActivateManager,MakeDocument,MakeContext,BindIdentity,SourceQI,Advise,CompositionQI,Push,Associate,FocusStage}){
        for(bool output:{false,true}){
            Session f;f.Register();failStage=stage;returnFailedOutput=output;
            NativeClosedTextCollection out;out.serial=999;
            CHECK(!f.value->Activate(f.source,out,f.error));CHECK(out.serial==999&&f.value->State()==WindowsTextSession::Phase::Fault);
            CHECK(!WindowsTextSession::Destroy(f.value));
            failStage=-1;returnFailedOutput=false;
            const auto before=f.source.events.size();const auto polls=f.source.polls;
            const auto released=f.value->FaultRetirePreservingEvents();
            CHECK(released.hooksRemoved&&released.nativeReleased&&!released.graceful);
            CHECK(f.source.events.size()==before&&f.source.polls==polls&&!ackCalls);f.platform.Balanced();
        }
    }
    for(const auto stage:{Terminate,RestoreAssociation,Unadvise,Pop,Deactivate}){
        Session f;f.Start();failStage=stage;if(stage==Terminate)failCode=TF_E_NOLOCK;
        WindowsTextSessionCleanup out;out.session=999;
        CHECK(!f.value->Quiesce(*f.coordinator,f.source,out,f.error));CHECK(out.session==999&&f.owner.retired==1);
        CHECK(!calls[Uninitialize]&&registered.userdata);failStage=-1;
        CHECK(f.value->FaultRetirePreservingEvents().nativeReleased);CHECK(!f.owner.published);
    }
}
static void AdmissionFailures() {
    {Session f;enabled=true;CHECK(!f.value->Register(f.error));CHECK(!registered.userdata&&!calls[Init]);
        CHECK(f.value->FaultRetirePreservingEvents().nativeReleased);CHECK(enabled&&!retireCalls);}
    for(int which=0;which<5;++which){Session f;CHECK(f.value->Register(f.error));enabled=true;
        if(which==0)++f.source.status.providerEpoch;
        if(which==1)f.source.status.engineToken=0;
        if(which==2)f.source.status.fenceEventType=0;
        if(which==3)f.source.status.pending=OQ4_NativeFence{1,0,8,9};
        if(which==4)onObserve=[&]{CHECK(!f.value->Register(f.error));};
        CHECK(!f.value->Bind(f.source,f.error));onObserve={};
        CHECK(!calls[Init]&&registered.userdata&&!WindowsTextSession::Destroy(f.value));
        CHECK(f.value->FaultRetirePreservingEvents().nativeReleased&&!enabled);}
    {Session f;f.Start();auto bad=f.editor.barrier;++bad.editor.backend;NativeTextCollectionCoordinator replacement(bad);
        CHECK(!f.value->Quiesce(replacement,f.source,f.cleanup,f.error));CHECK(!f.owner.retired&&!calls[Terminate]);}
}
static void ReentryAndOwnership() {
    for(auto stage:{MakeContext,Push,FocusStage}){
        Session f;f.Register();onStage=[&](Stage s){if(s==stage){NativeClosedTextCollection out;CHECK(!f.value->Activate(f.source,out,f.error));}};
        CHECK(!f.value->Activate(f.source,f.closed,f.error));onStage={};CHECK(f.value->State()==WindowsTextSession::Phase::Fault);
        CHECK(!calls[Uninitialize]&&registered.userdata);
    }
    {Session f;f.Start();onStage=[&](Stage s){if(s==Pop){auto* same=f.value;CHECK(!WindowsTextSession::Destroy(same)&&same==f.value);}};
        CHECK(!f.value->Quiesce(*f.coordinator,f.source,f.cleanup,f.error));onStage={};}
    {Session f;f.Register();onStage=[&](Stage s){if(s==Push)f.owner.replaced=true;};
        CHECK(!f.value->Activate(f.source,f.closed,f.error));onStage={};CHECK(!f.owner.applied);}
    for(auto stage:{MakeContext,Push,Associate,FocusStage}){
        Session f;f.Register();onStage=[&](Stage s){if(s==stage)windowCurrent=false;};
        CHECK(!f.value->Activate(f.source,f.closed,f.error));onStage={};
        const auto r=f.value->FaultRetirePreservingEvents();CHECK(r.nativeReleased&&!calls[RestoreAssociation]);
    }
    {Session f;f.Start();windowCurrent=false;CHECK(!f.value->Quiesce(*f.coordinator,f.source,f.cleanup,f.error));
        const auto r=f.value->FaultRetirePreservingEvents();CHECK(r.nativeReleased&&!r.associationRestored&&!calls[RestoreAssociation]);}
    {Session f;std::thread worker([&]{CHECK(!f.value->Register(f.error));auto* p=f.value;CHECK(!WindowsTextSession::Destroy(p));
        CHECK(!f.value->FaultRetirePreservingEvents().nativeReleased);});worker.join();CHECK(f.value->State()==WindowsTextSession::Phase::Created&&!calls[Init]);}
    for(int which=0;which<3;++which){Session f;f.Start();
        if(which==0)++moduleEpoch;if(which==1)++registrationClaim;if(which==2)++generation;
        const auto oldCalls=providerCalls,oldRetires=retireCalls,oldUnregisters=unregisters;
        denyAllocations=true;const auto released=f.value->FaultRetirePreservingEvents();denyAllocations=false;
        CHECK(!released.providerRetired&&!released.hooksRemoved&&!released.nativeReleased&&registered.userdata);
        CHECK(retireCalls==oldRetires&&unregisters==oldUnregisters&&f.owner.retired==1&&!calls[Uninitialize]);
        if(which<2)CHECK(providerCalls==oldCalls);
        CHECK(!WindowsTextSession::Destroy(f.value));
    }
}
static void ExactBatchAndRelease() {
    for(int which=0;which<6;++which){Session f;f.Start();f.Quiesce();
        auto seal=f.cleanup;auto token=f.disposition.expected;
        if(which==0)++seal.session;if(which==1)++seal.closed.serial;if(which==2)++token.batch.serial;
        if(which==3)++token.ledger;if(which==4)++f.source.status.engineToken;if(which==5)f.disposition.valid=false;
        WindowsTextSessionRelease out;out.graceful=true;
        CHECK(!f.value->FinishQuiesce(seal,f.ingress,f.source,*f.batch,token,f.disposition,f.fence,out,f.error));
        CHECK(out.graceful&&ackCalls==1&&registered.userdata&&!calls[Uninitialize]&&f.observedStore->Healthy());
    }
    for(int which=0;which<5;++which){Session f;f.Start();f.Quiesce();
        if(which==0)f.fence.okay=false;
        if(which==1)onAck=[&]{++f.source.status.engineToken;};
        if(which==2)onAck=[&]{f.disposition.valid=false;};
        if(which==3)onAck=[&]{CHECK(!f.value->FaultRetirePreservingEvents().nativeReleased);};
        if(which==4){f.fence.okay=false;f.fence.removeOnFailure=true;}
        WindowsTextSessionRelease out;
        CHECK(!f.value->FinishQuiesce(f.cleanup,f.ingress,f.source,*f.batch,f.disposition.expected,f.disposition,f.fence,out,f.error));
        CHECK(registered.userdata&&!calls[Uninitialize]);onAck={};
    }
    {Session f;f.Start();f.Quiesce();unregisterFails=true;WindowsTextSessionRelease out;
        CHECK(!f.value->FinishQuiesce(f.cleanup,f.ingress,f.source,*f.batch,f.disposition.expected,f.disposition,f.fence,out,f.error));
        CHECK(registered.userdata&&!calls[Uninitialize]&&!WindowsTextSession::Destroy(f.value));
        unregisterFails=false;CHECK(f.value->FaultRetirePreservingEvents().nativeReleased);}
    {Session f;f.Start();f.source.Add(SDL_EVENT_KEY_DOWN);const auto count=f.source.events.size();const auto polls=f.source.polls;
        denyAllocations=true;const auto out=f.value->FaultRetirePreservingEvents();denyAllocations=false;
        CHECK(out.nativeReleased&&!out.graceful&&f.source.events.size()==count&&f.source.polls==polls&&ackCalls==1);
        f.platform.Balanced();}
    {Session f;f.Register();CHECK(f.value->Activate(f.source,f.closed,f.error));f.Read();
        CHECK(f.coordinator->ReconcileLifecycle(f.closed,f.ingress,f.source,*f.batch,f.owner,f.value->Collections(),f.result,f.error));
        auto bad=f.result.completion;++bad.serial;
        onObserve=[&]{bad=f.result.completion;};
        CHECK(!f.value->FinishActivation(*f.coordinator,bad,f.ingress,f.source,*f.batch,f.disposition.expected,f.disposition,f.fence,f.error));
        onObserve={};CHECK(!ackCalls);
    }
    {Session f;f.Start();f.Quiesce();auto token=f.disposition.expected;bool changed=false;
        onObserve=[&]{if(!changed){changed=true;++token.serial;f.disposition.expected=token;}};
        WindowsTextSessionRelease out;
        CHECK(!f.value->FinishQuiesce(f.cleanup,f.ingress,f.source,*f.batch,token,f.disposition,f.fence,out,f.error));
        onObserve={};CHECK(ackCalls==1&&registered.userdata&&f.observedStore->Healthy());
    }
    {Session f;f.Register();CHECK(f.value->Activate(f.source,f.closed,f.error));f.Read();
        CHECK(f.coordinator->ReconcileLifecycle(f.closed,f.ingress,f.source,*f.batch,f.owner,f.value->Collections(),f.result,f.error));
        unsigned observations=0;onObserve=[&]{if(++observations==2)f.disposition.valid=false;};
        CHECK(!f.value->FinishActivation(*f.coordinator,f.result.completion,f.ingress,f.source,*f.batch,f.disposition.expected,f.disposition,f.fence,f.error));
        onObserve={};CHECK(observations>=2&&!ackCalls&&registered.userdata);
    }
    for(int which=0;which<3;++which){Session f;f.Start();f.Quiesce();bool changed=false;
        onObserve=[&]{if(!changed&&!f.observedStore->Healthy()){
            changed=true;if(which==0)++moduleEpoch;if(which==1)++registrationClaim;if(which==2)windowCurrent=false;
        }};
        WindowsTextSessionRelease out;
        CHECK(!f.value->FinishQuiesce(f.cleanup,f.ingress,f.source,*f.batch,f.disposition.expected,f.disposition,f.fence,out,f.error));
        onObserve={};CHECK(changed&&ackCalls==1&&f.source.status.pending&&registered.userdata&&!calls[Uninitialize]);
    }
}
static void CompositionAndForeignRevision() {
    {Session f;Composition live;live.range.context=&f.platform.manager.document.context;live.range.start=0;live.range.length=1;
        f.Register();onStage=[&](Stage s){if(s==Push){BOOL accepted=FALSE;OK(gStore->OnStartComposition(&live,&accepted));CHECK(accepted);f.platform.composition.live=&live;}};
        CHECK(f.value->Activate(f.source,f.closed,f.error));onStage={};f.Read();
        CHECK(f.coordinator->ReconcileLifecycle(f.closed,f.ingress,f.source,*f.batch,f.owner,f.value->Collections(),f.result,f.error));
        CHECK(f.result.composing&&!f.result.settled&&f.owner.applied==1);
        CHECK(f.value->FinishActivation(*f.coordinator,f.result.completion,f.ingress,f.source,*f.batch,f.disposition.expected,f.disposition,f.fence,f.error));
        f.Quiesce();CHECK(f.cleanup.closed.pending.count==1&&f.cleanup.closed.admittedCallbacks&&f.owner.applied==1);
        CHECK(!f.owner.published&&f.editor.View().state.text=="1");f.Finish();CHECK(live.refs==1&&live.range.refs==1);
    }
    {Session f;f.Register();
        // Exact current editor revision is captured, never guessed from text.
        onStage=[&](Stage s){if(s==Push){const auto revision=f.editor.barrier.editor.revision;OK(gStore->SyncEngine(gNative,revision,1,revision+1,"1",1,1));}};
        CHECK(!f.value->Activate(f.source,f.closed,f.error));onStage={};CHECK(!f.owner.applied);
    }
    {Session f;f.Start();Composition late;late.range.context=&f.platform.manager.document.context;
        BOOL accepted=TRUE;OK(gStore->OnStartComposition(&late,&accepted));CHECK(!accepted&&gStore->Healthy());
        CHECK(!f.owner.applied&&requestEditSessions==0);f.Quiesce();f.Finish();}
}
int main(){
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ERROR,_CRTDBG_FILE_STDERR);
#endif
    SuccessAndApartment();StageFailures();AdmissionFailures();ReentryAndOwnership();ExactBatchAndRelease();CompositionAndForeignRevision();
    std::printf("PASS %u checks\n",checks);return 0;
}
