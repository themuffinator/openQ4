// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "TextProvenanceIngress.h"
#include "../../ui/retained/TextInputBrokerWire.h"
#include <algorithm>

#if defined(OPENQ4_SDL_TEXT_PROVENANCE)
#include <SDL3/SDL_openq4_text_provenance.h>
#endif

namespace openq4::ui {
namespace {
bool Fail(std::string& error, const char* message) { error = message; return false; }
}

bool TextProvenanceSupported() {
#if defined(OPENQ4_SDL_TEXT_PROVENANCE)
	return true;
#else
	return false;
#endif
}

bool ImportTextProvenanceRecord(const OQ4_TextRecord& native,
	std::string_view copiedUtf8, TextNativeRecord& out, std::string& error) {
#if defined(OPENQ4_SDL_TEXT_PROVENANCE)
	if (native.version != 1 || !native.sequence || !native.window_lifetime || !native.window_id ||
		native.text_bytes != copiedUtf8.size() || copiedUtf8.size() > TextInputMaxBytes)
		return Fail(error, "Invalid native text record version, length or window identity");
	if (!ValidateTextInputUtf8(copiedUtf8, error)) return false;
	TextNativeRecord candidate;
	switch (native.kind) {
		case OQ4_TEXT_SESSION_BEGIN: candidate.kind = TextNativeKind::SessionBegin; break;
		case OQ4_TEXT_SESSION_END: candidate.kind = TextNativeKind::SessionEnd; break;
		case OQ4_TEXT_BEGIN: candidate.kind = TextNativeKind::Begin; break;
		case OQ4_TEXT_PREEDIT: candidate.kind = TextNativeKind::Preedit; break;
		case OQ4_TEXT_RESULT: candidate.kind = TextNativeKind::Result; break;
		case OQ4_TEXT_END: candidate.kind = TextNativeKind::End; break;
		case OQ4_TEXT_CANCEL: candidate.kind = TextNativeKind::Cancel; break;
		case OQ4_TEXT_COMMIT: candidate.kind = TextNativeKind::Commit; break;
		case OQ4_TEXT_WINDOW_END: candidate.kind = TextNativeKind::WindowEnd; break;
		default: return Fail(error, "Unknown native text record kind");
	}
	switch (native.origin) {
		case OQ4_TEXT_UNKNOWN: candidate.origin = TextNativeOrigin::Unknown; break;
		case OQ4_TEXT_UNMARKED_CHARACTER: candidate.origin = TextNativeOrigin::UnmarkedCharacter; break;
		case OQ4_TEXT_OBSERVED_COMPOSITION: candidate.origin = TextNativeOrigin::ObservedComposition; break;
		default: return Fail(error, "Unknown native text origin");
	}
	candidate.sequence = native.sequence; candidate.window = native.window_lifetime;
	candidate.session = native.session; candidate.composition = native.composition;
	if ((candidate.kind == TextNativeKind::SessionBegin || candidate.kind == TextNativeKind::SessionEnd) && native.native_message)
		return Fail(error, "Native text session API notification has unexpected message metadata");
	if (candidate.kind == TextNativeKind::Preedit) {
		if (!MakeTextInputPreedit(copiedUtf8, TextIndexUnit::Utf16CodeUnits,
			native.selection_start, native.selection_length, candidate.input, error)) return false;
	} else {
		if (native.selection_start != -1 || native.selection_length != -1)
			return Fail(error, "Only native preedit may carry selection offsets");
		if (candidate.kind == TextNativeKind::Commit) {
			if (!MakeTextInputCommit(copiedUtf8, candidate.input, error)) return false;
		} else if (candidate.kind != TextNativeKind::Result && !copiedUtf8.empty()) {
			return Fail(error, "Native lifecycle metadata cannot carry text");
		}
		// Result's captured native bytes were validated above. They deliberately
		// do not become candidate.input: actual SDL admission emits Commit later.
	}
	// Reuse the exact broker wire grammar, including unscoped Unknown metadata,
	// rather than inventing source/session identifiers or a second shape policy.
	std::string checkedFrame;
	if (!EncodeTextInputBrokerRecord(candidate, checkedFrame, error)) return false;
	out = std::move(candidate); error.clear(); return true;
#else
	(void)native; (void)copiedUtf8; (void)out;
	return Fail(error, "Native text provenance provider is unavailable in this build");
#endif
}

void TextMarkerLedger::Poison() {
	healthy = false; entries.fill({}); count = 0;
}
bool TextMarkerLedger::Reset(std::uint64_t providerGeneration, std::string& error) {
	if (providerGeneration <= generation) { Poison(); return Fail(error, "Text marker provider generation must advance"); }
	entries.fill({}); count = 0; generation = providerGeneration; healthy = true; error.clear(); return true;
}
bool TextMarkerLedger::RegisterCommit(std::uint32_t marker, const TextNativeRecord& commit, std::string& error) {
	const bool validToken = marker && marker <= MaxToken;
	const bool ordered = validToken && marker > lastToken && commit.sequence > lastSequence;
	// An issued identifier cannot be retried as new after malformed input or
	// poison. Preserve observed high-water marks even if this attempt fails.
	if (validToken) lastToken = (std::max)(lastToken, marker);
	lastSequence = (std::max)(lastSequence, commit.sequence);
	if (!healthy || !ordered || commit.kind != TextNativeKind::Commit) {
		Poison(); return Fail(error, "Text marker is unhealthy, invalid, reused, out of order or not a Commit");
	}
	std::string checkedFrame;
	if (!EncodeTextInputBrokerRecord(commit, checkedFrame, error)) { Poison(); return false; }
	for (auto& entry : entries) if (!entry.token) {
		entry = {marker, commit.sequence}; ++count; error.clear(); return true;
	}
	Poison(); return Fail(error, "Text marker association budget exceeded");
}
TextMarkerConsumption TextMarkerLedger::Consume(std::uint32_t association, TextNativeRecord& echo, std::string& error) {
	if (!association) { error.clear(); return TextMarkerConsumption::Unassociated; }
	if (healthy && association <= MaxToken) {
		for (auto& entry : entries) if (entry.token == association) {
			TextNativeRecord candidate; candidate.kind = TextNativeKind::Echo; candidate.echoOf = entry.sequence;
			entry = {}; --count; echo = std::move(candidate); error.clear(); return TextMarkerConsumption::Echo;
		}
	}
	if (association <= MaxToken) lastToken = (std::max)(lastToken, association);
	Poison(); error = "Native public text association is failed, missing, reused or unhealthy";
	return TextMarkerConsumption::Failed;
}

} // namespace openq4::ui
