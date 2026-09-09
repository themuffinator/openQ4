// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "TextInput.h"

#include <utility>

namespace openq4::ui {
namespace {

bool Fail(std::string& error, const char* diagnostic) {
	error = diagnostic;
	return false;
}

// Advances one scalar, rejecting all non-shortest encodings and Unicode-invalid
// values. The caller bounds the complete string before scanning it.
bool Next(std::string_view text, std::size_t& offset, std::uint32_t& scalar) {
	const auto lead = static_cast<unsigned char>(text[offset++]);
	if (lead < 0x80) { scalar = lead; return lead != 0; }
	unsigned tails;
	std::uint32_t minimum;
	if (lead >= 0xc2 && lead <= 0xdf) { tails = 1; scalar = lead & 0x1f; minimum = 0x80; }
	else if (lead >= 0xe0 && lead <= 0xef) { tails = 2; scalar = lead & 0x0f; minimum = 0x800; }
	else if (lead >= 0xf0 && lead <= 0xf4) { tails = 3; scalar = lead & 0x07; minimum = 0x10000; }
	else return false;
	if (text.size() - offset < tails) return false;
	for (unsigned i = 0; i < tails; ++i) {
		const auto byte = static_cast<unsigned char>(text[offset++]);
		if ((byte & 0xc0) != 0x80) return false;
		scalar = (scalar << 6) | (byte & 0x3f);
	}
	return scalar >= minimum && scalar <= 0x10ffff && !(scalar >= 0xd800 && scalar <= 0xdfff);
}

bool ValidUnits(TextIndexUnit units) {
	return units == TextIndexUnit::Utf8Bytes || units == TextIndexUnit::UnicodeScalars ||
		units == TextIndexUnit::Utf16CodeUnits;
}

// Called only after full UTF-8 validation. Every unit count is bounded by the
// byte length, including UTF-16, so additions cannot overflow here.
bool ByteOffset(std::string_view text, TextIndexUnit units, std::uint64_t requested,
	std::size_t& byteOffset) {
	std::size_t offset = 0;
	std::uint64_t count = 0;
	while (count < requested && offset < text.size()) {
		const auto before = offset;
		std::uint32_t scalar = 0;
		if (!Next(text, offset, scalar)) return false;
		count += units == TextIndexUnit::Utf8Bytes ? offset - before :
			units == TextIndexUnit::Utf16CodeUnits && scalar > 0xffff ? 2 : 1;
	}
	if (count != requested) return false;
	byteOffset = offset;
	return true;
}

} // namespace

bool ValidateTextInputUtf8(std::string_view text, std::string& error) {
	if (text.size() > TextInputMaxBytes) return Fail(error, "Text exceeds the UTF-8 transport limit");
	std::size_t offset = 0;
	while (offset < text.size()) {
		std::uint32_t scalar = 0;
		if (!Next(text, offset, scalar)) return Fail(error, "Text is not NUL-free scalar UTF-8");
	}
	error.clear();
	return true;
}

bool ValidateTextInputEvent(const TextInputEvent& event, std::string& error) {
	if (!ValidateTextInputUtf8(event.text, error)) return false;
	switch (event.kind) {
	case TextInputKind::Commit:
		if (event.selectionStart || event.selectionLength)
			return Fail(error, "Committed text cannot carry a preedit selection");
		break;
	case TextInputKind::CancelComposition:
		if (!event.text.empty() || event.selectionStart || event.selectionLength)
			return Fail(error, "Composition cancellation cannot carry text or selection");
		break;
	case TextInputKind::Preedit: {
		if (!event.selectionStart && event.selectionLength)
			return Fail(error, "A preedit selection length requires a known start");
		if (!event.selectionStart) break;
		const auto start = *event.selectionStart;
		if (start > event.text.size()) return Fail(error, "Preedit selection start is out of range");
		std::size_t byteOffset;
		if (!ByteOffset(event.text, TextIndexUnit::Utf8Bytes, start, byteOffset))
			return Fail(error, "Preedit selection start splits a Unicode scalar");
		if (!event.selectionLength) break;
		if (*event.selectionLength > event.text.size() - start)
			return Fail(error, "Preedit selection length is out of range");
		if (!ByteOffset(event.text, TextIndexUnit::Utf8Bytes, start + *event.selectionLength, byteOffset))
			return Fail(error, "Preedit selection end splits a Unicode scalar");
		break;
	}
	default: return Fail(error, "Unknown text input kind");
	}
	error.clear();
	return true;
}

bool MakeTextInputCommit(std::string_view text, TextInputEvent& out, std::string& error) {
	if (!ValidateTextInputUtf8(text, error)) return false;
	TextInputEvent candidate;
	candidate.kind = TextInputKind::Commit;
	candidate.text.assign(text);
	out = std::move(candidate);
	return true;
}

bool MakeTextInputPreedit(std::string_view text, TextIndexUnit units,
	std::int64_t start, std::int64_t length, TextInputEvent& out, std::string& error) {
	if (!ValidUnits(units)) return Fail(error, "Unknown preedit index unit");
	if (!ValidateTextInputUtf8(text, error)) return false;
	if (start < -1 || length < -1 || (start == -1 && length != -1))
		return Fail(error, "Invalid or incoherent unknown preedit selection");
	TextInputEvent candidate;
	candidate.kind = TextInputKind::Preedit;
	if (start >= 0) {
		// The byte bound is an upper bound for every supported index unit. Check
		// it before adding raw signed counts, which may be arbitrary input.
		if (static_cast<std::uint64_t>(start) > text.size() ||
			(length >= 0 && static_cast<std::uint64_t>(length) > text.size() - static_cast<std::size_t>(start)))
			return Fail(error, "Preedit selection exceeds the source text");
		std::size_t begin;
		if (!ByteOffset(text, units, static_cast<std::uint64_t>(start), begin))
			return Fail(error, "Preedit start is not an in-range scalar boundary");
		candidate.selectionStart = begin;
		if (length >= 0) {
			std::size_t end;
			if (!ByteOffset(text, units, static_cast<std::uint64_t>(start + length), end))
				return Fail(error, "Preedit end is not an in-range scalar boundary");
			candidate.selectionLength = end - begin;
		}
	}
	candidate.text.assign(text);
	out = std::move(candidate);
	error.clear();
	return true;
}

void MakeTextInputCancel(TextInputEvent& out) { out = TextInputEvent{}; }

} // namespace openq4::ui
