// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "NativeTextEditor.h"
#include <algorithm>
#include <limits>
#include <set>
#include <type_traits>

namespace openq4::ui {
namespace {
bool Fail(std::string& error,const char* message){error=message;return false;}
bool SameState(const TextEditState& a,const TextEditState& b){return a.text==b.text && a.anchor==b.anchor && a.caret==b.caret;}
TextEditState State(const NativeTextSnapshot& value){return {value.text,value.anchor,value.caret};}
NativeTextSnapshot Snapshot(const TextEditState& value){return {value.text,value.anchor,value.caret,{}};}
bool ValidOwner(const TextEditorIdentity& id) {
	if(!id.allocation || !id.backend || !id.document || !id.modal || !id.window || !id.session || !id.revision ||
		id.control.empty() || id.control.size()>TextInputBroker::MaxControlBytes) return false;
	return std::all_of(id.control.begin(),id.control.end(),[](unsigned char c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.';});
}
bool Charge(std::size_t& sum,std::size_t amount) {
	constexpr std::size_t limit=1024*1024;
	if(amount>limit || sum>limit-amount) return false;
	sum+=amount;return true;
}
bool ValidEnvelope(const NativeTextTransaction& tx,std::string& error) {
	if(tx.operations.empty() || tx.operations.size()>256 || tx.after.text.size()>TextInputMaxBytes || tx.after.compositions.size()>32)
		return Fail(error,"Native transaction exceeds its envelope bounds");
	std::size_t bytes=0;
	if(!Charge(bytes,sizeof(tx)) || !Charge(bytes,tx.after.text.size()+1) ||
		!Charge(bytes,tx.after.compositions.size()*sizeof(std::pair<const std::uint64_t,NativeTextRange>)))
		return Fail(error,"Native transaction exceeds its payload bound");
	for(const auto& op:tx.operations) if(op.text.size()>TextInputMaxBytes || !Charge(bytes,sizeof(op)) || !Charge(bytes,op.text.size()+1))
		return Fail(error,"Native transaction exceeds its payload bound");
	return true;
}
bool Find(const std::vector<NativeTextBoundary>& map,std::uint32_t acp,std::size_t& out) {
	const auto at=std::lower_bound(map.begin(),map.end(),acp,[](const auto& b,std::uint32_t p){return b.acp<p;});
	if(at==map.end() || at->acp!=acp) return false;
	out=at->byte;return true;
}
std::size_t Shift(std::size_t p,std::size_t first,std::size_t last,std::size_t count,bool following) {
	if(p<first) return p;
	if(p>last) return p-last+first+count;
	if(p==last && first!=last) return first+count;
	return first+(following?count:0);
}
bool Field(const NativeTextSnapshot& value,const TextEditPolicy& policy,std::string& error) {
	// Reset validates exact field bytes; ReplaceRange would normalize CRLF and
	// invalidate the native transaction's subsequent ACP coordinates.
	TextEditBuffer checked;
	return checked.Reset(value.text,policy,error) && checked.SetSelection(value.anchor,value.caret,error);
}
bool Replay(const NativeTextSnapshot& before,std::uint64_t& high,const NativeTextTransaction& tx,
	const TextEditPolicy& policy,NativeTextSnapshot& after,std::string& error) {
	NativeTextSnapshot candidate=before;
	bool related=!before.compositions.empty(),metadata=false;
	std::set<std::uint64_t> retained;
	for(const auto& [id,range]:before.compositions){(void)range;retained.insert(id);}
	for(const auto& op:tx.operations) {
		std::vector<NativeTextBoundary> map;std::size_t first=0,last=0;
		if(!BuildNativeTextMap(candidate.text,map,error)) return false;
		if(op.kind!=NativeTextOperationKind::EndComposition && (!Find(map,op.first,first) || !Find(map,op.last,last)))
			return Fail(error,"Native operation range splits a scalar or exceeds the current text");
		if(op.kind!=NativeTextOperationKind::Select && first>last) return Fail(error,"Native range is reversed");
		switch(op.kind) {
		case NativeTextOperationKind::Replace: {
			if(op.composition || !ValidateTextInputUtf8(op.text,error)) return Fail(error,"Invalid native replacement payload");
			const auto remain=candidate.text.size()-(last-first);
			if(remain>policy.maxBytes || op.text.size()>policy.maxBytes-remain) return Fail(error,"Native replacement exceeds field bounds");
			candidate.text.replace(first,last-first,op.text);
			candidate.anchor=Shift(candidate.anchor,first,last,op.text.size(),true);
			candidate.caret=Shift(candidate.caret,first,last,op.text.size(),true);
			for(auto& [id,range]:candidate.compositions) {
				(void)id;range.first=Shift(range.first,first,last,op.text.size(),false);range.last=Shift(range.last,first,last,op.text.size(),true);
			}
			break;
		}
		case NativeTextOperationKind::Select:
			if(!op.text.empty() || op.composition) return Fail(error,"Native selection has unexpected fields");
			candidate.anchor=first;candidate.caret=last;break;
		case NativeTextOperationKind::BeginComposition:
			if(!op.text.empty() || !op.composition || op.composition<=high) return Fail(error,"Native composition identity was reused");
			high=op.composition;candidate.compositions.emplace(op.composition,NativeTextRange{first,last});related=metadata=true;retained.insert(op.composition);break;
		case NativeTextOperationKind::UpdateComposition: {
			if(!op.text.empty() || !op.composition) return Fail(error,"Native composition update has unexpected fields");
			const auto found=candidate.compositions.find(op.composition);
			if(found==candidate.compositions.end()) return Fail(error,"Native composition update has no active identity");
			found->second={first,last};related=metadata=true;break;
		}
		case NativeTextOperationKind::EndComposition:
			if(!op.text.empty() || op.first || op.last || !op.composition || !candidate.compositions.erase(op.composition))
				return Fail(error,"Native composition end has no active identity or has unexpected fields");
			related=metadata=true;break;
		default:return Fail(error,"Unknown native operation");
		}
		if(retained.size()>32 || candidate.compositions.size()>32) return Fail(error,"Native composition range budget exceeded");
		if(!Field(candidate,policy,error)) return false;
	}
	const bool documentChanged=!SameState(State(before),State(candidate));
	if(!documentChanged && !metadata) return Fail(error,"Native producer must not publish an empty transaction");
	if(tx.documentChanged!=documentChanged || tx.classification!=(related?NativeTextClassification::CompositionRelated:NativeTextClassification::Unclassified) || tx.after!=candidate)
		return Fail(error,"Native transaction result does not match its complete ordered operations");
	after=std::move(candidate);return true;
}
bool Adopt(TextEditBuffer& stable,const NativeTextSnapshot& snapshot,std::string& error) {
	TextEditHistory history=stable.CaptureHistory();
	if(snapshot.text!=stable.State().text) {
		history.undo.push_back(stable.State());history.redo.clear();
		std::size_t bytes=0;for(const auto& entry:history.undo)bytes+=entry.text.size();
		while(history.undo.size()>TextEditBuffer::MaxHistoryEntries || bytes>TextEditBuffer::MaxHistoryTextBytes) {
			bytes-=history.undo.front().text.size();history.undo.erase(history.undo.begin());
		}
	}
	return stable.RestoreHistory(State(snapshot),history,stable.Policy(),error);
}
}

struct NativeTextEditor::Impl {
	NativeTextEditorBarrier barrier;
	TextEditBuffer stable;
	NativeTextSnapshot presentation,stablePresentation;
	std::uint64_t compositionHigh=0,groupHigh=0;
	std::optional<NativeTextEditorBarrier> cloneOrigin;
	bool opened=false,active=false,awaiting=false,collectionMode=false;
	bool Owner(const TextEditorIdentity& expected,std::string& error) const {
		return active && expected==barrier.editor ? true:Fail(error,"Native editor owner or revision is stale");
	}
	bool Exact(const NativeTextEditorBarrier& expected,std::string& error) const {
		return active && expected==barrier ? true:Fail(error,"Native editor completion barrier is stale");
	}
	bool Revision(std::uint64_t revision,bool changed,std::string& error) const {
		return revision && revision>=barrier.editor.revision && (!changed || revision>barrier.editor.revision) ? true:
			Fail(error,"Native editor receipt requires a current or advanced engine revision");
	}
};
NativeTextEditor::NativeTextEditor():impl(std::make_unique<Impl>()){}
NativeTextEditor::~NativeTextEditor()=default;
std::unique_ptr<NativeTextEditor> NativeTextEditor::Clone() const {
	auto candidate=std::make_unique<NativeTextEditor>();
	candidate->impl=std::make_unique<Impl>(*impl);
	candidate->impl->cloneOrigin=impl->barrier;
	return candidate;
}
bool NativeTextEditor::Swap(const NativeTextEditorBarrier& expected,NativeTextEditor& candidate) noexcept {
	if(this==&candidate || !impl->active || expected!=impl->barrier ||
		!candidate.impl->cloneOrigin || *candidate.impl->cloneOrigin!=expected) return false;
	impl.swap(candidate.impl);
	impl->cloneOrigin.reset();candidate.impl->cloneOrigin.reset();return true;
}
bool NativeTextEditor::Open(NativeTextIdentity native,const TextEditorIdentity& owner,const TextEditBuffer& draft,std::string& error) {
	if(impl->opened || !native.document || !native.editorLease || !ValidOwner(owner) || draft.Composition())
		return Fail(error,"Native editor cannot open with this identity or active overlay");
	auto candidate=std::make_unique<Impl>();
	if(!candidate->stable.RestoreHistory(draft.State(),draft.CaptureHistory(),draft.Policy(),error)) return false;
	candidate->barrier={native,owner,0,1,0};candidate->presentation=Snapshot(draft.State());candidate->stablePresentation=candidate->presentation;candidate->opened=candidate->active=true;
	impl.swap(candidate);error.clear();return true;
}
NativeTextEditorView NativeTextEditor::View() const {
	return {impl->barrier,impl->stable.State(),impl->stable.CaptureHistory(),impl->stable.Policy(),impl->presentation,impl->active,impl->awaiting};
}
NativeTextEditorBarrier NativeTextEditor::Barrier() const{return impl->barrier;}
bool NativeTextEditor::Active() const{return impl->active;}
bool NativeTextEditor::BeginCollection(const NativeTextEditorBarrier& expected,const NativeTextCollection& collection,
	NativeTextEditorBarrier& out,std::string& error) {
	if(!impl->Exact(expected,error)) return false;
	if(impl->barrier.collectionOpen || impl->awaiting || !collection.dispatch || !collection.fenceSequence ||
		collection.dispatch<=impl->barrier.collection.dispatch || collection.fenceSequence<=impl->barrier.collection.fenceSequence ||
		collection.lastTransactionSequence<impl->barrier.sequence || collection.lastTransactionSequence-impl->barrier.sequence>32)
		return Fail(error,"Native collection descriptor is stale, overlapping or exceeds transaction bounds");
	auto candidate=std::make_unique<Impl>(*impl);
	candidate->collectionMode=true;candidate->barrier.collection=collection;candidate->barrier.collectionOpen=true;
	NativeTextEditorBarrier receipt=candidate->barrier;
	static_assert(std::is_nothrow_move_assignable_v<NativeTextEditorBarrier>);
	impl.swap(candidate);out=std::move(receipt);error.clear();return true;
}
bool NativeTextEditor::CompleteCollection(const NativeTextEditorBarrier& expected,const NativeTextCollection& collection,
	NativeTextEditorBarrier& out,std::string& error) {
	if(!impl->Exact(expected,error)) return false;
	if(!impl->collectionMode || !impl->barrier.collectionOpen || collection!=impl->barrier.collection ||
		impl->barrier.sequence!=collection.lastTransactionSequence)
		return Fail(error,"Native collection completion does not match its complete ordered offers");
	auto candidate=std::make_unique<Impl>(*impl);
	candidate->barrier.collectionOpen=false;
	candidate->awaiting=candidate->barrier.group && candidate->presentation.compositions.empty();
	NativeTextEditorBarrier receipt=candidate->barrier;
	impl.swap(candidate);out=std::move(receipt);error.clear();return true;
}
bool NativeTextEditor::Apply(const NativeTextOffer& offer,const TextEditorIdentity& expected,std::uint64_t revision,
	NativeTextEditorReceipt& out,std::string& error) {
	if(!impl->Owner(expected,error)) return false;
	const auto& tx=offer.transaction;
	if(impl->collectionMode && (!impl->barrier.collectionOpen || tx.nativeDispatch!=impl->barrier.collection.dispatch ||
		tx.sequence>impl->barrier.collection.lastTransactionSequence))
		return Fail(error,"Native transaction is outside its observed collection");
	if(impl->awaiting || offer.expectedEngineRevision!=expected.revision || tx.identity!=impl->barrier.native ||
		impl->barrier.sequence==UINT64_MAX || tx.sequence!=impl->barrier.sequence+1 || impl->barrier.shadowRevision==UINT64_MAX ||
		tx.shadowBefore!=impl->barrier.shadowRevision || tx.shadowAfter!=impl->barrier.shadowRevision+1)
		return Fail(error,"Native transaction is out of order or awaiting settlement");
	if(!ValidEnvelope(tx,error)) return false;
	auto candidate=std::make_unique<Impl>(*impl);
	NativeTextSnapshot presentation;
	if(!Replay(impl->presentation,candidate->compositionHigh,tx,impl->stable.Policy(),presentation,error)) return false;
	if(impl->collectionMode || tx.classification==NativeTextClassification::CompositionRelated) {
		if(!candidate->barrier.group) {
			if(candidate->groupHigh==UINT64_MAX) return Fail(error,"Native composition group identity exhausted");
			candidate->barrier.group=++candidate->groupHigh;
		}
		candidate->awaiting=!impl->collectionMode && presentation.compositions.empty();
	} else {
		if(!Adopt(candidate->stable,presentation,error)) return false;
		candidate->stablePresentation=Snapshot(candidate->stable.State());
	}
	const bool changed=presentation!=impl->presentation || candidate->barrier.group!=impl->barrier.group || candidate->awaiting!=impl->awaiting;
	if(!impl->Revision(revision,changed,error)) return false;
	candidate->presentation=std::move(presentation);candidate->barrier.sequence=tx.sequence;candidate->barrier.shadowRevision=tx.shadowAfter;candidate->barrier.editor.revision=revision;
	NativeTextEditorReceipt receipt{NativeTextEditorEffect::Acknowledge,impl->barrier,candidate->barrier,changed};
	static_assert(std::is_nothrow_move_assignable_v<NativeTextEditorReceipt>);
	impl.swap(candidate);out=std::move(receipt);error.clear();return true;
}
bool NativeTextEditor::SettleComposition(const NativeTextEditorBarrier& expected,std::uint64_t revision,
	NativeTextEditorReceipt& out,std::string& error) {
	if(!impl->Exact(expected,error)) return false;
	if(impl->barrier.collectionOpen || !impl->barrier.group || !impl->awaiting || !impl->presentation.compositions.empty()) return Fail(error,"Native composition group has not completed");
	if(!impl->Revision(revision,true,error)) return false;
	auto candidate=std::make_unique<Impl>(*impl);
	if(!Adopt(candidate->stable,candidate->presentation,error)) return false;
	candidate->stablePresentation=Snapshot(candidate->stable.State());
	candidate->barrier.group=0;candidate->awaiting=false;candidate->barrier.editor.revision=revision;
	// SyncEngine receives the exact already-acknowledged native text/selection.
	// Its production contract advances no shadow revision for identical bytes.
	NativeTextEditorReceipt receipt{NativeTextEditorEffect::SyncEngine,impl->barrier,candidate->barrier,true};
	impl.swap(candidate);out=std::move(receipt);error.clear();return true;
}
bool NativeTextEditor::CancelComposition(const NativeTextEditorBarrier& expected,std::uint64_t revision,
	NativeTextEditorReceipt& out,std::string& error) {
	if(!impl->Exact(expected,error)) return false;
	if(!impl->barrier.group) return Fail(error,"No native composition group to cancel");
	return Retire(expected,revision,out,error);
}
bool NativeTextEditor::Retire(const NativeTextEditorBarrier& expected,std::uint64_t revision,
	NativeTextEditorReceipt& out,std::string& error) {
	if(!impl->Exact(expected,error) || !impl->Revision(revision,true,error)) return false;
	auto candidate=std::make_unique<Impl>(*impl);
	candidate->presentation=Snapshot(candidate->stable.State());candidate->barrier.group=0;candidate->awaiting=false;
	candidate->barrier.collectionOpen=false;
	candidate->barrier.editor.revision=revision;candidate->active=false;
	NativeTextEditorReceipt receipt{NativeTextEditorEffect::Retire,impl->barrier,candidate->barrier,true};
	impl.swap(candidate);out=std::move(receipt);error.clear();return true;
}
bool NativeTextEditor::NumberCandidate(const TextEditorIdentity& expected,const TextNumberPolicy& policy,double& out,std::string& error) const {
	if(!impl->Owner(expected,error)) return false;
	if(impl->barrier.collectionOpen || impl->barrier.group || impl->awaiting || !impl->presentation.compositions.empty()) return Fail(error,"Native composition must settle before numeric commit preparation");
	double value=0;
	if(ParseTextNumber(impl->stable.State().text,policy,value)!=TextNumberStatus::Valid) return Fail(error,"Native numeric draft is incomplete, invalid or out of range");
	out=value;error.clear();return true;
}
bool NativeTextEditor::Abandon(const NativeTextEditorBarrier& expected) noexcept {
	if(!impl->active || expected!=impl->barrier) return false;
	// Stable snapshot is prepared before every normal publication. Swapping its
	// standard-allocator containers cannot allocate, even at revision exhaustion.
	static_assert(noexcept(impl->presentation.text.swap(impl->stablePresentation.text)));
	static_assert(noexcept(impl->presentation.compositions.swap(impl->stablePresentation.compositions)));
	impl->presentation.text.swap(impl->stablePresentation.text);
	impl->presentation.compositions.swap(impl->stablePresentation.compositions);
	using std::swap;swap(impl->presentation.anchor,impl->stablePresentation.anchor);swap(impl->presentation.caret,impl->stablePresentation.caret);
	impl->active=false;impl->awaiting=false;impl->barrier.group=0;impl->barrier.collectionOpen=false;return true;
}
} // namespace openq4::ui
