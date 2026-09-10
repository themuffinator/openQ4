// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "NativeTextDocument.h"
#include "TextEdit.h"
#include "TextInputBroker.h"

namespace openq4::ui {
struct NativeTextCollection {
	std::uint64_t dispatch=0, fenceSequence=0, lastTransactionSequence=0;
	bool operator==(const NativeTextCollection&) const = default;
};
struct NativeTextEditorBarrier {
	NativeTextIdentity native;
	TextEditorIdentity editor;
	std::uint64_t sequence=0, shadowRevision=0, group=0;
	NativeTextCollection collection{};
	bool collectionOpen=false;
	bool operator==(const NativeTextEditorBarrier&) const = default;
};
enum class NativeTextEditorEffect { Acknowledge, SyncEngine, Retire };
struct NativeTextEditorReceipt {
	NativeTextEditorEffect effect=NativeTextEditorEffect::Acknowledge;
	NativeTextEditorBarrier before, after;
	// Text/selection/group presentation changed; excludes revision-only progress.
	// Bridge outcomes must compare authoritative before/after editor revisions.
	bool presentationChanged=false;
};
struct NativeTextEditorView {
	NativeTextEditorBarrier barrier;
	TextEditState draft;
	TextEditHistory history;
	TextEditPolicy policy;
	// Complete native presentation. Do not convert this to the old single
	// preedit overlay: selection/ranges index THESE exact UTF-8 bytes.
	NativeTextSnapshot presentation;
	bool active=false, awaitingSettlement=false;
};

// Portable atomic reconciliation, not an input provider or setting dispatch.
// Open binds a complete editor identity and native lease to a stable draft.
// This class owns no accepted numeric value and never manufactures a Commit.
// The caller validates current eligibility and publishes this candidate together
// with its Interaction editor state, then performs the returned native receipt.
//
// A group includes all concurrent compositions and all Begin/End pairs within
// one indivisible native transaction. Stable draft/history stay unchanged until
// explicit Settle at a real completed native/fence barrier; End alone is not
// such a barrier. Independent per-range cancellation needs richer native edit
// ownership metadata and is not inferred here. Cancel/Retire restore the stable
// draft and retire the binding; recreate native context from that draft before
// continuing. No native identity, group or receipt may be restored from a save.
//
// Input/output objects and error must not alias each other or internal data.
// Failures, including allocation exceptions, leave this object and output
// receipts unchanged. Owner-supplied revisions are exact engine editor revisions
// (not merely text revisions). The owner must supply non-reused native/editor IDs.
class NativeTextEditor {
public:
	NativeTextEditor();
	~NativeTextEditor();
	NativeTextEditor(const NativeTextEditor&)=delete;
	NativeTextEditor& operator=(const NativeTextEditor&)=delete;
	// Copies are preparation candidates, never independent native authorities.
	// Prepare all Interaction state and View copies before publication, with no
	// foreign callbacks. Swap checks the live source barrier and clone origin,
	// then publishes without allocation. A rejected swap changes neither object.
	// After success the candidate holds the old state but cannot publish it again.
	std::unique_ptr<NativeTextEditor> Clone() const;
	bool Swap(const NativeTextEditorBarrier& expected,NativeTextEditor& candidate) noexcept;
	bool Open(NativeTextIdentity,const TextEditorIdentity&,const TextEditBuffer&,std::string& error);
	NativeTextEditorView View() const;
	NativeTextEditorBarrier Barrier() const;
	bool Active() const;
	// Bind caller-observed, verified collection metadata before its first offer.
	// The descriptor itself proves no native fence or physical/direct authority.
	// Protocol-only transitions preserve engine revision and emit no native ACK.
	// The first Begin permanently requires scoped Apply for this binding.
	bool BeginCollection(const NativeTextEditorBarrier&,const NativeTextCollection&,
		NativeTextEditorBarrier& out,std::string& error);
	bool CompleteCollection(const NativeTextEditorBarrier&,const NativeTextCollection&,
		NativeTextEditorBarrier& out,std::string& error);
	bool Apply(const NativeTextOffer&,const TextEditorIdentity& expected,
		std::uint64_t acceptedEngineRevision,NativeTextEditorReceipt& out,std::string& error);
	// Barrier equality is necessary, not proof of native queue exhaustion. The
	// engine supplies that proof; this pure model cannot observe a native pump.
	// Collection mode requires CompleteCollection first; never settle early to
	// unblock later offers from the same collection. Live ranges carry across
	// completed collections until the complete atomic group can settle.
	bool SettleComposition(const NativeTextEditorBarrier&,std::uint64_t acceptedEngineRevision,
		NativeTextEditorReceipt& out,std::string& error);
	bool CancelComposition(const NativeTextEditorBarrier&,std::uint64_t acceptedEngineRevision,
		NativeTextEditorReceipt& out,std::string& error);
	bool Retire(const NativeTextEditorBarrier&,std::uint64_t acceptedEngineRevision,
		NativeTextEditorReceipt& out,std::string& error);
	// Emergency disposal after failed native receipt/revision allocation. Exact
	// current barrier only; no allocation, revision issuance or native callback.
	// Restores stable presentation and permanently retires this binding. The host
	// must separately retire/discard its native object; never continue either side.
	bool Abandon(const NativeTextEditorBarrier&) noexcept;
	// Read-only explicit numeric commit preparation. No active/unsettled group,
	// exact editor identity and Valid parsing are required. Invalid/incomplete
	// draft text remains editable; accepted application values are never changed.
	bool NumberCandidate(const TextEditorIdentity&,const TextNumberPolicy&,double& out,std::string& error) const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace openq4::ui
