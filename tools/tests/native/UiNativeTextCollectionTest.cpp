// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "ui/retained/NativeTextEditor.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
using namespace openq4::ui;
static unsigned checks=0;
static bool failAllocation=false;
void* operator new(std::size_t size){if(failAllocation)throw std::bad_alloc();if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
static bool Same(const TextEditState& a,const TextEditState& b){return a.text==b.text && a.anchor==b.anchor && a.caret==b.caret;}
static bool Same(const TextEditHistory& a,const TextEditHistory& b){
	if(a.undo.size()!=b.undo.size() || a.redo.size()!=b.redo.size())return false;
	for(std::size_t i=0;i<a.undo.size();++i)if(!Same(a.undo[i],b.undo[i]))return false;
	for(std::size_t i=0;i<a.redo.size();++i)if(!Same(a.redo[i],b.redo[i]))return false;
	return true;
}
static bool Same(const NativeTextEditorView& a,const NativeTextEditorView& b){return a.barrier==b.barrier && Same(a.draft,b.draft) && Same(a.history,b.history) && a.presentation==b.presentation && a.active==b.active && a.awaitingSettlement==b.awaitingSettlement;}
static bool Same(const NativeTextEditorReceipt& a,const NativeTextEditorReceipt& b){return a.before==b.before && a.after==b.after && a.effect==b.effect && a.presentationChanged==b.presentationChanged;}
struct Pair {
	NativeTextIdentity id{20,21};TextEditorIdentity owner{1,2,3,4,5,6,100,"number"};
	NativeTextDocument native;NativeTextEditor editor;std::string error;
	explicit Pair(std::string_view text="12") {TextEditBuffer draft;CHECK(draft.Reset(text,{},error));CHECK(draft.SetSelection(text.size(),0,error));Open(draft);}
	explicit Pair(const TextEditBuffer& draft){Open(draft);}
	void Open(const TextEditBuffer& draft){CHECK(native.Open(id,owner.revision,draft.State().text,draft.State().anchor,draft.State().caret,error));CHECK(editor.Open(id,owner,draft,error));}
	void Queue(std::uint64_t dispatch,const std::function<void(const NativeTextLockScope&)>& body){
		NativeTextLockScope scope;CHECK(native.RequestLock(id,NativeTextAccess::ReadWrite,true,scope,error)==NativeTextLockResult::Granted);body(scope);
		std::uint64_t published=0;CHECK(native.FinishLock(scope,dispatch,published,error) && published);
	}
	NativeTextOffer Offer(){NativeTextOffer offer;CHECK(native.PeekOffer(id,offer,error));return offer;}
	NativeTextCollection Begin(std::uint64_t dispatch,std::uint64_t fence){
		const auto before=editor.Barrier();NativeTextEditorBarrier out;NativeTextCollection collection{dispatch,fence,before.sequence+native.PendingCount()};
		CHECK(editor.BeginCollection(before,collection,out,error));CHECK(out==editor.Barrier() && out.collectionOpen && out.editor.revision==before.editor.revision);return collection;
	}
	NativeTextEditorReceipt Apply(bool advance=true){
		const auto before=editor.Barrier();const auto offer=Offer();NativeTextEditorReceipt receipt;
		CHECK(editor.Apply(offer,before.editor,before.editor.revision+(advance?1:0),receipt,error));
		CHECK(receipt.effect==NativeTextEditorEffect::Acknowledge);
		CHECK(native.Acknowledge(id,receipt.after.sequence,receipt.after.shadowRevision,receipt.before.editor.revision,receipt.after.editor.revision,error));
		CHECK(native.EngineRevision()==editor.Barrier().editor.revision && native.ShadowRevision()>=editor.Barrier().shadowRevision);return receipt;
	}
	void Complete(const NativeTextCollection& collection){const auto before=editor.Barrier();NativeTextEditorBarrier out;CHECK(editor.CompleteCollection(before,collection,out,error));CHECK(!out.collectionOpen && out==editor.Barrier() && out.editor.revision==before.editor.revision);CHECK(native.PendingCount()==0 && native.ShadowRevision()==out.shadowRevision);}
	void Settle(){const auto before=editor.Barrier();NativeTextEditorReceipt receipt;CHECK(editor.SettleComposition(before,before.editor.revision+1,receipt,error));const auto view=editor.View();CHECK(receipt.effect==NativeTextEditorEffect::SyncEngine && receipt.after.shadowRevision==before.shadowRevision);CHECK(native.SyncEngine(id,before.editor.revision,before.shadowRevision,receipt.after.editor.revision,view.presentation.text,view.presentation.anchor,view.presentation.caret,error));CHECK(native.EngineRevision()==receipt.after.editor.revision && native.ShadowRevision()==receipt.after.shadowRevision);}
	void RejectApply(const NativeTextOffer& offer){const auto before=editor.View();NativeTextEditorReceipt out;out.after.group=999;const auto prior=out;CHECK(!editor.Apply(offer,before.barrier.editor,before.barrier.editor.revision+1,out,error));CHECK(Same(editor.View(),before) && Same(out,prior));}
	void Retire(){const auto before=editor.Barrier();NativeTextEditorReceipt out;CHECK(editor.Retire(before,before.editor.revision+1,out,error));CHECK(out.effect==NativeTextEditorEffect::Retire && !out.after.collectionOpen);CHECK(native.Retire(id,error));}
};
static void InsertionBeginAndEndTail(){
	Pair p;const auto stable=p.editor.View();
	p.Queue(10,[&](auto s){CHECK(p.native.ReplaceACP(s,1,2,"3",p.error));CHECK(p.native.SelectACP(s,2,0,p.error));});
	p.Queue(10,[&](auto s){CHECK(p.native.BeginComposition(s,1,1,2,p.error));});
	p.Queue(10,[&](auto s){CHECK(p.native.EndComposition(s,1,p.error));});
	p.Queue(10,[&](auto s){CHECK(p.native.ReplaceACP(s,0,1,"4",p.error));CHECK(p.native.SelectACP(s,0,2,p.error));});
	CHECK(p.native.PendingCount()==4);const auto collection=p.Begin(10,100);std::uint64_t group=0;
	for(int i=0;i<4;++i){const auto offer=p.Offer();CHECK(offer.transaction.classification==(i==0 || i==3?NativeTextClassification::Unclassified:NativeTextClassification::CompositionRelated));p.Apply();
		const auto view=p.editor.View();CHECK(Same(view.draft,stable.draft) && Same(view.history,stable.history) && !view.awaitingSettlement);
		if(!group)group=view.barrier.group;
		CHECK(group && group==view.barrier.group);
		NativeTextEditorReceipt receipt;CHECK(!p.editor.SettleComposition(view.barrier,view.barrier.editor.revision+1,receipt,p.error));double value=999;CHECK(!p.editor.NumberCandidate(view.barrier.editor,{0,100,true},value,p.error) && value==999);
	}
	p.Complete(collection);CHECK(p.editor.View().awaitingSettlement && p.editor.View().draft.text=="12");p.Settle();
	const auto final=p.editor.View();CHECK(final.draft.text=="43" && final.draft.anchor==0 && final.draft.caret==2);CHECK(final.history.undo.size()==1 && Same(final.history.undo[0],stable.draft));
	// Once collection mode is entered it never falls back to unscoped Apply.
	p.Queue(10,[&](auto s){CHECK(p.native.ReplaceACP(s,0,1,"5",p.error));});p.RejectApply(p.Offer());p.Retire();
}
static void MultipleRangesAndLaterBegin(){
	Pair p("abcd");
	p.Queue(1,[&](auto s){CHECK(p.native.BeginComposition(s,1,0,2,p.error));CHECK(p.native.BeginComposition(s,2,1,3,p.error));});
	p.Queue(1,[&](auto s){CHECK(p.native.EndComposition(s,1,p.error));CHECK(p.native.EndComposition(s,2,p.error));});
	p.Queue(1,[&](auto s){CHECK(p.native.ReplaceACP(s,0,0,"X",p.error));CHECK(p.native.BeginComposition(s,3,1,4,p.error));});
	p.Queue(1,[&](auto s){CHECK(p.native.UpdateComposition(s,3,2,2,p.error));CHECK(p.native.EndComposition(s,3,p.error));});
	const auto collection=p.Begin(1,1);p.Apply();auto view=p.editor.View();CHECK(view.presentation.compositions.size()==2);CHECK((view.presentation.compositions.at(1)==NativeTextRange{0,2}));CHECK((view.presentation.compositions.at(2)==NativeTextRange{1,3}));const auto group=view.barrier.group;
	p.Apply();CHECK(p.editor.View().presentation.compositions.empty() && !p.editor.View().awaitingSettlement);p.Apply();CHECK(p.editor.View().barrier.group==group && p.editor.View().presentation.compositions.size()==1);p.Apply();p.Complete(collection);p.Settle();
	view=p.editor.View();CHECK(view.draft.text=="Xabcd" && view.history.undo.size()==1 && view.history.undo.front().text=="abcd");
}
static void LiveRangesAcrossCollections(){
	Pair p;
	p.Queue(20,[&](auto s){CHECK(p.native.ReplaceACP(s,0,1,"4",p.error));CHECK(p.native.BeginComposition(s,1,0,1,p.error));});
	auto collection=p.Begin(20,40);p.Apply();const auto group=p.editor.Barrier().group;p.Complete(collection);CHECK(!p.editor.View().awaitingSettlement && p.editor.View().draft.text=="12");
	NativeTextEditorReceipt receipt;CHECK(!p.editor.SettleComposition(p.editor.Barrier(),p.editor.Barrier().editor.revision+1,receipt,p.error));
	// Empty native collections preserve a live composition without settling it.
	collection=p.Begin(21,41);p.Complete(collection);CHECK(p.editor.Barrier().group==group && p.editor.View().presentation.compositions.size()==1);
	p.Queue(22,[&](auto s){CHECK(p.native.UpdateComposition(s,1,0,1,p.error));});
	p.Queue(22,[&](auto s){CHECK(p.native.ReplaceACP(s,1,2,"5",p.error));CHECK(p.native.EndComposition(s,1,p.error));});
	collection=p.Begin(22,42);const auto metadata=p.Apply(false);CHECK(!metadata.presentationChanged && metadata.before.editor.revision==metadata.after.editor.revision);
	p.Apply();CHECK(p.editor.Barrier().group==group);p.Complete(collection);p.Settle();CHECK(p.editor.View().draft.text=="45" && p.editor.View().history.undo.size()==1);
}
static void EmptyCollectionsAndProtocolBarriers(){
	Pair p;const auto before=p.editor.View();auto oldClone=p.editor.Clone();const auto collection=p.Begin(1,1);const auto open=p.editor.View();
	CHECK(!p.editor.Swap(before.barrier,*oldClone));CHECK(!p.editor.Swap(open.barrier,*oldClone));
	double value=999;CHECK(!p.editor.NumberCandidate(open.barrier.editor,{0,100,true},value,p.error) && value==999);NativeTextEditorReceipt receipt;CHECK(!p.editor.SettleComposition(open.barrier,101,receipt,p.error));
	oldClone=p.editor.Clone();p.Complete(collection);const auto closed=p.editor.View();CHECK(!p.editor.Swap(open.barrier,*oldClone));CHECK(!p.editor.Swap(closed.barrier,*oldClone));
	CHECK(closed.barrier.editor.revision==before.barrier.editor.revision && closed.barrier.sequence==0 && closed.barrier.shadowRevision==1 && closed.barrier.group==0);
	CHECK(Same(closed.draft,before.draft) && Same(closed.history,before.history));CHECK(p.editor.NumberCandidate(closed.barrier.editor,{0,100,true},value,p.error) && value==12);
	NativeTextEditorBarrier out;CHECK(!p.editor.CompleteCollection(closed.barrier,collection,out,p.error));
	const auto next=p.Begin(2,2);p.Complete(next);CHECK(p.editor.Barrier().collection.dispatch==2);
}
static void InvalidDescriptorsAndOffers(){
	Pair p;NativeTextEditorBarrier out;out.group=123;const auto prior=out;const auto initial=p.editor.View();
	for(const auto c:{NativeTextCollection{0,1,0},{1,0,0},{1,1,33},{1,1,UINT64_MAX}}){CHECK(!p.editor.BeginCollection(initial.barrier,c,out,p.error));CHECK(Same(p.editor.View(),initial) && out==prior);}
	for(int field=0;field<9;++field){auto stale=initial.barrier;switch(field){case 0:++stale.native.document;break;case 1:++stale.native.editorLease;break;case 2:++stale.editor.allocation;break;case 3:++stale.editor.modal;break;case 4:++stale.editor.window;break;case 5:++stale.editor.session;break;case 6:++stale.editor.revision;break;case 7:stale.editor.control="other";break;case 8:++stale.shadowRevision;break;}CHECK(!p.editor.BeginCollection(stale,{1,1,0},out,p.error));CHECK(Same(p.editor.View(),initial) && out==prior);}
	p.Queue(10,[&](auto s){CHECK(p.native.ReplaceACP(s,0,1,"3",p.error));});p.Queue(10,[&](auto s){CHECK(p.native.ReplaceACP(s,1,2,"4",p.error));});
	const auto collection=p.Begin(10,20);const auto opened=p.editor.View();
	CHECK(!p.editor.BeginCollection(opened.barrier,{11,21,2},out,p.error));CHECK(Same(p.editor.View(),opened) && out==prior);
	CHECK(!p.editor.CompleteCollection(opened.barrier,collection,out,p.error));CHECK(Same(p.editor.View(),opened) && out==prior);
	for(int bad=0;bad<4;++bad){auto offer=p.Offer();if(bad==0)offer.transaction.nativeDispatch=0;if(bad==1)offer.transaction.nativeDispatch=11;if(bad==2)offer.transaction.sequence=3;if(bad==3)++offer.expectedEngineRevision;p.RejectApply(offer);}
	p.Apply();auto partial=p.editor.View();CHECK(!p.editor.CompleteCollection(partial.barrier,collection,out,p.error));CHECK(Same(p.editor.View(),partial) && out==prior);
	p.Apply();const auto all=p.editor.View();
	for(int bad=0;bad<3;++bad){auto wrong=collection;if(bad==0)++wrong.dispatch;if(bad==1)++wrong.fenceSequence;if(bad==2)--wrong.lastTransactionSequence;CHECK(!p.editor.CompleteCollection(all.barrier,wrong,out,p.error));CHECK(Same(p.editor.View(),all) && out==prior);}
	CHECK(!p.editor.CompleteCollection(opened.barrier,collection,out,p.error));p.Complete(collection);auto completed=p.editor.View();
	CHECK(!p.editor.BeginCollection(completed.barrier,{11,21,2},out,p.error));CHECK(Same(p.editor.View(),completed));p.Settle();const auto settled=p.editor.View();
	for(const auto c:{NativeTextCollection{10,21,2},{11,20,2},{9,22,2},{12,19,2},{11,21,1}}){CHECK(!p.editor.BeginCollection(settled.barrier,c,out,p.error));CHECK(Same(p.editor.View(),settled) && out==prior);}
	// A lying short watermark cannot admit additional actual producer offers.
	Pair shortCount;shortCount.Queue(1,[&](auto s){CHECK(shortCount.native.ReplaceACP(s,0,1,"3",shortCount.error));});shortCount.Queue(1,[&](auto s){CHECK(shortCount.native.ReplaceACP(s,1,2,"4",shortCount.error));});
	CHECK(shortCount.editor.BeginCollection(shortCount.editor.Barrier(),{1,1,1},out,shortCount.error));shortCount.Apply();shortCount.RejectApply(shortCount.Offer());shortCount.Retire();CHECK(shortCount.editor.View().draft.text=="12");
	// A declared longer collection cannot complete with missing producer offers.
	Pair longCount;longCount.Queue(1,[&](auto s){CHECK(longCount.native.ReplaceACP(s,0,1,"3",longCount.error));});CHECK(longCount.editor.BeginCollection(longCount.editor.Barrier(),{1,1,2},out,longCount.error));longCount.Apply();
	CHECK(!longCount.editor.CompleteCollection(longCount.editor.Barrier(),{1,1,2},out,longCount.error));longCount.Retire();
}
static void CancelAndAbandon(){
	TextEditBuffer original;std::string error;CHECK(original.Reset("12",{},error));CHECK(original.ReplaceRange(0,2,"34",error));CHECK(original.Undo(error));const auto history=original.CaptureHistory();
	Pair p(original);p.Queue(1,[&](auto s){CHECK(p.native.ReplaceACP(s,0,2,"99",p.error));CHECK(p.native.BeginComposition(s,1,0,2,p.error));});p.Queue(1,[&](auto s){CHECK(p.native.EndComposition(s,1,p.error));});p.Begin(1,1);p.Apply();
	const auto before=p.editor.Barrier();NativeTextEditorReceipt receipt;auto stale=before;--stale.collection.lastTransactionSequence;
	CHECK(!p.editor.CancelComposition(stale,before.editor.revision+1,receipt,p.error));CHECK(p.editor.CancelComposition(before,before.editor.revision+1,receipt,p.error));CHECK(p.native.Retire(p.id,p.error));
	CHECK(!p.editor.Active() && !p.editor.Barrier().collectionOpen && Same(p.editor.View().draft,original.State()) && Same(p.editor.View().history,history));
	Pair q(original);q.Queue(5,[&](auto s){CHECK(q.native.ReplaceACP(s,0,2,"7",q.error));});q.Begin(5,6);q.Apply();const auto current=q.editor.Barrier();
	failAllocation=true;CHECK(q.editor.Abandon(current));failAllocation=false;CHECK(q.native.Retire(q.id,q.error));CHECK(!q.editor.Active() && !q.editor.Barrier().collectionOpen && q.editor.View().presentation.text=="12" && Same(q.editor.View().history,history));
	Pair empty;empty.Begin(1,1);empty.Retire();CHECK(!empty.editor.Barrier().collectionOpen);
}
static void AllocationAndCheckedPublication(){
	Pair p("123456789012345678901234567890");const auto before=p.editor.View();NativeTextEditorBarrier out;out.group=777;const auto prior=out;bool threw=false;
	failAllocation=true;try{p.editor.BeginCollection(before.barrier,{1,1,0},out,p.error);}catch(const std::bad_alloc&){threw=true;}failAllocation=false;CHECK(threw && Same(p.editor.View(),before) && out==prior);
	auto candidate=p.editor.Clone();CHECK(candidate->BeginCollection(before.barrier,{1,1,0},out,p.error));const auto prepared=candidate->View();
	failAllocation=true;CHECK(p.editor.Swap(before.barrier,*candidate));failAllocation=false;CHECK(Same(p.editor.View(),prepared));
	const auto opened=p.editor.View();threw=false;out=prior;failAllocation=true;
	try{p.editor.CompleteCollection(opened.barrier,{1,1,0},out,p.error);}catch(const std::bad_alloc&){threw=true;}failAllocation=false;CHECK(threw && Same(p.editor.View(),opened) && out==prior);
	candidate=p.editor.Clone();CHECK(candidate->CompleteCollection(opened.barrier,{1,1,0},out,p.error));const auto complete=candidate->View();
	failAllocation=true;CHECK(p.editor.Swap(opened.barrier,*candidate));failAllocation=false;CHECK(Same(p.editor.View(),complete));
}
static void CompletedCancelRollbackAndScalars(){
	TextEditBuffer draft;std::string error;CHECK(draft.Reset("12",{},error));CHECK(draft.ReplaceRange(0,2,"34",error));CHECK(draft.Undo(error));const auto history=draft.CaptureHistory();
	Pair canceled(draft);canceled.Queue(1,[&](auto s){CHECK(canceled.native.BeginComposition(s,1,0,2,canceled.error));CHECK(canceled.native.ReplaceACP(s,0,2,"56",canceled.error));});canceled.Queue(1,[&](auto s){CHECK(canceled.native.EndComposition(s,1,canceled.error));});canceled.Queue(1,[&](auto s){CHECK(canceled.native.ReplaceACP(s,0,1,"7",canceled.error));});
	const auto collection=canceled.Begin(1,1);canceled.Apply();canceled.Apply();canceled.Apply();canceled.Complete(collection);CHECK(canceled.editor.View().presentation.text=="76" && canceled.editor.View().draft.text=="12");
	NativeTextEditorReceipt receipt;const auto barrier=canceled.editor.Barrier();CHECK(canceled.editor.CancelComposition(barrier,barrier.editor.revision+1,receipt,error));CHECK(canceled.native.Retire(canceled.id,error));CHECK(Same(canceled.editor.View().draft,draft.State()) && Same(canceled.editor.View().history,history));
	Pair rollback(draft);rollback.Queue(2,[&](auto s){CHECK(rollback.native.BeginComposition(s,1,0,2,rollback.error));CHECK(rollback.native.ReplaceACP(s,0,2,"90",rollback.error));});rollback.Queue(2,[&](auto s){CHECK(rollback.native.EndComposition(s,1,rollback.error));});rollback.Queue(2,[&](auto s){CHECK(rollback.native.ReplaceACP(s,0,2,"12",rollback.error));CHECK(rollback.native.SelectACP(s,static_cast<std::uint32_t>(draft.State().anchor),static_cast<std::uint32_t>(draft.State().caret),rollback.error));});
	auto complete=rollback.Begin(2,3);rollback.Apply();rollback.Apply();rollback.Apply();rollback.Complete(complete);rollback.Settle();CHECK(Same(rollback.editor.View().draft,draft.State()) && Same(rollback.editor.View().history,history));
	Pair scalar("A\xf0\x9f\x98\x80Z");scalar.Queue(5,[&](auto s){CHECK(scalar.native.BeginComposition(s,1,1,3,scalar.error));});scalar.Queue(5,[&](auto s){CHECK(scalar.native.EndComposition(s,1,scalar.error));});scalar.Queue(5,[&](auto s){CHECK(scalar.native.ReplaceACP(s,1,3,"\xc3\xa9",scalar.error));CHECK(scalar.native.SelectACP(s,3,1,scalar.error));});
	complete=scalar.Begin(5,5);scalar.Apply();CHECK((scalar.editor.View().presentation.compositions.at(1)==NativeTextRange{1,5}));scalar.Apply();scalar.Apply();scalar.Complete(complete);scalar.Settle();CHECK(scalar.editor.View().draft.text=="A\xc3\xa9Z" && scalar.editor.View().draft.anchor==4 && scalar.editor.View().draft.caret==1 && scalar.editor.View().history.undo.size()==1);
}
static void RepeatedGroupsAndBounds(){
	Pair p("0");std::uint64_t previousGroup=0;
	for(std::uint64_t i=1;i<=70;++i){const auto old=p.editor.View().draft.text;p.Queue(i,[&](auto s){CHECK(p.native.ReplaceACP(s,0,static_cast<std::uint32_t>(old.size()),std::to_string(i),p.error));CHECK(p.native.BeginComposition(s,i,0,1,p.error));});p.Queue(i,[&](auto s){CHECK(p.native.EndComposition(s,i,p.error));});
		const auto collection=p.Begin(i,i);p.Apply();CHECK(p.editor.Barrier().group>previousGroup);previousGroup=p.editor.Barrier().group;p.Apply();p.Complete(collection);p.Settle();CHECK(p.editor.View().draft.text==std::to_string(i));}
	const auto history=p.editor.View().history;CHECK(history.undo.size()==64 && history.undo.front().text=="6" && history.undo.back().text=="69");
	Pair bound;bound.Queue(1,[&](auto s){CHECK(bound.native.BeginComposition(s,1,0,1,bound.error));});for(unsigned i=1;i<32;++i)bound.Queue(1,[&](auto s){CHECK(bound.native.UpdateComposition(s,1,0,1,bound.error));});
	const auto collection=bound.Begin(1,1);CHECK(collection.lastTransactionSequence==32);bound.Apply();for(unsigned i=1;i<32;++i)bound.Apply(false);bound.Complete(collection);CHECK(bound.editor.View().presentation.compositions.size()==1 && !bound.editor.View().awaitingSettlement);
	bound.Queue(2,[&](auto s){CHECK(bound.native.EndComposition(s,1,bound.error));});
	const auto final=bound.Begin(2,2);bound.Apply();bound.Complete(final);CHECK(bound.editor.View().awaitingSettlement);bound.Settle();
	CHECK(bound.editor.View().draft.text=="12" && bound.editor.View().history.undo.empty() && bound.editor.View().history.redo.empty());
}
int main(){InsertionBeginAndEndTail();MultipleRangesAndLaterBegin();LiveRangesAcrossCollections();EmptyCollectionsAndProtocolBarriers();InvalidDescriptorsAndOffers();CancelAndAbandon();AllocationAndCheckedPublication();CompletedCancelRollbackAndScalars();RepeatedGroupsAndBounds();std::printf("PASS %u checks\n",checks);}
