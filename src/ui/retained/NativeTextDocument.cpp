// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "NativeTextDocument.h"
#include "TextInput.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <set>
#include <utility>

namespace openq4::ui {
namespace {
bool Fail(std::string& error, const char* text) { error = text; return false; }
bool FindAcp(const std::vector<NativeTextBoundary>& map, std::uint32_t acp, std::size_t& out) {
	const auto found = std::lower_bound(map.begin(),map.end(),acp,[](const auto& b,auto value){return b.acp<value;});
	if (found==map.end() || found->acp!=acp) return false;
	out=found->byte; return true;
}
bool FindByte(const std::vector<NativeTextBoundary>& map, std::size_t byte, std::uint32_t& out) {
	const auto found = std::lower_bound(map.begin(),map.end(),byte,[](const auto& b,auto value){return b.byte<value;});
	if (found==map.end() || found->byte!=byte) return false;
	out=found->acp; return true;
}
bool Snapshot(std::string_view text, std::size_t anchor, std::size_t caret, std::size_t limit,
	NativeTextSnapshot& out,std::string& error) {
	std::vector<NativeTextBoundary> map; std::uint32_t ignored;
	if (text.size()>limit) return Fail(error,"Native document exceeds its byte limit");
	if (!BuildNativeTextMap(text,map,error)) return false;
	if (!FindByte(map,anchor,ignored) || !FindByte(map,caret,ignored))
		return Fail(error,"Native document selection is not on scalar boundaries");
	NativeTextSnapshot candidate; candidate.text=text; candidate.anchor=anchor; candidate.caret=caret;
	out=std::move(candidate); return true;
}
std::size_t Adjust(std::size_t position,std::size_t first,std::size_t last,std::size_t inserted,bool following) {
	if (position<first) return position;
	if (position>last) return position-last+first+inserted;
	if (position==last && first!=last) return first+inserted;
	return first+(following ? inserted : 0);
}
bool DocumentChanged(const NativeTextSnapshot& a,const NativeTextSnapshot& b) {
	return a.text!=b.text || a.anchor!=b.anchor || a.caret!=b.caret;
}
bool Charge(std::size_t& total,std::size_t value,std::size_t limit) {
	if (total>limit || value>limit-total) return false;
	total+=value; return true;
}
bool Cost(const NativeTextTransaction& transaction,std::size_t limit,std::size_t& out) {
	std::size_t total=0;
	if (!Charge(total,sizeof(NativeTextTransaction),limit) ||
		!Charge(total,transaction.after.text.size()+1,limit) ||
		!Charge(total,transaction.after.compositions.size()*sizeof(std::pair<const std::uint64_t,NativeTextRange>),limit)) return false;
	for (const auto& operation:transaction.operations)
		if (!Charge(total,sizeof(NativeTextOperation),limit) || !Charge(total,operation.text.size()+1,limit)) return false;
	out=total; return true;
}
} // namespace

bool BuildNativeTextMap(std::string_view text,std::vector<NativeTextBoundary>& out,std::string& error) {
	if (!ValidateTextInputUtf8(text,error)) return false;
	std::vector<NativeTextBoundary> candidate; candidate.reserve(text.size()+1); candidate.push_back({0,0});
	std::size_t byte=0; std::uint32_t acp=0;
	while (byte<text.size()) {
		// Complete strict validation above makes scalar width unambiguous.
		const auto lead=static_cast<unsigned char>(text[byte]);
		const std::size_t width=lead<0x80 ? 1 : lead<0xe0 ? 2 : lead<0xf0 ? 3 : 4;
		byte+=width; acp+=width==4 ? 2 : 1; candidate.push_back({byte,acp});
	}
	out=std::move(candidate); error.clear(); return true;
}

struct NativeTextDocument::Impl {
	struct Frame {
		NativeTextLockScope scope;
		NativeTextSnapshot working;
		std::vector<NativeTextOperation> operations;
		std::uint64_t compositionHigh=0;
		bool poisoned=false, related=false;
	};
	NativeTextLimits limits;
	NativeTextIdentity identity;
	NativeTextSnapshot state;
	std::optional<Frame> frame;
	std::deque<NativeTextTransaction> pending;
	std::uint64_t engineRevision=0,shadowRevision=0,transactionSequence=0,lockSequence=0,compositionHigh=0;
	std::size_t pendingBytes=0;
	bool opened=false,retired=false,deferredWrite=false;

