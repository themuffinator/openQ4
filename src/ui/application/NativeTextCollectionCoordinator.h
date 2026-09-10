// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "../retained/Interaction.h"
#include "../../sys/sdl3/NativeQueueBatch.h"
#include <thread>

namespace openq4::ui {
enum class NativeClosedCollectionKind { Pump, Lifecycle };
// Copied from an explicitly CLOSED native-store collection, never guessed from
// event counts or a pending watermark. The platform adapter maps its exact scope
// kind/serial/dispatch and immutable Close receipt into this portable value.
struct NativeClosedTextCollection {
	NativeTextIdentity identity;
	std::uint64_t serial = 0, dispatch = 0;
	NativeClosedCollectionKind kind = NativeClosedCollectionKind::Pump;
	NativeTextPendingSnapshot pending;
	std::uint64_t admittedCallbacks = 0;
	bool operator==(const NativeClosedTextCollection&) const = default;
};

// Every method resolves the supplied original owner afresh; no borrowed GUI or
// Interaction pointer can survive a callback. Current/Publish are strictly
// callback-free and allocation-free. Refresh may poll authoritative host values.
// The caller keeps these endpoints and coordinator alive through each call.
class NativeTextCollectionOwner {
public:
	virtual ~NativeTextCollectionOwner() = default;
	virtual bool Refresh(const NativeTextEditorBarrier&, NativeTextEditorView&, std::string&) = 0;
	virtual bool Current(const NativeTextEditorBarrier&) const noexcept = 0;
	virtual bool Begin(const NativeTextEditorBarrier&, const NativeTextCollection&, NativeTextEditorBarrier&, std::string&) = 0;
	virtual bool Apply(const NativeTextEditorBarrier&, const NativeTextOffer&, NativeTextEditorReceipt&, std::string&) = 0;
	virtual bool Complete(const NativeTextEditorBarrier&, const NativeTextCollection&, NativeTextEditorBarrier&, std::string&) = 0;
	virtual std::unique_ptr<Interaction::NativeSettlement> PrepareSettlement(const NativeTextEditorBarrier&, std::string&) = 0;
	virtual bool PublishSettlement(Interaction::NativeSettlement&, NativeTextEditorReceipt&) noexcept = 0;
	// Emergency disposal may touch only this exact old allocation/backend/document/
	// modal/session/native lease, never a replacement owner. Preserve stable draft.
	virtual void RetireExact(const NativeTextIdentity&, const TextEditorIdentity&) noexcept = 0;
};
class NativeTextCollectionStore {
public:
	virtual ~NativeTextCollectionStore() = default;
	virtual bool Closed(const NativeTextEditorBarrier&, std::uint64_t dispatch, NativeClosedTextCollection&, std::string&) = 0;
	// A callback-free check: scope is still CLOSED, with exactly this immutable
	// receipt. Opening/replacing/retiring a scope makes this false.
	virtual bool StillClosed(const NativeClosedTextCollection&) const noexcept = 0;
	virtual bool Pending(const NativeTextEditorBarrier&, std::uint64_t dispatch, NativeTextPendingSnapshot&, std::string&) = 0;
	virtual bool Peek(const NativeTextIdentity&, NativeTextOffer&, std::string&) = 0;
	virtual bool Acknowledge(const NativeTextEditorReceipt&, std::string&) = 0;
	virtual bool Sync(const NativeTextEditorReceipt&, const NativeTextSnapshot&, std::string&) = 0;
	virtual void RetireExact(const NativeTextIdentity&) noexcept = 0;
};
class NativeTextCollectionFence {
public:
	virtual ~NativeTextCollectionFence() = default;
	virtual bool Acknowledge(const NativeQueueStatus&, std::string&) = 0;
};
struct NativeTextCollectionCompletion {
	std::uint64_t coordinator = 0, serial = 0;
	NativeQueueReceipt batch;
	bool operator==(const NativeTextCollectionCompletion&) const = default;
};
struct NativeTextCollectionResult {
	NativeTextCollectionCompletion completion;
	std::uint32_t transactions = 0;
	bool settled = false, composing = false;
};

// One coordinator per already-attached immutable native/editor lease. It does
// not attach a field, open/close a native scope, pump, enable a provider, interpret
// SDL text or dispatch ordinary events. Queue membership conveys ordering only,
// never physical/direct origin or permission to insert text.
//
// Reconcile starts only from an owned verified batch and a separate exact CLOSED
// Pump scope. Apply publishes transient native presentation and ACKs its exact
// offers; stable text/history wait for complete verified settlement. A prepared
// settlement is synced natively before callback-free checked local publication.
// Live ranges can span collections and make one undo group when finally settled.
//
// CompleteFence is separate: the caller must first account for EVERY ordinary
// event in the immutable batch, including Outside/Sentinel entries. Neither
// method consumes, modifies or silently drops those records. The completion
// token proves this coordinator's native reconciliation only.
//
// Any failure/reentry latches RetireRequired. The outer call retires only the
// original native/editor lease. Provider retirement and ingress reset/re-enable
// are EXTERNAL requirements; discard this coordinator after retirement. An
// empty closed native collection is not evidence of text-service participation.
// Calls are confined to the constructing engine thread. Worker calls refuse
// without touching provider/editor/coordinator state. Outputs remain unchanged
// on failure; input/output/error objects must not alias.
class NativeTextCollectionCoordinator {
public:
	explicit NativeTextCollectionCoordinator(const NativeTextEditorBarrier& attached);
	NativeTextCollectionCoordinator(const NativeTextCollectionCoordinator&) = delete;
	NativeTextCollectionCoordinator& operator=(const NativeTextCollectionCoordinator&) = delete;
	bool Reconcile(NativeQueueIngress&, NativeQueueSource&, const NativeQueueBatch&,
		NativeTextCollectionOwner&, NativeTextCollectionStore&, NativeTextCollectionResult& out, std::string& error);
	bool CompleteFence(NativeQueueIngress&, NativeQueueSource&, const NativeTextCollectionCompletion&,
		NativeTextCollectionOwner&, NativeTextCollectionStore&, NativeTextCollectionFence&, std::string& error);
	bool NeedsRetirement() const noexcept { return std::this_thread::get_id()!=thread || phase == Phase::RetireRequired; }
private:
	enum class Phase { Ready, Reconciling, AwaitingFence, RetireRequired };
	bool Check(NativeQueueIngress&, NativeQueueSource&, NativeTextCollectionOwner&, NativeTextCollectionStore&, std::string&);
	bool CheckPending(NativeTextCollectionStore&, std::uint32_t remaining, std::string&);
	bool Fail(std::string&, const char*) noexcept;
	void Retire(NativeTextCollectionOwner&, NativeTextCollectionStore&) noexcept;
	Phase phase = Phase::Ready;
	bool calling = false, retired = false;
	std::uint64_t identity = 0, serial = 0, lastScope = 0;
	NativeTextEditorBarrier current;
	std::optional<NativeQueueStatus> queue;
	NativeTextCollectionCompletion completion;
	NativeClosedTextCollection closed;
	const std::thread::id thread = std::this_thread::get_id();
};
} // namespace openq4::ui
