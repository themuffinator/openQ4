// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Actual Windows SDK interfaces and production methods. No COM activation,
// native input, clipboard, message-pump or window calls.
#include "sys/sdl3/WindowsTextStore.h"
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <thread>
#include <stdexcept>
#include <new>
using namespace openq4::sys;
using namespace openq4::ui;
static unsigned checks=0;
static bool rejectAllocations=false;
void* operator new(std::size_t size){if(rejectAllocations)throw std::bad_alloc();if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
#define OK(x) CHECK((x)==S_OK)

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
struct Sink : Counted<ITextStoreACPSink> {
	std::function<HRESULT(DWORD)> callback;
	std::function<void()> referenceCallback;
	ULONG STDMETHODCALLTYPE AddRef() override{const auto count=Counted::AddRef();if(referenceCallback)referenceCallback();return count;}
	ULONG STDMETHODCALLTYPE Release() override{const auto count=Counted::Release();if(referenceCallback)referenceCallback();return count;}
	std::function<HRESULT(DWORD,const TS_TEXTCHANGE*)> notice;
	unsigned notifications=0, calls=0;
	unsigned layoutNotices=0;
	HRESULT STDMETHODCALLTYPE OnLockGranted(DWORD flags) override {++calls;return callback?callback(flags):S_OK;}
	HRESULT STDMETHODCALLTYPE OnTextChange(DWORD flags,const TS_TEXTCHANGE* change) override{CHECK(flags==0 && change);++notifications;return notice?notice(TS_AS_TEXT_CHANGE,change):S_OK;}
	HRESULT STDMETHODCALLTYPE OnSelectionChange() override{++notifications;return notice?notice(TS_AS_SEL_CHANGE,nullptr):S_OK;}
	HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode code,TsViewCookie view) override{CHECK(code==TS_LC_CHANGE && view==1);++layoutNotices;return notice?notice(TS_AS_LAYOUT_CHANGE,nullptr):S_OK;}
	HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD) override{++notifications;return S_OK;}
	HRESULT STDMETHODCALLTYPE OnAttrsChange(LONG,LONG,ULONG,const TS_ATTRID*) override{++notifications;return S_OK;}
	HRESULT STDMETHODCALLTYPE OnStartEditTransaction() override{++notifications;return S_OK;}
	HRESULT STDMETHODCALLTYPE OnEndEditTransaction() override{++notifications;return S_OK;}
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
	Sink sink; Context context; WindowsTextStore* store=nullptr;
	NativeTextIdentity id{17,29};
	explicit Fixture(std::string_view text="abc",NativeTextLimits limits={}) {
		OK(WindowsTextStore::Create(id,10,text,0,0,reinterpret_cast<HWND>(1),&store,limits));
		OK(store->AdviseSink(__uuidof(ITextStoreACPSink),&sink,TS_AS_ALL_SINKS));
		OK(store->BindContext(id,&context));
		Begin();
	}
	~Fixture(){if(store){OK(store->UnadviseSink(&sink));OK(store->Retire(id));CHECK(store->Release()==0);}CHECK(sink.refs==1 && context.refs==1);CHECK(!sink.notifications);}
	void Lock(DWORD flags,const std::function<void()>& f) {
		Begin();
		sink.callback=[&](DWORD got){CHECK(got==flags);f();return S_OK;};HRESULT result=E_FAIL;OK(store->RequestLock(flags,&result));OK(result);sink.callback={};
	}
	void Close(){WindowsTextLifecycle state;OK(store->QueryLifecycle(id,state));if(state.collectionOpen){WindowsTextCollectionReceipt receipt;OK(store->CloseCollection(state.collection,receipt));}}
	void Begin(std::uint64_t dispatch=0,WindowsTextCollectionKind kind=WindowsTextCollectionKind::Lifecycle){
		WindowsTextLifecycle state;OK(store->QueryLifecycle(id,state));
		if(state.collectionOpen){if(!dispatch)return;Close();OK(store->QueryLifecycle(id,state));}
		WindowsTextCollection scope;OK(store->OpenCollection(id,state.engineRevision,state.acknowledgedSequence,state.acknowledgedShadowRevision,
			dispatch?dispatch:state.lastDispatch+1,kind,scope));
	}
	NativeTextOffer Offer(){Close();NativeTextOffer offer;OK(store->Peek(id,offer));return offer;}
	void Ack(){const auto offer=Offer();OK(store->Acknowledge(id,offer.transaction.sequence,offer.transaction.shadowAfter,offer.expectedEngineRevision,offer.expectedEngineRevision+1));}
};
static std::wstring Read(WindowsTextStore* store) {
	WCHAR text[128]{};ULONG count=99,runs=99;LONG next=-9;
	OK(store->GetText(0,-1,text,128,&count,nullptr,0,&runs,&next));CHECK(runs==0 && next==LONG(count));return {text,count};
}
static void IdentityAndLocks() {
	Fixture f;void *a=nullptr,*b=nullptr,*c=nullptr;
	OK(f.store->QueryInterface(__uuidof(ITextStoreACP),&a));
	OK(f.store->QueryInterface(__uuidof(ITfTextEditSink),&b));
	OK(static_cast<ITfTextEditSink*>(b)->QueryInterface(__uuidof(IUnknown),&c));CHECK(a==c);
	static_cast<IUnknown*>(c)->Release();static_cast<ITfTextEditSink*>(b)->Release();static_cast<ITextStoreACP*>(a)->Release();
	OK(f.store->QueryInterface(__uuidof(ITfContextOwnerCompositionSink),&b));
	OK(static_cast<ITfContextOwnerCompositionSink*>(b)->QueryInterface(__uuidof(IUnknown),&c));CHECK(a==c);
	static_cast<IUnknown*>(c)->Release();static_cast<ITfContextOwnerCompositionSink*>(b)->Release();
	CHECK(f.store->QueryInterface(__uuidof(ITfRange),&b)==E_NOINTERFACE && b==nullptr);
	CHECK(f.sink.refs==2);OK(f.store->AdviseSink(__uuidof(ITextStoreACPSink),&f.sink,TS_AS_TEXT_CHANGE));CHECK(f.sink.refs==2);
	Sink other;CHECK(f.store->AdviseSink(__uuidof(ITextStoreACPSink),&other,0)==CONNECT_E_ADVISELIMIT);CHECK(other.refs==1);
	CHECK(f.store->UnadviseSink(&other)==CONNECT_E_NOCONNECTION);
	LONG end=77;CHECK(f.store->GetEndACP(&end)==TS_E_NOLOCK && end==77);
	HRESULT session=E_ABORT;CHECK(f.store->RequestLock(0,&session)==E_INVALIDARG && session==E_ABORT);
	CHECK(f.store->RequestLock(TS_LF_READ,nullptr)==E_INVALIDARG);
	unsigned stage=0;
	f.sink.callback=[&](DWORD flags) -> HRESULT {
		if(stage++==0) {
			CHECK(flags==TS_LF_READ);OK(f.store->GetEndACP(&end));CHECK(end==3);
			TS_TEXTCHANGE change{7,8,9};CHECK(f.store->SetText(0,0,1,L"x",1,&change)==TS_E_NOLOCK && change.acpStart==7);
			HRESULT nested=E_FAIL;OK(f.store->RequestLock(TS_LF_READWRITE,&nested));CHECK(nested==TS_S_ASYNC);
			OK(f.store->RequestLock(TS_LF_SYNC|TS_LF_READWRITE,&nested));CHECK(nested==TS_E_SYNCHRONOUS);
			CHECK(stage==1);
		} else {
			CHECK(stage==2 && flags==TS_LF_READWRITE);TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"Z",1,&change));
		}
		return S_OK;
	};
	OK(f.store->RequestLock(TS_LF_READ,&session));OK(session);CHECK(stage==2 && f.Offer().transaction.after.text=="Zbc");
	f.Ack();CHECK(f.store->GetEndACP(&end)==TS_E_NOLOCK);
	HRESULT wrong=S_OK;std::thread thread([&]{TS_STATUS status{};wrong=f.store->GetStatus(&status);});thread.join();CHECK(wrong==RPC_E_WRONG_THREAD);
}
static void Utf16AndEdits() {
	Fixture f("A\xf0\x9f\x98\x80Z");
	LONG q1=-1,q2=-1;OK(f.store->QueryInsert(1,3,1,&q1,&q2));CHECK(q1==3 && q2==3);
	CHECK(f.store->QueryInsert(2,3,1,&q1,&q2)==E_INVALIDARG);
	f.Lock(TS_LF_READ,[&]{
		LONG end=0;OK(f.store->GetEndACP(&end));CHECK(end==4);
		std::wstring result;
		for(LONG i=0;i<4;++i){WCHAR unit=0;ULONG n=0,r=0;LONG next=0;TS_RUNINFO run{};OK(f.store->GetText(i,-1,&unit,1,&n,&run,1,&r,&next));CHECK(n==1 && r==1 && run.uCount==1 && next==i+1);result.push_back(unit);}
		CHECK(result==L"A\U0001f600Z");
		ULONG n=3,r=3;LONG next=-1;TS_RUNINFO run{};OK(f.store->GetText(0,-1,nullptr,0,&n,&run,1,&r,&next));CHECK(n==0 && r==1 && run.uCount==4 && next==4);
		WCHAR text=7;CHECK(f.store->GetText(-1,4,&text,1,&n,nullptr,0,&r,&next)==TS_E_INVALIDPOS && text==7);
		CHECK(f.store->GetText(0,5,&text,1,&n,nullptr,0,&r,&next)==TS_E_INVALIDPOS && text==7);
	});
	f.Lock(TS_LF_READWRITE,[&]{
		TS_TEXTCHANGE change{91,92,93};CHECK(f.store->SetText(0,2,3,L"!",1,&change)==TS_E_INVALIDPOS);CHECK(change.acpStart==91);
		const WCHAR bad[]={0xd800};CHECK(f.store->SetText(0,1,3,bad,1,&change)==E_INVALIDARG);
		TS_SELECTION_ACP select{1,3,{TS_AE_START,FALSE}};OK(f.store->SetSelection(1,&select));
		TS_SELECTION_ACP read{};ULONG got=0;OK(f.store->GetSelection(TS_DEFAULT_SELECTION,1,&read,&got));CHECK(got==1 && read.style.ase==TS_AE_START && read.acpStart==1 && read.acpEnd==3);
		LONG first=-1,last=-1;OK(f.store->InsertTextAtSelection(TS_IAS_QUERYONLY,L"hi",2,&first,&last,nullptr));CHECK(first==3 && last==3 && Read(f.store)==L"A\U0001f600Z");
		OK(f.store->InsertTextAtSelection(0,L"hi",2,&first,&last,&change));CHECK(first==3 && last==3 && change.acpStart==1 && change.acpOldEnd==3 && change.acpNewEnd==3);
		CHECK(Read(f.store)==L"AhiZ");
		OK(f.store->InsertTextAtSelection(TS_IAS_NOQUERY,L"!",1,nullptr,nullptr,&change));CHECK(Read(f.store)==L"Ahi!Z");
		CHECK(f.store->InsertTextAtSelection(TS_IAS_NOQUERY|TS_IAS_QUERYONLY,L"",0,nullptr,nullptr,&change)==E_INVALIDARG);
	});
	const auto offer=f.Offer();CHECK(offer.transaction.after.text=="Ahi!Z" && offer.transaction.after.anchor==4 && offer.transaction.after.caret==4);
	CHECK(offer.transaction.classification==NativeTextClassification::Unclassified);
	CHECK(f.store->Acknowledge(f.id,offer.transaction.sequence,offer.transaction.shadowAfter,10,10)==E_INVALIDARG);
	f.Ack();
	f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"B",1,&change));});
	f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE change;OK(f.store->SetText(0,1,2,L"C",1,&change));});
	const auto first=f.Offer();CHECK(first.expectedEngineRevision==11);f.Ack();CHECK(f.Offer().expectedEngineRevision==12);f.Ack();
	NativeTextOffer empty;CHECK(f.store->Peek(f.id,empty)==S_FALSE);
}
static void CompositionsAndLateMetadata() {
	Composition composition;Fixture f;
	composition.range.context=&f.context;composition.range.start=0;composition.range.length=2;
	f.Close();
	BOOL accepted=TRUE;OK(f.store->OnStartComposition(&composition,&accepted));CHECK(!accepted);
	f.Lock(TS_LF_READWRITE,[&]{
		TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"XY",2,&change));
		composition.range.reenter=[&]{TS_SELECTION_ACP sel{0,0,{TS_AE_END,FALSE}};CHECK(f.store->SetSelection(1,&sel)==E_UNEXPECTED);};
		OK(f.store->OnStartComposition(&composition,&accepted));CHECK(accepted);composition.range.reenter={};
		OK(f.store->OnStartComposition(&composition,&accepted));CHECK(!accepted);
		Range updated;updated.context=&f.context;updated.start=1;updated.length=2;
		OK(f.store->OnUpdateComposition(&composition,&updated));
		OK(f.store->OnEndComposition(&composition));
	});
	const auto offer=f.Offer();CHECK(offer.transaction.classification==NativeTextClassification::CompositionRelated);
	CHECK(offer.transaction.operations.front().kind==NativeTextOperationKind::Select);
	bool updated=false;for(const auto& operation:offer.transaction.operations)if(operation.kind==NativeTextOperationKind::UpdateComposition){CHECK(operation.first==1 && operation.last==3);updated=true;}CHECK(updated);
	CHECK(offer.transaction.after.compositions.empty());
	CHECK(composition.refs==2 && composition.range.refs==1);
	EditRecord record;OK(f.store->OnEndEdit(&f.context,42,&record));CHECK(f.store->EditReceipts()==1);
	CHECK(f.Offer()==offer);Context foreign;CHECK(f.store->OnEndEdit(&foreign,42,&record)==E_UNEXPECTED);CHECK(f.store->EditReceipts()==1);
	f.Ack();
	// Ended identity cannot be re-used even after acknowledged publication.
	f.Lock(TS_LF_READWRITE,[&]{OK(f.store->OnStartComposition(&composition,&accepted));CHECK(!accepted);});
	CHECK(f.store->OnEndComposition(&composition)==E_UNEXPECTED && !f.store->Healthy());
	CHECK(f.store->OnEndEdit(&f.context,42,&record)==TF_E_DISCONNECTED);
}
static void ForeignContextAndPendingBegin() {
	Composition comp;Fixture f;Context other;comp.range.context=&other;
	f.sink.callback=[&](DWORD){BOOL ok=TRUE;CHECK(FAILED(f.store->OnStartComposition(&comp,&ok)) && !ok);return S_OK;};
	HRESULT session=S_OK;OK(f.store->RequestLock(TS_LF_READWRITE,&session));CHECK(FAILED(session) && !f.store->Healthy());
	Composition delayed;Fixture g;delayed.range.context=&g.context;delayed.range.length=1;
	g.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE change;OK(g.store->SetText(0,0,1,L"Z",1,&change));});
	const auto before=g.Offer();BOOL accepted=TRUE;OK(g.store->OnStartComposition(&delayed,&accepted));CHECK(!accepted && g.Offer()==before);
	CHECK(before.transaction.classification==NativeTextClassification::Unclassified);
	CHECK(g.store->OnUpdateComposition(&delayed,nullptr)==E_UNEXPECTED && !g.store->Healthy());
}
static void FaultsAndReentry() {
	for(int mode=0;mode<4;++mode) {
		NativeTextLimits limits;if(mode==0)limits.pendingTransactions=1;if(mode==3)limits.operations=2;
		Fixture f("abc",limits);
		if(mode==0)f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"0",1,&change));});
		f.sink.callback=[&](DWORD) -> HRESULT {
			TS_TEXTCHANGE change;const HRESULT changed=f.store->SetText(0,0,1,L"1",1,&change);
			if(mode==3) {CHECK(FAILED(changed));return S_OK;}OK(changed);
			if(mode==1)throw std::bad_alloc();if(mode==2)return E_ABORT;return S_OK;
		};
		HRESULT session=S_OK;const HRESULT hr=f.store->RequestLock(TS_LF_READWRITE,&session);
		CHECK((mode==1?hr==E_OUTOFMEMORY:hr==S_OK && FAILED(session)) && !f.store->Healthy());
		NativeTextOffer out;CHECK(FAILED(f.store->Peek(f.id,out)));
	}
	Fixture f;
	f.sink.callback=[&](DWORD){OK(f.store->Retire(f.id));TS_TEXTCHANGE change;CHECK(f.store->SetText(0,0,1,L"X",1,&change)==TF_E_DISCONNECTED);return S_OK;};
	HRESULT session=S_OK;OK(f.store->RequestLock(TS_LF_READWRITE,&session));CHECK(session==TF_E_DISCONNECTED);
	// A callback can drop the owner's final reference; RequestLock's self/sink
	// references keep both objects alive through callback return and finalization.
	Sink sink;WindowsTextStore* store=nullptr;OK(WindowsTextStore::Create({51,52},1,"",0,0,reinterpret_cast<HWND>(1),&store));
	OK(store->AdviseSink(__uuidof(ITextStoreACPSink),&sink,0));
	sink.callback=[&](DWORD){OK(store->UnadviseSink(&sink));CHECK(store->Release()>=1);return S_OK;};
	OK(store->RequestLock(TS_LF_READ,&session));OK(session);CHECK(sink.refs==1);
}
static WindowsTextLayout Layout(Fixture& f) {
	WindowsTextLayout layout;layout.identity=f.id;layout.engineRevision=10;layout.shadowRevision=1;layout.layoutRevision=1;layout.text="abc";layout.visible=true;layout.screen={10,20,50,40};
	for(LONG i=0;i<3;++i)layout.cells.push_back({i,i+1,{10+i*10,20,20+i*10,40},{10+i*10,30},{20+i*10,30},i==2});
	return layout;
}
static void LayoutAndUnsupported() {
	Fixture f;auto layout=Layout(f);OK(f.store->SetLayout(layout));CHECK(f.store->SetLayout(layout)==E_INVALIDARG);
	RECT bounds{};OK(f.store->GetScreenExt(1,&bounds));CHECK(bounds.right==50);
	LONG hit=-1;POINT point{18,30};OK(f.store->GetACPFromPoint(1,&point,0,&hit));CHECK(hit==0);OK(f.store->GetACPFromPoint(1,&point,GXFPF_ROUND_NEAREST,&hit));CHECK(hit==1);
	point={80,30};CHECK(f.store->GetACPFromPoint(1,&point,0,&hit)==TS_E_INVALIDPOINT);OK(f.store->GetACPFromPoint(1,&point,GXFPF_NEAREST,&hit));CHECK(hit==3);
	f.Lock(TS_LF_READ,[&]{
		BOOL clipped=FALSE;OK(f.store->GetTextExt(1,1,3,&bounds,&clipped));CHECK(bounds.left==20 && bounds.right==40 && clipped);
		CHECK(f.store->GetTextExt(1,1,1,&bounds,&clipped)==E_INVALIDARG);CHECK(f.store->GetTextExt(1,1,4,&bounds,&clipped)==TS_E_INVALIDPOS);
		BOOL insert=TRUE;OK(f.store->QueryInsertEmbedded(nullptr,nullptr,&insert));CHECK(!insert);
		IDataObject* formatted=reinterpret_cast<IDataObject*>(1);CHECK(f.store->GetFormattedText(0,1,&formatted)==E_NOTIMPL && !formatted);
		IUnknown* embedded=reinterpret_cast<IUnknown*>(1);CHECK(f.store->GetEmbedded(0,GUID{},__uuidof(IUnknown),&embedded)==TS_E_NOOBJECT && !embedded);
		LONG next=-1,offset=-1;BOOL found=TRUE;OK(f.store->FindNextAttrTransition(0,3,0,nullptr,0,&next,&found,&offset));CHECK(next==3 && !found && offset==0);
	});
	layout.layoutRevision=2;layout.visible=false;OK(f.store->SetLayout(layout));
	f.Lock(TS_LF_READ,[&]{BOOL clipped=TRUE;OK(f.store->GetTextExt(1,0,1,&bounds,&clipped));CHECK(bounds.left==0 && bounds.right==0);});
	OK(f.store->RequestSupportedAttrs(TS_ATTR_FIND_WANT_VALUE,0,nullptr));ULONG fetched=99;OK(f.store->RetrieveRequestedAttrs(0,nullptr,&fetched));CHECK(fetched==0);
	f.Lock(TS_LF_READWRITE,[&]{
		CHECK(f.store->InsertEmbedded(0,0,0,nullptr,nullptr)==TS_E_FORMAT);
		TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"XYZ",3,&change));BOOL clipped=FALSE;CHECK(f.store->GetTextExt(1,0,1,&bounds,&clipped)==TS_E_NOLAYOUT);
	});
	CHECK(f.store->GetScreenExt(1,&bounds)==TS_E_NOLAYOUT);layout.layoutRevision=3;CHECK(f.store->SetLayout(layout)==E_INVALIDARG);
	f.Ack();
}
static void MetadataAcknowledgements() {
	Composition comp;Fixture f;comp.range.context=&f.context;comp.range.length=1;
	f.Lock(TS_LF_READWRITE,[&]{BOOL accepted=FALSE;OK(f.store->OnStartComposition(&comp,&accepted));CHECK(accepted);OK(f.store->OnEndComposition(&comp));});
	const auto offer=f.Offer();CHECK(!offer.transaction.documentChanged && offer.transaction.classification==NativeTextClassification::CompositionRelated);
	CHECK(f.store->Acknowledge({999,999},offer.transaction.sequence,offer.transaction.shadowAfter,10,10)==E_INVALIDARG);
	CHECK(f.store->Acknowledge(f.id,offer.transaction.sequence+1,offer.transaction.shadowAfter,10,10)==E_INVALIDARG);
	CHECK(f.store->Acknowledge(f.id,offer.transaction.sequence,offer.transaction.shadowAfter,10,9)==E_INVALIDARG);
	CHECK(f.Offer()==offer);OK(f.store->Acknowledge(f.id,offer.transaction.sequence,offer.transaction.shadowAfter,10,10));
	CHECK(f.store->Reject(f.id,offer.transaction.sequence,10)==E_INVALIDARG && f.store->Healthy());
	f.Begin(100);CHECK(f.store->SetDispatch(f.id,99)==E_UNEXPECTED);
	f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"x",1,&change));CHECK(f.store->SetDispatch(f.id,101)==E_UNEXPECTED);});
	CHECK(f.Offer().transaction.nativeDispatch==100);const auto queued=f.Offer();
	OK(f.store->Reject(f.id,queued.transaction.sequence,10));CHECK(!f.store->Healthy());
}
static void CompositionAcrossLocks() {
	Composition comp;Fixture f;comp.range.context=&f.context;comp.range.length=1;
	f.Lock(TS_LF_READWRITE,[&]{BOOL accepted=FALSE;OK(f.store->OnStartComposition(&comp,&accepted));CHECK(accepted);});
	const auto first=f.Offer();CHECK(first.transaction.after.compositions.size()==1);f.Ack();
	f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"xyz",3,&change));comp.range.length=3;OK(f.store->OnUpdateComposition(&comp,nullptr));OK(f.store->OnEndComposition(&comp));});
	const auto second=f.Offer();CHECK(second.transaction.classification==NativeTextClassification::CompositionRelated && second.transaction.after.text=="xyzbc" && second.transaction.after.compositions.empty());f.Ack();
}
static void DeferredSinkChange() {
	Sink next;Fixture f;HRESULT session=E_FAIL;
	f.sink.callback=[&](DWORD){HRESULT deferred=E_FAIL;OK(f.store->RequestLock(TS_LF_READWRITE,&deferred));CHECK(deferred==TS_S_ASYNC);OK(f.store->UnadviseSink(&f.sink));OK(f.store->AdviseSink(__uuidof(ITextStoreACPSink),&next,0));return S_OK;};
	CHECK(f.store->RequestLock(TS_LF_READ,&session)==E_FAIL && !f.store->Healthy() && next.calls==0);
	OK(f.store->UnadviseSink(&next));CHECK(next.refs==1);
	// Fixture destructor expects an advised original sink; retired stores cannot
	// be re-advised. Finish this fixture manually without pretending replacement.
	CHECK(f.store->Release()==0);f.store=nullptr;
}
static void ApplicationNotifications() {
	Fixture f;unsigned text=0,selection=0,writes=0;
	f.sink.callback=[&](DWORD flags) {
		CHECK(Read(f.store)==L"A\U0001f600Z");
		if(flags==TS_LF_READWRITE) {
			CHECK(text==1 && selection==1);++writes;
			TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"!",1,&change));
		} else {
			CHECK(flags==TS_LF_READ);
			HRESULT session=E_FAIL;OK(f.store->RequestLock(TS_LF_READWRITE,&session));CHECK(session==TS_S_ASYNC);
		}
		return S_OK;
	};
	f.sink.notice=[&](DWORD kind,const TS_TEXTCHANGE* change) {
		if(kind==TS_AS_TEXT_CHANGE) {++text;CHECK(change->acpStart==0 && change->acpOldEnd==3 && change->acpNewEnd==4);}
		else {CHECK(kind==TS_AS_SEL_CHANGE);++selection;}
		CHECK(writes==0);
		CHECK(f.store->SyncEngine(f.id,11,2,12,"bad",0,0)==E_UNEXPECTED);
		CHECK(f.store->UnadviseSink(&f.sink)==E_UNEXPECTED);
		CHECK(f.store->AdviseSink(__uuidof(ITextStoreACPSink),&f.sink,0)==E_UNEXPECTED);
		CHECK(f.store->BindContext(f.id,&f.context)==E_UNEXPECTED);
		CHECK(f.store->SetDispatch(f.id,999)==E_UNEXPECTED);
		NativeTextOffer offer;CHECK(f.store->Peek(f.id,offer)==E_UNEXPECTED);
		HRESULT session=E_FAIL;OK(f.store->RequestLock(TS_LF_SYNC|TS_LF_READWRITE,&session));CHECK(session==TS_E_SYNCHRONOUS);
		OK(f.store->RequestLock(TS_LF_READ,&session));OK(session);
		return S_OK;
	};
	OK(f.store->SyncEngine(f.id,10,1,11,"A\xf0\x9f\x98\x80Z",6,1));
	CHECK(text==1 && selection==1 && writes==1 && f.sink.notifications==2);
	const auto offer=f.Offer();CHECK(offer.expectedEngineRevision==11 && offer.transaction.shadowBefore==2 && offer.transaction.after.text=="!\xf0\x9f\x98\x80Z");
	f.Ack();CHECK(f.sink.notifications==2);f.sink.notifications=0;f.sink.notice={};f.sink.callback={};
	f.Lock(TS_LF_READ,[&]{CHECK(Read(f.store)==L"!\U0001f600Z");});
	// Same native snapshot at a fresh engine revision produces no false notice
	// or shadow increment. It must still invalidate the old engine layout lease.
	OK(f.store->SyncEngine(f.id,12,3,13,"!\xf0\x9f\x98\x80Z",1,1));
	CHECK(!f.sink.notifications);
	OK(f.store->SyncEngine(f.id,13,3,14,"!\xf0\x9f\x98\x80Z",6,1));
	CHECK(f.sink.notifications==1);f.sink.notifications=0;
	CHECK(f.store->SyncEngine(f.id,13,4,15,"bad",0,0)==E_INVALIDARG);
	CHECK(f.store->SyncEngine(f.id,14,3,15,"bad",0,0)==E_INVALIDARG);
	CHECK(f.store->SyncEngine({999,999},14,4,15,"bad",0,0)==E_INVALIDARG);
	CHECK(f.store->SyncEngine(f.id,14,4,15,"bad",4,0)==E_INVALIDARG);
	OK(f.store->SyncEngine(f.id,14,4,15,"!\xf0\x9f\x98\x80Z",6,1));
	CHECK(!f.sink.notifications);
	f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"x",1,&change));});
	CHECK(f.store->SyncEngine(f.id,15,5,16,"bad",0,0)==E_INVALIDARG);
	f.Ack();CHECK(!f.sink.notifications);
}
static void ApplicationNoticeFaultsAndLayout() {
	for(int mode=0;mode<3;++mode) {
		Fixture f;unsigned calls=0;
		f.sink.notice=[&](DWORD kind,const TS_TEXTCHANGE*) -> HRESULT {
			CHECK(kind==TS_AS_TEXT_CHANGE);++calls;
			HRESULT session=E_FAIL;OK(f.store->RequestLock(TS_LF_READWRITE,&session));CHECK(session==TS_S_ASYNC);
			if(mode==0)return E_ABORT;
			if(mode==1)throw std::bad_alloc();
			OK(f.store->Retire(f.id));return S_OK;
		};
		CHECK(FAILED(f.store->SyncEngine(f.id,10,1,11,"x",1,1)) && !f.store->Healthy());
		CHECK(calls==1 && f.sink.calls==0);f.sink.notifications=0;
	}
	Fixture f;auto layout=Layout(f);unsigned layouts=0;
	f.sink.notice=[&](DWORD kind,const TS_TEXTCHANGE*) {
		CHECK(kind==TS_AS_LAYOUT_CHANGE);++layouts;
		RECT rect{};OK(f.store->GetScreenExt(1,&rect));CHECK(rect.right==50);
		CHECK(f.store->SetLayout(layout)==E_INVALIDARG);
		return S_OK;
	};
	OK(f.store->SetLayout(layout));CHECK(layouts==1);
	OK(f.store->SyncEngine(f.id,10,1,11,"abc",0,0));
	RECT rect{};CHECK(f.store->GetScreenExt(1,&rect)==TS_E_NOLAYOUT);
	layout.engineRevision=11;layout.layoutRevision=2;OK(f.store->SetLayout(layout));CHECK(layouts==2);
	OK(f.store->AdviseSink(__uuidof(ITextStoreACPSink),&f.sink,0));
	OK(f.store->SyncEngine(f.id,11,1,12,"d",1,1));CHECK(!f.sink.notifications);
	// The application can lose its final store reference during a notification.
	// The active API and copied sink references must survive callback return.
	Sink sink;WindowsTextStore* store=nullptr;
	OK(WindowsTextStore::Create({81,82},1,"a",0,0,reinterpret_cast<HWND>(1),&store));
	OK(store->AdviseSink(__uuidof(ITextStoreACPSink),&sink,TS_AS_TEXT_CHANGE));
	sink.notice=[&](DWORD kind,const TS_TEXTCHANGE*) {CHECK(kind==TS_AS_TEXT_CHANGE);CHECK(store->Release()>=1);return S_OK;};
	OK(store->SyncEngine({81,82},1,1,2,"b",0,0));CHECK(sink.refs==1 && sink.notifications==1);
	Fixture deferred;
	deferred.sink.notice=[&](DWORD,const TS_TEXTCHANGE*) {HRESULT session=E_FAIL;OK(deferred.store->RequestLock(TS_LF_READWRITE,&session));CHECK(session==TS_S_ASYNC);return S_OK;};
	deferred.sink.callback=[&](DWORD flags) {CHECK(flags==TS_LF_READWRITE);return E_ABORT;};
	CHECK(FAILED(deferred.store->SyncEngine(deferred.id,10,1,11,"x",0,0)) && !deferred.store->Healthy());
	CHECK(deferred.sink.calls==1);deferred.sink.notifications=0;
}
static void PendingCollectionQueries(){
	Fixture f;NativeTextPendingSnapshot out{{99,98},97,96,95,94,93,92,91};const auto sentinel=out;
	f.Close();
	const auto calls=f.sink.calls,notices=f.sink.notifications,layoutNotices=f.sink.layoutNotices;const auto refs=f.sink.refs;
	OK(f.store->QueryPendingCollection(f.id,10,0,1,77,out));CHECK((out==NativeTextPendingSnapshot{f.id,10,0,1,1,77,0,0}));
	CHECK(f.sink.calls==calls && f.sink.notifications==notices && f.sink.layoutNotices==layoutNotices && f.sink.refs==refs);
	for(int field=0;field<5;++field){out=sentinel;auto id=f.id;std::uint64_t revision=10,sequence=0,shadow=1,dispatch=77;
		if(field==0)++id.editorLease;if(field==1)++revision;if(field==2)++sequence;if(field==3)++shadow;if(field==4)dispatch=0;
		CHECK(f.store->QueryPendingCollection(id,revision,sequence,shadow,dispatch,out)==E_INVALIDARG && out==sentinel);
	}
	f.Begin(77);f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE changed;OK(f.store->SetText(0,0,1,L"X",1,&changed));});
	f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE changed;OK(f.store->SetText(0,1,2,L"Y",1,&changed));});
	const auto copied=f.Offer();const auto priorCalls=f.sink.calls;NativeTextPendingSnapshot saved;
	OK(f.store->QueryPendingCollection(f.id,10,0,1,77,saved));CHECK((saved==NativeTextPendingSnapshot{f.id,10,0,1,3,77,2,2}));CHECK(f.Offer()==copied && f.sink.calls==priorCalls);
	out=sentinel;CHECK(f.store->QueryPendingCollection(f.id,11,1,2,77,out)==E_INVALIDARG && out==sentinel);
	f.Ack();OK(f.store->QueryPendingCollection(f.id,11,1,2,77,out));CHECK(out.count==1 && out.lastSequence==2 && out.shadowRevision==3);CHECK(saved.count==2 && saved.engineRevision==10);
	f.Ack();OK(f.store->QueryPendingCollection(f.id,12,2,3,88,out));CHECK(out.count==0 && out.dispatch==88);
	CHECK(f.sink.notifications==notices && f.sink.layoutNotices==layoutNotices);
	f.Begin(78);f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE changed;OK(f.store->SetText(0,0,1,L"Z",1,&changed));});
	f.Close();WindowsTextCollection rejected;
	CHECK(f.store->OpenCollection(f.id,12,2,3,79,WindowsTextCollectionKind::Pump,rejected)==E_INVALIDARG);
	OK(f.store->QueryPendingCollection(f.id,12,2,3,78,out));CHECK(out.count==1);
	out=sentinel;CHECK(f.store->QueryPendingCollection(f.id,12,2,3,79,out)==E_INVALIDARG && out==sentinel);
	out=sentinel;HRESULT threadResult=S_OK;std::thread worker([&]{threadResult=f.store->QueryPendingCollection(f.id,12,2,3,78,out);});worker.join();CHECK(threadResult==RPC_E_WRONG_THREAD && out==sentinel);
	OK(f.store->Retire(f.id));CHECK(f.store->QueryPendingCollection(f.id,12,2,3,78,out)==E_UNEXPECTED && out==sentinel);
}
static void PendingCollectionCallbackGates(){
	Fixture f;NativeTextPendingSnapshot out{{99,98},97,96,95,94,93,92,91};const auto sentinel=out;
	for(auto access:{DWORD(TS_LF_READ),DWORD(TS_LF_READWRITE)})f.Lock(access,[&]{CHECK(f.store->QueryPendingCollection(f.id,10,0,1,77,out)==E_UNEXPECTED && out==sentinel);});
	unsigned callbacks=0;f.sink.callback=[&](DWORD flags){++callbacks;CHECK(f.store->QueryPendingCollection(f.id,10,0,1,77,out)==E_UNEXPECTED && out==sentinel);
		if(flags==TS_LF_READ){HRESULT result=E_FAIL;OK(f.store->RequestLock(TS_LF_READWRITE,&result));CHECK(result==TS_S_ASYNC);}else CHECK(flags==TS_LF_READWRITE);
		return S_OK;};
	HRESULT session=E_FAIL;OK(f.store->RequestLock(TS_LF_READ,&session));OK(session);CHECK(callbacks==2);f.sink.callback={};
	f.Close();
	OK(f.store->QueryPendingCollection(f.id,10,0,1,77,out));CHECK(out.count==0);
	// Reentry while acquiring a foreign canonical COM identity is refused.
	struct QueryIdentity : Counted<IUnknown> {
		std::function<void()> query;
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** result) override{query();return Counted<IUnknown>::QueryInterface(iid,result);}
	} identity;
	out=sentinel;identity.query=[&]{CHECK(f.store->QueryPendingCollection(f.id,10,0,1,77,out)==E_UNEXPECTED && out==sentinel);};
	CHECK(f.store->BindContext(f.id,&identity)==E_UNEXPECTED);CHECK(identity.refs==1 && f.store->Healthy());
	// Application notice has a current snapshot, but cannot expose a boundary
	// before its whole callback/deferred-write batch has finished.
	f.Begin();
	f.sink.notice=[&](DWORD kind,const TS_TEXTCHANGE*){CHECK(kind==TS_AS_TEXT_CHANGE);CHECK(f.store->QueryPendingCollection(f.id,11,0,2,77,out)==E_UNEXPECTED && out==sentinel);HRESULT result=E_FAIL;OK(f.store->RequestLock(TS_LF_READWRITE,&result));CHECK(result==TS_S_ASYNC);return S_OK;};
	f.sink.callback=[&](DWORD flags){CHECK(flags==TS_LF_READWRITE);CHECK(f.store->QueryPendingCollection(f.id,11,0,2,77,out)==E_UNEXPECTED && out==sentinel);return S_OK;};
	OK(f.store->SyncEngine(f.id,10,1,11,"xyz",0,0));CHECK(f.sink.notifications==1);f.sink.notifications=0;f.sink.notice={};f.sink.callback={};
	f.Close();
	const auto calls=f.sink.calls;OK(f.store->QueryPendingCollection(f.id,11,0,2,77,out));CHECK(out.count==0 && out.acknowledgedShadowRevision==2 && f.sink.calls==calls);
	Fixture zero;zero.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE changed;OK(zero.store->SetText(0,0,1,L"!",1,&changed));});out=sentinel;
	zero.Close();
	CHECK(zero.store->QueryPendingCollection(zero.id,10,0,1,77,out)==E_INVALIDARG && out==sentinel);
}
static void IdleCompositionLifecycle(){
	Composition first,second;Fixture f("A\xf0\x9f\x98\x80Z");first.range.context=&f.context;first.range.start=1;first.range.length=2;
	second.range.context=&f.context;second.range.start=0;second.range.length=0;f.Begin(77);
	BOOL accepted=FALSE;OK(f.store->OnStartComposition(&first,&accepted));CHECK(accepted && f.sink.calls==0 && first.rangeCalls==1);
	OK(f.store->OnStartComposition(&first,&accepted));CHECK(!accepted && f.sink.calls==0);
	OK(f.store->OnStartComposition(&second,&accepted));CHECK(accepted);
	Range next;next.context=&f.context;next.start=0;next.length=1;OK(f.store->OnUpdateComposition(&first,&next));
	CHECK(next.extentCalls==1 && first.rangeCalls==1 && f.sink.calls==0);
	first.rangeResult=E_UNEXPECTED;second.rangeResult=E_UNEXPECTED;
	OK(f.store->OnEndComposition(&first));OK(f.store->OnEndComposition(&second));CHECK(first.rangeCalls==1 && second.rangeCalls==1);
	const auto begin=f.Offer();CHECK(begin.transaction.sequence==1 && begin.transaction.shadowBefore==1 && begin.transaction.shadowAfter==2 && !begin.transaction.documentChanged);
	CHECK((begin.transaction.operations.size()==1 && begin.transaction.after.text=="A\xf0\x9f\x98\x80Z" && begin.transaction.after.compositions.at(1)==NativeTextRange{1,5}));
	CHECK(begin.transaction.classification==NativeTextClassification::CompositionRelated && begin.transaction.nativeDispatch==77);
	NativeTextPendingSnapshot pending;OK(f.store->QueryPendingCollection(f.id,10,0,1,77,pending));CHECK(pending.count==5 && pending.lastSequence==5 && pending.shadowRevision==6);
	for(unsigned sequence=1;sequence<=5;++sequence){const auto offer=f.Offer();CHECK(offer.transaction.sequence==sequence && !offer.transaction.documentChanged && offer.expectedEngineRevision==10);
		if(sequence==3)CHECK((offer.transaction.after.compositions.at(1)==NativeTextRange{0,1} && offer.transaction.after.compositions.contains(2)));
		if(sequence==5)CHECK(offer.transaction.after.compositions.empty());
		OK(f.store->Acknowledge(f.id,sequence,offer.transaction.shadowAfter,10,10));}
	f.Begin(78);OK(f.store->OnStartComposition(&first,&accepted));CHECK(!accepted && first.rangeCalls==1);
	Composition third;third.range.context=&f.context;third.range.length=1;OK(f.store->OnStartComposition(&third,&accepted));CHECK(accepted);
	OK(f.store->OnEndComposition(&third));CHECK(f.sink.calls==0);
	CHECK(f.Offer().transaction.after.compositions.contains(3));
	OK(f.store->Retire(f.id));CHECK(f.store->OnUpdateComposition(&third,nullptr)==TF_E_DISCONNECTED && f.store->OnEndComposition(&third)==TF_E_DISCONNECTED);
}
static void IdleMetadataOrdering(){
	Composition composition;Fixture f;composition.range.context=&f.context;composition.range.length=2;f.Begin(91);
	f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"XY",2,&change));});
	BOOL accepted=FALSE;OK(f.store->OnStartComposition(&composition,&accepted));CHECK(accepted && f.sink.calls==1);
	f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"Z",1,&change));});
	OK(f.store->OnEndComposition(&composition));
	// End in one native callback does not prevent a later lock in the same
	// externally tagged collection, and it never relabels this later insertion.
	f.Lock(TS_LF_READWRITE,[&]{TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"Q",1,&change));});
	const auto insertion=f.Offer();CHECK(insertion.transaction.classification==NativeTextClassification::Unclassified);
	NativeTextPendingSnapshot pending;OK(f.store->QueryPendingCollection(f.id,10,0,1,91,pending));CHECK(pending.count==5 && f.sink.calls==3);
	for(unsigned i=1;i<=5;++i){auto offer=f.Offer();CHECK(offer.transaction.sequence==i && offer.transaction.nativeDispatch==91);
		CHECK(offer.transaction.classification==((i==1||i==5)?NativeTextClassification::Unclassified:NativeTextClassification::CompositionRelated));
		CHECK(offer.transaction.documentChanged==(i==1||i==3||i==5));f.Ack();}
}
static void IdleMetadataReentryAndFaults(){
	Composition first,nested;Fixture f;first.range.context=&f.context;first.range.length=1;nested.range.context=&f.context;nested.range.length=0;f.Begin(88);
	unsigned attempts=0;auto reenter=[&]{++attempts;BOOL accepted=TRUE;CHECK(f.store->OnStartComposition(&nested,&accepted)==TF_E_DISCONNECTED && !accepted);
		HRESULT session=E_ABORT;CHECK(f.store->RequestLock(TS_LF_READWRITE,&session)==E_UNEXPECTED && session==E_ABORT);
		CHECK(f.store->SetDispatch(f.id,89)==E_UNEXPECTED);CHECK(f.store->BindContext(f.id,&f.context)==E_UNEXPECTED);
		CHECK(f.store->SyncEngine(f.id,10,1,11,"changed",0,0)==E_UNEXPECTED);
		NativeTextPendingSnapshot out{{9,9},9,9,9,9,9,9,9},saved=out;CHECK(f.store->QueryPendingCollection(f.id,10,0,1,88,out)==E_UNEXPECTED && out==saved);
		CHECK(f.store->Acknowledge(f.id,1,2,10,10)==E_UNEXPECTED);CHECK(f.store->UnadviseSink(&f.sink)==E_UNEXPECTED);
		TS_SELECTION_ACP selection{0,0,{TS_AE_END,FALSE}};CHECK(f.store->SetSelection(1,&selection)==E_UNEXPECTED);};
	first.range.reenter=reenter;first.range.releasing=reenter;BOOL accepted=FALSE;OK(f.store->OnStartComposition(&first,&accepted));first.range.reenter={};first.range.releasing={};
	CHECK(accepted && attempts>=2 && f.sink.calls==0 && f.store->Healthy());OK(f.store->OnEndComposition(&first));
	for(int mode=0;mode<6;++mode){Composition bad;Context other;Fixture g("A\xf0\x9f\x98\x80Z");g.Begin(77);bad.range.context=&g.context;bad.range.length=1;
		if(mode==0)bad.range.context=&other;
		if(mode==1){bad.range.start=2;bad.range.length=1;}
		if(mode==2)bad.range.extentResult=E_FAIL;
		if(mode==3)bad.range.reenter=[&]{OK(g.store->Retire(g.id));};
		if(mode==4)bad.range.releasing=[&]{OK(g.store->Retire(g.id));};
		if(mode==5)bad.range.reenter=[] {throw std::bad_alloc();};
		accepted=TRUE;CHECK(FAILED(g.store->OnStartComposition(&bad,&accepted)) && !accepted && !g.store->Healthy());
		bad.range.reenter={};bad.range.releasing={};CHECK(g.sink.calls==0 && bad.refs==1 && bad.range.refs==1);
	}
	Composition wrongThread;Fixture thread;wrongThread.range.context=&thread.context;thread.Begin(77);HRESULT result=S_OK;accepted=TRUE;
	std::thread worker([&]{result=thread.store->OnStartComposition(&wrongThread,&accepted);});worker.join();CHECK(result==RPC_E_WRONG_THREAD && accepted && wrongThread.rangeCalls==0);
	Composition stale;Fixture retired;stale.range.context=&retired.context;retired.Begin(77);OK(retired.store->Retire(retired.id));
	CHECK(retired.store->OnStartComposition(&stale,&accepted)==TF_E_DISCONNECTED && !accepted && stale.rangeCalls==0);
}
static void IdleMetadataLimitsAndUnsupported(){
	Composition comp;NativeTextLimits limits;limits.pendingTransactions=1;Fixture full("abc",limits);comp.range.context=&full.context;comp.range.length=1;full.Begin(77);BOOL accepted=FALSE;
	OK(full.store->OnStartComposition(&comp,&accepted));CHECK(accepted);CHECK(full.store->OnEndComposition(&comp)==E_FAIL && !full.store->Healthy() && full.sink.calls==0);
	Composition unknown;Fixture wrong;unknown.range.context=&wrong.context;wrong.Begin(77);CHECK(wrong.store->OnEndComposition(&unknown)==E_UNEXPECTED && !wrong.store->Healthy() && unknown.rangeCalls==0);
	Composition readOnly;Fixture read;readOnly.range.context=&read.context;read.Begin(77);read.Lock(TS_LF_READ,[&]{OK(read.store->OnStartComposition(&readOnly,&accepted));CHECK(!accepted);});CHECK(read.store->Healthy() && readOnly.rangeCalls==0);
	Composition notice;Fixture app;notice.range.context=&app.context;app.Begin(77);
	app.sink.notice=[&](DWORD kind,const TS_TEXTCHANGE*){CHECK(kind==TS_AS_TEXT_CHANGE);OK(app.store->OnStartComposition(&notice,&accepted));CHECK(!accepted);return S_OK;};
	OK(app.store->SyncEngine(app.id,10,1,11,"xyz",0,0));CHECK(app.store->Healthy() && notice.rangeCalls==0 && app.sink.notifications==1);app.sink.notifications=0;
	Composition ending;Fixture notification;ending.range.context=&notification.context;ending.range.length=1;notification.Begin(77);OK(notification.store->OnStartComposition(&ending,&accepted));CHECK(accepted);const auto begin=notification.Offer();OK(notification.store->Acknowledge(notification.id,1,2,10,10));
	notification.sink.notice=[&](DWORD kind,const TS_TEXTCHANGE*){CHECK(kind==TS_AS_LAYOUT_CHANGE);CHECK(notification.store->OnEndComposition(&ending)==E_UNEXPECTED);return S_OK;};
	auto layout=Layout(notification);layout.shadowRevision=2;CHECK(notification.store->SetLayout(layout)==TF_E_DISCONNECTED && !notification.store->Healthy());CHECK(begin.transaction.after.compositions.contains(1));
}
static WindowsTextLifecycle Status(Fixture& f){WindowsTextLifecycle state;OK(f.store->QueryLifecycle(f.id,state));return state;}
static void CallbackScopeAuthority(){
	Composition composition;Fixture f;composition.range.context=&f.context;f.Close();
	const auto idle=Status(f);CHECK(idle.healthy && idle.safeToRenew && !idle.collectionOpen && idle.pending==0);
	OK(f.store->SetDispatch(f.id,90));CHECK(Status(f)==idle); // Observation alone grants no authority.
	unsigned reads=0;f.sink.callback=[&](DWORD flags){CHECK(flags==TS_LF_READ);++reads;CHECK(Read(f.store)==L"abc");
		NativeTextPendingSnapshot pending;CHECK(f.store->QueryPendingCollection(f.id,10,0,1,90,pending)==E_UNEXPECTED);
		HRESULT nested=E_ABORT;OK(f.store->RequestLock(TS_LF_READWRITE,&nested));CHECK(nested==TS_E_NOLOCK);return S_OK;};
	HRESULT session=E_ABORT;OK(f.store->RequestLock(TS_LF_READ,&session));OK(session);CHECK(reads==1 && Status(f)==idle);
	for(DWORD flags:{DWORD(TS_LF_READWRITE),DWORD(TS_LF_SYNC|TS_LF_READWRITE)}){session=E_ABORT;OK(f.store->RequestLock(flags,&session));CHECK(session==TS_E_NOLOCK && reads==1);}
	BOOL accepted=TRUE;OK(f.store->OnStartComposition(&composition,&accepted));CHECK(!accepted && composition.rangeCalls==0 && Status(f)==idle);
	f.sink.callback={};WindowsTextCollection out{{900,901},902,903,WindowsTextCollectionKind::Lifecycle},saved=out;
	for(unsigned i=0;i<7;++i){auto id=f.id;std::uint64_t engine=10,ack=0,shadow=1,dispatch=91;auto kind=WindowsTextCollectionKind::Pump;
		if(i==0)++id.document;if(i==1)++engine;if(i==2)++ack;if(i==3)++shadow;if(i==4)dispatch=0;if(i==5)dispatch=89;if(i==6)kind=static_cast<WindowsTextCollectionKind>(99);
		const auto hr=f.store->OpenCollection(id,engine,ack,shadow,dispatch,kind,out);CHECK(hr==(i==0?E_UNEXPECTED:E_INVALIDARG));
		CHECK(out==saved && Status(f)==idle);
	}
	OK(f.store->OpenCollection(f.id,10,0,1,91,WindowsTextCollectionKind::Pump,out));const auto active=out;
	CHECK(active.identity==f.id && active.serial==idle.lastScopeSerial+1 && active.dispatch==91);
	CHECK(Status(f).collection==active && !Status(f).safeToRenew);
	CHECK(f.store->SyncEngine(f.id,10,1,11,"z",0,0)==E_UNEXPECTED);CHECK(f.store->BindContext(f.id,&f.context)==E_UNEXPECTED);
	CHECK(f.store->SetLayout(Layout(f))==E_INVALIDARG && f.store->SetDispatch(f.id,92)==E_UNEXPECTED);
	WindowsTextCollectionReceipt receipt;receipt.admittedCallbacks=999;const auto prior=receipt;
	for(unsigned i=0;i<5;++i){auto wrong=active;if(i==0)++wrong.identity.document;if(i==1)++wrong.identity.editorLease;if(i==2)++wrong.serial;if(i==3)++wrong.dispatch;if(i==4)wrong.kind=WindowsTextCollectionKind::Lifecycle;
		CHECK(f.store->CloseCollection(wrong,receipt)==E_UNEXPECTED && receipt==prior);CHECK(f.store->AbortCollection(wrong)==E_INVALIDARG && Status(f).collection==active);}
	NativeTextOffer offer;CHECK(f.store->Peek(f.id,offer)==E_UNEXPECTED);
	NativeTextPendingSnapshot pending;CHECK(f.store->QueryPendingCollection(f.id,10,0,1,91,pending)==E_UNEXPECTED);
	CHECK(f.store->Acknowledge(f.id,1,2,10,11)==E_UNEXPECTED);
	WindowsTextCollection another;CHECK(f.store->OpenCollection(f.id,10,0,1,92,WindowsTextCollectionKind::Pump,another)==E_UNEXPECTED);
	// An admitted read callback with no writes still requires a provider fence.
	f.Lock(TS_LF_READ,[&]{CHECK(Read(f.store)==L"abc");});
	OK(f.store->CloseCollection(active,receipt));CHECK(receipt.collection==active && receipt.admittedCallbacks==1 && receipt.pending.count==0 && receipt.pending.dispatch==91);
	const auto immutable=receipt;CHECK(f.store->CloseCollection(active,receipt)==E_UNEXPECTED && receipt==immutable);
	CHECK(f.store->OpenCollection(f.id,10,0,1,91,WindowsTextCollectionKind::Pump,another)==E_INVALIDARG);
	OK(f.store->OpenCollection(f.id,10,0,1,92,WindowsTextCollectionKind::Pump,another));CHECK(another.serial>active.serial);
	CHECK(f.store->AbortCollection(active)==E_INVALIDARG);OK(f.store->CloseCollection(another,receipt));CHECK(receipt.admittedCallbacks==0 && receipt.pending.count==0 && immutable.admittedCallbacks==1);
}
static void ScopeReentryAbortAndActivity(){
	Fixture f;const auto scope=Status(f).collection;WindowsTextCollectionReceipt receipt;receipt.admittedCallbacks=999;const auto saved=receipt;
	unsigned steps=0;f.sink.callback=[&](DWORD flags){++steps;
		WindowsTextLifecycle state;state.pending=999;const auto previous=state;CHECK(f.store->QueryLifecycle(f.id,state)==E_UNEXPECTED && state==previous);
		CHECK(f.store->CloseCollection(scope,receipt)==E_UNEXPECTED && receipt==saved);
		WindowsTextCollection opened{{1,2},3,4,WindowsTextCollectionKind::Pump},prior=opened;
		CHECK(f.store->OpenCollection(f.id,10,0,1,2,WindowsTextCollectionKind::Pump,opened)==E_UNEXPECTED && opened==prior);
		if(flags==TS_LF_READ){HRESULT next=E_ABORT;OK(f.store->RequestLock(TS_LF_READWRITE,&next));CHECK(next==TS_S_ASYNC);}
		else {CHECK(flags==TS_LF_READWRITE);TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"!",1,&change));}
		return S_OK;};
	HRESULT session=E_ABORT;OK(f.store->RequestLock(TS_LF_READ,&session));OK(session);CHECK(steps==2);
	OK(f.store->CloseCollection(scope,receipt));CHECK(receipt.admittedCallbacks==2 && receipt.pending.count==1 && receipt.pending.lastSequence==1 && receipt.pending.shadowRevision==2);
	CHECK(!Status(f).safeToRenew);f.Ack();CHECK(Status(f).safeToRenew);
	f.Begin();Composition unsolicited;unsolicited.range.context=&f.context;unsigned referenceCalls=0;
	f.sink.referenceCallback=[&]{++referenceCalls;HRESULT reentered=E_ABORT;CHECK(f.store->RequestLock(TS_LF_READWRITE,&reentered)==E_UNEXPECTED && reentered==E_ABORT);
		BOOL accepted=TRUE;CHECK(f.store->OnStartComposition(&unsolicited,&accepted)==TF_E_DISCONNECTED && !accepted);
		CHECK(f.store->OnUpdateComposition(&unsolicited,nullptr)==TF_E_DISCONNECTED && f.store->OnEndComposition(&unsolicited)==TF_E_DISCONNECTED);
		CHECK(f.store->Healthy());};
	f.sink.callback=[](DWORD){return S_OK;};session=E_ABORT;OK(f.store->RequestLock(TS_LF_READWRITE,&session));OK(session);
	f.sink.referenceCallback={};CHECK(referenceCalls==2);f.Close();
	{
		Fixture beforeGrant;const auto abortScope=Status(beforeGrant).collection;unsigned references=0;
		beforeGrant.sink.referenceCallback=[&]{if(references++==0)OK(beforeGrant.store->AbortCollection(abortScope));};
		HRESULT untouched=E_ABORT;CHECK(beforeGrant.store->RequestLock(TS_LF_READWRITE,&untouched)==TF_E_DISCONNECTED && untouched==E_ABORT);
		beforeGrant.sink.referenceCallback={};CHECK(!beforeGrant.store->Healthy() && beforeGrant.sink.calls==0);
	}
	// Wrong-thread outputs, stale close and exact abort are independently checked.
	f.Begin();const auto next=Status(f).collection;WindowsTextLifecycle unchanged;unchanged.pending=99;const auto sentinel=unchanged;
	HRESULT foreign=S_OK;std::thread worker([&]{foreign=f.store->QueryLifecycle(f.id,unchanged);});worker.join();CHECK(foreign==RPC_E_WRONG_THREAD && unchanged==sentinel);
	f.sink.callback=[&](DWORD){TS_TEXTCHANGE change;OK(f.store->SetText(0,0,1,L"x",1,&change));
		rejectAllocations=true;const HRESULT aborted=f.store->AbortCollection(next);rejectAllocations=false;OK(aborted);return S_OK;};
	session=E_ABORT;OK(f.store->RequestLock(TS_LF_READWRITE,&session));CHECK(session==TF_E_DISCONNECTED && !f.store->Healthy());
	receipt=saved;CHECK(f.store->CloseCollection(next,receipt)==E_UNEXPECTED && receipt==saved);
	CHECK(!Status(f).healthy && !Status(f).safeToRenew && Status(f).pending==0);
	CHECK(f.store->AbortCollection(next)==E_INVALIDARG);
	Composition range;Fixture g;range.range.context=&g.context;range.range.length=1;const auto metadataScope=Status(g).collection;
	range.range.reenter=[&]{WindowsTextCollectionReceipt output=saved;CHECK(g.store->CloseCollection(metadataScope,output)==E_UNEXPECTED && output==saved);WindowsTextLifecycle status;CHECK(g.store->QueryLifecycle(g.id,status)==E_UNEXPECTED);};
	BOOL accepted=FALSE;OK(g.store->OnStartComposition(&range,&accepted));CHECK(accepted);range.range.reenter={};
	OK(g.store->OnEndComposition(&range));g.Close();CHECK(Status(g).admittedCallbacks==2 && Status(g).retainedCompositions==1 && Status(g).liveCompositions==0);
}
static void LifecycleNoticeScopeAndLateCallbacks(){
	Fixture outside;outside.Close();unsigned writes=0,reads=0;
	outside.sink.callback=[&](DWORD flags){CHECK(flags==TS_LF_READ);++reads;return S_OK;};
	outside.sink.notice=[&](DWORD,const TS_TEXTCHANGE*){HRESULT session=E_ABORT;
		NativeTextPendingSnapshot pending;CHECK(outside.store->QueryPendingCollection(outside.id,11,0,2,1,pending)==E_UNEXPECTED);
		for(DWORD flags:{DWORD(TS_LF_READWRITE),DWORD(TS_LF_SYNC|TS_LF_READWRITE)}){OK(outside.store->RequestLock(flags,&session));CHECK(session==TS_E_NOLOCK);}
		OK(outside.store->RequestLock(TS_LF_READ,&session));OK(session);return S_OK;};
	OK(outside.store->SyncEngine(outside.id,10,1,11,"new",0,0));CHECK(outside.store->Healthy() && reads==1 && writes==0 && Status(outside).pending==0);
	outside.sink.notifications=0;outside.sink.notice={};outside.sink.callback={};
	outside.Begin(20,WindowsTextCollectionKind::Lifecycle);const auto lifecycle=Status(outside).collection;
	outside.sink.notice=[&](DWORD,const TS_TEXTCHANGE*){HRESULT session=E_ABORT;OK(outside.store->RequestLock(TS_LF_READWRITE,&session));CHECK(session==TS_S_ASYNC && writes==0);return S_OK;};
	outside.sink.callback=[&](DWORD flags){CHECK(flags==TS_LF_READWRITE);++writes;TS_TEXTCHANGE change;OK(outside.store->SetText(0,0,1,L"!",1,&change));return S_OK;};
	OK(outside.store->SyncEngine(outside.id,11,2,12,"NEW",0,0));CHECK(writes==1);
	WindowsTextCollectionReceipt receipt;OK(outside.store->CloseCollection(lifecycle,receipt));
	CHECK(receipt.collection==lifecycle && receipt.pending.engineRevision==12 && receipt.pending.acknowledgedShadowRevision==3 && receipt.pending.shadowRevision==4 && receipt.pending.count==1 && receipt.admittedCallbacks==1);
	CHECK(outside.Offer().transaction.nativeDispatch==20);outside.Ack();outside.sink.notifications=0;outside.sink.notice={};outside.sink.callback={};
	// Engine revision changes in a lifecycle collection do not alter its identity.
	outside.Begin(21,WindowsTextCollectionKind::Lifecycle);const auto emptyScope=Status(outside).collection;
	OK(outside.store->SyncEngine(outside.id,13,4,14,"!EW",1,1));
	OK(outside.store->CloseCollection(emptyScope,receipt));CHECK(receipt.collection==emptyScope && receipt.pending.engineRevision==14 && receipt.pending.shadowRevision==4 && !receipt.pending.count);
	for(bool update:{false,true}){Composition composition;Fixture f;composition.range.context=&f.context;composition.range.length=1;BOOL accepted=FALSE;
		OK(f.store->OnStartComposition(&composition,&accepted));CHECK(accepted);f.Close();const auto rangeCalls=composition.rangeCalls;
		const auto begin=f.Offer();OK(f.store->Acknowledge(f.id,begin.transaction.sequence,begin.transaction.shadowAfter,10,10));
		CHECK(Status(f).liveCompositions==1 && !Status(f).pending && !Status(f).safeToRenew);
		OK(f.store->SetDispatch(f.id,99));const HRESULT late=update?f.store->OnUpdateComposition(&composition,nullptr):f.store->OnEndComposition(&composition);
		CHECK(late==E_UNEXPECTED && !f.store->Healthy() && composition.rangeCalls==rangeCalls);
	}
}
static void ScopeCompositionBudget(){
	std::vector<std::unique_ptr<Composition>> objects;objects.reserve(257);Fixture f;
	for(unsigned i=0;i<256;++i){objects.push_back(std::make_unique<Composition>());auto& c=*objects.back();c.range.context=&f.context;c.range.length=1;
		f.Lock(TS_LF_READWRITE,[&]{BOOL accepted=FALSE;OK(f.store->OnStartComposition(&c,&accepted));CHECK(accepted);OK(f.store->OnEndComposition(&c));});
		const auto offer=f.Offer();CHECK(offer.transaction.after.compositions.empty());OK(f.store->Acknowledge(f.id,offer.transaction.sequence,offer.transaction.shadowAfter,10,10));
		const auto state=Status(f);CHECK(state.retainedCompositions==i+1 && !state.liveCompositions && !state.pending && state.safeToRenew && !state.renewalRequired);
	}
	const auto stable=Status(f);CHECK(stable.retainedCompositions==256 && objects.front()->refs==2);
	f.Begin();const auto scope=Status(f).collection;objects.push_back(std::make_unique<Composition>());auto& extra=*objects.back();extra.range.context=&f.context;
	BOOL accepted=TRUE;OK(f.store->OnStartComposition(&extra,&accepted));CHECK(!accepted && extra.rangeCalls==0);
	CHECK(Status(f).renewalRequired && !Status(f).safeToRenew && Status(f).retainedCompositions==256);
	WindowsTextCollectionReceipt receipt;receipt.admittedCallbacks=991;const auto unchanged=receipt;
	CHECK(f.store->CloseCollection(scope,receipt)==E_FAIL && receipt==unchanged && !f.store->Healthy());
	CHECK(Status(f).renewalRequired && !Status(f).safeToRenew && Status(f).retainedCompositions==256);
	// Retirement keeps anti-reuse references until this old store is destroyed.
	CHECK(objects.front()->refs==2 && f.store->OnStartComposition(objects.front().get(),&accepted)==TF_E_DISCONNECTED && !accepted);
}
int main(){IdentityAndLocks();Utf16AndEdits();CompositionsAndLateMetadata();ForeignContextAndPendingBegin();FaultsAndReentry();LayoutAndUnsupported();MetadataAcknowledgements();CompositionAcrossLocks();DeferredSinkChange();ApplicationNotifications();ApplicationNoticeFaultsAndLayout();PendingCollectionQueries();PendingCollectionCallbackGates();IdleCompositionLifecycle();IdleMetadataOrdering();IdleMetadataReentryAndFaults();IdleMetadataLimitsAndUnsupported();CallbackScopeAuthority();ScopeReentryAbortAndActivity();LifecycleNoticeScopeAndLateCallbacks();ScopeCompositionBudget();std::printf("PASS %u checks\n",checks);}
