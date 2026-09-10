// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Interaction.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

static long failAfter=-1;
void* operator new(std::size_t size) { if(failAfter==0) throw std::bad_alloc(); if(failAfter>0)--failAfter; if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc(); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
using namespace openq4::ui;
static unsigned checks;
#define CHECK(value) do { ++checks; if(!(value)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value); std::exit(1); } } while(false)
static bool Same(const TextEditState& a,const TextEditState& b) {return a.text==b.text && a.anchor==b.anchor && a.caret==b.caret;}
static bool Same(const std::vector<TextEditState>& a,const std::vector<TextEditState>& b) {
	if(a.size()!=b.size())return false; for(size_t i=0;i<a.size();++i)if(!Same(a[i],b[i]))return false;return true;
}
struct Fixture {
	DocumentModel model; Interaction input; std::string error;
	std::map<std::string,ControlReadback> values; std::map<std::string,ControlBounds> bounds;
	NativeTextIdentity nativeId{100,200}; NativeTextDocument native;
	NativeTextEditorBarrier barrier; std::uint64_t dispatch=10,fence=20;
	Fixture() {
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
static void CollectionAndRanges() {
	Fixture f;f.Text("1.25");CHECK(f.input.SetNumberSelection("n0",f.View().identity,4,1,f.error));
	const auto before=f.Save().widgets.at("n0").number.value();f.Attach();
	const auto offer=f.Offer([&](auto lock){CHECK(f.native.ReplaceACP(lock,0,4,"12345",f.error));CHECK(f.native.BeginComposition(lock,1,0,3,f.error));CHECK(f.native.BeginComposition(lock,2,2,5,f.error));CHECK(f.native.BeginComposition(lock,3,5,5,f.error));CHECK(f.native.SelectACP(lock,5,1,f.error));});
	NativeTextEditorReceipt rejected;CHECK(!f.input.ApplyNumberNative(f.barrier,offer,rejected,f.error));
	f.Begin(1);CHECK(f.View().nativeUnsettled && f.Drafts().blocking.at(0).nativeUnsettled);f.Apply(offer);
	const auto native=*f.View().nativePresentation;CHECK(native.text=="12345" && native.anchor==5 && native.caret==1 && native.compositions.size()==3);
	CHECK((native.compositions.at(2)==NativeTextRange{2,5}));CHECK(Same(f.View().state,before.state));
	auto saved=f.Save().widgets.at("n0").number.value();CHECK(Same(saved.state,before.state) && Same(saved.undo,before.undo) && Same(saved.redo,before.redo));
	CHECK(!f.input.SettleNumberNative(f.barrier,rejected,f.error));f.Complete();CHECK(!f.input.SettleNumberNative(f.barrier,rejected,f.error));
	++f.dispatch;++f.fence;
	const auto end=f.Offer([&](auto lock){CHECK(f.native.ReplaceACP(lock,0,5,"1e-",f.error));for(auto token:{1u,2u,3u})CHECK(f.native.EndComposition(lock,token,f.error));CHECK(f.native.SelectACP(lock,3,0,f.error));});
	f.Begin(2);f.Apply(end);CHECK(Same(f.View().state,before.state) && f.View().nativeUnsettled);CHECK(!f.input.SettleNumberNative(f.barrier,rejected,f.error));f.Complete();
	CHECK(f.View().nativeUnsettled && f.View().nativePresentation->compositions.empty());f.Settle();
	CHECK(!f.View().nativeUnsettled && f.View().state.text=="1e-" && f.View().state.anchor==3 && f.View().state.caret==0);
	CHECK(f.View().status!=TextNumberStatus::Valid && std::get<double>(f.input.Widget("n0")->accepted)==1.0 && f.input.TakeActions().empty());
	saved=f.Save().widgets.at("n0").number.value();CHECK(saved.undo.size()==before.undo.size()+1 && Same(saved.undo.back(),before.state));
	CHECK(!f.input.CommitNumberEdit("n0",f.View().identity,f.error));f.Retire();
	CHECK(f.input.UndoNumberEdit("n0",f.View().identity,false,f.error));CHECK(Same(f.View().state,before.state));
	CHECK(f.input.UndoNumberEdit("n0",f.View().identity,true,f.error));CHECK(f.View().state.text=="1e-");
}
static void GatesAndRetirement() {
	for(unsigned reason=0;reason<7;++reason) {
		Fixture f;f.Text("1.5");const auto stable=f.View().state;f.Attach();
		const auto tx=f.Offer([&](auto lock){CHECK(f.native.ReplaceACP(lock,0,3,"1.75",f.error));CHECK(f.native.BeginComposition(lock,1,0,4,f.error));});f.Begin(1);f.Apply(tx);
		const auto id=f.View().identity;const auto expected=f.barrier;const auto stamp=f.Drafts().barrier;
		CHECK(!f.input.SetNumberSelection("n0",id,0,0,f.error));CHECK(!f.input.ReplaceNumberSelection("n0",id,"2",f.error));
		CHECK(!f.input.ApplyNumberInput("n0",id,TextInputEvent{},f.error));CHECK(!f.input.UndoNumberEdit("n0",id,false,f.error));
		CHECK(!f.input.CommitNumberEdit("n0",id,f.error));CHECK(!f.input.ApplyNumberOperation("n0",id,TextEditOperation{},f.error));
		CHECK(f.Drafts().barrier==stamp && f.View().nativePresentation->text=="1.75");
		CHECK(f.input.FocusNumberDraft(stamp,"n0",f.error) && f.Drafts().barrier==stamp);
		if(reason==0)CHECK(f.input.CancelNumberEdit("n0",id));
		if(reason==1)CHECK(f.input.Focus("n1"));
		if(reason==2)f.input.Cancel();
		if(reason==3) {f.values["n0"].value=1.25;CHECK(f.input.SetReadbacks(f.values,f.error));CHECK(f.View().conflict);}
		if(reason==4) {auto save=f.Save();CHECK(f.input.RestoreWidgets(save,f.error));}
		if(reason==5)CHECK(f.input.SetEnabled("n0",false));
		if(reason==6)f.input.InvalidateLayout();
		CHECK(!f.View().nativePresentation && !f.View().nativeUnsettled && Same(f.View().state,stable));
		NativeTextEditorReceipt out;out.after.sequence=999;CHECK(!f.input.RetireNumberNative(expected,out,f.error) && out.after.sequence==999);
		CHECK(!f.input.ReplaceNumberSelection("n0",id,"2",f.error));
	}
}
static void CopiesAndAuthority() {
	Fixture f;f.Text("1.5");f.Attach();Interaction old=f.input;Interaction older=old;
	NativeTextEditorBarrier out;out.sequence=999;
	CHECK(!old.BeginNumberNativeCollection(f.barrier,{10,20,0},out,f.error) && out.sequence==999);
	CHECK(!older.BeginNumberNativeCollection(f.barrier,{10,20,0},out,f.error));
	const auto before=f.View();const auto oldDrafts=f.Drafts().barrier;f.Begin(0);CHECK(f.View().identity==before.identity);
	CHECK(f.Drafts().barrier!=oldDrafts && !f.input.DiscardNumberDrafts(oldDrafts,f.error));
	CHECK(!f.input.CanAdopt(old,f.error) && !f.input.Adopt(std::move(older),f.error));
	CHECK(f.View().nativeUnsettled);
	Interaction saved=f.input;saved.Cancel();CHECK(f.View().nativeUnsettled && f.View().active);
	Fixture other;CHECK(!other.input.Adopt(std::move(saved),f.error));
	f.Complete();Interaction prepared=f.input;Interaction grandchild=prepared;
	CHECK(prepared.Adopt(std::move(grandchild),f.error));CHECK(f.input.CanAdopt(prepared,f.error));
	const auto token=f.View().identity;CHECK(f.input.Adopt(std::move(prepared),f.error));
	CHECK(!f.View().nativePresentation && f.View().identity!=token);
	CHECK(!f.input.Adopt(std::move(prepared),f.error));
	// Actual owned moves transfer authority; moving a preparation never upgrades it.
	Fixture move;move.Attach();Interaction owning=std::move(move.input);NativeTextEditorReceipt receipt;
	CHECK(owning.RetireNumberNative(move.barrier,receipt,move.error));
	Fixture copies;copies.Attach();Interaction copy=copies.input;Interaction movedCopy=std::move(copy);
	CHECK(!movedCopy.RetireNumberNative(copies.barrier,receipt,copies.error));
	CHECK(copies.input.RetireNumberNative(copies.barrier,receipt,copies.error));
	// Even an unchanged local-only adoption consumes its candidate exactly once;
	// moved container storage cannot be replayed as an empty replacement later.
	Fixture local;Interaction once=local.input;
	CHECK(local.input.Adopt(std::move(once),local.error));
	CHECK(!local.input.Adopt(std::move(once),local.error) && local.View().state.text=="1");
}
static void ExactAndFailure() {
	Fixture f;f.Attach();const auto good=f.barrier;NativeTextEditorView sentinel;sentinel.presentation.text="unchanged";
	for(unsigned i=0;i<8;++i) {
		auto owner=good.editor;
		if(i==0)++owner.allocation;if(i==1)++owner.backend;if(i==2)++owner.document;if(i==3)++owner.modal;
		if(i==4)++owner.window;if(i==5)++owner.session;if(i==6)++owner.revision;if(i==7)owner.control="n1";
		CHECK(!f.input.QueryNumberNative("n0",owner,sentinel,f.error) && sentinel.presentation.text=="unchanged");
	}
	const auto tx=f.Offer([&](auto lock){CHECK(f.native.ReplaceACP(lock,0,1,"1.75",f.error));});f.Begin(1);
	const auto initial=f.View();NativeTextEditorReceipt out;out.after.sequence=999;
	auto forged=tx;forged.transaction.after.text="2";CHECK(!f.input.ApplyNumberNative(f.barrier,forged,out,f.error));
	CHECK(out.after.sequence==999 && f.View().identity==initial.identity && f.View().nativePresentation==initial.nativePresentation);
	f.Apply(tx);CHECK(!f.input.ApplyNumberNative(f.barrier,tx,out,f.error));f.Complete();f.Settle();
	CHECK(!f.input.ReplaceNumberSelection("n0",f.View().identity,"2",f.error));
	CHECK(!f.input.CommitNumberEdit("n0",f.View().identity,f.error));f.Retire();
	CHECK(f.input.CommitNumberEdit("n0",f.View().identity,f.error));CHECK(f.input.TakeActions().size()==1);
}
static void MultiOfferAndUnicode() {
	Fixture f;f.Text("1.25");const auto before=f.Save().widgets.at("n0").number.value();f.Attach();Interaction stale=f.input;
	const std::string utf8="1\xF0\x9F\x98\x80\xC3\xA9";
	const auto first=f.Offer([&](auto lock){CHECK(f.native.ReplaceACP(lock,0,4,utf8,f.error));CHECK(f.native.SelectACP(lock,4,1,f.error));});
	// The second producer lock is already queued behind the first, inside the
	// same indivisible collection. Its offered engine revision updates after ACK.
	NativeTextLockScope lock;CHECK(f.native.RequestLock(f.nativeId,NativeTextAccess::ReadWrite,true,lock,f.error)==NativeTextLockResult::Granted);
	CHECK(f.native.ReplaceACP(lock,3,4,"2",f.error));std::uint64_t seq=0;CHECK(f.native.FinishLock(lock,f.dispatch,seq,f.error) && seq==2);
	f.Begin(2);f.Apply(first);CHECK(f.View().nativePresentation->text==utf8 && f.View().nativePresentation->anchor==7 && f.View().nativePresentation->caret==1);
	NativeTextEditorBarrier out;CHECK(!f.input.CompleteNumberNativeCollection(f.barrier,f.barrier.collection,out,f.error));
	NativeTextOffer second;CHECK(f.native.PeekOffer(f.nativeId,second,f.error));CHECK(second.expectedEngineRevision==f.View().identity.revision);f.Apply(second);
	CHECK(Same(f.View().state,before.state));f.Complete();f.Settle();
	CHECK(f.View().state.text=="1\xF0\x9F\x98\x80" "2" && f.View().state.anchor==6 && f.View().state.caret==1);
	CHECK(!f.input.CanAdopt(stale,f.error) && !f.input.Adopt(std::move(stale),f.error));
	const auto saved=f.Save().widgets.at("n0").number.value();CHECK(saved.undo.size()==before.undo.size()+1 && Same(saved.undo.back(),before.state));
	CHECK(f.View().status!=TextNumberStatus::Valid);f.Retire();CHECK(f.input.UndoNumberEdit("n0",f.View().identity,false,f.error));CHECK(Same(f.View().state,before.state));
}
static void ExactEmergencyRetirement() {
	Fixture f;f.Text("1.25");const auto stable=f.Save().widgets.at("n0").number.value();f.Attach();
	const auto original=f.barrier;
	const auto tx=f.Offer([&](auto lock){CHECK(f.native.ReplaceACP(lock,0,4,"1.75",f.error));CHECK(f.native.BeginComposition(lock,1,0,4,f.error));});
	f.Begin(1);f.Apply(tx);CHECK(f.barrier.editor.revision!=original.editor.revision);
	for(unsigned i=0;i<9;++i) {
		auto owner=original.editor;auto native=original.native;
		if(i==0)++owner.allocation;if(i==1)++owner.backend;if(i==2)++owner.document;if(i==3)++owner.modal;
		if(i==4)++owner.window;if(i==5)++owner.session;if(i==6)owner.control="n1";
		if(i==7)++native.document;if(i==8)++native.editorLease;
		failAfter=0;const bool retired=f.input.RetireNumberNativeExact(native,owner);failAfter=-1;
		CHECK(!retired && f.input.IsNumberNativeCurrent(f.barrier));
	}
	Interaction copy=f.input;
	failAfter=0;const bool copied=copy.RetireNumberNativeExact(original.native,original.editor);failAfter=-1;CHECK(!copied);
	failAfter=0;const bool retired=f.input.RetireNumberNativeExact(original.native,original.editor);failAfter=-1;CHECK(retired);
	const auto after=f.Save().widgets.at("n0").number.value();
	CHECK(Same(after.state,stable.state)&&Same(after.undo,stable.undo)&&Same(after.redo,stable.redo));
	CHECK(!f.View().nativePresentation&&!f.View().nativeUnsettled&&f.input.TakeActions().empty());
	CHECK(f.native.Retire(original.native));++f.nativeId.document; // A replacement must use a fresh identity.
	const auto current=f.View();TextEditorIdentity replacement{1,2,3,f.input.ModalToken(),5,current.identity.session,current.identity.revision,"n0"};
	CHECK(f.input.AttachNumberNative("n0",current.identity,replacement,f.nativeId,f.barrier,f.error));
	failAfter=0;const bool old=f.input.RetireNumberNativeExact(original.native,original.editor);failAfter=-1;
	CHECK(!old&&f.input.IsNumberNativeCurrent(f.barrier));
}
static void AllocationAtomicity() {
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
	// MSVC debug iterators allocate a container proxy even in noexcept empty
	// string constructors. Global allocation failure there invokes terminate,
	// before the production operation can observe an allocation exception.
	// The standalone release-iterator runner executes the complete sweep.
	std::puts("Allocation failure sweep unavailable with MSVC debug iterators; run the standalone release-iterator suite.");
#else
	bool reachedSuccess=false;
	for(long point=0;point<160 && !reachedSuccess;++point) {
		Fixture f;f.Text("1.25");f.Attach();
		const auto tx=f.Offer([&](auto lock){CHECK(f.native.ReplaceACP(lock,0,4,std::string(100,'7'),f.error));CHECK(f.native.BeginComposition(lock,1,0,100,f.error));});
		f.Begin(1);const auto before=f.View();const auto draft=f.Drafts().barrier;
		NativeTextEditorReceipt out;out.after.sequence=999;bool result=false,threw=false;
		failAfter=point;try {result=f.input.ApplyNumberNative(f.barrier,tx,out,f.error);}catch(const std::bad_alloc&){threw=true;}failAfter=-1;
		if(result){reachedSuccess=true;CHECK(out.after.sequence==1);}
		else {CHECK(threw);CHECK(out.after.sequence==999 && f.View().identity==before.identity && f.View().nativePresentation==before.nativePresentation && f.Drafts().barrier==draft);}
	}
	CHECK(reachedSuccess);
#endif
}
int main() {
#if defined(_MSC_VER) && defined(_DEBUG)
	// A failing headless test must report to its log, never block on a dialog.
	for (int kind : {_CRT_WARN,_CRT_ERROR,_CRT_ASSERT}) {
		_CrtSetReportMode(kind,_CRTDBG_MODE_FILE);
		_CrtSetReportFile(kind,_CRTDBG_FILE_STDERR);
	}
#endif
	CollectionAndRanges();GatesAndRetirement();CopiesAndAuthority();ExactAndFailure();MultiOfferAndUnicode();ExactEmergencyRetirement();AllocationAtomicity();
	std::printf("Number native integration: %u checks passed.\n",checks);return 0;
}
