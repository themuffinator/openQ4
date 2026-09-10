// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "WindowsTextStore.h"
#if defined(_WIN32)
#include "../../ui/retained/TextInput.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>

namespace openq4::sys {
using namespace ui;
namespace {
template<class T> struct Ref {
	T* p = nullptr;
	Ref() = default;
	explicit Ref(T* value) : p(value) { if (p) p->AddRef(); }
	~Ref() { if (p) p->Release(); }
	Ref(const Ref&) = delete;
	Ref& operator=(const Ref&) = delete;
	T* Take() { T* result=p; p=nullptr; return result; }
};
struct CounterScope { unsigned& n; explicit CounterScope(unsigned& value) : n(value) { ++n; } ~CounterScope(){--n;} };
bool RectValid(const RECT& r) { return r.left<=r.right && r.top<=r.bottom; }
bool Contains(const RECT& r,const POINT& p) { return p.x>=r.left && p.x<r.right && p.y>=r.top && p.y<r.bottom; }
bool RectEmpty(const RECT& r) { return r.left==r.right || r.top==r.bottom; }
double Distance(const POINT& a,const POINT& b) { const double x=double(a.x)-b.x,y=double(a.y)-b.y; return x*x+y*y; }
bool FromWide(const WCHAR* text,ULONG size,std::string& out) {
	if ((!text && size) || size>65536) return false;
	std::string candidate;
	for (ULONG i=0;i<size;++i) {
		std::uint32_t c=text[i];
		if (!c || (c>=0xdc00 && c<=0xdfff)) return false;
		if (c>=0xd800 && c<=0xdbff) {
			if (++i>=size || text[i]<0xdc00 || text[i]>0xdfff) return false;
			c=0x10000+((c-0xd800)<<10)+(text[i]-0xdc00);
		}
		if (c<0x80) candidate.push_back(char(c));
		else if (c<0x800) {candidate.push_back(char(0xc0|(c>>6)));candidate.push_back(char(0x80|(c&63)));}
		else if (c<0x10000) {candidate.push_back(char(0xe0|(c>>12)));candidate.push_back(char(0x80|((c>>6)&63)));candidate.push_back(char(0x80|(c&63)));}
		else {candidate.push_back(char(0xf0|(c>>18)));candidate.push_back(char(0x80|((c>>12)&63)));candidate.push_back(char(0x80|((c>>6)&63)));candidate.push_back(char(0x80|(c&63)));}
	}
	if (candidate.size()>65536) return false;
	out=std::move(candidate); return true;
}
std::wstring ToWide(std::string_view text) {
	std::wstring out;
	for (std::size_t i=0;i<text.size();) {
		const auto lead=static_cast<unsigned char>(text[i++]);
		std::uint32_t c=lead; unsigned count=0;
		if (lead>=0xf0) {c=lead&7;count=3;} else if (lead>=0xe0) {c=lead&15;count=2;} else if (lead>=0xc0) {c=lead&31;count=1;}
		while (count--) c=(c<<6)|(static_cast<unsigned char>(text[i++])&63);
		if(c>0xffff) {c-=0x10000;out.push_back(WCHAR(0xd800+(c>>10)));out.push_back(WCHAR(0xdc00+(c&1023)));}
		else out.push_back(WCHAR(c));
	}
	return out;
}
}

struct WindowsTextStore::Impl {
	std::atomic<ULONG> refs{1};
	const std::thread::id thread=std::this_thread::get_id();
	NativeTextDocument document;
	NativeTextIdentity identity;
	HWND window=nullptr;
	bool healthy=true, upgrading=false;
	unsigned foreign=0;
	unsigned notifying=0;
	bool notificationWrite=false;
	ITextStoreACPSink* sink=nullptr;
	IUnknown* sinkIdentity=nullptr; // Borrowed from the owned sink reference.
	IUnknown* context=nullptr;
	DWORD mask=0;
	std::optional<NativeTextLockScope> scope;
	std::optional<WindowsTextLayout> layout;
	std::uint64_t dispatch=0, compositionHigh=0, receipts=0, layoutHigh=0;
	struct Composition { IUnknown* identity; std::uint64_t token; bool ended; };
	std::vector<Composition> compositions; // Up to 256 lifetime identities, refs prevent pointer reuse.
	explicit Impl(NativeTextLimits limits) : document(limits) {}
	~Impl() {
		for (auto& c:compositions) c.identity->Release();
		if(context) context->Release();
		if(sink) sink->Release();
	}
	bool Owner() const {return std::this_thread::get_id()==thread;}
	void Fault() noexcept {
		healthy=false; scope.reset(); layout.reset(); upgrading=false;
		notificationWrite=false;
		try {std::string e; (void)document.Retire(identity,e);} catch (...) {}
	}
	HRESULT Gate(bool write=false) const {
		if (!Owner()) return RPC_E_WRONG_THREAD;
		if (!healthy || !document.Active()) return TF_E_DISCONNECTED;
		if (foreign) return E_UNEXPECTED;
		if (!scope || (write && scope->access!=NativeTextAccess::ReadWrite)) return TS_E_NOLOCK;
		return S_OK;
	}
	bool Snapshot(NativeTextSnapshot& out) {
		std::string error;
		if(scope) return document.Read(*scope,out,error);
		NativeTextLockScope read;
		if(document.RequestLock(identity,NativeTextAccess::Read,true,read,error)!=NativeTextLockResult::Granted) return false;
		std::uint64_t published=0;
		if(!document.Read(read,out,error) || !document.FinishLock(read,0,published,error)) {Fault();return false;}
		return true;
	}
	HRESULT Range(LONG first,LONG last) {
		if(first<0 || last<first) return TS_E_INVALIDPOS;
		std::size_t ignored; std::string e;
		if(!document.AcpToByte(*scope,static_cast<std::uint32_t>(first),ignored,e) ||
			!document.AcpToByte(*scope,static_cast<std::uint32_t>(last),ignored,e)) return TS_E_INVALIDPOS;
		return S_OK;
	}
	HRESULT Selection(LONG& anchor,LONG& caret) {
		NativeTextSnapshot snapshot; std::string e; std::uint32_t a,c;
		if(!document.Read(*scope,snapshot,e) || !document.ByteToAcp(*scope,snapshot.anchor,a,e) || !document.ByteToAcp(*scope,snapshot.caret,c,e)) return E_FAIL;
		anchor=static_cast<LONG>(a); caret=static_cast<LONG>(c); return S_OK;
	}
	HRESULT Change(LONG first,LONG last,const WCHAR* text,ULONG count,TS_TEXTCHANGE* out) {
		if(!out) return E_INVALIDARG;
		HRESULT hr=Range(first,last); if(FAILED(hr)) return hr;
		std::string replacement,e;
		if(!FromWide(text,count,replacement)) return E_INVALIDARG;
		const LONG end=first+static_cast<LONG>(count);
		// Preserve documented SetText selection-then-insert ordering. Any failure
		// after accepted candidate edits faults the complete document in RequestLock.
		if(!document.SelectACP(*scope,first,last,e) || !document.ReplaceACP(*scope,first,last,replacement,e) ||
			!document.SelectACP(*scope,end,end,e)) return E_FAIL;
		layout.reset(); *out={first,last,end}; return S_OK;
	}
	HRESULT Canonical(IUnknown* from,Ref<IUnknown>& out) {
		if(!from) return E_INVALIDARG;
		CounterScope guard(foreign);
		return from->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&out.p));
	}
	HRESULT Extent(ITfCompositionView* composition,ITfRange* supplied,LONG& first,LONG& last) {
		Ref<ITfRange> range; Ref<ITfRangeACP> acp; Ref<ITfContext> rangeContext; Ref<IUnknown> rangeIdentity;
		CounterScope guard(foreign);
		HRESULT hr=S_OK;
		if(supplied) {supplied->AddRef();range.p=supplied;} else hr=composition->GetRange(&range.p);
		if(FAILED(hr) || !range.p) return FAILED(hr)?hr:E_UNEXPECTED;
		hr=range.p->GetContext(&rangeContext.p);
		if(FAILED(hr) || !rangeContext.p || !context) return E_UNEXPECTED;
		hr=rangeContext.p->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&rangeIdentity.p));
		if(FAILED(hr) || rangeIdentity.p!=context) return E_UNEXPECTED;
		hr=range.p->QueryInterface(__uuidof(ITfRangeACP),reinterpret_cast<void**>(&acp.p));
		if(FAILED(hr) || !acp.p) return FAILED(hr)?hr:E_UNEXPECTED;
		LONG count=0; hr=acp.p->GetExtent(&first,&count);
		if(FAILED(hr)) return hr;
		if(first<0 || count<0 || first>LONG_MAX-count) return TS_E_INVALIDPOS;
		last=first+count; return S_OK;
	}
	HRESULT LayoutReady() {
		if(!layout || layout->engineRevision!=document.EngineRevision() || layout->shadowRevision!=document.ShadowRevision()) return TS_E_NOLAYOUT;
		NativeTextSnapshot snapshot;
		if(!Snapshot(snapshot) || snapshot.text!=layout->text) return TS_E_NOLAYOUT;
		return S_OK;
	}
};

