// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "TextInputBrokerWire.h"

namespace openq4::ui {
namespace {
bool Fail(std::string& error, const char* message) { error = message; return false; }

std::uint8_t KindByte(TextNativeKind kind) {
	switch (kind) {
		case TextNativeKind::SessionBegin: return 1;
		case TextNativeKind::SessionEnd: return 2;
		case TextNativeKind::Begin: return 3;
		case TextNativeKind::Preedit: return 4;
		case TextNativeKind::Result: return 5;
		case TextNativeKind::End: return 6;
		case TextNativeKind::Cancel: return 7;
		case TextNativeKind::Commit: return 8;
		case TextNativeKind::WindowEnd: return 9;
		case TextNativeKind::Echo: return 10;
		case TextNativeKind::Poison: return 11;
	}
	return 0;
}
bool ReadKind(std::uint64_t value, TextNativeKind& kind) {
	switch (value) {
		case 1: kind = TextNativeKind::SessionBegin; return true;
		case 2: kind = TextNativeKind::SessionEnd; return true;
		case 3: kind = TextNativeKind::Begin; return true;
		case 4: kind = TextNativeKind::Preedit; return true;
		case 5: kind = TextNativeKind::Result; return true;
		case 6: kind = TextNativeKind::End; return true;
		case 7: kind = TextNativeKind::Cancel; return true;
		case 8: kind = TextNativeKind::Commit; return true;
		case 9: kind = TextNativeKind::WindowEnd; return true;
		case 10: kind = TextNativeKind::Echo; return true;
		case 11: kind = TextNativeKind::Poison; return true;
	}
	return false;
}
std::uint8_t OriginByte(TextNativeOrigin origin) {
	switch (origin) {
		case TextNativeOrigin::Unknown: return 0;
		case TextNativeOrigin::UnmarkedCharacter: return 1;
		case TextNativeOrigin::ObservedComposition: return 2;
		case TextNativeOrigin::ProvenDirect: return 3;
	}
	return 255;
}
bool ReadOrigin(std::uint64_t value, TextNativeOrigin& origin) {
	switch (value) {
		case 0: origin = TextNativeOrigin::Unknown; return true;
		case 1: origin = TextNativeOrigin::UnmarkedCharacter; return true;
		case 2: origin = TextNativeOrigin::ObservedComposition; return true;
		case 3: origin = TextNativeOrigin::ProvenDirect; return true;
	}
	return false;
}
std::uint8_t InputByte(TextInputKind kind) {
	switch (kind) {
		case TextInputKind::CancelComposition: return 0;
		case TextInputKind::Commit: return 1;
		case TextInputKind::Preedit: return 2;
	}
	return 255;
}
bool ReadInput(std::uint64_t value, TextInputKind& kind) {
	switch (value) {
		case 0: kind = TextInputKind::CancelComposition; return true;
		case 1: kind = TextInputKind::Commit; return true;
		case 2: kind = TextInputKind::Preedit; return true;
	}
	return false;
}

// Structural grammar shared with the frozen broker contract, not broker state
// admission. In particular, legitimate Unknown composition metadata is encoded
// unchanged and later dropped/tainted by the broker; it is not upgraded here.
bool ValidShape(const TextNativeRecord& record, std::string& error) {
	if (!KindByte(record.kind) || OriginByte(record.origin) == 255)
		return Fail(error, "Unknown text broker record kind or origin");
	if (!ValidateTextInputEvent(record.input, error)) return false;
	const bool metadata = record.input.kind == TextInputKind::CancelComposition;
	if (record.kind == TextNativeKind::Echo || record.kind == TextNativeKind::Poison) {
		if (record.sequence || record.window || record.session || record.composition ||
			record.origin != TextNativeOrigin::Unknown || !metadata ||
			(record.kind == TextNativeKind::Echo ? record.echoOf == 0 : record.echoOf != 0))
			return Fail(error, "Invalid echo/failure record fields");
		return true;
	}
	const bool unscopedMetadata = record.origin == TextNativeOrigin::Unknown &&
		(record.kind == TextNativeKind::Begin || record.kind == TextNativeKind::Preedit || record.kind == TextNativeKind::Result ||
		 record.kind == TextNativeKind::End || record.kind == TextNativeKind::Cancel);
	if (!record.sequence || !record.window || (!record.session && record.kind != TextNativeKind::WindowEnd && !unscopedMetadata) || record.echoOf)
		return Fail(error, "Native text record lacks its exact window/session identity");
	if (record.kind == TextNativeKind::SessionBegin || record.kind == TextNativeKind::SessionEnd || record.kind == TextNativeKind::WindowEnd) {
		if (record.composition || record.origin != TextNativeOrigin::Unknown || !metadata)
			return Fail(error, "Session/window lifecycle fields must be metadata only");
		return true;
	}
	if (record.kind == TextNativeKind::Commit) {
		if (record.input.kind != TextInputKind::Commit ||
			(record.origin == TextNativeOrigin::ObservedComposition && !record.composition) ||
			((record.origin == TextNativeOrigin::ProvenDirect || record.origin == TextNativeOrigin::UnmarkedCharacter) && record.composition))
			return Fail(error, "Commit fields contradict their text or native origin");
		return true;
	}
	if (record.kind == TextNativeKind::Preedit ? record.input.kind != TextInputKind::Preedit : !metadata)
		return Fail(error, "Composition metadata cannot carry committed text");
	if (record.origin != TextNativeOrigin::Unknown &&
		(record.origin != TextNativeOrigin::ObservedComposition || !record.composition))
		return Fail(error, "Composition fields contradict their native origin");
	return true;
}

// Callers supply compile-time header offsets/counts after checking frame bounds.
// Numeric assembly is independent of host byte order, alignment and word size.
void Put(std::string& bytes, std::size_t offset, std::uint64_t value, unsigned count) {
	for (unsigned i = 0; i < count; ++i) {
		bytes[offset + i] = static_cast<char>(value & 255u); value >>= 8;
	}
}
std::uint64_t Get(std::string_view bytes, std::size_t offset, unsigned count) {
	std::uint64_t value = 0;
	for (unsigned i = 0; i < count; ++i)
		value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
	return value;
}
}

bool EncodeTextInputBrokerRecord(const TextNativeRecord& record, std::string& outBytes, std::string& error) {
	if (!ValidShape(record, error)) return false;
	std::string candidate(TextInputBrokerWireHeaderBytes, '\0');
	candidate.replace(0, 4, "Q4TB");
	Put(candidate, 4, TextInputBrokerWireVersion, 2);
	Put(candidate, 6, TextInputBrokerWireHeaderBytes, 2);
	Put(candidate, 8, TextInputBrokerWireHeaderBytes + record.input.text.size(), 4);
	Put(candidate, 12, KindByte(record.kind), 1);
	Put(candidate, 13, OriginByte(record.origin), 1);
	Put(candidate, 14, InputByte(record.input.kind), 1);
	Put(candidate, 15, (record.input.selectionStart ? 1u : 0u) | (record.input.selectionLength ? 2u : 0u), 1);
	Put(candidate, 20, record.input.text.size(), 4);
	Put(candidate, 24, record.sequence, 8);
	Put(candidate, 32, record.window, 8);
	Put(candidate, 40, record.session, 8);
	Put(candidate, 48, record.composition, 8);
	Put(candidate, 56, record.echoOf, 8);
	Put(candidate, 64, record.input.selectionStart.value_or(0), 8);
	Put(candidate, 72, record.input.selectionLength.value_or(0), 8);
	candidate.append(record.input.text);
	outBytes.swap(candidate); error.clear(); return true;
}

bool DecodeTextInputBrokerRecord(std::string_view bytes, TextNativeRecord& out, std::string& error) {
	if (bytes.size() < TextInputBrokerWireHeaderBytes || bytes.size() > TextInputBrokerWireMaxFrameBytes)
		return Fail(error, "Text broker frame length is outside its bounds");
	if (bytes.substr(0, 4) != "Q4TB" || Get(bytes, 4, 2) != TextInputBrokerWireVersion ||
		Get(bytes, 6, 2) != TextInputBrokerWireHeaderBytes)
		return Fail(error, "Unknown text broker frame magic, version or header size");
	const auto textBytes = Get(bytes, 20, 4);
	if (textBytes > TextInputMaxBytes || Get(bytes, 8, 4) != bytes.size() ||
		bytes.size() != TextInputBrokerWireHeaderBytes + textBytes)
		return Fail(error, "Text broker frame is truncated or has trailing data");
	const auto flags = Get(bytes, 15, 1), start = Get(bytes, 64, 8), length = Get(bytes, 72, 8);
	if (Get(bytes, 16, 4) || flags > 3 || flags == 2 ||
		(!(flags & 1) && start) || (!(flags & 2) && length))
		return Fail(error, "Text broker reserved or absent offset fields are nonzero");
	// Check wide wire integers against bounded text before converting to size_t.
	if (start > textBytes || length > textBytes - start)
		return Fail(error, "Text broker preedit offsets exceed the payload");
	TextNativeRecord candidate;
	if (!ReadKind(Get(bytes, 12, 1), candidate.kind) || !ReadOrigin(Get(bytes, 13, 1), candidate.origin) ||
		!ReadInput(Get(bytes, 14, 1), candidate.input.kind))
		return Fail(error, "Unknown text broker wire enum");
	candidate.sequence = Get(bytes, 24, 8); candidate.window = Get(bytes, 32, 8);
	candidate.session = Get(bytes, 40, 8); candidate.composition = Get(bytes, 48, 8);
	candidate.echoOf = Get(bytes, 56, 8);
	if (flags & 1) candidate.input.selectionStart = static_cast<std::size_t>(start);
	if (flags & 2) candidate.input.selectionLength = static_cast<std::size_t>(length);
	candidate.input.text.assign(bytes.substr(TextInputBrokerWireHeaderBytes));
	if (!ValidShape(candidate, error)) return false;
	out = std::move(candidate); error.clear(); return true;
}

} // namespace openq4::ui
