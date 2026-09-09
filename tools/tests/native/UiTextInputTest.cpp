// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/TextInput.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace openq4::ui;
static unsigned checks;
static void Check(bool okay, const char* why) {
	++checks;
	if (!okay) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static bool Same(const TextInputEvent& a, const TextInputEvent& b) {
	return a.kind == b.kind && a.text == b.text && a.selectionStart == b.selectionStart &&
		a.selectionLength == b.selectionLength;
}
static TextInputEvent Sentinel() {
	TextInputEvent value; value.kind = TextInputKind::Preedit; value.text = "prior buffer";
	value.selectionStart = 2; value.selectionLength = 3; return value;
}
static std::string Encode(std::uint32_t scalar) {
	std::string text;
	if (scalar < 0x80) text += static_cast<char>(scalar);
	else if (scalar < 0x800) {
		text += static_cast<char>(0xc0 | (scalar >> 6)); text += static_cast<char>(0x80 | (scalar & 63));
	} else if (scalar < 0x10000) {
		text += static_cast<char>(0xe0 | (scalar >> 12)); text += static_cast<char>(0x80 | ((scalar >> 6) & 63));
		text += static_cast<char>(0x80 | (scalar & 63));
	} else {
		text += static_cast<char>(0xf0 | (scalar >> 18)); text += static_cast<char>(0x80 | ((scalar >> 12) & 63));
		text += static_cast<char>(0x80 | ((scalar >> 6) & 63)); text += static_cast<char>(0x80 | (scalar & 63));
	}
	return text;
}
static void RejectText(const std::string& text) {
	std::string error; auto event = Sentinel(); const auto saved = event;
	Check(!ValidateTextInputUtf8(text, error) && !error.empty(), "invalid UTF-8 returns a diagnostic");
	Check(!MakeTextInputCommit(text, event, error) && Same(event, saved), "failed commit preserves preedit and selection");
	Check(!MakeTextInputPreedit(text, TextIndexUnit::UnicodeScalars, -1, -1, event, error) && Same(event, saved),
		"failed preedit preserves the prior event");
}
static void RejectSelection(const std::string& text, TextIndexUnit unit, std::int64_t start, std::int64_t length) {
	std::string error; auto event = Sentinel(); const auto saved = event;
	Check(!MakeTextInputPreedit(text, unit, start, length, event, error) && !error.empty(), "invalid selection is rejected");
	Check(Same(event, saved), "invalid selection does not partially replace text, kind or offsets");
}
static void Utf8() {
	std::string error;
	for (std::uint32_t scalar = 0; scalar <= 0x10ffff; ++scalar) {
		const bool valid = scalar != 0 && !(scalar >= 0xd800 && scalar <= 0xdfff);
		Check(ValidateTextInputUtf8(Encode(scalar), error) == valid, "every Unicode scalar boundary and surrogate encoding");
		Check(error.empty() == valid, "scalar validation gives consistent diagnostics");
	}
	for (const auto& text : std::vector<std::string>{
		std::string("a\0b", 3), "\xc0\x80", "\xc1\xbf", "\xe0\x80\x80", "\xf0\x80\x80\x80",
		"\xed\xa0\x80", "\xed\xbf\xbf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff", "\xfe",
		"\x80", "\xbf", "\xc2", "\xe2\x82", "\xf0\x9f\x98", "\xe2\x28\xa1", "\xf0\x9f\x41\x80",
		"\xc2\xc2", "valid\x80", std::string(TextInputMaxBytes + 1, 'x')}) RejectText(text);
	for (const auto& text : std::vector<std::string>{"", "\t\n\r\x01\x7f", "\xef\xbb\xbf", "\xef\xbf\xbf",
		"e\xcc\x81", "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb", "\xf0\x9f\x87\xac\xf0\x9f\x87\xa7",
		"\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a", "\xe0\xa4\x95\xe0\xa5\x8d\xe0\xa4\xb7",
		"Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 \xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82",
		std::string(TextInputMaxBytes, 'x'), std::string(TextInputMaxBytes - 4, 'x') + Encode(0x1f600)}) {
		auto event = Sentinel(); error = "stale error";
		Check(MakeTextInputCommit(text, event, error) && error.empty(), "valid multi-scalar commit succeeds");
		Check(event.kind == TextInputKind::Commit && event.text == text && !event.selectionStart && !event.selectionLength,
			"transport retains exact bytes without normalization, line filtering or scalar splitting");
		Check(ValidateTextInputEvent(event, error), "builder produces a canonical commit");
	}
	RejectText(std::string(TextInputMaxBytes - 3, 'x') + Encode(0x1f600));
	RejectText(std::string(TextInputMaxBytes - 3, 'x') + std::string("\xf0\x9f\x98"));
	// Source views are allowed to reference the event being replaced.
	auto event = Sentinel(); const auto prior = event.text;
	Check(MakeTextInputCommit(event.text, event, error) && event.text == prior, "commit builder supports aliased source storage");
	Check(MakeTextInputPreedit(event.text, TextIndexUnit::Utf8Bytes, 1, 2, event, error) && event.text == prior,
		"preedit builder supports aliased source storage");
}
static void Selections() {
	const std::vector<std::uint32_t> scalars{ 'A', 0xa2, 0x20ac, 0x1f600, 'e', 0x301, 'Z' };
	std::string text;
	std::vector<std::size_t> bytes{0}, utf16{0};
	for (const auto scalar : scalars) {
		text += Encode(scalar); bytes.push_back(text.size()); utf16.push_back(utf16.back() + (scalar > 0xffff ? 2 : 1));
	}
	for (const auto units : {TextIndexUnit::Utf8Bytes, TextIndexUnit::UnicodeScalars, TextIndexUnit::Utf16CodeUnits}) {
		const auto unitOffset = [&](std::size_t index) { return units == TextIndexUnit::Utf8Bytes ? bytes[index] :
			units == TextIndexUnit::Utf16CodeUnits ? utf16[index] : index; };
		for (std::size_t begin = 0; begin < bytes.size(); ++begin) {
			for (std::size_t end = begin; end < bytes.size(); ++end) {
				auto event = Sentinel(); std::string error = "old";
				Check(MakeTextInputPreedit(text, units, unitOffset(begin), unitOffset(end)-unitOffset(begin), event, error),
					"every scalar-aligned range projects from each platform index unit");
				Check(event.text == text && event.kind == TextInputKind::Preedit && event.selectionStart == bytes[begin] &&
					event.selectionLength == bytes[end]-bytes[begin] && error.empty(), "projection produces exact byte selection");
				Check(ValidateTextInputEvent(event, error), "every projected range is canonical");
			}
			auto event = Sentinel(); std::string error;
			Check(MakeTextInputPreedit(text, units, unitOffset(begin), -1, event, error) && event.selectionStart == bytes[begin] &&
				!event.selectionLength, "known caret with unknown selection length stays independently unknown");
		}
		for (std::size_t index = 0; index <= unitOffset(scalars.size()) + 1; ++index) {
			bool boundary = false;
			for (std::size_t i = 0; i < bytes.size(); ++i) boundary |= index == unitOffset(i);
			if (!boundary) {
				RejectSelection(text, units, index, 0);
				RejectSelection(text, units, 0, index);
			}
		}
		std::string error; auto event = Sentinel();
		Check(MakeTextInputPreedit(text, units, -1, -1, event, error) && !event.selectionStart && !event.selectionLength,
			"fully unknown preedit selection stays unknown");
		for (const auto bad : {std::int64_t(-2), (std::numeric_limits<std::int64_t>::min)(), (std::numeric_limits<std::int64_t>::max)()}) {
			RejectSelection(text, units, bad, 0); RejectSelection(text, units, 0, bad);
			RejectSelection(text, units, bad, bad);
		}
		RejectSelection(text, units, -1, 0); RejectSelection(text, units, -1, 1);
		RejectSelection(text, units, unitOffset(scalars.size()), 1);
	}
	RejectSelection(text, static_cast<TextIndexUnit>(77), 0, 0);
	RejectSelection("", TextIndexUnit::UnicodeScalars, 1, 0);
	std::string error; auto event = Sentinel();
	Check(MakeTextInputPreedit("", TextIndexUnit::UnicodeScalars, 0, 0, event, error) && event.text.empty() &&
		event.kind == TextInputKind::Preedit, "empty preedit is still preedit, never a synthesized commit");
	Check(MakeTextInputPreedit("", TextIndexUnit::UnicodeScalars, -1, -1, event, error), "empty unknown preedit is valid");
	MakeTextInputCancel(event);
	Check(event.kind == TextInputKind::CancelComposition && event.text.empty() && !event.selectionStart && !event.selectionLength,
		"explicit cancellation clears transient preedit without creating committed text");
}
static void CanonicalEvents() {
	std::string error; auto event = Sentinel();
	Check(ValidateTextInputEvent(event, error), "sentinel is canonical");
	event.kind = static_cast<TextInputKind>(999);
	Check(!ValidateTextInputEvent(event, error), "unknown kind is not a text event");
	for (const auto kind : {TextInputKind::Commit, TextInputKind::CancelComposition}) {
		event = {}; event.kind = kind; event.selectionLength = 0;
		Check(!ValidateTextInputEvent(event, error), "non-preedit rejects even zero-length selection metadata");
		event.selectionLength.reset(); event.selectionStart = 0;
		Check(!ValidateTextInputEvent(event, error), "non-preedit rejects even zero caret metadata");
	}
	event = {}; event.text = "not cancellation";
	Check(!ValidateTextInputEvent(event, error), "cancel event cannot smuggle a commit");
	event = {}; event.kind = TextInputKind::Preedit; event.text = Encode(0x1f600);
	event.selectionLength = 0;
	Check(!ValidateTextInputEvent(event, error), "canonical length needs a known start");
	event.selectionStart = 1;
	Check(!ValidateTextInputEvent(event, error), "canonical byte start cannot split a scalar");
	event.selectionStart = 0; event.selectionLength = 1;
	Check(!ValidateTextInputEvent(event, error), "canonical byte end cannot split a scalar");
	event.selectionLength = (std::numeric_limits<std::size_t>::max)();
	Check(!ValidateTextInputEvent(event, error), "canonical byte length overflow is rejected");
	event.selectionStart = (std::numeric_limits<std::size_t>::max)(); event.selectionLength = 0;
	Check(!ValidateTextInputEvent(event, error), "canonical byte start overflow is rejected");
	event = {}; error = "old";
	Check(ValidateTextInputEvent(event, error) && error.empty(), "default cancellation is canonical and clears errors");
}
int main() {
	Utf8(); Selections(); CanonicalEvents();
	std::printf("Text input codec: %u checks passed (scalar transport only; no grapheme, shaping or live IME claim)\n", checks);
}