WindowsTextStore::WindowsTextStore(NativeTextLimits limits) : impl(std::make_unique<Impl>(limits)) {}
WindowsTextStore::~WindowsTextStore() = default;
template<class F> HRESULT WindowsTextStore::Safe(F&& operation) noexcept {
	if(!impl->Owner()) return RPC_E_WRONG_THREAD;
	Ref<WindowsTextStore> keep(this);
	try {return operation();}
	catch(const std::bad_alloc&) {impl->Fault();return E_OUTOFMEMORY;}
	catch(...) {impl->Fault();return E_UNEXPECTED;}
}
HRESULT WindowsTextStore::Create(NativeTextIdentity identity,std::uint64_t revision,std::string_view text,
	std::size_t anchor,std::size_t caret,HWND window,WindowsTextStore** out,NativeTextLimits limits) noexcept {
	if(!out) return E_INVALIDARG;
	try {
		std::unique_ptr<WindowsTextStore> candidate(new WindowsTextStore(limits)); std::string e;
		if(!window || !candidate->impl->document.Open(identity,revision,text,anchor,caret,e)) return E_INVALIDARG;
		candidate->impl->identity=identity;candidate->impl->window=window;*out=candidate.release();return S_OK;
	} catch(const std::bad_alloc&) {return E_OUTOFMEMORY;} catch(...) {return E_UNEXPECTED;}
}
HRESULT WindowsTextStore::QueryInterface(REFIID iid,void** out) {
	if(!out) return E_POINTER;
	*out=nullptr;
	if(iid==__uuidof(IUnknown) || iid==__uuidof(ITextStoreACP)) *out=static_cast<ITextStoreACP*>(this);
	else if(iid==__uuidof(ITfContextOwnerCompositionSink)) *out=static_cast<ITfContextOwnerCompositionSink*>(this);
	else if(iid==__uuidof(ITfTextEditSink)) *out=static_cast<ITfTextEditSink*>(this);
	else return E_NOINTERFACE;
	AddRef();return S_OK;
}
ULONG WindowsTextStore::AddRef(){return ++impl->refs;}
ULONG WindowsTextStore::Release(){const ULONG left=--impl->refs;if(!left) delete this;return left;}
bool WindowsTextStore::Healthy() const noexcept{return impl->Owner() && impl->healthy && impl->document.Active();}
std::uint64_t WindowsTextStore::EditReceipts() const noexcept{return impl->Owner()?impl->receipts:0;}
HRESULT WindowsTextStore::BindContext(NativeTextIdentity id,IUnknown* context) noexcept {return Safe([&]() -> HRESULT {
	if(id!=impl->identity || !Healthy() || impl->scope || impl->foreign || impl->notifying) return E_UNEXPECTED;
	Ref<IUnknown> identity;HRESULT hr=impl->Canonical(context,identity);if(FAILED(hr)) return hr;
	if(!Healthy() || !identity.p) return TF_E_DISCONNECTED;
	if(impl->context) return impl->context==identity.p?S_OK:E_UNEXPECTED;
	impl->context=identity.Take();return S_OK;
});}
HRESULT WindowsTextStore::SetDispatch(NativeTextIdentity id,std::uint64_t value) noexcept {return Safe([&]() -> HRESULT {
	if(id!=impl->identity || !Healthy() || impl->scope || impl->foreign || impl->notifying || value<impl->dispatch) return E_UNEXPECTED;
	impl->dispatch=value;return S_OK;
});}
HRESULT WindowsTextStore::Peek(NativeTextIdentity id,NativeTextOffer& out) noexcept {return Safe([&]() -> HRESULT {
	if(id!=impl->identity || !Healthy() || impl->scope || impl->foreign || impl->notifying) return E_UNEXPECTED;
	if(!impl->document.PendingCount()) return S_FALSE;
	std::string e;return impl->document.PeekOffer(id,out,e)?S_OK:E_FAIL;
});}
HRESULT WindowsTextStore::Acknowledge(NativeTextIdentity id,std::uint64_t tx,std::uint64_t shadow,std::uint64_t expected,std::uint64_t accepted) noexcept {return Safe([&]() -> HRESULT {
	if(impl->foreign || impl->notifying || impl->scope || !Healthy()) return E_UNEXPECTED;
	std::string e;if(!impl->document.Acknowledge(id,tx,shadow,expected,accepted,e)) return E_INVALIDARG;
	impl->layout.reset();return S_OK;
});}
HRESULT WindowsTextStore::Reject(NativeTextIdentity id,std::uint64_t tx,std::uint64_t expected) noexcept {return Safe([&]() -> HRESULT {
	if(impl->foreign || impl->notifying || impl->scope || !Healthy()) return E_UNEXPECTED;
	std::string e;if(!impl->document.Reject(id,tx,expected,e)) return E_INVALIDARG;
	impl->Fault();return S_OK;
});}
HRESULT WindowsTextStore::Retire(NativeTextIdentity id) noexcept {return Safe([&]() -> HRESULT {
	if(id!=impl->identity) return E_INVALIDARG;
	impl->Fault();return S_OK;
});}

