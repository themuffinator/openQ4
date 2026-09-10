// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Actual Windows SDK interfaces and production methods. No COM activation,
// native input, clipboard, message-pump or window calls.
#include "sys/sdl3/WindowsTextStore.h"
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <thread>
#include <stdexcept>
using namespace openq4::sys;
using namespace openq4::ui;
static unsigned checks=0;
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
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
		if(iid==__uuidof(ITfRange)) {if(!out)return E_POINTER;*out=static_cast<ITfRange*>(this);AddRef();return S_OK;}
		return Counted::QueryInterface(iid,out);
	}
	HRESULT STDMETHODCALLTYPE GetExtent(LONG* first,LONG* count) override{if(reenter) reenter();*first=start;*count=length;return S_OK;}
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
struct Composition : Counted<ITfCompositionView> {
	Range range;
	HRESULT STDMETHODCALLTYPE GetOwnerClsid(CLSID*) override{return E_NOTIMPL;}
	HRESULT STDMETHODCALLTYPE GetRange(ITfRange** out) override{range.AddRef();*out=&range;return S_OK;}
};
struct Fixture {
	Sink sink; Context context; WindowsTextStore* store=nullptr;
	NativeTextIdentity id{17,29};
	explicit Fixture(std::string_view text="abc",NativeTextLimits limits={}) {
		OK(WindowsTextStore::Create(id,10,text,0,0,reinterpret_cast<HWND>(1),&store,limits));
		OK(store->AdviseSink(__uuidof(ITextStoreACPSink),&sink,TS_AS_ALL_SINKS));
		OK(store->BindContext(id,&context));
	}
	~Fixture(){if(store){OK(store->UnadviseSink(&sink));OK(store->Retire(id));CHECK(store->Release()==0);}CHECK(sink.refs==1 && context.refs==1);CHECK(!sink.notifications);}
	void Lock(DWORD flags,const std::function<void()>& f) {
		sink.callback=[&](DWORD got){CHECK(got==flags);f();return S_OK;};HRESULT result=E_FAIL;OK(store->RequestLock(flags,&result));OK(result);sink.callback={};
	}
	NativeTextOffer Offer(){NativeTextOffer offer;OK(store->Peek(id,offer));return offer;}
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
	OK(f.store->SetDispatch(f.id,100));CHECK(f.store->SetDispatch(f.id,99)==E_UNEXPECTED);
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
int main(){IdentityAndLocks();Utf16AndEdits();CompositionsAndLateMetadata();ForeignContextAndPendingBegin();FaultsAndReentry();LayoutAndUnsupported();MetadataAcknowledgements();CompositionAcrossLocks();DeferredSinkChange();ApplicationNotifications();ApplicationNoticeFaultsAndLayout();std::printf("PASS %u checks\n",checks);}
