// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "../../ui/retained/TextInputBroker.h"

// Keep SDL headers out of the portable retained library and callers that only
// need the ledger/unsupported stub. The enabled implementation includes the
// actual private SDL public header; no replicated native record layout is used.
struct OQ4_TextRecord;

namespace openq4::ui {

// Compile the implementation with OPENQ4_SDL_TEXT_PROVENANCE only when the
// reviewed private provider header is available. This reports compilation
// support, not current native health, module lifetime, or input authorization.
bool TextProvenanceSupported();

// copiedUtf8 is the borrowed copy's exact text_bytes bytes, excluding the native
// Copy API's extra terminator. Full UTF-8 and UTF-16 preedit offset conversion
// complete before publishing out; input may alias out.input.text. Error storage
// must not alias input/output. Unsupported builds fail with out unchanged.
// Native Result may carry captured result bytes: validate them, then emit only
// metadata. Only native Commit becomes broker committed text. Unknown remains
// Unknown; this provider never produces ProvenDirect. Numeric window_id must
// be nonzero but is observational, not editor/window lifetime authority. Native
// message is opaque except SessionBegin/End API notifications require zero.
bool ImportTextProvenanceRecord(const OQ4_TextRecord& native,
	std::string_view copiedUtf8, TextNativeRecord& out, std::string& error);

enum class TextMarkerConsumption { Echo, Unassociated, Failed };

// Exact public marker -> canonical Commit sequence association, without text
// matching or sink dispatch. Positive 31-bit tokens follow the actual provider;
// 0 public association means legacy-only unassociated text, UINT32_MAX means
// failed native association. Register only validated Commit markers, in native
// order. Token and Commit sequence gaps are legal because other marker kinds
// share those counters; reuse/regression is never legal. Exact outstanding
// associations may be consumed in any order, once each. The result is a broker
// Echo (no text), not a newly admitted Commit or a re-derived native origin.
//
// All malformed/replayed/missing/overflow cases latch unhealthy and clear pending
// associations. Reset requires a newer provider generation and retains issued
// token/sequence high-water marks, including well-formed IDs seen in failed
// registration attempts. It cannot authorize old records after queue flushing.
//
// Caller MUST poison on import rejection, queue loss/flush/provider failure/unload and check
// provider/ledger/broker health immediately before any editor mutation. This
// helper cannot detect void Sys_QueEvent overflow, freed SDL records, platform
// unloading or a lost already-decoded engine event. Native IDs remain process-
// local; neither translation nor association establishes journal replay rights.
class TextMarkerLedger {
public:
	static constexpr std::size_t MaxEntries = 256;
	static constexpr std::uint32_t MaxToken = 0x7fffffffu;
	bool RegisterCommit(std::uint32_t marker, const TextNativeRecord& commit, std::string& error);
	// Echo output remains unchanged on Unassociated/Failed. Unassociated is
	// separate from retained authorization even while health is latched false.
	TextMarkerConsumption Consume(std::uint32_t association, TextNativeRecord& echo, std::string& error);
	void Poison();
	bool Reset(std::uint64_t providerGeneration, std::string& error);
	bool Healthy() const { return healthy; }
	std::size_t Pending() const { return count; }
	std::uint64_t ProviderGeneration() const { return generation; }
private:
	struct Entry { std::uint32_t token = 0; std::uint64_t sequence = 0; };
	std::array<Entry, MaxEntries> entries{};
	std::uint32_t lastToken = 0;
	std::uint64_t lastSequence = 0, generation = 1;
	std::size_t count = 0;
	bool healthy = true;
};

} // namespace openq4::ui