HRESULT WindowsTextStore::Notify(DWORD requested,const TS_TEXTCHANGE* change) {
	// The committed application snapshot is visible before calling the sink.
	// Hold the sink through the batch, including any deferred write callback.
	const DWORD mask=requested&impl->mask;
	if(!impl->sink || !mask) return S_OK;
	Ref<ITextStoreACPSink> sink;
	{CounterScope guard(impl->foreign);impl->sink->AddRef();sink.p=impl->sink;}
	if(!Healthy()) return TF_E_DISCONNECTED;
	auto check=[&](HRESULT hr) {
		if(FAILED(hr) || !Healthy() || impl->sink!=sink.p) {
			impl->Fault();return FAILED(hr)?hr:TF_E_DISCONNECTED;
		}
		return S_OK;
	};
	{
		CounterScope guard(impl->notifying);
		HRESULT hr=S_OK;
		if(mask&TS_AS_TEXT_CHANGE) {hr=check(sink.p->OnTextChange(0,change));if(FAILED(hr))return hr;}
		if(mask&TS_AS_SEL_CHANGE) {hr=check(sink.p->OnSelectionChange());if(FAILED(hr))return hr;}
		if(mask&TS_AS_LAYOUT_CHANGE) {hr=check(sink.p->OnLayoutChange(TS_LC_CHANGE,1));if(FAILED(hr))return hr;}
	}
	if(impl->notificationWrite) {
		impl->notificationWrite=false;
		HRESULT session=E_FAIL;const HRESULT hr=RequestLock(TS_LF_READWRITE,&session);
		if(FAILED(hr) || FAILED(session) || !Healthy()) {
			impl->Fault();return FAILED(hr)?hr:(FAILED(session)?session:TF_E_DISCONNECTED);
		}
	}
	return S_OK;
}
HRESULT WindowsTextStore::SyncEngine(NativeTextIdentity id,std::uint64_t expectedEngine,
	std::uint64_t expectedShadow,std::uint64_t revision,std::string_view text,
	std::size_t anchor,std::size_t caret) noexcept {return Safe([&]() -> HRESULT {
	if(!Healthy() || impl->foreign || impl->notifying || impl->scope) return E_UNEXPECTED;
	NativeTextSnapshot before;std::string e;
	if(!impl->Snapshot(before)) return E_FAIL;
	// Own the request before any foreign callback; model validation is atomic.
	const std::string candidate(text);
	if(!impl->document.SyncEngine(id,expectedEngine,expectedShadow,revision,candidate,anchor,caret,e)) return E_INVALIDARG;
	DWORD mask=0;TS_TEXTCHANGE change{};
	if(before.text!=candidate) {
		// A full-field range remains exact for all UTF-16 scalars and keeps
		// application changes independent of native transaction inference.
		change={0,static_cast<LONG>(ToWide(before.text).size()),static_cast<LONG>(ToWide(candidate).size())};
		mask|=TS_AS_TEXT_CHANGE;
	}
	if(before.anchor!=anchor || before.caret!=caret) mask|=TS_AS_SEL_CHANGE;
	if(mask) impl->layout.reset();
	return Notify(mask,&change);
});}

