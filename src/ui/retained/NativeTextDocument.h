// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openq4::ui {

struct NativeTextIdentity {
	std::uint64_t document = 0, editorLease = 0;
	bool operator==(const NativeTextIdentity&) const = default;
};
struct NativeTextBoundary {
	std::size_t byte = 0;
	std::uint32_t acp = 0;
	bool operator==(const NativeTextBoundary&) const = default;
};
// Strict scalar boundaries; no normalization, grapheme or bidi inference.
// Complete output is unchanged on failure. ACP never splits a surrogate pair.
bool BuildNativeTextMap(std::string_view text, std::vector<NativeTextBoundary>& out, std::string& error);

struct NativeTextRange {
	std::size_t first = 0, last = 0; // UTF-8 bytes in the corresponding snapshot.
	bool operator==(const NativeTextRange&) const = default;
};
struct NativeTextSnapshot {
	std::string text;
	std::size_t anchor = 0, caret = 0; // UTF-8 scalar boundaries, order preserved.
	std::map<std::uint64_t, NativeTextRange> compositions;
	bool operator==(const NativeTextSnapshot&) const = default;
};
enum class NativeTextAccess { Read, ReadWrite };
enum class NativeTextLockResult { Granted, Deferred, Refused };
struct NativeTextLockScope {
	NativeTextIdentity identity;
	std::uint64_t serial = 0;
	NativeTextAccess access = NativeTextAccess::Read;
	bool operator==(const NativeTextLockScope&) const = default;
};
enum class NativeTextOperationKind { Replace, Select, BeginComposition, UpdateComposition, EndComposition };
struct NativeTextOperation {
	NativeTextOperationKind kind = NativeTextOperationKind::Replace;
	std::uint32_t first = 0, last = 0; // ACP in the then-current ordered document.
	std::string text;
	std::uint64_t composition = 0;
	bool operator==(const NativeTextOperation&) const = default;
};
enum class NativeTextClassification { Unclassified, CompositionRelated };
struct NativeTextTransaction {
	NativeTextIdentity identity;
	std::uint64_t sequence = 0, nativeDispatch = 0;
	std::uint64_t shadowBefore = 0, shadowAfter = 0;
	NativeTextClassification classification = NativeTextClassification::Unclassified;
	bool documentChanged = false; // Text or selection; composition metadata is separate.
	std::vector<NativeTextOperation> operations;
	NativeTextSnapshot after;
	bool operator==(const NativeTextTransaction&) const = default;
};
struct NativeTextOffer {
	NativeTextTransaction transaction;
	std::uint64_t expectedEngineRevision = 0;
	bool operator==(const NativeTextOffer&) const = default;
};
struct NativeTextPendingSnapshot {
	NativeTextIdentity identity;
	std::uint64_t engineRevision=0, acknowledgedSequence=0, acknowledgedShadowRevision=0;
	std::uint64_t shadowRevision=0, dispatch=0, lastSequence=0;
	std::uint32_t count=0;
	bool operator==(const NativeTextPendingSnapshot&) const = default;
};
// A copied native callback observation, not a lock, lease or input authority.
struct NativeTextMetadataObservation {
	NativeTextIdentity identity;
	std::uint64_t engineRevision=0, shadowRevision=0, transactionSequence=0, acknowledgedSequence=0, dispatch=0;
	bool operator==(const NativeTextMetadataObservation&) const = default;
};
struct NativeTextLimits {
	std::size_t documentBytes = 65536, pendingTransactions = 32;
	std::size_t pendingBytes = 1024 * 1024, operations = 256, retainedCompositions = 32;
	// A lower lifetime budget is useful to hosts and deterministic exhaustion tests.
	std::uint64_t sequence = (std::numeric_limits<std::uint64_t>::max)();
};

// Pure shadow text store, not a COM adapter, event pump, native input provider or
// editor mutation route. A successful RequestLock/GrantDeferredWrite represents
// the explicit OnLockGranted callback scope. The adapter finishes it only after
// that callback returns; this model never invokes callbacks or enters a GUI.
//
// Open is one-shot. The owner must supply globally appropriate non-reused opaque
// identity; no native affinity or pending transaction may be restored from save.
// Composition tokens increase within this document. A watermark rejects retired
// tokens without retaining an unbounded lifetime set. Count/byte limits cover
// pending payloads, not allocator overhead; all container counts are bounded.
// Diagnostic error must not alias any input/output string. Allocation exceptions
// may propagate; committed state and published queue remain unchanged until a
// complete operation can be installed.
class NativeTextDocument {
public:
	explicit NativeTextDocument(NativeTextLimits limits = {});
	~NativeTextDocument();
	NativeTextDocument(const NativeTextDocument&) = delete;
	NativeTextDocument& operator=(const NativeTextDocument&) = delete;

