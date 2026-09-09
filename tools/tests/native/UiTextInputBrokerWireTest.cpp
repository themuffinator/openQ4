// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/TextInputBrokerWire.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#ifdef OPENQ4_BROKER_SHAPE_TEST
// Standalone source-bound harness extracts these actual frozen production
// functions unchanged. Ordinary native builds still run all independent codec
// oracles below; no duplicate broker implementation is linked into this test.
#include "BrokerShapeValidation.h"
#endif

using namespace openq4::ui;
namespace {
std::size_t checks = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
using Kind = TextNativeKind;
using Origin = TextNativeOrigin;
bool Equal(const TextNativeRecord& a, const TextNativeRecord& b) {
	return a.kind == b.kind && a.origin == b.origin && a.sequence == b.sequence && a.window == b.window &&
		a.session == b.session && a.composition == b.composition && a.echoOf == b.echoOf &&
		a.input.kind == b.input.kind && a.input.text == b.input.text &&
		a.input.selectionStart == b.input.selectionStart && a.input.selectionLength == b.input.selectionLength;
}
TextNativeRecord Record(Kind kind, Origin origin = Origin::Unknown, std::string text = "") {
	TextNativeRecord r; r.kind = kind; r.origin = origin; r.sequence = 0x0102030405060708ull;
	r.window = 0x1112131415161718ull; r.session = 0x2122232425262728ull;
	if (origin == Origin::ObservedComposition) r.composition = 0x3132333435363738ull;
	std::string error;
	if (kind == Kind::Commit) Check(MakeTextInputCommit(text, r.input, error), "create valid committed fixture");
	if (kind == Kind::Preedit) Check(MakeTextInputPreedit(text, TextIndexUnit::Utf8Bytes, -1, -1, r.input, error), "create valid preedit fixture");
	if (kind == Kind::Echo || kind == Kind::Poison) {
		r.sequence = r.window = r.session = r.composition = 0;
		if (kind == Kind::Echo) r.echoOf = (std::numeric_limits<std::uint64_t>::max)();
	}
	return r;
}
TextNativeRecord Sentinel() {
	auto r = Record(Kind::Preedit, Origin::ObservedComposition, "untouched 雪");
	r.input.selectionStart = 0; r.input.selectionLength = 3; return r;
}
std::string Encode(const TextNativeRecord& r) {
	std::string bytes = "unchanged", error = "old error";
	Check(EncodeTextInputBrokerRecord(r, bytes, error), "valid record encodes");
	Check(error.empty(), "successful encode clears error"); return bytes;
}
void RoundTrip(const TextNativeRecord& r) {
	auto bytes = Encode(r); auto out = Sentinel(); std::string error = "old error";
	Check(DecodeTextInputBrokerRecord(bytes, out, error), "valid frame decodes");
	Check(error.empty() && Equal(r, out), "all native provenance/text/offsets preserved exactly");
	Check(Encode(out) == bytes, "canonical reencoding is byte exact");
}
void RejectDecode(std::string_view bytes) {
	auto out = Sentinel(); const auto before = out; std::string error;
	Check(!DecodeTextInputBrokerRecord(bytes, out, error), "malformed wire frame rejected");
	Check(Equal(out, before), "decode rejection is atomic for every output field");
	Check(!error.empty(), "decode rejection reports error");
}
void RejectEncode(const TextNativeRecord& record) {
	std::string output = "encoded output must survive", error;
	Check(!EncodeTextInputBrokerRecord(record, output, error), "invalid record cannot encode");
	Check(output == "encoded output must survive" && !error.empty(), "encode rejection preserves old output and reports error");
}
void Put(std::string& data, std::size_t at, std::uint64_t value, std::size_t bytes) {
	for (std::size_t i = 0; i < bytes; ++i) data.at(at + i) = static_cast<char>((value >> (8 * i)) & 255);
}
std::string Hex(std::string_view source) {
	std::string result; int high = -1;
	for (char c : source) {
		if (c == ' ' || c == '\n') continue;
		int value = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
		Check(value >= 0, "literal byte fixture is hexadecimal");
		if (high < 0) high = value; else { result.push_back(static_cast<char>(high * 16 + value)); high = -1; }
	}
	Check(high < 0, "literal fixture has complete bytes"); return result;
}
std::string Payload(std::string base, std::string_view text) {
	base.resize(TextInputBrokerWireHeaderBytes); base.append(text);
	Put(base, 8, base.size(), 4); Put(base, 20, text.size(), 4); return base;
}

void LiteralAndKinds() {
	auto r = Record(Kind::Commit, Origin::ObservedComposition, "A雪🧪");
	const auto golden = Hex(
		"51 34 54 42 01 00 50 00 58 00 00 00 08 02 01 00 "
		"00 00 00 00 08 00 00 00 08 07 06 05 04 03 02 01 "
		"18 17 16 15 14 13 12 11 28 27 26 25 24 23 22 21 "
		"38 37 36 35 34 33 32 31 00 00 00 00 00 00 00 00 "
		"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
		"41 e9 9b aa f0 9f a7 aa");
	Check(Encode(r) == golden, "wire bytes match independent literal little-endian fixture");
	TextNativeRecord decoded; std::string error;
	Check(DecodeTextInputBrokerRecord(golden, decoded, error) && Equal(decoded, r), "literal fixture decodes exact 64-bit identities and Unicode");
	for (Kind kind : {Kind::SessionBegin, Kind::SessionEnd, Kind::WindowEnd, Kind::Echo, Kind::Poison}) {
		auto metadata = Record(kind); RoundTrip(metadata);
		if (kind == Kind::WindowEnd) { metadata.session = 0; RoundTrip(metadata); }
	}
	for (Kind kind : {Kind::Begin, Kind::Preedit, Kind::Result, Kind::End, Kind::Cancel}) {
		for (Origin origin : {Origin::Unknown, Origin::ObservedComposition}) {
			auto metadata = Record(kind, origin); RoundTrip(metadata);
			if (origin == Origin::Unknown) { metadata.composition = 998; RoundTrip(metadata); }
			metadata.session = 0;
			if (origin == Origin::Unknown) RoundTrip(metadata); else RejectEncode(metadata);
		}
	}
	for (Origin origin : {Origin::Unknown, Origin::ObservedComposition, Origin::UnmarkedCharacter, Origin::ProvenDirect}) {
		auto invalid = Record(Kind::Commit, origin); invalid.session = 0; RejectEncode(invalid);
	}
	for (Origin origin : {Origin::Unknown, Origin::ObservedComposition, Origin::UnmarkedCharacter, Origin::ProvenDirect}) {
		RoundTrip(Record(Kind::Commit, origin, "مرحبا\n雪🧪"));
		if (origin == Origin::Unknown) { auto old = Record(Kind::Commit); old.composition = 998; RoundTrip(old); }
	}
	for (unsigned flags : {0u, 1u, 3u}) {
		auto empty = Record(Kind::Preedit, Origin::ObservedComposition);
		if (flags & 1) empty.input.selectionStart = 0;
		if (flags & 2) empty.input.selectionLength = 0;
		RoundTrip(empty);
		Check(static_cast<unsigned char>(Encode(empty)[15]) == flags, "known zero and unknown preedit offsets remain distinct");
	}
	auto max = Record(Kind::Commit, Origin::ObservedComposition);
	max.sequence = max.window = max.session = max.composition = (std::numeric_limits<std::uint64_t>::max)();
	RoundTrip(max); // Codec validates framing, not monotonic live broker admission.
}

void FramingAndTruncation() {
	for (const auto& record : {Record(Kind::Poison), Record(Kind::Echo), Record(Kind::Commit, Origin::ObservedComposition, "A雪🧪"),
		Record(Kind::Commit, Origin::ProvenDirect, std::string(TextInputMaxBytes, 'x'))}) {
		const auto frame = Encode(record);
		for (std::size_t length = 0; length < frame.size(); ++length) RejectDecode(std::string_view(frame).substr(0, length));
		RejectDecode(frame + '\0'); RejectDecode(frame + frame);
	}
	const auto base = Encode(Record(Kind::Commit, Origin::ObservedComposition, "A雪🧪"));
	for (std::size_t i = 0; i < 8; ++i) {
		for (unsigned bit = 0; bit < 8; ++bit) { auto bad = base; bad[i] ^= static_cast<char>(1u << bit); RejectDecode(bad); }
	}
	for (std::size_t i = 16; i < 20; ++i) {
		for (unsigned bit = 0; bit < 8; ++bit) { auto bad = base; bad[i] = static_cast<char>(1u << bit); RejectDecode(bad); }
	}
	for (std::uint64_t length : {0ull, 79ull, 80ull, 87ull, 89ull, 65536ull, 0xffffffffull}) {
		auto bad = base; Put(bad, 8, length, 4); RejectDecode(bad);
	}
	for (std::uint64_t length : {0ull, 7ull, 9ull, 65536ull, 65537ull, 0xffffffffull}) {
		auto bad = base; Put(bad, 20, length, 4); RejectDecode(bad);
	}
	auto tooLarge = Payload(base, std::string(TextInputMaxBytes + 1, 'x')); RejectDecode(tooLarge);
	for (unsigned byte = 0; byte <= 255; ++byte) {
		if (byte == 0 || byte > 11) { auto bad = base; bad[12] = static_cast<char>(byte); RejectDecode(bad); }
		if (byte > 3) { auto bad = base; bad[13] = static_cast<char>(byte); RejectDecode(bad); }
		if (byte > 2) { auto bad = base; bad[14] = static_cast<char>(byte); RejectDecode(bad); }
		if (byte > 3 || byte == 2) { auto bad = base; bad[15] = static_cast<char>(byte); RejectDecode(bad); }
	}
	for (auto at : {24u, 32u, 40u}) { auto bad = base; Put(bad, at, 0, 8); RejectDecode(bad); }
	for (auto at : {56u, 64u, 72u}) {
		for (unsigned i = 0; i < 8; ++i) { auto bad = base; bad[at + i] = 1; RejectDecode(bad); }
	}
	for (auto origin : {1, 3}) { auto bad = base; bad[13] = static_cast<char>(origin); RejectDecode(bad); }
	auto noComposition = base; Put(noComposition, 48, 0, 8); RejectDecode(noComposition);
}

void UnicodeAndOffsets() {
	const auto commit = Encode(Record(Kind::Commit, Origin::ProvenDirect, "ok"));
	const std::vector<std::string> invalid = {std::string("a\0b", 3), "\x80", "\xc0\xaf", "\xc1\xbf", "\xc2", "\xe0\x80\x80",
		"\xed\xa0\x80", "\xf0\x80\x80\x80", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff", "\xe2\x82", "\xf0\x9f\xa7"};
	for (const auto& text : invalid) {
		RejectDecode(Payload(commit, text));
		auto record = Record(Kind::Commit, Origin::ProvenDirect); record.input.text = text; RejectEncode(record);
	}
	auto preedit = Record(Kind::Preedit, Origin::ObservedComposition, "A雪🧪é");
	std::string error;
	Check(MakeTextInputPreedit(preedit.input.text, TextIndexUnit::Utf16CodeUnits, 1, 3, preedit.input, error), "native UTF-16 conversion occurs before encoding");
	Check(preedit.input.selectionStart == 1 && preedit.input.selectionLength == 7, "wire receives normalized byte offsets");
	RoundTrip(preedit); const auto bytes = Encode(preedit);
	for (std::uint64_t start : {2ull, 3ull, 5ull, 6ull, 7ull, 65536ull, 0xffffffffffffffffull}) {
		auto bad = bytes; Put(bad, 64, start, 8); Put(bad, 72, 0, 8); RejectDecode(bad);
	}
	for (std::uint64_t length : {1ull, 2ull, 4ull, 5ull, 6ull, 99ull, 0xffffffffffffffffull}) {
		auto bad = bytes; Put(bad, 72, length, 8); RejectDecode(bad);
	}
	for (auto flags : {0, 1, 2}) { auto bad = bytes; bad[15] = static_cast<char>(flags); RejectDecode(bad); }
	auto caret = preedit; caret.input.selectionLength.reset(); RoundTrip(caret);
	auto noStart = preedit; noStart.input.selectionStart.reset(); RejectEncode(noStart);
	auto split = preedit; split.input.selectionStart = 2; RejectEncode(split);
	auto enormous = preedit; enormous.input.selectionLength = (std::numeric_limits<std::size_t>::max)(); RejectEncode(enormous);
	for (auto kind : {TextInputKind::Commit, TextInputKind::CancelComposition}) {
		auto bad = preedit; bad.input.kind = kind; RejectEncode(bad);
	}
}

void MetadataAndAliasing() {
	for (Kind kind : {Kind::SessionBegin, Kind::SessionEnd, Kind::WindowEnd, Kind::Begin, Kind::Result, Kind::End, Kind::Cancel, Kind::Echo, Kind::Poison}) {
		const bool observed = kind == Kind::Begin || kind == Kind::Result || kind == Kind::End || kind == Kind::Cancel;
		auto record = Record(kind, observed ? Origin::ObservedComposition : Origin::Unknown);
		const auto encoded = Encode(record);
		auto text = encoded; text[14] = 1; RejectDecode(text); // Even empty Commit is not metadata.
		auto preedit = encoded; preedit[14] = 2; RejectDecode(preedit);
		auto data = Payload(encoded, "injected"); RejectDecode(data);
		auto option = encoded; option[15] = 1; RejectDecode(option);
		record.input.kind = TextInputKind::Commit; RejectEncode(record);
	}
	for (Kind kind : {Kind::Echo, Kind::Poison}) {
		const auto encoded = Encode(Record(kind));
		for (auto field : {24u, 32u, 40u, 48u}) {
			for (unsigned i = 0; i < 8; ++i) { auto bad = encoded; bad[field + i] = 1; RejectDecode(bad); }
		}
		for (unsigned origin : {1u, 2u, 3u}) { auto bad = encoded; bad[13] = static_cast<char>(origin); RejectDecode(bad); }
		auto echo = encoded; Put(echo, 56, kind == Kind::Echo ? 0 : 1, 8); RejectDecode(echo);
	}
	auto record = Record(Kind::Commit, Origin::Unknown, "alias 雪"); record.composition = 66;
	const auto original = record; const auto encoded = Encode(record); std::string error;
	Check(EncodeTextInputBrokerRecord(record, record.input.text, error), "encode safely stages aliased input and output text");
	Check(record.input.text == encoded, "aliased encoding retains exact original payload and native fields");
	Check(DecodeTextInputBrokerRecord(std::string_view(record.input.text), record, error), "decode safely stages output whose member backs the frame view");
	Check(Equal(record, original), "aliased decode recovers every field");
	record.input.text = encoded + "extra"; const auto unchanged = record;
	Check(!DecodeTextInputBrokerRecord(record.input.text, record, error) && Equal(record, unchanged), "failed aliased decoding leaves backing bytes and all output fields unchanged");
	record = original; record.input.text.push_back('\0'); const auto invalid = record.input.text;
	Check(!EncodeTextInputBrokerRecord(record, record.input.text, error) && record.input.text == invalid, "failed aliased encoding retains invalid original text exactly");
}

void BoundedStressAndBrokerShape() {
	const std::array<std::string, 6> parts = {"a", "雪", "🧪", "é", "مرحبا", "\t\n"};
	for (unsigned i = 0; i < 2048; ++i) {
		std::string text;
		for (unsigned j = 0; j < i % 31; ++j) text += parts[(i + j) % parts.size()];
		auto record = Record(i % 2 ? Kind::Commit : Kind::Preedit, i % 3 ? Origin::ObservedComposition : Origin::Unknown, text);
		record.sequence = 1ull + i * 137ull; record.window = 1ull + i * 239ull; record.session = 1ull + i * 433ull;
		if (record.kind == Kind::Preedit && i % 4 == 0) { record.input.selectionStart = 0; record.input.selectionLength = text.size(); }
		RoundTrip(record);
	}
#ifdef OPENQ4_BROKER_SHAPE_TEST
	for (int kind = -1; kind <= 11; ++kind) for (int origin = -1; origin <= 4; ++origin)
		for (int input = -1; input <= 3; ++input) for (unsigned bits = 0; bits < 32; ++bits) {
			TextNativeRecord record; record.kind = static_cast<Kind>(kind); record.origin = static_cast<Origin>(origin);
			record.input.kind = static_cast<TextInputKind>(input);
			record.sequence = bits & 1; record.window = (bits >> 1) & 1; record.session = (bits >> 2) & 1;
			record.composition = (bits >> 3) & 1; record.echoOf = (bits >> 4) & 1;
			for (unsigned payload = 0; payload < 4; ++payload) {
				record.input.text = payload & 1 ? "雪" : "";
				record.input.selectionStart = payload & 2 ? std::optional<std::size_t>(0) : std::nullopt;
				std::string brokerError, wireError, bytes = "unchanged";
				const bool expected = frozen_broker_shape::ValidRecord(record, brokerError);
				const bool actual = EncodeTextInputBrokerRecord(record, bytes, wireError);
				Check(actual == expected, "codec structural acceptance matches actual frozen broker validation");
				if (actual) RoundTrip(record); else Check(bytes == "unchanged", "matrix rejection preserves encoded output");
			}
		}
#endif
}
}

int main() {
	try {
		LiteralAndKinds(); FramingAndTruncation(); UnicodeAndOffsets(); MetadataAndAliasing(); BoundedStressAndBrokerShape();
		std::cout << "UiTextInputBrokerWireTest: " << checks << " checks passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception& error) {
		std::cerr << "UiTextInputBrokerWireTest after " << checks << " checks: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