HRESULT WindowsTextStore::AdviseSink(REFIID iid,IUnknown* source,DWORD mask) {return Safe([&]() -> HRESULT {
	if(impl->foreign || impl->notifying || !Healthy()) return E_UNEXPECTED;
	if(iid!=__uuidof(ITextStoreACPSink) || !source || (mask&~TS_AS_ALL_SINKS)) return E_INVALIDARG;
	Ref<IUnknown> identity;HRESULT hr=impl->Canonical(source,identity);if(FAILED(hr) || !identity.p) return E_UNEXPECTED;
	if(!Healthy()) return TF_E_DISCONNECTED;
	if(impl->sink) {if(identity.p!=impl->sinkIdentity) return CONNECT_E_ADVISELIMIT;impl->mask=mask;return S_OK;}
	Ref<ITextStoreACPSink> sink;
	{CounterScope guard(impl->foreign);hr=source->QueryInterface(iid,reinterpret_cast<void**>(&sink.p));}
	if(FAILED(hr) || !sink.p) return CONNECT_E_ADVISELIMIT;
	if(!Healthy()) return TF_E_DISCONNECTED;
	impl->sinkIdentity=identity.p;impl->sink=sink.Take();impl->mask=mask;return S_OK;
});}
HRESULT WindowsTextStore::UnadviseSink(IUnknown* source) {return Safe([&]() -> HRESULT {
	if(impl->foreign || impl->notifying) return E_UNEXPECTED;
	Ref<IUnknown> identity;HRESULT hr=impl->Canonical(source,identity);if(FAILED(hr)) return E_INVALIDARG;
	if(!impl->sink || identity.p!=impl->sinkIdentity) return CONNECT_E_NOCONNECTION;
	ITextStoreACPSink* old=impl->sink;impl->sink=nullptr;impl->sinkIdentity=nullptr;impl->mask=0;
	{CounterScope guard(impl->foreign);old->Release();}
	return S_OK;
});}
HRESULT WindowsTextStore::RequestLock(DWORD flags,HRESULT* session) {return Safe([&]() -> HRESULT {
	if(!session || (flags&~(TS_LF_SYNC|TS_LF_READWRITE)) || ((flags&TS_LF_READWRITE)!=TS_LF_READ && (flags&TS_LF_READWRITE)!=TS_LF_READWRITE)) return E_INVALIDARG;
	if(!Healthy() || impl->foreign || !impl->sink) return E_UNEXPECTED;
	NativeTextLockScope granted;std::string e;
	const auto access=(flags&TS_LF_READWRITE)==TS_LF_READWRITE?NativeTextAccess::ReadWrite:NativeTextAccess::Read;
	if(impl->notifying && access==NativeTextAccess::ReadWrite) {
		if(flags&TS_LF_SYNC) {*session=TS_E_SYNCHRONOUS;return S_OK;}
		impl->notificationWrite=true;*session=TS_S_ASYNC;return S_OK;
	}
	const auto result=impl->document.RequestLock(impl->identity,access,(flags&TS_LF_SYNC)!=0,granted,e);
	if(result==NativeTextLockResult::Deferred) {impl->upgrading=true;*session=TS_S_ASYNC;return S_OK;}
	if(result==NativeTextLockResult::Refused) {*session=TS_E_SYNCHRONOUS;return S_OK;}
	Ref<ITextStoreACPSink> sink(impl->sink);
	auto callback=[&](NativeTextLockScope scope,DWORD callbackFlags) -> HRESULT {
		impl->scope=scope;
		HRESULT hr=sink.p->OnLockGranted(callbackFlags);
		if(FAILED(hr) || !Healthy()) {impl->Fault();return FAILED(hr)?hr:TF_E_DISCONNECTED;}
		std::uint64_t published=0;
		if(!impl->document.FinishLock(scope,impl->dispatch,published,e)) {impl->Fault();return E_FAIL;}
		impl->scope.reset();if(published) impl->layout.reset();return hr;
	};
	*session=callback(granted,flags);
	if(impl->upgrading && Healthy()) {
		impl->upgrading=false;
		if(impl->sink!=sink.p || !impl->document.GrantDeferredWrite(impl->identity,granted,e)) {impl->Fault();return E_FAIL;}
		if(FAILED(callback(granted,TS_LF_READWRITE))) return E_FAIL;
	}
	return S_OK;
});}

