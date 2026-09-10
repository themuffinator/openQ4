// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/application/NativeTextCollectionCoordinator.h"
#include <deque>
#include <thread>
#include <stdexcept>
using namespace openq4;
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
static constexpr bool allocationSweep=false;
#else
static constexpr bool allocationSweep=true;
#endif

static long failAfter=-1;
static bool noAllocation=false;
void* operator new(std::size_t size) { if(noAllocation){std::fputs("FAIL allocation after native synchronization\n",stderr);std::exit(1);} if(failAfter==0) throw std::bad_alloc(); if(failAfter>0)--failAfter; if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc(); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
using namespace openq4::ui;
static unsigned checks;
#define CHECK(value) do { ++checks; if(!(value)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value); std::exit(1); } } while(false)
static bool Same(const TextEditState& a,const TextEditState& b) {return a.text==b.text && a.anchor==b.anchor && a.caret==b.caret;}

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
// Queue/provider and closed COM scope are counted injected boundaries. Text
// transactions, editor/history, Interaction publication and ingress are real.
struct Hooks {
	std::function<void(const char*)> callback;
	unsigned effects=0;
	void At(const char* name) { if(callback)callback(name); }
};
struct Source final:NativeQueueSource {
	Hooks& hooks; NativeQueueStatus status{true,true,1,3,7,0x9000,{}};
	std::deque<std::pair<SDL_Event,OQ4_NativeQueueRecord>> records;
	std::uint64_t sequence=0; unsigned observations=0;
	explicit Source(Hooks& h):hooks(h) {}
	bool Observe(NativeQueueStatus& out,std::string&) override { ++observations;hooks.At("observe");out=status;return true; }
	int Poll(SDL_Event& e,OQ4_NativeQueueRecord& r) override {
		if(records.empty())return 0;
		e=records.front().first;r=records.front().second;records.pop_front();return 1;
	}
	bool CopyFence(const SDL_Event&,OQ4_NativeFence& out) override {if(!status.pending)return false;out=*status.pending;return true;}
	void Add(Uint32 type,Uint32 kind=OQ4_QUEUE_OUTSIDE,Uint32 ordinal=0) {
		SDL_Event event{};event.type=type;OQ4_NativeQueueRecord r{1,kind,ordinal,0,status.generation,++sequence,0,0};
		if(kind==OQ4_QUEUE_COLLECTION || kind==OQ4_QUEUE_FENCE)r.dispatch=status.pending->dispatch;
		if(kind==OQ4_QUEUE_FENCE){r.fence_sequence=status.pending->sequence;event.user.code=19;}
		if(type==SDL_EVENT_TEXT_INPUT)event.text.text="not insertion authority";
		records.emplace_back(event,r);
	}
	void Group(std::uint64_t dispatch,std::uint64_t fence) {
		status.pending=OQ4_NativeFence{1,1,dispatch,fence};Add(SDL_EVENT_KEY_DOWN);
		Add(SDL_EVENT_POLL_SENTINEL,OQ4_QUEUE_SENTINEL);sequence+=2;
		Add(SDL_EVENT_TEXT_INPUT,OQ4_QUEUE_COLLECTION,1);Add(SDL_EVENT_CLIPBOARD_UPDATE);
		Add(status.fenceEventType,OQ4_QUEUE_FENCE,1);
	}
};
struct Owner final:NativeTextCollectionOwner {
	Fixture& f; Hooks& hooks; bool replaced=false,publishNoAllocation=false;
	unsigned retired=0,begins=0,applies=0,completes=0,prepares=0,publishes=0;
	Owner(Fixture& fixture,Hooks& h):f(fixture),hooks(h) {}
	bool Refresh(const NativeTextEditorBarrier& b,NativeTextEditorView& out,std::string& error) override {
		hooks.At("refresh");return !replaced && f.input.QueryNumberNative(b.editor.control,b.editor,out,error);
	}
	bool Current(const NativeTextEditorBarrier& b) const noexcept override {return !replaced && f.input.IsNumberNativeCurrent(b);}
	bool Begin(const NativeTextEditorBarrier& b,const NativeTextCollection& c,NativeTextEditorBarrier& out,std::string& error) override {
		++begins;hooks.At("begin");return !replaced && f.input.BeginNumberNativeCollection(b,c,out,error);
	}
	bool Apply(const NativeTextEditorBarrier& b,const NativeTextOffer& offer,NativeTextEditorReceipt& out,std::string& error) override {
		++applies;hooks.At("apply");return !replaced && f.input.ApplyNumberNative(b,offer,out,error);
	}
	bool Complete(const NativeTextEditorBarrier& b,const NativeTextCollection& c,NativeTextEditorBarrier& out,std::string& error) override {
		++completes;hooks.At("complete");return !replaced && f.input.CompleteNumberNativeCollection(b,c,out,error);
	}
	std::unique_ptr<Interaction::NativeSettlement> PrepareSettlement(const NativeTextEditorBarrier& b,std::string& error) override {
		++prepares;hooks.At("prepare");return replaced?nullptr:f.input.PrepareNumberNativeSettlement(b,error);
	}
	bool PublishSettlement(Interaction::NativeSettlement& p,NativeTextEditorReceipt& out) noexcept override {
		++publishes;
		if(publishNoAllocation)failAfter=0;
		const bool result=!replaced && f.input.PublishNumberNativeSettlement(p,out);
		failAfter=-1;noAllocation=false;return result;
	}
	void RetireExact(const NativeTextIdentity& n,const TextEditorIdentity& editor) noexcept override {
		++retired;CHECK(n==f.nativeId && editor.allocation==1);
		if(!replaced)f.input.Cancel(); // actual stable draft preservation/lease removal
	}
};
struct Store final:NativeTextCollectionStore {
	Fixture& f; Hooks& hooks; NativeClosedTextCollection seal; bool closed=true,ackOkay=true,syncOkay=true;
	bool failAfterAck=false,failAfterSync=false,badPendingFinal=false;
	unsigned retired=0,acks=0,syncs=0;
	Store(Fixture& fixture,Hooks& h):f(fixture),hooks(h) {}
	void Seal(std::uint64_t serial) {
		seal={f.nativeId,serial,f.dispatch,NativeClosedCollectionKind::Pump,{},1};
		CHECK(f.native.QueryPendingCollection(f.nativeId,f.barrier.editor.revision,f.barrier.sequence,f.barrier.shadowRevision,f.dispatch,seal.pending,f.error));
	}
	bool Closed(const NativeTextEditorBarrier&,std::uint64_t,NativeClosedTextCollection& out,std::string&) override {
		hooks.At("closed");out=seal;return closed;
	}
	bool StillClosed(const NativeClosedTextCollection& expected) const noexcept override {return closed && expected==seal;}
	bool Pending(const NativeTextEditorBarrier& b,std::uint64_t dispatch,NativeTextPendingSnapshot& out,std::string& error) override {
		hooks.At("pending");const bool okay=f.native.QueryPendingCollection(b.native,b.editor.revision,b.sequence,b.shadowRevision,dispatch,out,error);
		if(okay && badPendingFinal)++out.lastSequence;
		return okay;
	}
	bool Peek(const NativeTextIdentity& id,NativeTextOffer& out,std::string& error) override {
		hooks.At("peek");return f.native.PeekOffer(id,out,error);
	}
	bool Acknowledge(const NativeTextEditorReceipt& r,std::string& error) override {
		++acks;hooks.At("ack");
		if(!ackOkay)return false;
		const bool okay=f.native.Acknowledge(r.after.native,r.after.sequence,r.after.shadowRevision,r.before.editor.revision,r.after.editor.revision,error);
		hooks.At("acked");return okay && !failAfterAck;
	}
	bool Sync(const NativeTextEditorReceipt& r,const NativeTextSnapshot& p,std::string& error) override {
		++syncs;hooks.At("sync");
		if(!syncOkay)return false;
		const bool okay=f.native.SyncEngine(r.before.native,r.before.editor.revision,r.before.shadowRevision,r.after.editor.revision,p.text,p.anchor,p.caret,error);
		hooks.At("synced");return okay && !failAfterSync;
	}
	void RetireExact(const NativeTextIdentity& id) noexcept override {
		++retired;closed=false;std::string error;CHECK(f.native.Retire(id,error));
	}
};
struct Fence final:NativeTextCollectionFence {
	Source& source;Hooks& hooks;unsigned acks=0;bool okay=true,failAfterAck=false;
	Fence(Source& s,Hooks& h):source(s),hooks(h) {}
	bool Acknowledge(const NativeQueueStatus& expected,std::string&) override {
		++acks;hooks.At("fence");
		if(!okay)return false;
		CHECK(source.status.pending && source.status.pending->sequence==expected.pending->sequence);
		source.status.pending.reset();hooks.At("fenced");return !failAfterAck;
	}
};
struct Run {
	Fixture f;Hooks hooks;Source source{hooks};Owner owner{f,hooks};Store store{f,hooks};Fence fence{source,hooks};
	NativeQueueIngress ingress;std::unique_ptr<const NativeQueueBatch> batch;std::unique_ptr<NativeTextCollectionCoordinator> coordinator;
	Run(){ f.Text("1.25");f.Attach();coordinator=std::make_unique<NativeTextCollectionCoordinator>(f.barrier); }
	void Offer(const std::string& text,bool begin=false,bool end=false) {
		f.Offer([&](auto lock) {
			NativeTextSnapshot before;CHECK(f.native.Read(lock,before,f.error));
			CHECK(f.native.ReplaceACP(lock,0,static_cast<std::uint32_t>(before.text.size()),text,f.error));
			if(begin)CHECK(f.native.BeginComposition(lock,1,0,static_cast<std::uint32_t>(text.size()),f.error));
			if(end)CHECK(f.native.EndComposition(lock,1,f.error));
			CHECK(f.native.SelectACP(lock,static_cast<std::uint32_t>(text.size()),0,f.error));
		});
	}
	void Read(unsigned serial=1) {
		store.Seal(serial);source.Group(f.dispatch,f.fence);
		const auto result=ingress.Read(source,batch,f.error);
		if(result!=NativeQueueRead::Ready)std::fprintf(stderr,"Read failed: %s\n",f.error.c_str());
		CHECK(result==NativeQueueRead::Ready && batch->Events().size()==5);
	}
	bool Reconcile(NativeTextCollectionResult& out) {return coordinator->Reconcile(ingress,source,*batch,owner,store,out,f.error);}
	void Complete(const NativeTextCollectionResult& result) {
		CHECK(coordinator->CompleteFence(ingress,source,result.completion,owner,store,fence,f.error));
		NativeTextEditorView view;const auto id=f.View().identity;
		auto expected=f.barrier.editor;expected.revision=id.revision;
		CHECK(f.input.QueryNumberNative("n0",expected,view,f.error));f.barrier=view.barrier;
	}
};
static void Success() {
	Run r;const auto before=r.f.Save().widgets.at("n0").number.value();
	r.Offer("1.5");r.Offer("1e-");r.Read();NativeTextCollectionResult out;
	r.owner.publishNoAllocation=true;CHECK(r.Reconcile(out));
	CHECK(out.transactions==2 && out.settled && !out.composing && r.store.acks==2 && r.store.syncs==1);
	CHECK(r.owner.publishes==1 && r.fence.acks==0 && r.source.status.pending && !r.coordinator->NeedsRetirement());
	CHECK(r.f.View().state.text=="1e-" && r.f.View().state.anchor==3 && r.f.View().state.caret==0);
	CHECK(r.f.View().status!=TextNumberStatus::Valid && r.f.input.TakeActions().empty());
	auto saved=r.f.Save().widgets.at("n0").number.value();CHECK(saved.undo.size()==before.undo.size()+1 && Same(saved.undo.back(),before.state));
	CHECK(r.batch->Events()[2].text=="not insertion authority" && r.batch->Events()[3].ignored);
	r.Complete(out);CHECK(r.fence.acks==1 && !r.source.status.pending);
	// Empty closed scope and a text-bearing SDL event add no text or undo.
	++r.f.dispatch;++r.f.fence;r.Read(2);CHECK(r.Reconcile(out) && !out.settled && out.transactions==0);
	CHECK(r.f.View().state.text=="1e-" && r.f.Save().widgets.at("n0").number->undo.size()==saved.undo.size());r.Complete(out);
}
static void AcrossCollections() {
	Run r;const auto before=r.f.View().state;r.Offer("1.7",true);r.Read();NativeTextCollectionResult out;
	CHECK(r.Reconcile(out) && out.composing && !out.settled && r.store.syncs==0);
	CHECK(Same(r.f.View().state,before) && r.f.View().nativePresentation->text=="1.7");r.Complete(out);
	++r.f.dispatch;++r.f.fence;r.Offer("1.75",false,true);r.Read(2);
	CHECK(r.Reconcile(out) && !out.composing && out.settled && r.store.syncs==1);
	CHECK(r.f.View().state.text=="1.75");r.Complete(out);
}
static void LifecycleCollections() {
	// Explicit application callbacks can start a native group, which completes
	// in a later ordinary pump. SDL text remains an owned record, not inserted.
	Run r;const auto stable=r.f.View().state;r.Offer("1.7",true);r.Read();
	r.store.seal.kind=NativeClosedCollectionKind::Lifecycle;
	auto requested=r.store.seal;NativeTextCollectionResult out;
	bool observed=false;r.hooks.callback=[&](const char* site) {
		if(!observed && std::string_view(site)=="observe") {observed=true;++requested.serial;}
	};
	CHECK(r.coordinator->ReconcileLifecycle(requested,r.ingress,r.source,*r.batch,r.owner,r.store,out,r.f.error));
	CHECK(observed && out.composing && !out.settled && out.transactions==1 && Same(r.f.View().state,stable));
	CHECK(r.batch->Events().size()==5 && r.batch->Events()[2].text=="not insertion authority");
	r.hooks.callback={};r.Complete(out);
	++r.f.dispatch;++r.f.fence;r.Offer("1.75",false,true);r.Read(2);
	r.hooks.callback=[](const char* site){if(std::string_view(site)=="synced")noAllocation=true;};
	CHECK(r.Reconcile(out) && out.settled && !out.composing);r.Complete(out);
	CHECK(r.f.View().state.text=="1.75" && r.f.Save().widgets.at("n0").number->undo.size()==2);
	// An explicitly empty lifecycle still has a real zero-event fence. It adds
	// no editor text/history and cannot bypass ordinary fence completion.
	++r.f.dispatch;++r.f.fence;r.store.Seal(3);r.store.seal.kind=NativeClosedCollectionKind::Lifecycle;
	r.store.seal.admittedCallbacks=0;
	r.source.status.pending=OQ4_NativeFence{1,0,r.f.dispatch,r.f.fence};
	r.source.Add(r.source.status.fenceEventType,OQ4_QUEUE_FENCE,0);
	CHECK(r.ingress.Read(r.source,r.batch,r.f.error)==NativeQueueRead::Ready && r.batch->Events().size()==1);
	CHECK(r.coordinator->ReconcileLifecycle(r.store.seal,r.ingress,r.source,*r.batch,r.owner,r.store,out,r.f.error));
	CHECK(!out.settled && !out.composing && out.transactions==0 && r.fence.acks==2);r.Complete(out);
	// Every supplied seal field is bound independently to the actual closed
	// application collection. A changed owner is never cleanup-write authority.
	for(unsigned bad=0;bad<17;++bad) {
		Run f;f.Offer("1.5");f.Read();f.store.seal.kind=NativeClosedCollectionKind::Lifecycle;
		auto wrong=f.store.seal;
		if(bad==0)wrong.kind=NativeClosedCollectionKind::Pump;
		if(bad==1)wrong.kind=static_cast<NativeClosedCollectionKind>(99);
		if(bad==2)++wrong.identity.document;
		if(bad==3)++wrong.identity.editorLease;
		if(bad==4)++wrong.serial;
		if(bad==5)++wrong.dispatch;
		if(bad==6)++wrong.pending.identity.document;
		if(bad==7)++wrong.pending.identity.editorLease;
		if(bad==8)++wrong.pending.engineRevision;
		if(bad==9)++wrong.pending.acknowledgedSequence;
		if(bad==10)++wrong.pending.acknowledgedShadowRevision;
		if(bad==11)++wrong.pending.lastSequence;
		if(bad==12)++wrong.pending.shadowRevision;
		if(bad==13)++wrong.pending.dispatch;
		if(bad==14)++wrong.pending.count;
		if(bad==15)++wrong.admittedCallbacks;
		if(bad==16)f.owner.replaced=true;
		out.transactions=999;
		CHECK(!f.coordinator->ReconcileLifecycle(wrong,f.ingress,f.source,*f.batch,f.owner,f.store,out,f.f.error));
		CHECK(out.transactions==999 && f.owner.begins==0 && f.store.acks==0 && f.owner.retired==1 && f.store.retired==1);
	}
	// The lifecycle entry never accepts a Pump receipt, even when both sides
	// agree on its bytes. Ordinary entry still rejects Lifecycle in BadReceipts.
	Run pump;pump.Read();
	CHECK(!pump.coordinator->ReconcileLifecycle(pump.store.seal,pump.ingress,pump.source,*pump.batch,pump.owner,pump.store,out,pump.f.error));
	CHECK(pump.owner.begins==0 && pump.fence.acks==0);
}
static void Failures() {
	for(const char* site:{"observe","closed","refresh","begin","pending","peek","apply","ack","acked","complete","prepare","sync","synced"}) {
		for(unsigned failure=0;failure<5;++failure) {
			Run r;r.Offer("1.75");r.Read();const auto stable=r.f.View().state;
			bool fired=false;r.hooks.callback=[&](const char* current) {
				if(fired || std::string_view(current)!=site)return;
				fired=true;
				if(failure==0)++r.source.status.engineToken;
				if(failure==1)r.owner.replaced=true;
				if(failure==2)r.store.closed=false;
				if(failure==3) {NativeTextCollectionResult nested;CHECK(!r.Reconcile(nested));}
				if(failure==4)throw std::runtime_error("Injected boundary exception");
			};
			NativeTextCollectionResult out;out.transactions=999;
			CHECK(!r.Reconcile(out) && fired && out.transactions==999 && r.coordinator->NeedsRetirement());
			CHECK(r.owner.retired==1 && r.store.retired==1 && r.fence.acks==0 && r.owner.publishes==0);
			CHECK(Same(r.f.View().state,stable));
			const auto effects=r.owner.applies;CHECK(!r.Reconcile(out) && r.owner.applies==effects && r.owner.retired==1);
		}
	}
	for(bool sync:{false,true}) {
		Run r;r.Offer("1.75");r.Read();r.store.ackOkay=sync;r.store.syncOkay=false;NativeTextCollectionResult out;
		CHECK(!r.Reconcile(out) && r.f.View().state.text=="1.25" && !r.f.View().nativePresentation);
		CHECK(r.owner.publishes==0 && r.store.acks==1 && r.store.syncs==(sync?1u:0u));
	}
}
static void BadReceipts() {
	for(unsigned bad=0;bad<12;++bad) {
		Run r;r.Offer("1.5");r.Read();auto& s=r.store.seal;
		if(bad==0)++s.identity.document;
		if(bad==1)s.serial=0;
		if(bad==2)++s.dispatch;
		if(bad==3)s.kind=NativeClosedCollectionKind::Lifecycle;
		if(bad==4)++s.pending.engineRevision;
		if(bad==5)++s.pending.acknowledgedSequence;
		if(bad==6)++s.pending.acknowledgedShadowRevision;
		if(bad==7)++s.pending.shadowRevision;
		if(bad==8)++s.pending.lastSequence;
		if(bad==9)s.admittedCallbacks=0;
		if(bad==10)s.pending.count=33;
		if(bad==11)++s.pending.identity.editorLease;
		NativeTextCollectionResult out;CHECK(!r.Reconcile(out) && r.owner.begins==0 && r.store.acks==0);
	}
	for(unsigned bad=0;bad<8;++bad) {
		Run r;r.Offer("1.5");r.Read();NativeTextCollectionResult out;CHECK(r.Reconcile(out));
		if(bad==0)++out.completion.serial;
		if(bad==1)++out.completion.coordinator;
		if(bad==2)++out.completion.batch.serial;
		if(bad==3)r.fence.okay=false;
		if(bad==4)++r.source.status.engineToken;
		if(bad==5)r.owner.replaced=true;
		if(bad==6)r.hooks.callback=[&](const char* s){if(std::string_view(s)=="fenced")++r.source.status.engineToken;};
		if(bad==7)r.fence.failAfterAck=true;
		CHECK(!r.coordinator->CompleteFence(r.ingress,r.source,out.completion,r.owner,r.store,r.fence,r.f.error));
		CHECK(r.coordinator->NeedsRetirement() && r.owner.retired==1 && r.store.retired==1);
		CHECK(r.f.View().state.text=="1.5"); // native settlement already succeeded, never silently rolled back
	}
	for(unsigned bad=0;bad<3;++bad) {
		Run r;r.Offer("1.5");r.Read();
		if(bad==0)r.store.failAfterAck=true;
		if(bad==1)r.store.failAfterSync=true;
		if(bad==2)r.store.badPendingFinal=true;
		NativeTextCollectionResult out;CHECK(!r.Reconcile(out) && r.owner.publishes==0 && r.f.View().state.text=="1.25");
		if(bad==2)CHECK(r.owner.applies==0 && r.store.acks==0);
	}
}
static void Prepared() {
	Run r;r.Offer(std::string(100,'7'));r.Read();
	// Drive exact actual methods to a completed un-settled collection.
	NativeTextOffer offer;CHECK(r.f.native.PeekOffer(r.f.nativeId,offer,r.f.error));r.f.Begin(1);r.f.Apply(offer);r.f.Complete();
	const auto stable=r.f.View().state;
	bool success=false;
	for(long point=0;point<160 && !success;++point) {
		std::unique_ptr<Interaction::NativeSettlement> prepared;failAfter=allocationSweep?point:-1;
		try {prepared=r.f.input.PrepareNumberNativeSettlement(r.f.barrier,r.f.error);}catch(const std::bad_alloc&) {}failAfter=-1;
		CHECK(Same(r.f.View().state,stable) && r.f.input.IsNumberNativeCurrent(r.f.barrier));
		if(!prepared)continue;
		success=true;NativeTextEditorReceipt receipt;receipt.after.sequence=999;
		Interaction copy=r.f.input;CHECK(!copy.PublishNumberNativeSettlement(*prepared,receipt) && receipt.after.sequence==999);
		failAfter=0;CHECK(r.f.input.IsNumberNativeCurrent(r.f.barrier));CHECK(r.f.input.PublishNumberNativeSettlement(*prepared,receipt));failAfter=-1;
		CHECK(receipt.effect==NativeTextEditorEffect::SyncEngine && r.f.View().state.text==std::string(100,'7'));
		CHECK(!r.f.input.PublishNumberNativeSettlement(*prepared,receipt));
	}
	CHECK(success);
	Run stale;stale.Offer("1.75");stale.Read();NativeTextOffer offer2;CHECK(stale.f.native.PeekOffer(stale.f.nativeId,offer2,stale.f.error));
	stale.f.Begin(1);stale.f.Apply(offer2);stale.f.Complete();auto prepared=stale.f.input.PrepareNumberNativeSettlement(stale.f.barrier,stale.f.error);CHECK(prepared);
	stale.f.input.Cancel();NativeTextEditorReceipt receipt;receipt.after.sequence=999;
	CHECK(!stale.f.input.PublishNumberNativeSettlement(*prepared,receipt) && receipt.after.sequence==999 && stale.f.View().state.text=="1.25");
}
static void FinalBoundary() {
	// There must be no allocating full-view query after successful foreign Sync.
	Run noAllocRun;noAllocRun.Offer(std::string(100,'7'));noAllocRun.Read();
	noAllocRun.owner.publishNoAllocation=true;
	noAllocRun.hooks.callback=[](const char* site){if(std::string_view(site)=="synced")::noAllocation=true;};
	NativeTextCollectionResult out;CHECK(noAllocRun.Reconcile(out));failAfter=-1;
	CHECK(out.settled && noAllocRun.owner.publishes==1);noAllocRun.Complete(out);
	// A queue probe itself can replace the owner after Refresh. The final
	// callback-free predicate must reject this, even with otherwise valid queue.
	for(unsigned mode=0;mode<3;++mode) {
		Run r;r.Offer("1.75");r.Read();bool armed=false,fired=false;
		r.hooks.callback=[&](const char* site) {
			if(std::string_view(site)=="synced")armed=true;
			if(armed && !fired && std::string_view(site)=="observe") {
				fired=true;
				if(mode==0)r.owner.replaced=true;
				if(mode==1)r.store.closed=false;
				if(mode==2){NativeTextCollectionResult nested;CHECK(!r.Reconcile(nested));}
			}
		};
		CHECK(!r.Reconcile(out) && fired && r.store.syncs==1 && r.owner.publishes==0);
		CHECK(r.f.View().state.text=="1.25" && r.store.retired==1);
	}
	// A different owner replacing the GUI inside Sync keeps its own text intact.
	Run replaced;replaced.Offer("1.75");replaced.Read();Fixture replacement;replacement.Text("1.9");
	replaced.hooks.callback=[&](const char* site){if(std::string_view(site)=="synced")replaced.owner.replaced=true;};
	CHECK(!replaced.Reconcile(out) && replacement.View().state.text=="1.9" && replaced.owner.publishes==0);
}
static void ThreadAndProtocol() {
	Run r;r.Read();NativeTextCollectionResult out;bool workerResult=true;
	std::thread worker([&]{std::string error;workerResult=r.coordinator->Reconcile(r.ingress,r.source,*r.batch,r.owner,r.store,out,error);});worker.join();
	CHECK(!workerResult && !r.coordinator->NeedsRetirement() && r.owner.begins==0);
	CHECK(r.Reconcile(out));r.Complete(out);
	CHECK(!r.coordinator->CompleteFence(r.ingress,r.source,out.completion,r.owner,r.store,r.fence,r.f.error));
	CHECK(r.fence.acks==1 && r.coordinator->NeedsRetirement());
	for(const char* site:{"fence","fenced"})for(unsigned fault=0;fault<3;++fault) {
		Run f;f.Offer("1.5");f.Read();NativeTextCollectionResult ready;CHECK(f.Reconcile(ready));
		bool fired=false;f.hooks.callback=[&](const char* current) {
			if(fired || std::string_view(current)!=site)return;
			fired=true;
			if(fault==0)f.owner.replaced=true;
			if(fault==1)f.store.closed=false;
			if(fault==2)CHECK(!f.coordinator->CompleteFence(f.ingress,f.source,ready.completion,f.owner,f.store,f.fence,f.f.error));
		};
		CHECK(!f.coordinator->CompleteFence(f.ingress,f.source,ready.completion,f.owner,f.store,f.fence,f.f.error));
		CHECK(fired && f.fence.acks==1 && f.owner.retired==1 && f.f.View().state.text=="1.5");
	}
}
static void BarrierQueries() {
	Run r;NativeTextEditorBarrier out;out.native={999,998};out.editor.control="unchanged";
	const auto sentinel=out;
	CHECK(r.coordinator->QueryBarrier(out,r.f.error) && out==r.f.barrier && r.source.observations==0);
	out=sentinel;bool workerResult=true;
	std::thread worker([&]{std::string error;workerResult=r.coordinator->QueryBarrier(out,error);});worker.join();
	CHECK(!workerResult && out==sentinel && !r.coordinator->NeedsRetirement());
	r.Offer("1.75");r.Read();unsigned refused=0;
	r.hooks.callback=[&](const char*) {
		NativeTextEditorBarrier nested=sentinel;std::string error;
		CHECK(!r.coordinator->QueryBarrier(nested,error) && nested==sentinel);++refused;
	};
	NativeTextCollectionResult result;CHECK(r.Reconcile(result) && refused && !r.coordinator->NeedsRetirement());
	r.hooks.callback={};
	CHECK(r.coordinator->QueryBarrier(out,r.f.error) && out.editor.revision>r.f.barrier.editor.revision && out.sequence==1 && !out.collectionOpen);
	const auto settled=out;r.Complete(result);
	CHECK(r.coordinator->QueryBarrier(out,r.f.error) && out==settled);
	CHECK(!r.coordinator->CompleteFence(r.ingress,r.source,result.completion,r.owner,r.store,r.fence,r.f.error));
	out=sentinel;CHECK(!r.coordinator->QueryBarrier(out,r.f.error) && out==sentinel);
	for(unsigned invalid=0;invalid<4;++invalid) {
		auto attached=r.f.barrier;
		if(invalid==0)attached.native.document=0;
		if(invalid==1)attached.native.editorLease=0;
		if(invalid==2)attached.editor.revision=0;
		if(invalid==3)attached.collectionOpen=true;
		NativeTextCollectionCoordinator bad(attached);
		CHECK(!bad.QueryBarrier(out,r.f.error) && out==sentinel);
	}
	// A long owned name forces a copy allocation after different leading IDs;
	// failure must not publish even the trivially copyable fields of the barrier.
	auto attached=r.f.barrier;attached.editor.control=std::string(256,'q');
	NativeTextCollectionCoordinator longName(attached);
	if(allocationSweep) {
		failAfter=0;const bool copied=longName.QueryBarrier(out,r.f.error);failAfter=-1;
		CHECK(!copied && out==sentinel && !longName.NeedsRetirement());
	}
	CHECK(longName.QueryBarrier(out,r.f.error) && out==attached);
	out.editor.control="caller-owned";
	CHECK(longName.QueryBarrier(out,r.f.error) && out==attached);
}
static void LongModalOwnerPredicate() {
	Fixture f;const std::string root(80,'r'),control(90,'n');
	f.model.root.id=root;f.model.root.children.front().id=control;
	f.values.clear();f.values[control]={1.0,false,{}};f.values["n1"]={1.0,false,{}};f.bounds.clear();f.bounds[control]={0,0,100,40,true};
	f.input.Reset(f.model);CHECK(f.input.SetReadbacks(f.values,f.error));f.input.SetBounds(f.bounds);
	CHECK(f.input.PushModal(root) && f.input.Focus(control) && f.input.BeginNumberEdit(control,f.error));
	const auto id=f.input.Widget(control)->number->identity;
	const TextEditorIdentity owner{1,2,3,f.input.ModalToken(),5,id.session,id.revision,control};
	NativeTextEditorBarrier barrier;CHECK(f.input.AttachNumberNative(control,id,owner,f.nativeId,barrier,f.error));
	noAllocation=true;CHECK(f.input.IsNumberNativeCurrent(barrier));noAllocation=false;
	for(unsigned field=0;field<8;++field) {
		auto wrong=barrier;
		if(field==0)++wrong.editor.allocation;
		if(field==1)++wrong.editor.backend;
		if(field==2)++wrong.editor.document;
		if(field==3)++wrong.editor.modal;
		if(field==4)++wrong.editor.window;
		if(field==5)++wrong.editor.session;
		if(field==6)++wrong.editor.revision;
		if(field==7)++wrong.native.editorLease;
		noAllocation=true;CHECK(!f.input.IsNumberNativeCurrent(wrong));noAllocation=false;
	}
	CHECK(f.input.SetEnabled(control,false));noAllocation=true;CHECK(!f.input.IsNumberNativeCurrent(barrier));noAllocation=false;
}
int main() {
#if defined(_MSC_VER) && defined(_DEBUG)
	for(int kind:{_CRT_WARN,_CRT_ERROR,_CRT_ASSERT}) {
		_CrtSetReportMode(kind,_CRTDBG_MODE_FILE);_CrtSetReportFile(kind,_CRTDBG_FILE_STDERR);
	}
#endif
	if(!allocationSweep)std::puts("Allocation sweep unsupported with debug STL iterator proxies; functional and no-allocation final publication checks remain enabled.");
	Success();AcrossCollections();LifecycleCollections();Failures();BadReceipts();Prepared();FinalBoundary();ThreadAndProtocol();BarrierQueries();LongModalOwnerPredicate();
	std::printf("Native collection coordinator: %u checks passed.\n",checks);return 0;
}
