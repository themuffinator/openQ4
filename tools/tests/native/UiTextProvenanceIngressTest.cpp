// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/sys/sdl3/TextProvenanceIngress.h"
#include "src/ui/retained/TextInputBrokerWire.h"
#include "src/ui/retained/TextEdit.h"
#include <SDL3/SDL_openq4_text_provenance.h>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

using namespace openq4::ui;
namespace {
std::size_t checks = 0;
void Check(bool ok, const char* reason) { ++checks; if (!ok) throw std::runtime_error(reason); }
bool Equal(const TextNativeRecord& a, const TextNativeRecord& b) {
	return a.kind == b.kind && a.origin == b.origin && a.sequence == b.sequence && a.window == b.window &&
		a.session == b.session && a.composition == b.composition && a.echoOf == b.echoOf && a.input.kind == b.input.kind &&
		a.input.text == b.input.text && a.input.selectionStart == b.input.selectionStart && a.input.selectionLength == b.input.selectionLength;
}
OQ4_TextRecord Native(Uint32 kind, Uint32 origin = OQ4_TEXT_UNKNOWN, std::string_view text = "") {
	OQ4_TextRecord r{}; r.version = 1; r.kind = kind; r.origin = origin;
	r.window_id = 7; r.window_lifetime = 55; r.session = 10; r.sequence = 1;
	r.composition = origin == OQ4_TEXT_OBSERVED_COMPOSITION ? 20 : 0;
	r.text_bytes = static_cast<Uint32>(text.size()); r.selection_start = r.selection_length = -1;
	return r;
}
TextNativeRecord Commit(std::uint64_t sequence = 1) {
	TextNativeRecord r; r.kind = TextNativeKind::Commit; r.origin = TextNativeOrigin::Unknown;
	r.sequence = sequence; r.window = 55; r.session = 10; std::string error;
	Check(MakeTextInputCommit("same text", r.input, error), "create valid canonical Commit"); return r;
}
void RejectNative(const OQ4_TextRecord& native, std::string_view text) {
	auto out = Commit(998); const auto before = out; std::string error;
	Check(!ImportTextProvenanceRecord(native, text, out, error), "malformed native record rejected");
	Check(Equal(out, before) && !error.empty(), "native import failure preserves every output field");
}
#if defined(OPENQ4_SDL_TEXT_PROVENANCE)
TextNativeRecord Import(const OQ4_TextRecord& native, std::string_view text = "") {
	TextNativeRecord out; std::string error = "old error";
	Check(ImportTextProvenanceRecord(native, text, out, error), "actual private-header record imports");
	Check(error.empty() && out.sequence == native.sequence && out.window == native.window_lifetime &&
		out.session == native.session && out.composition == native.composition && !out.echoOf, "native process IDs preserved exactly");
	std::string bytes; TextNativeRecord decoded;
	Check(EncodeTextInputBrokerRecord(out, bytes, error) && DecodeTextInputBrokerRecord(bytes, decoded, error) && Equal(decoded, out),
		"imported record satisfies actual broker wire grammar without losing provenance");
	return out;
}
#endif

void ProductionHeaderAndUnsupported() {
	static_assert(sizeof(decltype(OQ4_TextRecord{}.version)) == 4 && sizeof(decltype(OQ4_TextRecord{}.sequence)) == 8);
	static_assert(sizeof(decltype(OQ4_TextRecord{}.selection_start)) == 4 && std::is_signed_v<decltype(OQ4_TextRecord{}.selection_start)>);
	Check(OQ4_TEXT_SESSION_BEGIN == 1 && OQ4_TEXT_WINDOW_END == 9, "actual private-header kind constants");
	Check(OQ4_TEXT_UNKNOWN == 0 && OQ4_TEXT_UNMARKED_CHARACTER == 1 && OQ4_TEXT_OBSERVED_COMPOSITION == 2, "actual private-header origin constants");
#if defined(OPENQ4_SDL_TEXT_PROVENANCE)
	Check(TextProvenanceSupported(), "enabled implementation reports compilation support only");
#else
	Check(!TextProvenanceSupported(), "unsupported implementation does not advertise provider");
	RejectNative(Native(OQ4_TEXT_COMMIT, OQ4_TEXT_UNKNOWN, "valid"), "valid");
	RejectNative(Native(OQ4_TEXT_PREEDIT), "");
	RejectNative({}, "\xff");
#endif
}

#if defined(OPENQ4_SDL_TEXT_PROVENANCE)
void NativeKindsAndMetadata() {
	for (Uint32 kind : {OQ4_TEXT_SESSION_BEGIN, OQ4_TEXT_SESSION_END, OQ4_TEXT_WINDOW_END}) {
		auto r = Native(kind); const auto out = Import(r);
		Check(out.origin == TextNativeOrigin::Unknown && out.input.kind == TextInputKind::CancelComposition && out.input.text.empty(),
			"native lifecycle remains metadata only");
		if (kind == OQ4_TEXT_WINDOW_END) { r.session = 0; r.native_message = 0xffffffffu; Import(r); }
	}
	for (Uint32 kind : {OQ4_TEXT_BEGIN, OQ4_TEXT_PREEDIT, OQ4_TEXT_RESULT, OQ4_TEXT_END, OQ4_TEXT_CANCEL}) {
		for (Uint32 origin : {OQ4_TEXT_UNKNOWN, OQ4_TEXT_OBSERVED_COMPOSITION}) {
			auto r = Native(kind, origin); r.native_message = 0xffffffffu; Import(r);
			if (origin == OQ4_TEXT_UNKNOWN) { r.session = 0; r.composition = 91; Import(r); }
			else { r.session = 0; RejectNative(r, ""); }
		}
	}
	for (Uint32 origin : {OQ4_TEXT_UNKNOWN, OQ4_TEXT_UNMARKED_CHARACTER, OQ4_TEXT_OBSERVED_COMPOSITION}) {
		auto r = Native(OQ4_TEXT_COMMIT, origin, "雪🧪"); r.native_message = 0x0102;
		auto out = Import(r, "雪🧪");
		Check(out.origin == (origin == OQ4_TEXT_UNKNOWN ? TextNativeOrigin::Unknown :
			origin == OQ4_TEXT_UNMARKED_CHARACTER ? TextNativeOrigin::UnmarkedCharacter : TextNativeOrigin::ObservedComposition),
			"message metadata never upgrades native origin");
		Check(out.input.kind == TextInputKind::Commit && out.input.text == "雪🧪", "only canonical native Commit inserts its complete Unicode payload");
		r.window_id = 777; r.native_message = 0xffffffffu;
		Check(Equal(Import(r, "雪🧪"), out), "opaque message/numeric SDL window metadata never selects or relabels target");
		r.session = 0; RejectNative(r, "雪🧪");
	}
	auto result = Native(OQ4_TEXT_RESULT, OQ4_TEXT_OBSERVED_COMPOSITION, "captured result 雪");
	const auto metadata = Import(result, "captured result 雪");
	Check(metadata.kind == TextNativeKind::Result && metadata.input.kind == TextInputKind::CancelComposition && metadata.input.text.empty(),
		"captured native result validates and emits no inserted text");
	result.selection_start = result.selection_length = 0; RejectNative(result, "captured result 雪");
	result = Native(OQ4_TEXT_RESULT, OQ4_TEXT_UNKNOWN, "\xff"); RejectNative(result, "\xff");
	result = Native(OQ4_TEXT_RESULT, OQ4_TEXT_UNKNOWN, std::string("a\0b", 3)); RejectNative(result, std::string("a\0b", 3));
	std::string large(TextInputMaxBytes, 'x'); result = Native(OQ4_TEXT_RESULT, OQ4_TEXT_UNKNOWN, large);
	Check(Import(result, large).input.text.empty(), "bounded maximum native result is never an implicit commit");
}

void OffsetsAndMalformed() {
	const std::string text = "A雪🧪é";
	auto native = Native(OQ4_TEXT_PREEDIT, OQ4_TEXT_OBSERVED_COMPOSITION, text);
	native.selection_start = 1; native.selection_length = 3;
	auto out = Import(native, text);
	Check(out.input.selectionStart == 1 && out.input.selectionLength == 7, "actual UTF-16 scalar conversion produces UTF-8 byte selection");
	native.selection_length = -1; out = Import(native, text);
	Check(out.input.selectionStart == 1 && !out.input.selectionLength, "known caret with unknown selection length preserved");
	native.selection_start = -1; out = Import(native, text);
	Check(!out.input.selectionStart && !out.input.selectionLength, "unknown native offsets remain unknown");
	for (auto start : {-2, 3, 100, 0x7fffffff}) { native.selection_start = start; RejectNative(native, text); }
	native.selection_start = 1;
	for (auto length : {-2, 2, 100, 0x7fffffff}) { native.selection_length = length; RejectNative(native, text); }
	native.selection_start = -1; native.selection_length = 0; RejectNative(native, text);
	native = Native(OQ4_TEXT_PREEDIT, OQ4_TEXT_UNKNOWN); native.selection_start = native.selection_length = 0;
	out = Import(native); Check(out.input.text.empty() && out.input.selectionStart == 0 && out.input.selectionLength == 0, "empty native clear preserves known zero offsets");
	for (int variant = 0; variant < 16; ++variant) {
		auto r = Native(OQ4_TEXT_COMMIT, OQ4_TEXT_UNKNOWN, "abc"); std::string bytes = "abc";
		switch (variant) {
			case 0: r.version = 0; break;
			case 1: r.version = 2; break;
			case 2: r.kind = 0; break;
			case 3: r.kind = 10; break;
			case 4: r.origin = 3; break; // The observer has no ProvenDirect origin.
			case 5: r.window_id = 0; break;
			case 6: r.window_lifetime = 0; break;
			case 7: r.sequence = 0; break;
			case 8: r.text_bytes = 4; break;
			case 9: r.text_bytes = 2; break;
			case 10: r.text_bytes = 0xffffffffu; break;
			case 11: r.selection_start = 0; break;
			case 12: r.selection_length = 0; break;
			case 13: r.origin = OQ4_TEXT_UNMARKED_CHARACTER; r.composition = 8; break;
			case 14: r.origin = OQ4_TEXT_OBSERVED_COMPOSITION; break;
			case 15: bytes.assign(TextInputMaxBytes + 1, 'x'); r.text_bytes = static_cast<Uint32>(bytes.size()); break;
		}
		RejectNative(r, bytes);
	}
	for (Uint32 kind : {OQ4_TEXT_SESSION_BEGIN, OQ4_TEXT_SESSION_END}) {
		auto r = Native(kind); r.native_message = 1; RejectNative(r, "");
		r = Native(kind); r.session = 0; RejectNative(r, "");
	}
	for (Uint32 kind : {OQ4_TEXT_SESSION_BEGIN, OQ4_TEXT_SESSION_END, OQ4_TEXT_WINDOW_END}) {
		auto r = Native(kind); r.composition = 1; RejectNative(r, "");
		r = Native(kind); r.origin = OQ4_TEXT_OBSERVED_COMPOSITION; RejectNative(r, "");
	}
	for (const auto& bad : std::vector<std::string>{"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf0\x9f\xa7", "\x80", std::string("\0", 1)})
		RejectNative(Native(OQ4_TEXT_COMMIT, OQ4_TEXT_UNKNOWN, bad), bad);
	for (Uint32 kind : {OQ4_TEXT_BEGIN, OQ4_TEXT_END, OQ4_TEXT_CANCEL, OQ4_TEXT_SESSION_BEGIN, OQ4_TEXT_SESSION_END, OQ4_TEXT_WINDOW_END})
		RejectNative(Native(kind, OQ4_TEXT_UNKNOWN, "hidden"), "hidden");
	auto alias = Commit(998); alias.input.text = text;
	native = Native(OQ4_TEXT_PREEDIT, OQ4_TEXT_OBSERVED_COMPOSITION, text); native.selection_start = 1; native.selection_length = 3;
	std::string error;
	Check(ImportTextProvenanceRecord(native, alias.input.text, alias, error) && alias.input.text == text && alias.input.selectionLength == 7,
		"native input view may safely alias output text");
	const auto before = alias; native.version = 2;
	Check(!ImportTextProvenanceRecord(native, alias.input.text, alias, error) && Equal(alias, before), "failed aliased import is atomic");
}

void ComposedPipeline() {
	TextInputBroker broker; TextMarkerLedger ledger; TextEditBuffer buffer; std::string error;
	TextBrokerContext context{TextBrokerRoute::Retained, 55, 10, TextEditorIdentity{1, 1, 1, 1, 55, 1, 1, "number"}};
	Check(buffer.Reset("", {}, error) && broker.Synchronize(context, error), "real composed pipeline initialized");
	std::uint64_t sequence = 0; unsigned writes = 0;
	auto deliver = [&](OQ4_TextRecord native, std::string_view text) {
		native.sequence = ++sequence; auto record = Import(native, text);
		if (record.kind == TextNativeKind::Commit) Check(ledger.RegisterCommit(static_cast<std::uint32_t>(sequence), record, error), "register canonical Commit marker before echo");
		auto result = broker.Process(record);
		if (result.delivery) {
			Check(ledger.Healthy() && broker.Healthy() && result.delivery->target == *context.editor, "caller checks both health and exact owner immediately before mutation");
			Check(buffer.Apply(result.delivery->input, error), "actual text buffer applies imported event"); ++writes; ++context.editor->revision;
			Check(broker.Complete(result.delivery->token, TextDeliveryOutcome::AppliedChanged, context, error), "actual delivery receipt advances only original affinity");
		}
		return result.disposition;
	};
	deliver(Native(OQ4_TEXT_SESSION_BEGIN), ""); deliver(Native(OQ4_TEXT_BEGIN, OQ4_TEXT_OBSERVED_COMPOSITION), "");
	deliver(Native(OQ4_TEXT_PREEDIT, OQ4_TEXT_OBSERVED_COMPOSITION, "候補"), "候補");
	Check(buffer.PresentedText() == "候補" && buffer.State().text.empty(), "preedit is presentation only");
	for (Uint32 kind : {OQ4_TEXT_BEGIN, OQ4_TEXT_PREEDIT, OQ4_TEXT_RESULT, OQ4_TEXT_END, OQ4_TEXT_CANCEL}) {
		auto record = Native(kind, OQ4_TEXT_UNKNOWN); record.session = 0; record.composition = 998;
		Check(deliver(record, "") == TextBrokerDisposition::Dropped && buffer.PresentedText() == "候補",
			"unscoped native lifecycle cannot touch a different active session's editor or affinity");
	}
	deliver(Native(OQ4_TEXT_RESULT, OQ4_TEXT_OBSERVED_COMPOSITION, "1.05"), "1.05");
	Check(writes == 1 && buffer.State().text.empty(), "captured Result never dispatches insertion");
	deliver(Native(OQ4_TEXT_PREEDIT, OQ4_TEXT_OBSERVED_COMPOSITION), "");
	deliver(Native(OQ4_TEXT_COMMIT, OQ4_TEXT_OBSERVED_COMPOSITION, "1.05"), "1.05");
	TextNativeRecord echo;
	Check(ledger.Consume(static_cast<std::uint32_t>(sequence), echo, error) == TextMarkerConsumption::Echo && echo.echoOf == sequence,
		"exact public token resolves only its canonical Commit sequence");
	Check(broker.Process(echo).disposition == TextBrokerDisposition::Ignored && buffer.State().text == "1.05" && writes == 3,
		"public echo neither inserts again nor changes original typed text");
}
#endif

void MarkerLedger() {
	TextMarkerLedger ledger; TextNativeRecord echo = Commit(99); const auto untouched = echo; std::string error;
	Check(ledger.Consume(0, echo, error) == TextMarkerConsumption::Unassociated && Equal(echo, untouched) && ledger.Healthy(), "zero association is explicitly legacy-only and nonmutating");
	Check(ledger.RegisterCommit(10, Commit(20), error) && ledger.RegisterCommit(30, Commit(40), error), "non-Commit markers may create gaps between registered tokens/sequences");
	Check(ledger.Consume(30, echo, error) == TextMarkerConsumption::Echo && echo.echoOf == 40 && !echo.sequence && !echo.window && !echo.session && !echo.composition,
		"exact association resolves out of registration order without source guessing");
	Check(echo.input.text.empty() && echo.kind == TextNativeKind::Echo && echo.origin == TextNativeOrigin::Unknown, "echo contains only canonical sequence association");
	Check(ledger.Consume(10, echo, error) == TextMarkerConsumption::Echo && echo.echoOf == 20 && ledger.Pending() == 0, "older outstanding exact association still resolves once");
	const auto lastEcho = echo;
	Check(ledger.Consume(10, echo, error) == TextMarkerConsumption::Failed && !ledger.Healthy() && Equal(echo, lastEcho), "replayed association latches failure and preserves output");
	Check(ledger.Consume(0, echo, error) == TextMarkerConsumption::Unassociated && !ledger.Healthy(), "unassociated legacy disposition never clears latched health");
	Check(ledger.Reset(2, error), "explicit newer provider generation resets pending storage");
	Check(!ledger.RegisterCommit(30, Commit(41), error) && !ledger.Healthy(), "reset never permits an old consumed marker token");
	Check(ledger.Reset(3, error) && ledger.RegisterCommit(31, Commit(42), error), "fresh token and sequence admit after explicit reconciliation");
	for (std::uint32_t token : {0u, 0x80000000u, 0xffffffffu}) {
		TextMarkerLedger invalid; Check(!invalid.RegisterCommit(token, Commit(), error) && !invalid.Healthy(), "only actual positive31-bit provider tokens may register");
	}
	for (std::uint32_t association : {1u, 0x80000000u, 0xffffffffu}) {
		TextMarkerLedger invalid; const auto before = echo;
		Check(invalid.Consume(association, echo, error) == TextMarkerConsumption::Failed && !invalid.Healthy() && Equal(echo, before), "unknown or failed public association never guesses or publishes echo");
	}
	TextMarkerLedger unseen;
	Check(unseen.Consume(77, echo, error) == TextMarkerConsumption::Failed && unseen.Reset(2, error), "unregistered public association requires reconciliation");
	Check(!unseen.RegisterCommit(77, Commit(1), error), "failed public association token cannot reappear as a new registration after reset");
	for (int variant = 0; variant < 8; ++variant) {
		TextMarkerLedger invalid; auto commit = Commit(10);
		Check(invalid.RegisterCommit(10, commit, error), "initial ledger association");
		auto token = 11u; commit.sequence = 11;
		if (variant == 0) token = 10;
		if (variant == 1) token = 9;
		if (variant == 2) commit.sequence = 10;
		if (variant == 3) commit.sequence = 9;
		if (variant == 4) commit.kind = TextNativeKind::Result;
		if (variant == 5) commit.input.text.push_back('\0');
		if (variant == 6) commit.echoOf = 9;
		if (variant == 7) commit.session = 0;
		Check(!invalid.RegisterCommit(token, commit, error) && !invalid.Healthy() && invalid.Pending() == 0, "regression/reuse/malformed/non-Commit registration clears associations and latches failure");
	}
	TextMarkerLedger attempts; auto bad = Commit(50); bad.input.text.push_back('\0');
	Check(!attempts.RegisterCommit(50, bad, error) && attempts.Reset(2, error), "failed issued marker retained through reset");
	Check(!attempts.RegisterCommit(50, Commit(51), error), "malformed marker registration cannot retry same token as valid");
	TextMarkerLedger full;
	for (std::uint32_t i = 1; i <= TextMarkerLedger::MaxEntries; ++i) Check(full.RegisterCommit(i, Commit(i), error), "bounded exact marker entry accepted");
	Check(full.Pending() == 256 && !full.RegisterCommit(257, Commit(257), error) && !full.Healthy() && full.Pending() == 0, "marker overflow fails closed instead of evicting an unknown association");
	TextMarkerLedger stream;
	for (std::uint32_t i = 1; i <= 4096; ++i) {
		Check(stream.RegisterCommit(i, Commit(i), error), "sustained bounded marker stream registers");
		Check(stream.Consume(i, echo, error) == TextMarkerConsumption::Echo && echo.echoOf == i && stream.Pending() == 0,
			"sustained bounded marker stream consumes exact association once");
	}
	Check(!stream.Reset(1, error) && !stream.Healthy(), "provider generation regression never clears failure");
	TextMarkerLedger maximum;
	Check(maximum.RegisterCommit(TextMarkerLedger::MaxToken, Commit((std::numeric_limits<std::uint64_t>::max)()), error), "maximum actual provider identities retain all bits");
	Check(maximum.Consume(TextMarkerLedger::MaxToken, echo, error) == TextMarkerConsumption::Echo && echo.echoOf == (std::numeric_limits<std::uint64_t>::max)(), "maximum canonical sequence not truncated");
}
}

int main() {
	try {
		ProductionHeaderAndUnsupported();
#if defined(OPENQ4_SDL_TEXT_PROVENANCE)
		NativeKindsAndMetadata(); OffsetsAndMalformed(); ComposedPipeline();
#endif
		MarkerLedger();
		std::cout << "UiTextProvenanceIngressTest: " << checks << " checks passed\n"; return EXIT_SUCCESS;
	} catch (const std::exception& error) {
		std::cerr << "UiTextProvenanceIngressTest after " << checks << " checks: " << error.what() << '\n'; return EXIT_FAILURE;
	}
}