HRESULT WindowsTextStore::GetStatus(TS_STATUS* out) {return Safe([&]() -> HRESULT {
	if(!out) return E_INVALIDARG;if(impl->foreign) return E_UNEXPECTED;
	*out={Healthy()?0u:DWORD(TS_SD_READONLY),TS_SS_NOHIDDENTEXT};return S_OK;
});}
HRESULT WindowsTextStore::QueryInsert(LONG first,LONG last,ULONG count,LONG* outFirst,LONG* outLast) {return Safe([&]() -> HRESULT {
	if(!outFirst || !outLast || count>65536) return E_INVALIDARG;
	if(!Healthy() || impl->foreign) return E_UNEXPECTED;
	NativeTextSnapshot snapshot;std::vector<NativeTextBoundary> map;std::string e;
	if(!impl->Snapshot(snapshot) || !BuildNativeTextMap(snapshot.text,map,e)) return E_FAIL;
	auto boundary=[&](LONG acp){return acp>=0 && std::any_of(map.begin(),map.end(),[&](const auto& b){return b.acp==static_cast<std::uint32_t>(acp);});};
	if(first>last || !boundary(first) || !boundary(last)) return E_INVALIDARG;
	*outFirst=last;*outLast=last;return S_OK;
});}
HRESULT WindowsTextStore::GetSelection(ULONG index,ULONG count,TS_SELECTION_ACP* out,ULONG* fetched) {return Safe([&]() -> HRESULT {
	if(!fetched || (count && !out)) return E_INVALIDARG;
	HRESULT hr=impl->Gate();if(FAILED(hr)) return hr;
	if(index!=0 && index!=TS_DEFAULT_SELECTION) return TS_E_NOSELECTION;
	if(!count) {*fetched=0;return S_OK;}
	LONG a,c;hr=impl->Selection(a,c);if(FAILED(hr)) return hr;
	*out={std::min(a,c),std::max(a,c),{c<a?TS_AE_START:TS_AE_END,FALSE}};*fetched=1;return S_OK;
});}
HRESULT WindowsTextStore::SetSelection(ULONG count,const TS_SELECTION_ACP* value) {return Safe([&]() -> HRESULT {
	if(count!=1 || !value || value->style.fInterimChar || (value->style.ase!=TS_AE_START && value->style.ase!=TS_AE_END && value->style.ase!=TS_AE_NONE) ||
		(value->style.ase==TS_AE_NONE && value->acpStart!=value->acpEnd)) return E_INVALIDARG;
	HRESULT hr=impl->Gate(true);if(FAILED(hr)) return hr;hr=impl->Range(value->acpStart,value->acpEnd);if(FAILED(hr)) return hr;
	const LONG a=value->style.ase==TS_AE_START?value->acpEnd:value->acpStart,c=value->style.ase==TS_AE_START?value->acpStart:value->acpEnd;
	std::string e;return impl->document.SelectACP(*impl->scope,a,c,e)?S_OK:E_FAIL;
});}
HRESULT WindowsTextStore::GetText(LONG first,LONG last,WCHAR* plain,ULONG capacity,ULONG* written,TS_RUNINFO* runs,ULONG runCapacity,ULONG* runCount,LONG* next) {return Safe([&]() -> HRESULT {
	if(!written || !runCount || !next || (capacity && !plain) || (runCapacity && !runs)) return E_INVALIDARG;
	HRESULT hr=impl->Gate();if(FAILED(hr)) return hr;
	NativeTextSnapshot snapshot;if(!impl->Snapshot(snapshot)) return E_FAIL;const auto wide=ToWide(snapshot.text);
	if(last==-1) last=static_cast<LONG>(wide.size());
	if(first<0 || last<first || static_cast<std::size_t>(last)>wide.size()) return TS_E_INVALIDPOS;
	// GetText may stream individual UTF-16 code units; only mutation endpoints
	// must be scalar boundaries. The next call can continue at a low surrogate.
	ULONG take=static_cast<ULONG>(last-first);
	if(capacity) take=std::min(take,capacity);else if(!runCapacity) take=0;
	if(capacity) std::copy_n(wide.data()+first,take,plain);
	if(runCapacity && take) *runs={take,TS_RT_PLAIN};
	*written=capacity?take:0;*runCount=runCapacity && take?1:0;*next=first+static_cast<LONG>(take);return S_OK;
});}
HRESULT WindowsTextStore::SetText(DWORD flags,LONG first,LONG last,const WCHAR* text,ULONG count,TS_TEXTCHANGE* change) {return Safe([&]() -> HRESULT {
	if(flags&~TS_ST_CORRECTION) return E_INVALIDARG;
	HRESULT hr=impl->Gate(true);return FAILED(hr)?hr:impl->Change(first,last,text,count,change);
});}
HRESULT WindowsTextStore::InsertTextAtSelection(DWORD flags,const WCHAR* text,ULONG count,LONG* first,LONG* last,TS_TEXTCHANGE* change) {return Safe([&]() -> HRESULT {
	if((flags&~(TS_IAS_NOQUERY|TS_IAS_QUERYONLY)) || flags==(TS_IAS_NOQUERY|TS_IAS_QUERYONLY) || (!(flags&TS_IAS_NOQUERY) && (!first || !last))) return E_INVALIDARG;
	HRESULT hr=impl->Gate((flags&TS_IAS_QUERYONLY)==0);if(FAILED(hr)) return hr;
	LONG a,c;hr=impl->Selection(a,c);if(FAILED(hr)) return hr;const LONG start=std::min(a,c),end=std::max(a,c);
	std::string replacement;if(!FromWide(text,count,replacement)) return E_INVALIDARG;
	if(flags&TS_IAS_QUERYONLY) {*first=start+static_cast<LONG>(count);*last=*first;return S_OK;}
	hr=impl->Change(start,end,text,count,change);if(FAILED(hr)) return hr;
	if(!(flags&TS_IAS_NOQUERY)) {*first=start+static_cast<LONG>(count);*last=*first;}return S_OK;
});}
HRESULT WindowsTextStore::GetEndACP(LONG* end) {return Safe([&]() -> HRESULT {
	if(!end) return E_INVALIDARG;HRESULT hr=impl->Gate();if(FAILED(hr)) return hr;
	NativeTextSnapshot snapshot;if(!impl->Snapshot(snapshot)) return E_FAIL;*end=static_cast<LONG>(ToWide(snapshot.text).size());return S_OK;
});}