	bool Open(NativeTextIdentity, std::uint64_t engineRevision,
		std::string_view text, std::size_t anchor, std::size_t caret, std::string& error);
	NativeTextIdentity Identity() const;
	bool Active() const;
	std::uint64_t EngineRevision() const;
	std::uint64_t ShadowRevision() const;
	std::size_t PendingCount() const;
	std::size_t PendingBytes() const;

	NativeTextLockResult RequestLock(NativeTextIdentity, NativeTextAccess, bool synchronous,
		NativeTextLockScope& out, std::string& error);
	bool GrantDeferredWrite(NativeTextIdentity, NativeTextLockScope& out, std::string& error);
	bool Read(const NativeTextLockScope&, NativeTextSnapshot& out, std::string& error) const;
	bool AcpToByte(const NativeTextLockScope&, std::uint32_t acp, std::size_t& out, std::string& error) const;
	bool ByteToAcp(const NativeTextLockScope&, std::size_t byte, std::uint32_t& out, std::string& error) const;

	// Replacements use following gravity for selection endpoints; composition
	// ranges use preceding start/following end gravity. Explicit subsequent Select
	// and UpdateComposition operations override these checked tracked positions.
	bool ReplaceACP(const NativeTextLockScope&, std::uint32_t first, std::uint32_t last,
		std::string_view replacement, std::string& error);
	bool SelectACP(const NativeTextLockScope&, std::uint32_t anchor, std::uint32_t caret, std::string& error);
	bool BeginComposition(const NativeTextLockScope&, std::uint64_t token,
		std::uint32_t first, std::uint32_t last, std::string& error);
	bool UpdateComposition(const NativeTextLockScope&, std::uint64_t token,
		std::uint32_t first, std::uint32_t last, std::string& error);
	bool EndComposition(const NativeTextLockScope&, std::uint64_t token, std::string& error);
	// Separately observed composition callbacks outside actual text-store locks.
	// Capture before querying foreign ranges; Publish rechecks the complete value.
	// Only Begin/Update/End with empty text are allowed. End requires zero range.
	// No text/selection or prior transaction is changed/reclassified. One success
	// appends one metadata-only offer using the ordinary queue/ACK/shadow order.
	// Both outputs remain unchanged on refusal; caller provides native dispatch
	// provenance separately. No callback, lock serial or OnLockGranted is invented.
	bool CaptureCompositionObservation(NativeTextIdentity, std::uint64_t externallyObservedDispatch,
		NativeTextMetadataObservation& out, std::string& error) const;
	bool PublishCompositionObservation(const NativeTextMetadataObservation&, const NativeTextOperation&,
		std::uint64_t& published, std::string& error);

	// A bad write poisons only this candidate. Matching Finish refuses, unlocks
	// deterministically and leaves published state intact; Abort also unlocks.
	// Invalid/stale scopes never release or modify another callback's lock.
	// published is zero for a read/no-op lock, otherwise the new transaction ID.
	bool FinishLock(const NativeTextLockScope&, std::uint64_t nativeDispatch,
		std::uint64_t& published, std::string& error);
	bool AbortLock(const NativeTextLockScope&, std::string& error);

	// Only FIFO front is offered. Its payload is immutable; the separate expected
	// engine revision is bound when it becomes front, after the preceding ACK.
	bool PeekOffer(NativeTextIdentity, NativeTextOffer& out, std::string& error) const;
	// Read-only queue watermark; returns copied values, never offers or authority.
	// Caller supplies a separately verified held dispatch/fence and the editor's
	// exact ACKed sequence/shadow/revision. A copied immutable Peek is harmless;
	// an applied-but-unacknowledged offer no longer matches that editor barrier.
	// Empty queues still require exact identity/revisions and nonzero dispatch,
	// but do not prove this document participated in the supplied dispatch.
	bool QueryPendingCollection(NativeTextIdentity, std::uint64_t expectedEngineRevision,
		std::uint64_t expectedAcknowledgedSequence, std::uint64_t expectedAcknowledgedShadowRevision,
		std::uint64_t externallyObservedDispatch, NativeTextPendingSnapshot& out, std::string& error) const;
	bool Acknowledge(NativeTextIdentity, std::uint64_t transaction, std::uint64_t shadowAfter,
		std::uint64_t expectedEngineRevision, std::uint64_t acceptedEngineRevision, std::string& error);
	// Rejection retires the original document and drops every dependent offer.
	bool Reject(NativeTextIdentity, std::uint64_t transaction,
		std::uint64_t expectedEngineRevision, std::string& error);
	bool SyncEngine(NativeTextIdentity, std::uint64_t expectedEngineRevision,
		std::uint64_t expectedShadowRevision, std::uint64_t newEngineRevision,
		std::string_view text, std::size_t anchor, std::size_t caret, std::string& error);
	bool Retire(NativeTextIdentity, std::string& error);
	// Exact retirement for failure/teardown paths. No diagnostic construction or
	// allocation, including with MSVC debug iterator proxies enabled.
	bool Retire(NativeTextIdentity) noexcept;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace openq4::ui