	bool Owner(NativeTextIdentity expected,std::string& error) const {
		if (!opened || retired || expected!=identity) return Fail(error,"Native document identity is unavailable");
		return true;
	}
	bool Scope(const NativeTextLockScope& expected,bool write,std::string& error,bool allowPoison=false) const {
		if (!Owner(expected.identity,error)) return false;
		if (!frame || !expected.serial || expected!=frame->scope) return Fail(error,"Native lock callback scope is stale");
		if (write && expected.access!=NativeTextAccess::ReadWrite) return Fail(error,"Native operation requires a write lock");
		if (frame->poisoned && !allowPoison) return Fail(error,"Native write candidate was refused");
		return true;
	}
	bool Poison(std::string& error,const char* message) {
		frame->poisoned=true; return Fail(error,message);
	}
	bool Grant(NativeTextAccess access,NativeTextLockScope& out,std::string& error) {
		if (lockSequence>=limits.sequence) return Fail(error,"Native lock identity budget exhausted");
		Frame candidate; candidate.scope={identity,lockSequence+1,access}; candidate.working=state;
		candidate.compositionHigh=compositionHigh; candidate.related=!state.compositions.empty();
		frame=std::move(candidate); ++lockSequence; out=frame->scope; error.clear(); return true;
	}
	bool Retained(const Frame& candidate) const {
		std::set<std::uint64_t> ids;
		const auto add=[&](const auto& snapshot,const auto& operations) {
			for (const auto& [token,range]:snapshot.compositions) { (void)range; ids.insert(token); }
			for (const auto& operation:operations) if (operation.composition) ids.insert(operation.composition);
		};
		add(candidate.working,candidate.operations);
		for (const auto& transaction:pending) add(transaction.after,transaction.operations);
		return ids.size()<=limits.retainedCompositions;
	}
	bool Compose(Frame& candidate,const NativeTextOperation& operation,std::size_t first,std::size_t last,std::string& error) {
		auto& working=candidate.working;
		switch(operation.kind) {
		case NativeTextOperationKind::BeginComposition:
			if (!operation.composition || operation.composition<=candidate.compositionHigh)
				return Fail(error,"Native composition identity was reused or unordered");
			candidate.compositionHigh=operation.composition;
			working.compositions.emplace(operation.composition,NativeTextRange{first,last}); break;
		case NativeTextOperationKind::UpdateComposition: {
			const auto found=working.compositions.find(operation.composition);
			if (found==working.compositions.end()) return Fail(error,"Native composition update has no live origin");
			found->second={first,last}; break;
		}
		case NativeTextOperationKind::EndComposition:
			if (!working.compositions.erase(operation.composition)) return Fail(error,"Native composition end has no live origin");
			break;
		default: return Fail(error,"Unknown native composition operation");
		}
		candidate.related=true;return true;
	}
	bool Mutate(const NativeTextLockScope& scope,NativeTextOperation operation,std::string& error) {
		if (!Scope(scope,true,error)) return false;
		if (frame->operations.size()>=limits.operations) return Poison(error,"Native transaction operation budget exhausted");
		Frame candidate=*frame;
		std::vector<NativeTextBoundary> map; std::size_t first=0,last=0;
		if (!BuildNativeTextMap(candidate.working.text,map,error)) { frame->poisoned=true; return false; }
		if (operation.kind!=NativeTextOperationKind::EndComposition &&
			(!FindAcp(map,operation.first,first) || !FindAcp(map,operation.last,last)))
			return Poison(error,"Native ACP range splits a scalar or exceeds the document");
		if (operation.kind!=NativeTextOperationKind::Select && first>last)
			return Poison(error,"Native range is reversed");
		auto& working=candidate.working;
		switch (operation.kind) {
		case NativeTextOperationKind::Replace: {
			if (!ValidateTextInputUtf8(operation.text,error)) { frame->poisoned=true; return false; }
			const auto retained=working.text.size()-(last-first);
			if (retained>limits.documentBytes || operation.text.size()>limits.documentBytes-retained)
				return Poison(error,"Native replacement exceeds the document byte limit");
			working.text.replace(first,last-first,operation.text);
			working.anchor=Adjust(working.anchor,first,last,operation.text.size(),true);
			working.caret=Adjust(working.caret,first,last,operation.text.size(),true);
			for (auto& [token,range]:working.compositions) {
				(void)token; range.first=Adjust(range.first,first,last,operation.text.size(),false);
				range.last=Adjust(range.last,first,last,operation.text.size(),true);
			}
			break;
		}
		case NativeTextOperationKind::Select: working.anchor=first; working.caret=last; break;
		case NativeTextOperationKind::BeginComposition:
		case NativeTextOperationKind::UpdateComposition:
		case NativeTextOperationKind::EndComposition:
			if (!Compose(candidate,operation,first,last,error)) {frame->poisoned=true;return false;}
			break;
		default: return Poison(error,"Unknown native text operation");
		}
		candidate.operations.push_back(std::move(operation));
		if (!Retained(candidate)) return Poison(error,"Native retained composition budget exhausted");
		NativeTextTransaction sized; sized.after=candidate.working; sized.operations=candidate.operations; std::size_t cost;
		if (!Cost(sized,limits.pendingBytes,cost)) return Poison(error,"Native transaction payload budget exhausted");
		frame=std::move(candidate); error.clear(); return true;
	}
};

NativeTextDocument::NativeTextDocument(NativeTextLimits limits):impl(std::make_unique<Impl>()) { impl->limits=limits; }
NativeTextDocument::~NativeTextDocument()=default;
bool NativeTextDocument::Open(NativeTextIdentity identity,std::uint64_t revision,std::string_view text,
	std::size_t anchor,std::size_t caret,std::string& error) {
	if (impl->opened) return Fail(error,"Native document cannot be reopened");
	const auto& limits=impl->limits;
	if (!identity.document || !identity.editorLease || !revision || !limits.documentBytes || limits.documentBytes>TextInputMaxBytes ||
		!limits.pendingTransactions || limits.pendingTransactions>32 || !limits.pendingBytes || limits.pendingBytes>1024*1024 ||
		!limits.operations || limits.operations>256 || !limits.retainedCompositions || limits.retainedCompositions>32 || !limits.sequence)
		return Fail(error,"Invalid native document identity or limits");
	NativeTextSnapshot candidate;
	if (!Snapshot(text,anchor,caret,limits.documentBytes,candidate,error)) return false;
	impl->state=std::move(candidate); impl->identity=identity; impl->engineRevision=revision; impl->shadowRevision=1; impl->opened=true;
	error.clear(); return true;
}
NativeTextIdentity NativeTextDocument::Identity() const { return impl->identity; }
bool NativeTextDocument::Active() const { return impl->opened && !impl->retired; }
std::uint64_t NativeTextDocument::EngineRevision() const { return impl->engineRevision; }
std::uint64_t NativeTextDocument::ShadowRevision() const { return impl->shadowRevision; }
std::size_t NativeTextDocument::PendingCount() const { return impl->pending.size(); }
std::size_t NativeTextDocument::PendingBytes() const { return impl->pendingBytes; }

NativeTextLockResult NativeTextDocument::RequestLock(NativeTextIdentity identity,NativeTextAccess access,bool synchronous,
	NativeTextLockScope& out,std::string& error) {
	if (!impl->Owner(identity,error)) return NativeTextLockResult::Refused;
	if (access!=NativeTextAccess::Read && access!=NativeTextAccess::ReadWrite) {
		Fail(error,"Unknown native lock access"); return NativeTextLockResult::Refused;
	}
	if (impl->frame) {
		if (impl->frame->scope.access==NativeTextAccess::Read && access==NativeTextAccess::ReadWrite && !synchronous) {
			impl->deferredWrite=true; error.clear(); return NativeTextLockResult::Deferred;
		}
		Fail(error,"Native nested lock cannot be granted synchronously"); return NativeTextLockResult::Refused;
	}
	if (impl->deferredWrite) { Fail(error,"Native deferred write must be granted first"); return NativeTextLockResult::Refused; }
	return impl->Grant(access,out,error) ? NativeTextLockResult::Granted : NativeTextLockResult::Refused;
}
bool NativeTextDocument::GrantDeferredWrite(NativeTextIdentity identity,NativeTextLockScope& out,std::string& error) {
	if (!impl->Owner(identity,error)) return false;
	if (impl->frame || !impl->deferredWrite) return Fail(error,"Native deferred write is unavailable");
	if (!impl->Grant(NativeTextAccess::ReadWrite,out,error)) return false;
	impl->deferredWrite=false; return true;
}
bool NativeTextDocument::Read(const NativeTextLockScope& scope,NativeTextSnapshot& out,std::string& error) const {
	if (!impl->Scope(scope,false,error)) return false;
	NativeTextSnapshot candidate=impl->frame->working; out=std::move(candidate); error.clear(); return true;
}
bool NativeTextDocument::AcpToByte(const NativeTextLockScope& scope,std::uint32_t acp,std::size_t& out,std::string& error) const {
	if (!impl->Scope(scope,false,error)) return false;
	std::vector<NativeTextBoundary> map; std::size_t candidate;
	if (!BuildNativeTextMap(impl->frame->working.text,map,error)) return false;
	if (!FindAcp(map,acp,candidate)) return Fail(error,"Native ACP is not a scalar boundary");
	out=candidate; error.clear(); return true;
}
bool NativeTextDocument::ByteToAcp(const NativeTextLockScope& scope,std::size_t byte,std::uint32_t& out,std::string& error) const {
	if (!impl->Scope(scope,false,error)) return false;
	std::vector<NativeTextBoundary> map; std::uint32_t candidate;
	if (!BuildNativeTextMap(impl->frame->working.text,map,error)) return false;
	if (!FindByte(map,byte,candidate)) return Fail(error,"Native byte index is not a scalar boundary");
	out=candidate; error.clear(); return true;
}
bool NativeTextDocument::ReplaceACP(const NativeTextLockScope& scope,std::uint32_t first,std::uint32_t last,
	std::string_view text,std::string& error) {
	if (!impl->Scope(scope,true,error)) return false;
	if (text.size()>impl->limits.documentBytes) return impl->Poison(error,"Native replacement exceeds the document byte limit");
	return impl->Mutate(scope,{NativeTextOperationKind::Replace,first,last,std::string(text),0},error);
}
bool NativeTextDocument::SelectACP(const NativeTextLockScope& scope,std::uint32_t anchor,std::uint32_t caret,std::string& error) {
	return impl->Mutate(scope,{NativeTextOperationKind::Select,anchor,caret,{},0},error);
}
bool NativeTextDocument::BeginComposition(const NativeTextLockScope& scope,std::uint64_t token,std::uint32_t first,
	std::uint32_t last,std::string& error) {
	return impl->Mutate(scope,{NativeTextOperationKind::BeginComposition,first,last,{},token},error);
}
bool NativeTextDocument::UpdateComposition(const NativeTextLockScope& scope,std::uint64_t token,std::uint32_t first,
	std::uint32_t last,std::string& error) {
	return impl->Mutate(scope,{NativeTextOperationKind::UpdateComposition,first,last,{},token},error);
}
bool NativeTextDocument::EndComposition(const NativeTextLockScope& scope,std::uint64_t token,std::string& error) {
	return impl->Mutate(scope,{NativeTextOperationKind::EndComposition,0,0,{},token},error);
}
bool NativeTextDocument::CaptureCompositionObservation(NativeTextIdentity identity,std::uint64_t dispatch,
	NativeTextMetadataObservation& out,std::string& error) const {
	if (!impl->Owner(identity,error)) return false;
	if (impl->frame || impl->deferredWrite || !dispatch || impl->pending.size()>32 || impl->pending.size()>impl->transactionSequence)
		return Fail(error,"Native composition observation requires an idle document and observed dispatch");
	const NativeTextMetadataObservation candidate{identity,impl->engineRevision,impl->shadowRevision,impl->transactionSequence,
		impl->transactionSequence-impl->pending.size(),dispatch};
	out=candidate;error.clear();return true;
}
bool NativeTextDocument::PublishCompositionObservation(const NativeTextMetadataObservation& observed,
	const NativeTextOperation& operation,std::uint64_t& published,std::string& error) {
	if (!impl->Owner(observed.identity,error)) return false;
	if (impl->frame || impl->deferredWrite || !observed.dispatch || observed.engineRevision!=impl->engineRevision ||
		observed.shadowRevision!=impl->shadowRevision || observed.transactionSequence!=impl->transactionSequence ||
		impl->pending.size()>32 || impl->pending.size()>impl->transactionSequence ||
		observed.acknowledgedSequence!=impl->transactionSequence-impl->pending.size())
		return Fail(error,"Native composition callback observation is stale or blocked");
	if ((operation.kind!=NativeTextOperationKind::BeginComposition && operation.kind!=NativeTextOperationKind::UpdateComposition &&
		operation.kind!=NativeTextOperationKind::EndComposition) || !operation.composition || !operation.text.empty() ||
		(operation.kind==NativeTextOperationKind::EndComposition && (operation.first || operation.last)))
		return Fail(error,"Native composition callback must contain only exact metadata");
	Impl::Frame candidate;candidate.working=impl->state;candidate.compositionHigh=impl->compositionHigh;
	std::vector<NativeTextBoundary> map;std::size_t first=0,last=0;
	if (!BuildNativeTextMap(candidate.working.text,map,error)) return false;
	if (operation.kind!=NativeTextOperationKind::EndComposition &&
		(!FindAcp(map,operation.first,first) || !FindAcp(map,operation.last,last) || first>last))
		return Fail(error,"Native composition ACP range splits a scalar or exceeds the document");
	if (!impl->Compose(candidate,operation,first,last,error)) return false;
	candidate.operations.push_back(operation);
	if (!impl->Retained(candidate)) return Fail(error,"Native retained composition budget exhausted");
	if (impl->pending.size()>=impl->limits.pendingTransactions || impl->shadowRevision>=impl->limits.sequence ||
		impl->transactionSequence>=impl->limits.sequence || impl->pendingBytes>impl->limits.pendingBytes)
		return Fail(error,"Native composition pending transaction or revision budget exhausted");
	NativeTextTransaction transaction;
	transaction.identity=observed.identity;transaction.sequence=impl->transactionSequence+1;transaction.nativeDispatch=observed.dispatch;
	transaction.shadowBefore=impl->shadowRevision;transaction.shadowAfter=impl->shadowRevision+1;
	transaction.classification=NativeTextClassification::CompositionRelated;transaction.documentChanged=false;
	transaction.operations=std::move(candidate.operations);transaction.after=candidate.working;
	std::size_t cost;
	if (!Cost(transaction,impl->limits.pendingBytes-impl->pendingBytes,cost)) return Fail(error,"Native composition pending payload budget exhausted");
	// Every allocation precedes publication. Metadata changes neither text nor
	// directional selection; the existing FIFO acknowledgement owns editor revision.
	impl->pending.push_back(std::move(transaction));
	impl->state=std::move(candidate.working);impl->compositionHigh=candidate.compositionHigh;
	++impl->shadowRevision;++impl->transactionSequence;impl->pendingBytes+=cost;
	published=impl->transactionSequence;error.clear();return true;
}

bool NativeTextDocument::FinishLock(const NativeTextLockScope& scope,std::uint64_t dispatch,std::uint64_t& published,std::string& error) {
	if (!impl->Scope(scope,false,error,true)) return false;
	if (impl->frame->poisoned) { impl->frame.reset(); return Fail(error,"Native write transaction was refused atomically"); }
	if (scope.access==NativeTextAccess::Read) { impl->frame.reset(); published=0; error.clear(); return true; }
	const auto& frame=*impl->frame;
	const bool changed=DocumentChanged(impl->state,frame.working);
	const bool metadata=std::any_of(frame.operations.begin(),frame.operations.end(),[](const auto& op){return op.composition!=0;});
	if (!changed && !metadata) { impl->frame.reset(); published=0; error.clear(); return true; }
	if (impl->pending.size()>=impl->limits.pendingTransactions || impl->shadowRevision>=impl->limits.sequence ||
		impl->transactionSequence>=impl->limits.sequence) {
		impl->frame.reset(); return Fail(error,"Native pending transaction or revision budget exhausted");
	}
	NativeTextTransaction transaction;
	transaction.identity=impl->identity; transaction.sequence=impl->transactionSequence+1; transaction.nativeDispatch=dispatch;
	transaction.shadowBefore=impl->shadowRevision; transaction.shadowAfter=impl->shadowRevision+1;
	transaction.classification=frame.related ? NativeTextClassification::CompositionRelated : NativeTextClassification::Unclassified;
	transaction.documentChanged=changed; transaction.operations=frame.operations; transaction.after=frame.working;
	std::size_t cost;
	if (!Cost(transaction,impl->limits.pendingBytes-impl->pendingBytes,cost)) {
		impl->frame.reset(); return Fail(error,"Native pending payload budget exhausted");
	}
	// All fallible construction precedes publishing; the remaining moves are of
	// standard-allocator containers and do not allocate.
	impl->pending.push_back(std::move(transaction));
	impl->state=std::move(impl->frame->working); impl->compositionHigh=impl->frame->compositionHigh;
	++impl->shadowRevision; ++impl->transactionSequence; impl->pendingBytes+=cost;
	impl->frame.reset(); published=impl->transactionSequence; error.clear(); return true;
}
bool NativeTextDocument::AbortLock(const NativeTextLockScope& scope,std::string& error) {
	if (!impl->Scope(scope,false,error,true)) return false;
	impl->frame.reset(); impl->deferredWrite=false; error.clear(); return true;
}
bool NativeTextDocument::PeekOffer(NativeTextIdentity identity,NativeTextOffer& out,std::string& error) const {
	if (!impl->Owner(identity,error)) return false;
	if (impl->frame || impl->pending.empty()) return Fail(error,"Native offer is unavailable during a lock or without pending work");
	NativeTextOffer candidate{impl->pending.front(),impl->engineRevision}; out=std::move(candidate); error.clear(); return true;
}
bool NativeTextDocument::QueryPendingCollection(NativeTextIdentity identity,std::uint64_t expectedEngine,
	std::uint64_t expectedAcknowledged,std::uint64_t expectedAcknowledgedShadow,std::uint64_t dispatch,
	NativeTextPendingSnapshot& out,std::string& error) const {
	if (!impl->Owner(identity,error)) return false;
	const auto count=impl->pending.size();
	// Check arithmetic bounds before subtraction or narrowing, even though the
	// normal producer also bounds its private queue at publication.
	if (impl->frame || impl->deferredWrite || !dispatch || count>32 || count>impl->transactionSequence ||
		expectedEngine!=impl->engineRevision)
		return Fail(error,"Native pending collection is locked, stale or malformed");
	const auto acknowledged=impl->transactionSequence-count;
	const auto acknowledgedShadow=count?impl->pending.front().shadowBefore:impl->shadowRevision;
	if (expectedAcknowledged!=acknowledged || expectedAcknowledgedShadow!=acknowledgedShadow)
		return Fail(error,"Native pending collection does not match the acknowledged editor barrier");
	auto sequence=acknowledged,shadow=acknowledgedShadow;
	for (const auto& transaction:impl->pending) {
		if (sequence==UINT64_MAX || shadow==UINT64_MAX || transaction.identity!=identity ||
			transaction.sequence!=sequence+1 || transaction.nativeDispatch!=dispatch || transaction.shadowBefore!=shadow ||
			transaction.shadowAfter!=shadow+1)
			return Fail(error,"Native pending collection has mixed dispatch or discontinuous transactions");
		sequence=transaction.sequence;shadow=transaction.shadowAfter;
	}
	if (sequence!=impl->transactionSequence || shadow!=impl->shadowRevision)
		return Fail(error,"Native pending collection does not reach the published watermark");
	const NativeTextPendingSnapshot candidate{identity,impl->engineRevision,acknowledged,acknowledgedShadow,
		impl->shadowRevision,dispatch,impl->transactionSequence,static_cast<std::uint32_t>(count)};
	out=candidate;error.clear();return true;
}
bool NativeTextDocument::Acknowledge(NativeTextIdentity identity,std::uint64_t transaction,std::uint64_t shadowAfter,
	std::uint64_t expected,std::uint64_t accepted,std::string& error) {
	if (!impl->Owner(identity,error)) return false;
	if (impl->frame || impl->pending.empty() || impl->pending.front().sequence!=transaction ||
		impl->pending.front().shadowAfter!=shadowAfter || expected!=impl->engineRevision || !accepted || accepted<expected ||
		(impl->pending.front().documentChanged && accepted==expected))
		return Fail(error,"Native acknowledgement does not match the current front and editor revision");
	std::size_t cost=0; (void)Cost(impl->pending.front(),impl->limits.pendingBytes,cost);
	impl->pending.pop_front(); impl->pendingBytes-=cost; impl->engineRevision=accepted; error.clear(); return true;
}
bool NativeTextDocument::Reject(NativeTextIdentity identity,std::uint64_t transaction,std::uint64_t expected,std::string& error) {
	if (!impl->Owner(identity,error)) return false;
	if (impl->frame || impl->pending.empty() || impl->pending.front().sequence!=transaction || expected!=impl->engineRevision)
		return Fail(error,"Native rejection does not name the current front");
	return Retire(identity,error);
}
bool NativeTextDocument::SyncEngine(NativeTextIdentity identity,std::uint64_t expectedEngine,std::uint64_t expectedShadow,
	std::uint64_t revision,std::string_view text,std::size_t anchor,std::size_t caret,std::string& error) {
	if (!impl->Owner(identity,error)) return false;
	if (impl->frame || impl->deferredWrite || !impl->pending.empty() || !impl->state.compositions.empty() ||
		expectedEngine!=impl->engineRevision || expectedShadow!=impl->shadowRevision || !revision || revision<expectedEngine)
		return Fail(error,"Native engine synchronization conflicts with pending ownership or revisions");
	NativeTextSnapshot candidate;
	if (!Snapshot(text,anchor,caret,impl->limits.documentBytes,candidate,error)) return false;
	const bool changed=DocumentChanged(impl->state,candidate);
	if (changed && (revision==expectedEngine || impl->shadowRevision>=impl->limits.sequence))
		return Fail(error,"Native changed engine snapshot needs fresh editor and shadow revisions");
	impl->state=std::move(candidate); impl->engineRevision=revision; if (changed) ++impl->shadowRevision;
	error.clear(); return true;
}
bool NativeTextDocument::Retire(NativeTextIdentity identity,std::string& error) {
	if (!Retire(identity)) return Fail(error,"Native retirement identity does not match");
	error.clear(); return true;
}
bool NativeTextDocument::Retire(NativeTextIdentity identity) noexcept {
	if (!impl->opened || identity!=impl->identity) return false;
	impl->retired=true; impl->frame.reset(); impl->deferredWrite=false; impl->pending.clear(); impl->pendingBytes=0;
	return true;
}
} // namespace openq4::ui