HRESULT WindowsTextStore::SetLayout(const WindowsTextLayout& value) noexcept {return Safe([&]() -> HRESULT {
	if(!Healthy() || impl->foreign || impl->notifying || impl->scope || impl->document.PendingCount() || value.identity!=impl->identity || value.engineRevision!=impl->document.EngineRevision() ||
		value.shadowRevision!=impl->document.ShadowRevision() || !value.layoutRevision || !RectValid(value.screen)) return E_INVALIDARG;
	if(value.layoutRevision<=impl->layoutHigh) return E_INVALIDARG;
	NativeTextSnapshot snapshot;std::string e;std::vector<NativeTextBoundary> map;
	if(!impl->Snapshot(snapshot) || value.text!=snapshot.text || !BuildNativeTextMap(value.text,map,e) || value.cells.size()+1!=map.size()) return E_INVALIDARG;
	for(std::size_t i=0;i<value.cells.size();++i) {
		const auto& c=value.cells[i];
		if(c.first!=static_cast<LONG>(map[i].acp) || c.last!=static_cast<LONG>(map[i+1].acp) || !RectValid(c.ink)) return E_INVALIDARG;
	}
	WindowsTextLayout candidate=value;impl->layout=std::move(candidate);impl->layoutHigh=value.layoutRevision;
	return Notify(TS_AS_LAYOUT_CHANGE,nullptr);
});}
HRESULT WindowsTextStore::GetActiveView(TsViewCookie* out) {return Safe([&]() -> HRESULT {if(!out) return E_INVALIDARG;if(!Healthy() || impl->foreign) return E_UNEXPECTED;*out=1;return S_OK;});}
HRESULT WindowsTextStore::GetWnd(TsViewCookie view,HWND* out) {return Safe([&]() -> HRESULT {if(!out || view!=1) return E_INVALIDARG;if(!Healthy() || impl->foreign) return E_UNEXPECTED;*out=impl->window;return S_OK;});}
HRESULT WindowsTextStore::GetScreenExt(TsViewCookie view,RECT* out) {return Safe([&]() -> HRESULT {
	if(view!=1 || !out) return E_INVALIDARG;if(!Healthy() || impl->foreign) return E_UNEXPECTED;
	HRESULT hr=impl->LayoutReady();if(FAILED(hr)) return hr;*out=impl->layout->visible?impl->layout->screen:RECT{};return S_OK;
});}
HRESULT WindowsTextStore::GetTextExt(TsViewCookie view,LONG first,LONG last,RECT* out,BOOL* clipped) {return Safe([&]() -> HRESULT {
	if(view!=1 || !out || !clipped || first==last) return E_INVALIDARG;
	HRESULT hr=impl->Gate();if(FAILED(hr)) return hr;hr=impl->Range(first,last);if(FAILED(hr)) return hr;hr=impl->LayoutReady();if(FAILED(hr)) return hr;
	RECT result{};bool any=false,clip=false;
	if(impl->layout->visible) for(const auto& c:impl->layout->cells) if(c.first>=first && c.last<=last) {
		clip=clip||c.clipped;if(RectEmpty(c.ink)) continue;
		if(!any) {result=c.ink;any=true;} else {result.left=std::min(result.left,c.ink.left);result.top=std::min(result.top,c.ink.top);result.right=std::max(result.right,c.ink.right);result.bottom=std::max(result.bottom,c.ink.bottom);}
	}
	*out=result;*clipped=clip?TRUE:FALSE;return S_OK;
});}
HRESULT WindowsTextStore::GetACPFromPoint(TsViewCookie view,const POINT* point,DWORD flags,LONG* out) {return Safe([&]() -> HRESULT {
	if(view!=1 || !point || !out || (flags&~(GXFPF_NEAREST|GXFPF_ROUND_NEAREST))) return E_INVALIDARG;
	if(!Healthy() || impl->foreign) return E_UNEXPECTED;HRESULT hr=impl->LayoutReady();if(FAILED(hr)) return hr;
	if(!impl->layout->visible) return TS_E_INVALIDPOINT;
	const WindowsTextCell* hit=nullptr;
	for(const auto& c:impl->layout->cells) if(Contains(c.ink,*point)) {hit=&c;break;}
	if(hit) {*out=(flags&GXFPF_ROUND_NEAREST) && Distance(*point,hit->after)<Distance(*point,hit->before)?hit->last:hit->first;return S_OK;}
	if(!(flags&GXFPF_NEAREST) || impl->layout->cells.empty()) return TS_E_INVALIDPOINT;
	double best=std::numeric_limits<double>::infinity();LONG found=0;
	for(const auto& c:impl->layout->cells) for(const auto& stop:{std::pair{c.first,c.before},std::pair{c.last,c.after}}) {
		const double d=Distance(*point,stop.second);if(d<best) {best=d;found=stop.first;}
	}
	*out=found;return S_OK;
});}

