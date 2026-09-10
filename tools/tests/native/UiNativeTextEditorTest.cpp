// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "ui/retained/NativeTextEditor.h"
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <limits>
#include <new>
static bool failAllocation=false;
void* operator new(std::size_t size){if(failAllocation)throw std::bad_alloc();if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
using namespace openq4::ui;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
static bool Same(const TextEditState& a,const TextEditState& b){return a.text==b.text && a.anchor==b.anchor && a.caret==b.caret;}
static bool Same(const TextEditHistory& a,const TextEditHistory& b){
	if(a.undo.size()!=b.undo.size() || a.redo.size()!=b.redo.size())return false;
	for(std::size_t i=0;i<a.undo.size();++i)if(!Same(a.undo[i],b.undo[i]))return false;
	for(std::size_t i=0;i<a.redo.size();++i)if(!Same(a.redo[i],b.redo[i]))return false;
	return true;
}
static bool Same(const NativeTextEditorView& a,const NativeTextEditorView& b){return a.barrier==b.barrier && Same(a.draft,b.draft) && Same(a.history,b.history) && a.presentation==b.presentation && a.active==b.active && a.awaitingSettlement==b.awaitingSettlement && a.policy.maxBytes==b.policy.maxBytes && a.policy.multiline==b.policy.multiline && a.policy.allowTab==b.policy.allowTab;}
static bool Same(const NativeTextEditorReceipt& a,const NativeTextEditorReceipt& b){return a.before==b.before && a.after==b.after && a.effect==b.effect && a.presentationChanged==b.presentationChanged;}
static TextEditorIdentity Owner(){return {1,2,3,4,5,6,100,"number"};}
struct Pair {
	NativeTextIdentity id{20,21};NativeTextDocument native;NativeTextEditor editor;std::string error;
	explicit Pair(std::string_view text="12",TextEditPolicy policy={}) {
		TextEditBuffer draft;CHECK(draft.Reset(text,policy,error));CHECK(draft.SetSelection(text.size(),0,error));Open(draft);
	}
	explicit Pair(const TextEditBuffer& draft){Open(draft);}
	void Open(const TextEditBuffer& draft){const auto owner=Owner();CHECK(editor.Open(id,owner,draft,error));CHECK(native.Open(id,owner.revision,draft.State().text,draft.State().anchor,draft.State().caret,error));}
	NativeTextOffer Produce(const std::function<void(const NativeTextLockScope&)>& f) {
		NativeTextLockScope scope;CHECK(native.RequestLock(id,NativeTextAccess::ReadWrite,true,scope,error)==NativeTextLockResult::Granted);
		f(scope);std::uint64_t published=0;CHECK(native.FinishLock(scope,77,published,error) && published);NativeTextOffer offer;CHECK(native.PeekOffer(id,offer,error));return offer;
	}
	void Receipt(const NativeTextEditorReceipt& result){
		CHECK(result.before.native==id && result.after.native==id);
		switch(result.effect){
		case NativeTextEditorEffect::Acknowledge:CHECK(native.Acknowledge(id,result.after.sequence,result.after.shadowRevision,result.before.editor.revision,result.after.editor.revision,error));break;
		case NativeTextEditorEffect::SyncEngine:{const auto view=editor.View();CHECK(native.SyncEngine(id,result.before.editor.revision,result.before.shadowRevision,result.after.editor.revision,view.presentation.text,view.presentation.anchor,view.presentation.caret,error));break;}
		case NativeTextEditorEffect::Retire:CHECK(native.Retire(id,error));return;
		}
		CHECK(native.EngineRevision()==editor.Barrier().editor.revision && native.ShadowRevision()==editor.Barrier().shadowRevision);
	}
	NativeTextEditorReceipt Apply(const NativeTextOffer& offer,bool advance=true){NativeTextEditorReceipt result;const auto expected=editor.Barrier().editor;CHECK(editor.Apply(offer,expected,expected.revision+(advance?1:0),result,error));Receipt(result);return result;}
	void Settle(){NativeTextEditorReceipt result;const auto before=editor.Barrier();CHECK(editor.SettleComposition(before,before.editor.revision+1,result,error));CHECK(result.effect==NativeTextEditorEffect::SyncEngine && result.before.shadowRevision==result.after.shadowRevision);Receipt(result);}
	void Reject(const NativeTextOffer& offer){const auto before=editor.View();NativeTextEditorReceipt result;result.presentationChanged=true;result.after.group=42;const auto unchanged=result;CHECK(!editor.Apply(offer,before.barrier.editor,before.barrier.editor.revision+1,result,error));CHECK(Same(editor.View(),before) && Same(result,unchanged));}
};
static void RangesAndNumericDraft() {
	Pair p("1.25");
	const auto offer=p.Produce([&](auto scope){CHECK(p.native.SelectACP(scope,0,0,p.error));CHECK(p.native.ReplaceACP(scope,2,4,"50",p.error));CHECK(p.native.SelectACP(scope,4,1,p.error));});
	const auto receipt=p.Apply(offer);CHECK(receipt.presentationChanged);
	const auto v=p.editor.View();CHECK(v.draft.text=="1.50" && v.draft.anchor==4 && v.draft.caret==1 && v.history.undo.size()==1 && v.history.undo[0].anchor==4 && v.history.undo[0].caret==0);
	double number=-99;CHECK(p.editor.NumberCandidate(v.barrier.editor,{0,2,true},number,p.error) && number==1.5);
	TextEditBuffer restored;CHECK(restored.RestoreHistory(v.draft,v.history,v.policy,p.error));CHECK(restored.Undo(p.error));CHECK(restored.State().text=="1.25" && restored.State().anchor==4 && restored.State().caret==0);CHECK(restored.Redo(p.error));CHECK(Same(restored.State(),v.draft));
	const auto incomplete=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,0,4,"1e-",p.error));});p.Apply(incomplete);
	number=71;CHECK(!p.editor.NumberCandidate(p.editor.Barrier().editor,{0,2,true},number,p.error) && number==71);CHECK(p.editor.View().draft.text=="1e-");
}
static void CompositionLifecycle() {
	Pair p("0.25");const auto stable=p.editor.View();
	const auto begin=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,0,4,"1",p.error));CHECK(p.native.BeginComposition(scope,1,0,1,p.error));CHECK(p.native.SelectACP(scope,1,0,p.error));});p.Apply(begin);
	CHECK(p.editor.View().presentation.text=="1" && Same(p.editor.View().draft,stable.draft) && Same(p.editor.View().history,stable.history));
	double value=17;CHECK(!p.editor.NumberCandidate(p.editor.Barrier().editor,{0,2,true},value,p.error) && value==17);
	const auto group=p.editor.Barrier().group;CHECK(group!=0);
	NativeTextEditorReceipt ignored;CHECK(!p.editor.SettleComposition(p.editor.Barrier(),p.editor.Barrier().editor.revision+1,ignored,p.error));
	const auto update=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,0,1,"1.75",p.error));CHECK(p.native.UpdateComposition(scope,1,0,4,p.error));CHECK(p.native.SelectACP(scope,4,1,p.error));});p.Apply(update);
	CHECK(p.editor.Barrier().group==group && p.editor.View().presentation.compositions.at(1)==(NativeTextRange{0,4}));
	const auto end=p.Produce([&](auto scope){CHECK(p.native.EndComposition(scope,1,p.error));});p.Apply(end);
	CHECK(p.editor.View().awaitingSettlement && p.editor.View().presentation.compositions.empty() && Same(p.editor.View().draft,stable.draft));
	const auto afterEnd=p.editor.View();p.Reject(end);CHECK(Same(p.editor.View(),afterEnd));
	p.Settle();const auto done=p.editor.View();CHECK(done.draft.text=="1.75" && done.draft.anchor==4 && done.draft.caret==1 && done.history.undo.size()==1 && Same(done.history.undo[0],stable.draft));
	CHECK(!done.barrier.group && !done.awaitingSettlement && p.editor.NumberCandidate(done.barrier.editor,{0,2,true},value,p.error) && value==1.75);
	// Production SyncEngine receipt keeps native and reconciler shadow aligned;
	// the next actual native transaction starts at that same shadow revision.
	const auto next=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,0,4,"2",p.error));CHECK(p.native.BeginComposition(scope,2,0,1,p.error));CHECK(p.native.EndComposition(scope,2,p.error));});p.Apply(next);CHECK(p.editor.Barrier().group>group);p.Settle();CHECK(p.editor.View().history.undo.size()==2);
}
static void MultipleAndEmptyRanges() {
	Pair p("abcdef");const auto before=p.editor.View();
	auto tx=p.Produce([&](auto scope){CHECK(p.native.BeginComposition(scope,1,0,3,p.error));CHECK(p.native.BeginComposition(scope,2,2,5,p.error));CHECK(p.native.BeginComposition(scope,3,6,6,p.error));});p.Apply(tx);
	CHECK(p.editor.View().presentation.compositions.size()==3);
	tx=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,1,3,"Q",p.error));CHECK(p.native.SelectACP(scope,4,1,p.error));CHECK(p.native.EndComposition(scope,1,p.error));});p.Apply(tx);
	const auto snapshot=p.editor.View().presentation;CHECK(snapshot.text=="aQdef" && snapshot.compositions.at(2)==(NativeTextRange{1,4}) && snapshot.compositions.at(3)==(NativeTextRange{5,5}));
	CHECK(!p.editor.View().awaitingSettlement && Same(p.editor.View().draft,before.draft));
	tx=p.Produce([&](auto scope){CHECK(p.native.EndComposition(scope,2,p.error));CHECK(p.native.EndComposition(scope,3,p.error));CHECK(p.native.BeginComposition(scope,4,0,0,p.error));CHECK(p.native.EndComposition(scope,4,p.error));});p.Apply(tx);p.Settle();
	CHECK(p.editor.View().history.undo.size()==1 && p.editor.View().draft.text=="aQdef" && p.editor.View().draft.anchor==4 && p.editor.View().draft.caret==1);
	const auto previous=p.editor.View();
	tx=p.Produce([&](auto scope){CHECK(p.native.BeginComposition(scope,5,0,0,p.error));CHECK(p.native.EndComposition(scope,5,p.error));});p.Apply(tx);p.Settle();CHECK(Same(p.editor.View().history,previous.history) && Same(p.editor.View().draft,previous.draft));
}
static void CancelAndRollback() {
	std::string e;TextEditBuffer draft;CHECK(draft.Reset("1",{},e));CHECK(draft.ReplaceSelection("2",e));CHECK(draft.ReplaceSelection("3",e));CHECK(draft.Undo(e));CHECK(draft.SetSelection(2,0,e));
	Pair p(draft);const auto before=p.editor.View();
	auto tx=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,0,2,"9",p.error));CHECK(p.native.BeginComposition(scope,1,0,1,p.error));});p.Apply(tx);
	NativeTextEditorReceipt receipt;const auto current=p.editor.Barrier();auto stale=current;++stale.editor.modal;CHECK(!p.editor.CancelComposition(stale,current.editor.revision+1,receipt,e));
	CHECK(p.editor.CancelComposition(current,current.editor.revision+1,receipt,e));CHECK(receipt.effect==NativeTextEditorEffect::Retire);p.Receipt(receipt);
	const auto canceled=p.editor.View();CHECK(!canceled.active && Same(canceled.draft,before.draft) && Same(canceled.history,before.history) && canceled.presentation.text==before.draft.text && canceled.presentation.compositions.empty());
	CHECK(!p.editor.CancelComposition(current,current.editor.revision+2,receipt,e));CHECK(!p.editor.Open({90,91},Owner(),draft,e));
	TextEditBuffer resumed;CHECK(resumed.RestoreHistory(canceled.draft,canceled.history,canceled.policy,e));NativeTextEditor fresh;auto owner=canceled.barrier.editor;++owner.session;++owner.revision;CHECK(fresh.Open({90,91},owner,resumed,e));CHECK(Same(fresh.View().draft,before.draft) && Same(fresh.View().history,before.history));
	// Native cancellation may itself restore exact stable bytes and selection.
	// Settling that final shadow creates no text history and preserves redo.
	Pair q(draft);
	tx=q.Produce([&](auto scope){CHECK(q.native.ReplaceACP(scope,0,2,"9",e));CHECK(q.native.BeginComposition(scope,1,0,1,e));});q.Apply(tx);
	tx=q.Produce([&](auto scope){CHECK(q.native.ReplaceACP(scope,0,1,"12",e));CHECK(q.native.SelectACP(scope,2,0,e));CHECK(q.native.EndComposition(scope,1,e));});q.Apply(tx);q.Settle();CHECK(Same(q.editor.View().draft,before.draft) && Same(q.editor.View().history,before.history));
}
static void ValidationAtomicity() {
	Pair p("A\xf0\x9f\x98\x80Z");
	const auto valid=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,1,3,"X",p.error));CHECK(p.native.SelectACP(scope,3,0,p.error));});
	for(int mode=0;mode<15;++mode){auto bad=valid;auto& t=bad.transaction;
		switch(mode){case 0:++t.identity.document;break;case 1:++bad.expectedEngineRevision;break;case 2:++t.sequence;break;case 3:++t.shadowBefore;break;case 4:++t.shadowAfter;break;case 5:t.after.text="forged";break;case 6:++t.after.anchor;break;case 7:t.documentChanged=!t.documentChanged;break;case 8:t.classification=NativeTextClassification::CompositionRelated;break;case 9:t.operations[0].first=2;break;case 10:t.operations[0].text=std::string("\0",1);break;case 11:t.operations[1].kind=static_cast<NativeTextOperationKind>(99);break;case 12:t.operations[1].composition=99;break;case 13:t.operations.resize(257);break;case 14:t.after.compositions.emplace(99,NativeTextRange{0,1});break;}p.Reject(bad);}
	// Match the forged after-snapshot to an incorrectly rounded-up surrogate
	// endpoint: exact endpoint validation must reject independently of the final
	// replay comparison, even when an attacker also controls the result fields.
	auto split=valid;split.transaction.operations[0].first=2;split.transaction.after.text="A\xf0\x9f\x98\x80XZ";split.transaction.after.anchor=5;p.Reject(split);
	const auto before=p.editor.View();NativeTextEditorReceipt receipt;auto owner=before.barrier.editor;++owner.backend;CHECK(!p.editor.Apply(valid,owner,owner.revision+1,receipt,p.error) && Same(p.editor.View(),before));
	CHECK(!p.editor.Apply(valid,before.barrier.editor,before.barrier.editor.revision,receipt,p.error) && Same(p.editor.View(),before));p.Apply(valid);
	const auto multibyte=p.editor.View();CHECK(multibyte.draft.text=="AXZ" && multibyte.draft.anchor==3 && multibyte.draft.caret==0);
	// Invalid middle field state cannot be hidden by a later valid replacement.
	Pair lines("ab");auto invalid=lines.Produce([&](auto scope){CHECK(lines.native.ReplaceACP(scope,0,1,"\n",lines.error));CHECK(lines.native.ReplaceACP(scope,0,1,"x",lines.error));});lines.Reject(invalid);
	Pair size("abcd",{4,false,false});auto oversized=size.Produce([&](auto scope){CHECK(size.native.ReplaceACP(scope,0,0,"x",size.error));});size.Reject(oversized);
}
static void RepeatedAndHistoryBudget() {
	Pair p("0");std::uint64_t previousGroup=0;
	for(std::uint64_t token=1;token<=100;++token){auto tx=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,0,static_cast<std::uint32_t>(p.editor.View().presentation.text.size()),std::to_string(token),p.error));CHECK(p.native.BeginComposition(scope,token,0,1,p.error));CHECK(p.native.EndComposition(scope,token,p.error));});p.Apply(tx);CHECK(p.editor.Barrier().group>previousGroup);previousGroup=p.editor.Barrier().group;p.Settle();}
	const auto view=p.editor.View();CHECK(view.draft.text=="100" && view.history.undo.size()==64 && view.history.undo.front().text=="36" && view.history.undo.back().text=="99");
	Pair large(std::string(65536,'a'));
	for(std::uint64_t token=1;token<=20;++token){auto tx=large.Produce([&](auto scope){CHECK(large.native.ReplaceACP(scope,0,1,token%2?"b":"a",large.error));CHECK(large.native.BeginComposition(scope,token,0,1,large.error));CHECK(large.native.EndComposition(scope,token,large.error));});large.Apply(tx);large.Settle();}
	const auto history=large.editor.View().history;CHECK(history.undo.size()==16);std::size_t bytes=0;for(const auto& state:history.undo)bytes+=state.text.size();CHECK(bytes==TextEditBuffer::MaxHistoryTextBytes);
}
static void NoopMetadataAndStaleBarrier() {
	Pair p;auto tx=p.Produce([&](auto scope){CHECK(p.native.BeginComposition(scope,1,0,1,p.error));});p.Apply(tx);
	const auto earlier=p.editor.Barrier();tx=p.Produce([&](auto scope){CHECK(p.native.UpdateComposition(scope,1,0,1,p.error));});const auto noChange=p.Apply(tx,false);CHECK(!noChange.presentationChanged);
	tx=p.Produce([&](auto scope){CHECK(p.native.EndComposition(scope,1,p.error));});p.Apply(tx);
	NativeTextEditorReceipt receipt;const auto before=p.editor.View();CHECK(!p.editor.SettleComposition(earlier,before.barrier.editor.revision+1,receipt,p.error) && Same(p.editor.View(),before));
	auto stale=before.barrier;++stale.group;CHECK(!p.editor.SettleComposition(stale,before.barrier.editor.revision+1,receipt,p.error));
	CHECK(!p.editor.SettleComposition(before.barrier,before.barrier.editor.revision,receipt,p.error));p.Settle();
	CHECK(!p.editor.CancelComposition(p.editor.Barrier(),p.editor.Barrier().editor.revision+1,receipt,p.error));
	const auto stable=p.editor.View();CHECK(p.editor.Retire(stable.barrier,stable.barrier.editor.revision+1,receipt,p.error));p.Receipt(receipt);CHECK(!p.editor.Active() && Same(p.editor.View().draft,stable.draft));
}
static void AllocationFailureAndAbandonment() {
	Pair p;auto tx=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,0,2,"5",p.error));});p.Apply(tx);
	const auto stable=p.editor.View();
	tx=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,0,1,"99",p.error));CHECK(p.native.BeginComposition(scope,1,0,2,p.error));});
	NativeTextEditorReceipt receipt;receipt.after.group=999;const auto originalReceipt=receipt;const auto expected=stable.barrier.editor;
	bool threw=false;failAllocation=true;
	try{p.editor.Apply(tx,expected,expected.revision+1,receipt,p.error);}catch(const std::bad_alloc&){threw=true;}
	failAllocation=false;CHECK(threw && Same(p.editor.View(),stable) && Same(receipt,originalReceipt));
	CHECK(p.editor.Apply(tx,expected,expected.revision+1,receipt,p.error));
	// A failed receipt cannot permit either side to continue. Exact abandonment
	// restores the latest stable draft, not the original document-open text.
	CHECK(!p.native.Acknowledge(p.id,receipt.after.sequence,receipt.after.shadowRevision,receipt.before.editor.revision+1,receipt.after.editor.revision,p.error));
	auto stale=receipt.before;CHECK(!p.editor.Abandon(stale));
	const auto now=p.editor.Barrier();failAllocation=true;CHECK(p.editor.Abandon(now));failAllocation=false;
	CHECK(!p.editor.Active() && Same(p.editor.View().draft,stable.draft) && Same(p.editor.View().history,stable.history) && p.editor.View().presentation.text=="5");
	CHECK(p.native.Retire(p.id,p.error));CHECK(!p.editor.Abandon(p.editor.Barrier()));
	Pair q;tx=q.Produce([&](auto scope){CHECK(q.native.ReplaceACP(scope,0,2,"7",q.error));CHECK(q.native.BeginComposition(scope,1,0,1,q.error));CHECK(q.native.EndComposition(scope,1,q.error));});q.Apply(tx);
	const auto unsettled=q.editor.View();threw=false;failAllocation=true;
	try{q.editor.SettleComposition(unsettled.barrier,unsettled.barrier.editor.revision+1,receipt,q.error);}catch(const std::bad_alloc&){threw=true;}
	failAllocation=false;CHECK(threw && Same(q.editor.View(),unsettled));q.Settle();
	// Emergency retirement does not require a fresh revision at UINT64_MAX.
	NativeTextEditor exhausted;TextEditBuffer draft;std::string error;CHECK(draft.Reset("8",{},error));auto owner=Owner();owner.revision=UINT64_MAX;
	CHECK(exhausted.Open({80,81},owner,draft,error));const auto barrier=exhausted.Barrier();CHECK(!exhausted.Retire(barrier,UINT64_MAX,receipt,error));
	failAllocation=true;CHECK(exhausted.Abandon(barrier));failAllocation=false;CHECK(!exhausted.Active() && exhausted.View().presentation.text=="8");
}
static void CandidatePublication() {
	Pair p("123456789012345678901234567890");const auto before=p.editor.View();
	const auto offer=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,0,1,"9",p.error));});
	bool threw=false;failAllocation=true;
	try{auto copy=p.editor.Clone();(void)copy;}catch(const std::bad_alloc&){threw=true;}
	failAllocation=false;CHECK(threw && Same(p.editor.View(),before));
	auto candidate=p.editor.Clone();NativeTextEditorReceipt receipt;
	CHECK(candidate->Apply(offer,before.barrier.editor,before.barrier.editor.revision+1,receipt,p.error));
	// View/Interaction installation may allocate AFTER reconciliation. Only the
	// disposable clone has changed; neither canonical owner nor native ACK has.
	threw=false;failAllocation=true;
	try{const auto view=candidate->View();(void)view;}catch(const std::bad_alloc&){threw=true;}
	failAllocation=false;CHECK(threw && Same(p.editor.View(),before));
	const auto prepared=candidate->View();TextEditBuffer interactionCandidate;
	CHECK(interactionCandidate.RestoreHistory(prepared.draft,prepared.history,prepared.policy,p.error));
	CHECK(Same(interactionCandidate.State(),prepared.draft));
	failAllocation=true;CHECK(p.editor.Swap(before.barrier,*candidate));failAllocation=false;
	CHECK(Same(p.editor.View(),prepared) && Same(candidate->View(),before));
	CHECK(!p.editor.Swap(prepared.barrier,*candidate));p.Receipt(receipt);
	// An intervening canonical edit invalidates an older prepared candidate,
	// even if the owner accidentally allows callbacks during preparation.
	const auto current=p.editor.View();auto stale=p.editor.Clone();
	const auto next=p.Produce([&](auto scope){CHECK(p.native.ReplaceACP(scope,0,1,"8",p.error));});
	CHECK(stale->Apply(next,current.barrier.editor,current.barrier.editor.revision+1,receipt,p.error));
	const auto staleView=stale->View();p.Apply(next);const auto advanced=p.editor.View();
	CHECK(!p.editor.Swap(current.barrier,*stale));CHECK(!p.editor.Swap(advanced.barrier,*stale));
	CHECK(Same(p.editor.View(),advanced) && Same(stale->View(),staleView));
	CHECK(!p.editor.Swap(advanced.barrier,p.editor));
	// Abandonment invalidates an otherwise unchanged clone barrier too.
	auto retired=p.editor.Clone();CHECK(p.editor.Abandon(advanced.barrier));
	CHECK(!p.editor.Swap(advanced.barrier,*retired));CHECK(p.native.Retire(p.id,p.error));
}
int main(){RangesAndNumericDraft();CompositionLifecycle();MultipleAndEmptyRanges();CancelAndRollback();ValidationAtomicity();RepeatedAndHistoryBudget();NoopMetadataAndStaleBarrier();AllocationFailureAndAbandonment();CandidatePublication();std::printf("PASS %u checks\n",checks);}
