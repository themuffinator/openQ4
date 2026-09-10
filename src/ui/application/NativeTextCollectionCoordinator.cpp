// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "NativeTextCollectionCoordinator.h"
#include <atomic>
#include <limits>

namespace openq4::ui {
namespace {
std::atomic<std::uint64_t> nextCoordinator{1};
std::uint64_t Identity() {
	auto value = nextCoordinator.load();
	while (value && value != UINT64_MAX) if (nextCoordinator.compare_exchange_weak(value,value+1)) return value;
	return 0;
}
struct Calling { bool& flag; explicit Calling(bool& value):flag(value) { flag=true; } ~Calling() { flag=false; } };
bool SameProvider(const NativeQueueStatus& a,const NativeQueueStatus& b) {
	return a.mainThread && a.healthy && a.providerEpoch && a.generation && a.engineToken &&
		a.providerEpoch==b.providerEpoch && a.generation==b.generation && a.engineToken==b.engineToken && a.fenceEventType==b.fenceEventType;
}
bool SameOwner(const TextEditorIdentity& a,const TextEditorIdentity& b) {
	return a.allocation==b.allocation && a.backend==b.backend && a.document==b.document && a.modal==b.modal &&
		a.window==b.window && a.session==b.session && a.control==b.control;
}
bool SameReceipt(const NativeTextEditorReceipt& a,const NativeTextEditorReceipt& b) {
	return a.effect==b.effect && a.before==b.before && a.after==b.after && a.presentationChanged==b.presentationChanged;
}
bool GoodTransition(const NativeTextEditorBarrier& before,const NativeTextEditorBarrier& after) {
	return before.native==after.native && SameOwner(before.editor,after.editor) && after.editor.revision>=before.editor.revision;
}
}
NativeTextCollectionCoordinator::NativeTextCollectionCoordinator(const NativeTextEditorBarrier& attached)
	:identity(Identity()),current(attached) {}
bool NativeTextCollectionCoordinator::QueryBarrier(NativeTextEditorBarrier& out,std::string& error) const noexcept {
	const auto reject=[&](const char* message) {
		try { error=message; } catch (...) { error.clear(); }
		return false;
	};
	// Check the thread before reading mutable coordinator state.
	if (std::this_thread::get_id()!=thread) return reject("Native barrier query requires its engine thread");
	if (calling || retired || (phase!=Phase::Ready && phase!=Phase::AwaitingFence) ||
		!identity || current.collectionOpen || !current.native.document ||
		!current.native.editorLease || !current.editor.revision)
		return reject("Native barrier query requires an idle attached coordinator");
	try {
		auto snapshot=current;
		static_assert(std::is_nothrow_move_assignable_v<NativeTextEditorBarrier>);
		out=std::move(snapshot);error.clear();return true;
	} catch (...) { return reject("Native barrier snapshot allocation failed"); }
}
bool NativeTextCollectionCoordinator::Fail(std::string& error,const char* message) noexcept {
	phase=Phase::RetireRequired;
	try {error=message;} catch (...) {error.clear();}
	return false;
}
void NativeTextCollectionCoordinator::Retire(NativeTextCollectionOwner& owner,NativeTextCollectionStore& store) noexcept {
	if (retired) return;
	retired=true;phase=Phase::RetireRequired;
	// End native admission first. Reentry can only observe the latched failure.
	store.RetireExact(current.native);owner.RetireExact(current.native,current.editor);
}
bool NativeTextCollectionCoordinator::Check(NativeQueueIngress& ingress,NativeQueueSource& source,
	NativeTextCollectionOwner& owner,NativeTextCollectionStore& store,std::string& error) {
	if (phase==Phase::RetireRequired || !ingress.Validate(source,completion.batch,error) || phase==Phase::RetireRequired)
		return Fail(error,"Native collection queue proof changed");
	NativeTextEditorView observed;
	if (!owner.Refresh(current,observed,error) || phase==Phase::RetireRequired || !observed.active || observed.barrier!=current)
		return Fail(error,"Native collection owner changed");
	// Refresh can call host code. Validate queue again, followed only by cheap
	// no-callback owner/scope predicates before the next effect.
	if (!ingress.Validate(source,completion.batch,error) || phase==Phase::RetireRequired ||
		!owner.Current(current) || !store.StillClosed(closed)) return Fail(error,"Native collection authority changed during observation");
	return true;
}
bool NativeTextCollectionCoordinator::CheckPending(NativeTextCollectionStore& store,std::uint32_t remaining,std::string& error) {
	NativeTextPendingSnapshot pending;
	if (!store.Pending(current,closed.dispatch,pending,error) || phase==Phase::RetireRequired ||
		pending.identity!=current.native || pending.engineRevision!=current.editor.revision ||
		pending.acknowledgedSequence!=current.sequence || pending.acknowledgedShadowRevision!=current.shadowRevision ||
		pending.dispatch!=closed.dispatch || pending.lastSequence!=closed.pending.lastSequence ||
		pending.shadowRevision!=closed.pending.shadowRevision || pending.count!=remaining)
		return Fail(error,"Native collection pending watermark changed");
	return true;
}
bool NativeTextCollectionCoordinator::Reconcile(NativeQueueIngress& ingress,NativeQueueSource& source,
	const NativeQueueBatch& batch,NativeTextCollectionOwner& owner,NativeTextCollectionStore& store,
	NativeTextCollectionResult& out,std::string& error) {
	return ReconcileKind({},ingress,source,batch,owner,store,out,error);
}
bool NativeTextCollectionCoordinator::ReconcileLifecycle(const NativeClosedTextCollection& completed,
	NativeQueueIngress& ingress,NativeQueueSource& source,const NativeQueueBatch& batch,
	NativeTextCollectionOwner& owner,NativeTextCollectionStore& store,NativeTextCollectionResult& out,std::string& error) {
	// The receipt contains only bounded value fields. Freeze it before any foreign
	// observation; callbacks cannot mutate the requested lifecycle identity.
	return ReconcileKind(completed,ingress,source,batch,owner,store,out,error);
}
bool NativeTextCollectionCoordinator::ReconcileKind(std::optional<NativeClosedTextCollection> requested,
	NativeQueueIngress& ingress,NativeQueueSource& source,const NativeQueueBatch& batch,
	NativeTextCollectionOwner& owner,NativeTextCollectionStore& store,NativeTextCollectionResult& out,std::string& error) {
	if (std::this_thread::get_id()!=thread) { error="Native collection requires its engine thread";return false; }
	if (calling) return Fail(error,"Reentrant native collection reconciliation");
	Calling guard(calling);
	const auto reject=[&](const char* message) { Fail(error,message);Retire(owner,store);return false; };
	try {
		if ((requested && requested->kind!=NativeClosedCollectionKind::Lifecycle) ||
			phase!=Phase::Ready || !identity || serial==UINT64_MAX || !batch.Status().pending || batch.Events().empty() ||
			batch.Events().back().record.kind!=OQ4_QUEUE_FENCE || current.collectionOpen ||
			!current.native.document || !current.native.editorLease || !current.editor.revision)
			return reject("Native reconciliation requires a fresh owned fence and attached editor");
		phase=Phase::Reconciling;
		if (queue && !SameProvider(batch.Status(),*queue)) return reject("Native provider changed within an editor lease");
		queue=batch.Status();completion={identity,++serial,batch.Receipt()};
		if (!ingress.Validate(source,completion.batch,error) || phase==Phase::RetireRequired || !owner.Current(current))
			return reject("Native collection initial queue or owner proof failed");
		NativeClosedTextCollection seal;
		if (!store.Closed(current,queue->pending->dispatch,seal,error) || phase==Phase::RetireRequired ||
			seal.identity!=current.native || !seal.serial || seal.serial<=lastScope ||
			(requested ? seal!=*requested : seal.kind!=NativeClosedCollectionKind::Pump) ||
			seal.dispatch!=queue->pending->dispatch || seal.pending.identity!=current.native ||
			seal.pending.dispatch!=seal.dispatch || seal.pending.engineRevision!=current.editor.revision ||
			seal.pending.acknowledgedSequence!=current.sequence || seal.pending.acknowledgedShadowRevision!=current.shadowRevision ||
			seal.pending.count>32 || seal.pending.lastSequence<current.sequence || seal.pending.shadowRevision<current.shadowRevision ||
			seal.pending.lastSequence-current.sequence!=seal.pending.count || seal.pending.shadowRevision-current.shadowRevision!=seal.pending.count ||
			(seal.pending.count && !seal.admittedCallbacks)) return reject("Native collection has no exact closed store receipt");
		closed=seal;
		if (!Check(ingress,source,owner,store,error)) return reject("Native authority changed before collection begin");
		const NativeTextCollection collection{seal.dispatch,queue->pending->sequence,seal.pending.lastSequence};
		NativeTextEditorBarrier begun;
		if (!owner.Begin(current,collection,begun,error) || phase==Phase::RetireRequired || !GoodTransition(current,begun) ||
			begun.editor.revision!=current.editor.revision || begun.sequence!=current.sequence || begun.shadowRevision!=current.shadowRevision ||
			begun.group!=current.group || !begun.collectionOpen || begun.collection!=collection)
			return reject("Native collection begin was refused");
		current=std::move(begun);
		std::size_t bytes=0;
		for (std::uint32_t index=0;index<seal.pending.count;++index) {
			if (!CheckPending(store,seal.pending.count-index,error) || !Check(ingress,source,owner,store,error))
				return reject("Native collection changed before its next offer");
			NativeTextOffer offer;
			if (!store.Peek(current.native,offer,error) || phase==Phase::RetireRequired)
				return reject("Native collection offer is unavailable");
			const auto& tx=offer.transaction;
			if (tx.identity!=current.native || tx.nativeDispatch!=seal.dispatch || current.sequence==UINT64_MAX || tx.sequence!=current.sequence+1 ||
				current.shadowRevision==UINT64_MAX || tx.shadowBefore!=current.shadowRevision || tx.shadowAfter!=current.shadowRevision+1 ||
				offer.expectedEngineRevision!=current.editor.revision || tx.operations.size()>256 || tx.after.compositions.size()>32)
				return reject("Native collection offer does not match its ordered barrier");
			const auto charge=[&](std::size_t amount) { if (amount>1024*1024-bytes) return false;bytes+=amount;return true; };
			if (!charge(sizeof(tx)) || !charge(tx.after.text.size()) || !charge(tx.after.compositions.size()*sizeof(NativeTextRange)))
				return reject("Native collection payload exceeds its budget");
			for (const auto& operation:tx.operations) if (!charge(sizeof(operation)) || !charge(operation.text.size()))
				return reject("Native collection operations exceed their budget");
			if (!Check(ingress,source,owner,store,error)) return reject("Native authority changed before offer publication");
			NativeTextEditorReceipt receipt;
			if (!owner.Apply(current,offer,receipt,error) || phase==Phase::RetireRequired || receipt.effect!=NativeTextEditorEffect::Acknowledge ||
				receipt.before!=current || !GoodTransition(current,receipt.after) || receipt.after.sequence!=tx.sequence ||
				receipt.after.shadowRevision!=tx.shadowAfter || receipt.after.collection!=collection || !receipt.after.collectionOpen)
				return reject("Native collection offer publication failed");
			current=receipt.after;
			if (!Check(ingress,source,owner,store,error)) return reject("Native authority changed before offer acknowledgement");
			if (!store.Acknowledge(receipt,error) || phase==Phase::RetireRequired ||
				!CheckPending(store,seal.pending.count-index-1,error) || !Check(ingress,source,owner,store,error))
				return reject("Native collection acknowledgement failed");
		}
		if (!CheckPending(store,0,error) || !Check(ingress,source,owner,store,error)) return reject("Native collection is not completely drained");
		NativeTextEditorBarrier completed;
		if (!owner.Complete(current,collection,completed,error) || phase==Phase::RetireRequired || !GoodTransition(current,completed) ||
			completed.editor.revision!=current.editor.revision || completed.sequence!=current.sequence || completed.shadowRevision!=current.shadowRevision ||
			completed.group!=current.group || completed.collectionOpen || completed.collection!=collection)
			return reject("Native collection completion failed");
		current=std::move(completed);
		if (!Check(ingress,source,owner,store,error)) return reject("Native owner changed before settlement decision");
		NativeTextEditorView view;
		if (!owner.Refresh(current,view,error) || phase==Phase::RetireRequired || view.barrier!=current || !view.active)
			return reject("Native settlement view is stale");
		NativeTextCollectionResult result{completion,seal.pending.count,false,!view.presentation.compositions.empty()};
		if (view.awaitingSettlement) {
			if (!Check(ingress,source,owner,store,error)) return reject("Native authority changed before settlement preparation");
			auto prepared=owner.PrepareSettlement(current,error);
			if (!prepared || phase==Phase::RetireRequired) return reject("Native settlement preparation failed");
			const auto& receipt=prepared->Receipt();
			if (receipt.effect!=NativeTextEditorEffect::SyncEngine || receipt.before!=current || !GoodTransition(current,receipt.after) ||
				receipt.after.editor.revision<=current.editor.revision || receipt.after.sequence!=current.sequence ||
				receipt.after.shadowRevision!=current.shadowRevision || receipt.after.group || receipt.after.collectionOpen ||
				receipt.after.collection!=collection || prepared->Presentation()!=view.presentation)
				return reject("Native settlement candidate does not match the completed presentation");
			auto after=receipt.after;
			NativeTextEditorReceipt published; // Debug STL default string proxies may allocate.
			// All string-bearing publication outputs exist before foreign synchronization.
			if (!CheckPending(store,0,error) || !Check(ingress,source,owner,store,error)) return reject("Native authority changed before final synchronization");
			if (!store.Sync(receipt,prepared->Presentation(),error) || phase==Phase::RetireRequired)
				return reject("Native settlement synchronization failed");
			NativeTextPendingSnapshot synchronized;
			if (!store.Pending(after,seal.dispatch,synchronized,error) || phase==Phase::RetireRequired ||
				synchronized.identity!=after.native || synchronized.engineRevision!=after.editor.revision ||
				synchronized.acknowledgedSequence!=after.sequence || synchronized.acknowledgedShadowRevision!=after.shadowRevision ||
				synchronized.shadowRevision!=after.shadowRevision || synchronized.lastSequence!=after.sequence || synchronized.count || synchronized.dispatch!=seal.dispatch ||
				!ingress.Validate(source,completion.batch,error) || phase==Phase::RetireRequired ||
				!owner.Current(current) || !store.StillClosed(closed)) return reject("Native settlement lost authority after synchronization");
			if (!owner.PublishSettlement(*prepared,published) || phase==Phase::RetireRequired || !SameReceipt(receipt,published))
				return reject("Native settlement local publication failed");
			static_assert(std::is_nothrow_move_assignable_v<NativeTextEditorBarrier>);
			current=std::move(after);result.settled=true;
		} else if (!Check(ingress,source,owner,store,error)) return reject("Native collection changed before completion receipt");
		lastScope=seal.serial;phase=Phase::AwaitingFence;out=result;error.clear();return true;
	} catch (...) { return reject("Native collection allocation or callback failure"); }
}
bool NativeTextCollectionCoordinator::CompleteFence(NativeQueueIngress& ingress,NativeQueueSource& source,
	const NativeTextCollectionCompletion& expected,NativeTextCollectionOwner& owner,NativeTextCollectionStore& store,
	NativeTextCollectionFence& fence,std::string& error) {
	if (std::this_thread::get_id()!=thread) { error="Native collection requires its engine thread";return false; }
	if (calling) return Fail(error,"Reentrant native collection fence completion");
	Calling guard(calling);
	const auto reject=[&](const char* message) { Fail(error,message);Retire(owner,store);return false; };
	try {
		if (phase!=Phase::AwaitingFence || expected!=completion || !queue) return reject("Native collection completion receipt is stale");
		if (!CheckPending(store,0,error) || !Check(ingress,source,owner,store,error)) return reject("Native collection changed before fence acknowledgement");
		if (!fence.Acknowledge(*queue,error) || phase==Phase::RetireRequired || !owner.Current(current) || !store.StillClosed(closed) ||
			!ingress.Finish(source,completion.batch,error) || phase==Phase::RetireRequired)
			return reject("Native collection fence acknowledgement failed");
		NativeQueueStatus after;
		if (!source.Observe(after,error) || phase==Phase::RetireRequired || !SameProvider(after,*queue) || after.pending ||
			!owner.Current(current) || !store.StillClosed(closed)) return reject("Native collection changed after fence acknowledgement");
		phase=Phase::Ready;error.clear();return true;
	} catch (...) { return reject("Native collection fence callback failure"); }
}
} // namespace openq4::ui