HRESULT WindowsTextStore::OnStartComposition(ITfCompositionView* source,BOOL* accepted) {return Safe([&]() -> HRESULT {
	if(!accepted || !source) return E_INVALIDARG;
	*accepted=FALSE;
	if(impl->foreign || !Healthy()) return TF_E_DISCONNECTED;
	if(!impl->scope || impl->scope->access!=NativeTextAccess::ReadWrite) return S_OK;
	Ref<IUnknown> identity;HRESULT hr=impl->Canonical(source,identity);if(FAILED(hr) || !identity.p) {impl->Fault();return E_FAIL;}
	for(const auto& c:impl->compositions) if(c.identity==identity.p) return S_OK;
	if(impl->compositions.size()>=256 || impl->compositionHigh==UINT64_MAX) return S_OK;
	LONG first,last;hr=impl->Extent(source,nullptr,first,last);if(FAILED(hr) || !Healthy()) {impl->Fault();return E_FAIL;}
	impl->compositions.reserve(impl->compositions.size()+1);std::string e;
	const auto token=impl->compositionHigh+1;
	if(!impl->document.BeginComposition(*impl->scope,token,first,last,e)) {impl->Fault();return E_FAIL;}
	impl->compositions.push_back({identity.Take(),token,false});impl->compositionHigh=token;*accepted=TRUE;return S_OK;
});}
HRESULT WindowsTextStore::OnUpdateComposition(ITfCompositionView* source,ITfRange* range) {return Safe([&]() -> HRESULT {
	if(!source) return E_INVALIDARG;if(!Healthy() || impl->foreign) return TF_E_DISCONNECTED;
	Ref<IUnknown> identity;HRESULT hr=impl->Canonical(source,identity);if(FAILED(hr)) {impl->Fault();return E_FAIL;}
	auto found=std::find_if(impl->compositions.begin(),impl->compositions.end(),[&](const auto& c){return c.identity==identity.p;});
	if(found==impl->compositions.end() || found->ended || FAILED(impl->Gate(true))) {impl->Fault();return E_UNEXPECTED;}
	LONG first,last;hr=impl->Extent(source,range,first,last);if(FAILED(hr) || !Healthy()) {impl->Fault();return E_FAIL;}
	std::string e;if(!impl->document.UpdateComposition(*impl->scope,found->token,first,last,e)) {impl->Fault();return E_FAIL;}return S_OK;
});}
HRESULT WindowsTextStore::OnEndComposition(ITfCompositionView* source) {return Safe([&]() -> HRESULT {
	if(!source) return E_INVALIDARG;if(!Healthy() || impl->foreign) return TF_E_DISCONNECTED;
	Ref<IUnknown> identity;HRESULT hr=impl->Canonical(source,identity);if(FAILED(hr)) {impl->Fault();return E_FAIL;}
	auto found=std::find_if(impl->compositions.begin(),impl->compositions.end(),[&](const auto& c){return c.identity==identity.p;});
	if(found==impl->compositions.end() || found->ended || FAILED(impl->Gate(true))) {impl->Fault();return E_UNEXPECTED;}
	std::string e;if(!impl->document.EndComposition(*impl->scope,found->token,e)) {impl->Fault();return E_FAIL;}found->ended=true;return S_OK;
});}
HRESULT WindowsTextStore::OnEndEdit(ITfContext* context,TfEditCookie,ITfEditRecord* record) {return Safe([&]() -> HRESULT {
	if(!context || !record) return E_INVALIDARG;if(!Healthy() || impl->foreign) return TF_E_DISCONNECTED;
	Ref<IUnknown> identity;HRESULT hr=impl->Canonical(context,identity);if(FAILED(hr)) return hr;
	if(!Healthy()) return TF_E_DISCONNECTED;
	if(!impl->context || identity.p!=impl->context) return E_UNEXPECTED;
	if(impl->receipts==UINT64_MAX) {impl->Fault();return E_FAIL;}
	++impl->receipts;return S_OK;
});}

