// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "TextInput.h"
#include <array>

namespace openq4::ui {

// Process-local identities only. The adapter supplies nonzero, never-reused
// allocation/backend/document/modal/window/edit-session tokens. Revision changes
// on any external edit/selection change; only a checked delivery receipt may
// advance the revision of an existing native composition binding.
struct TextEditorIdentity {
	std::uint64_t allocation = 0, backend = 0, document = 0, modal = 0;
	std::uint64_t window = 0, session = 0, revision = 0;
	std::string control;
	bool operator==(const TextEditorIdentity&) const = default;
};
enum class TextBrokerRoute { Unavailable, Legacy, Retained };
struct TextBrokerContext {
	TextBrokerRoute route = TextBrokerRoute::Unavailable;
	std::uint64_t nativeWindow = 0, nativeSession = 0;
	// Retained without an eligible editor is distinct from a legacy sink.
	std::optional<TextEditorIdentity> editor;
	bool operator==(const TextBrokerContext&) const = default;
};

enum class TextNativeKind {
	SessionBegin, SessionEnd, Begin, Preedit, Result, End, Cancel, Commit,
	WindowEnd, Echo, Poison
};
enum class TextNativeOrigin {
	Unknown, UnmarkedCharacter, ObservedComposition,
	// Reserved for a provider that proves direct causation. Windows WM_CHAR is
	// UnmarkedCharacter; neither the Windows bridge nor policy may upgrade it.
	ProvenDirect
};
struct TextNativeRecord {
	TextNativeKind kind = TextNativeKind::Poison;
	TextNativeOrigin origin = TextNativeOrigin::Unknown;
	std::uint64_t sequence = 0, window = 0, session = 0, composition = 0;
	// Echo has no independent sequence/payload. The bridge must resolve the
	// exact public association to its canonical private Commit sequence.
	std::uint64_t echoOf = 0;
	TextInputEvent input;
};
// Unknown Begin/Preedit/Result/End/Cancel may have session=0 when a native
// window reports composition metadata outside active text input. They retain
// their sequence/window/origin, but can never bind or edit an editor. Other
// origins still require an actual nonzero native text session.
enum class TextUnmarkedPolicy {
	Reject,
	// A deliberately partial provider qualification: canceled, retired or
	// ambiguous composition taints this native session until SessionEnd followed
	// by a fresh SessionBegin. This does not prove lossless Windows mixed batches.
	QualifiedCleanSession
};
enum class TextBrokerDisposition { Ignored, Dropped, Retained, Legacy, Failed };
struct TextBrokerDelivery {
	std::uint64_t token = 0, sequence = 0;
	TextEditorIdentity target;
	TextInputEvent input;
};
struct TextBrokerResult {
	TextBrokerDisposition disposition = TextBrokerDisposition::Ignored;
	std::optional<TextBrokerDelivery> delivery;
	// Canonical admitted Commit, delivered exactly once by the caller's existing
	// legacy conversion path. Never deliver its explicitly associated Echo too.
	// This retains existing within-legacy-sink behavior; it does not prove legacy
	// field affinity or causation for Unknown commits without a known origin.
	std::optional<TextInputEvent> legacy;
	std::string diagnostic;
};
enum class TextDeliveryOutcome { AppliedChanged, AppliedNoChange, Rejected };

// Ordered, main-thread value model. It calls no sink, native API or Session code.
// Collect native events first; at EventLoop dispatch, Synchronize after commands
// and complete Session actions, Process exactly one ordered record, validate the
// returned target again immediately before applying, then Complete immediately.
// A pending retained delivery permits only Complete or Poison: even an identical
// Synchronize or another record is treated as a reentrant/abandoned delivery.
// Legacy results do not create a receipt; dispatch them immediately at the same
// ordered boundary and Synchronize after legacy commands have finished.
class TextInputBroker {
public:
	static constexpr std::size_t MaxWindows = 8;
	static constexpr std::size_t MaxRetiredWindows = 256;
	static constexpr std::size_t MaxEchoes = 256;
	static constexpr std::size_t MaxControlBytes = 128;
	explicit TextInputBroker(TextUnmarkedPolicy policy = TextUnmarkedPolicy::Reject);
	bool Synchronize(const TextBrokerContext& context, std::string& error);
	TextBrokerResult Process(const TextNativeRecord& record);
	// Changed requires only revision to advance strictly. NoChange and Rejected
	// require the exact original context/revision; neither may relabel a binding.
	// NoChange is an explicit successful no-op, including empty clear operations.
	bool Complete(std::uint64_t token, TextDeliveryOutcome outcome,
		const TextBrokerContext& resultingContext, std::string& error);
	void Poison();
	// Only after the caller checked native health and reconciled queued records.
	// A strictly newer provider generation is required (initial generation is 1).
	// This preserves native sequence/retired-lifetime records and session taint.
	// The next record must be a lifecycle boundary; only then may its sequence
	// bridge records explicitly discarded by the caller. Otherwise private
	// sequences must be contiguous, catching filtered/flushed marker loss. Every
	// existing window must cross SessionEnd + new SessionBegin before retention
	// resumes; recreating a broker to reuse old process tokens is unsupported.
	bool Reset(std::uint64_t providerGeneration, std::string& error);
	bool Healthy() const { return healthy; }
	bool Pending() const { return pending.has_value(); }
	std::uint64_t ProviderGeneration() const { return providerGeneration; }
private:
	struct Window {
		std::uint64_t id = 0, session = 0, composition = 0, lastComposition = 0;
		bool open = false, tainted = false, barrierRequired = false, legacyOrigin = false;
		std::optional<TextEditorIdentity> binding;
	};
	struct PendingDelivery {
		std::uint64_t token = 0, window = 0, composition = 0;
		TextBrokerContext before;
	};
	Window* Find(std::uint64_t id);
	void Retire(Window& window);
	void RetireMismatched();
	TextBrokerResult Fail(const char* diagnostic);
	TextBrokerResult Deliver(const TextNativeRecord& record, const TextInputEvent& input,
		const TextEditorIdentity& target, std::uint64_t composition);
	bool RememberCommit(std::uint64_t sequence);
	TextUnmarkedPolicy policy;
	TextBrokerContext context;
	std::array<Window, MaxWindows> windows{};
	// Native lifetime allocation and first text-session observation can differ
	// in order. An exact bounded retired set avoids an invalid global watermark.
	// Exhaustion fails closed; Reset never forgets ended process-local identities.
	std::array<std::uint64_t, MaxRetiredWindows> retiredWindows{};
	std::array<std::uint64_t, MaxEchoes> echoes{};
	std::optional<PendingDelivery> pending;
	std::uint64_t providerGeneration = 1, lastSequence = 0;
	std::uint64_t nextDelivery = 0;
	bool healthy = true, sequenceBoundary = false;
};

} // namespace openq4::ui