// This plain store has no embedded objects, persistence format, or attributes.
// Do not advertise fabricated success for unsupported insertion/serialization.
HRESULT WindowsTextStore::GetFormattedText(LONG,LONG,IDataObject** out) {return Safe([&]() -> HRESULT {if(!out)return E_INVALIDARG;HRESULT hr=impl->Gate();if(FAILED(hr))return hr;*out=nullptr;return E_NOTIMPL;});}
HRESULT WindowsTextStore::GetEmbedded(LONG,REFGUID,REFIID,IUnknown** out) {return Safe([&]() -> HRESULT {if(!out)return E_INVALIDARG;HRESULT hr=impl->Gate();if(FAILED(hr))return hr;*out=nullptr;return TS_E_NOOBJECT;});}
HRESULT WindowsTextStore::QueryInsertEmbedded(const GUID*,const FORMATETC*,BOOL* out) {return Safe([&]() -> HRESULT {if(!out)return E_INVALIDARG;HRESULT hr=impl->Gate();if(FAILED(hr))return hr;*out=FALSE;return S_OK;});}
HRESULT WindowsTextStore::InsertEmbedded(DWORD,LONG,LONG,IDataObject*,TS_TEXTCHANGE*) {return Safe([&]() -> HRESULT {HRESULT hr=impl->Gate(true);return FAILED(hr)?hr:TS_E_FORMAT;});}
HRESULT WindowsTextStore::InsertEmbeddedAtSelection(DWORD,IDataObject*,LONG*,LONG*,TS_TEXTCHANGE*) {return Safe([&]() -> HRESULT {HRESULT hr=impl->Gate(true);return FAILED(hr)?hr:TS_E_FORMAT;});}
HRESULT WindowsTextStore::RequestSupportedAttrs(DWORD flags,ULONG count,const TS_ATTRID* attrs) {return Safe([&]() -> HRESULT {if((flags&~TS_ATTR_FIND_WANT_VALUE) || count>256 || (count && !attrs))return E_INVALIDARG;if(!Healthy() || impl->foreign)return E_UNEXPECTED;return S_OK;});}
HRESULT WindowsTextStore::RequestAttrsAtPosition(LONG position,ULONG count,const TS_ATTRID* attrs,DWORD flags) {return Safe([&]() -> HRESULT {if((flags&~TS_ATTR_FIND_WANT_VALUE) || count>256 || (count && !attrs))return E_INVALIDARG;HRESULT hr=impl->Gate();if(FAILED(hr))return hr;return impl->Range(position,position);});}
HRESULT WindowsTextStore::RequestAttrsTransitioningAtPosition(LONG position,ULONG count,const TS_ATTRID* attrs,DWORD flags) {return RequestAttrsAtPosition(position,count,attrs,flags);}
HRESULT WindowsTextStore::FindNextAttrTransition(LONG first,LONG halt,ULONG count,const TS_ATTRID* attrs,DWORD flags,LONG* next,BOOL* found,LONG* offset) {return Safe([&]() -> HRESULT {if(!next || !found || !offset || (flags&~(TS_ATTR_FIND_BACKWARDS|TS_ATTR_FIND_WANT_OFFSET)) || count>256 || (count && !attrs))return E_INVALIDARG;HRESULT hr=impl->Gate();if(FAILED(hr))return hr;hr=impl->Range(std::min(first,halt),std::max(first,halt));if(FAILED(hr))return hr;*next=halt;*found=FALSE;*offset=0;return S_OK;});}
HRESULT WindowsTextStore::RetrieveRequestedAttrs(ULONG count,TS_ATTRVAL* out,ULONG* fetched) {return Safe([&]() -> HRESULT {if(!fetched || (count && !out))return E_INVALIDARG;if(!Healthy() || impl->foreign)return E_UNEXPECTED;*fetched=0;return S_OK;});}
} // namespace openq4::sys
#endif
