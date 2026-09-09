// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openq4::ui {

// A transport bound, not a field's editing or line-length policy. No truncation,
// Unicode normalization, control filtering, shaping or grapheme segmentation.
inline constexpr std::size_t TextInputMaxBytes = 65536;

enum class TextInputKind { Commit, Preedit, CancelComposition };
enum class TextIndexUnit { Utf8Bytes, UnicodeScalars, Utf16CodeUnits };

struct TextInputEvent {
	TextInputKind kind = TextInputKind::CancelComposition;
	std::string text;
	// Preedit selection in UTF-8 bytes. Unknown start implies unknown length;
	// a known caret with unknown selection length is represented independently.
	// These are scalar boundaries, not necessarily grapheme/caret boundaries.
	std::optional<std::size_t> selectionStart;
	std::optional<std::size_t> selectionLength;
};

// Rejects NUL, malformed/overlong UTF-8, surrogate encodings and oversize input.
// Other Unicode scalars (including newlines and controls) remain unchanged for
// the eventual field policy. Empty strings are valid. Errors are diagnostics,
// not localized product copy. Every successful bool API clears error. The error
// string must not alias the input text or any member of the output event.
bool ValidateTextInputUtf8(std::string_view text, std::string& error);
bool ValidateTextInputEvent(const TextInputEvent& event, std::string& error);

// Builders publish only a complete valid candidate; out is unchanged on failure.
// Commit is independent of preedit; clearing preedit never synthesizes a commit.
bool MakeTextInputCommit(std::string_view text, TextInputEvent& out, std::string& error);
bool MakeTextInputPreedit(std::string_view text, TextIndexUnit units,
	std::int64_t start, std::int64_t length, TextInputEvent& out, std::string& error);
void MakeTextInputCancel(TextInputEvent& out);

// The preedit builder takes explicit platform units, never guesses from text.
// -1 means unknown; a known length requires a known start. All known endpoints
// must be in range and on a Unicode scalar boundary. UTF-16 offsets splitting
// a surrogate pair and byte offsets splitting UTF-8 are rejected. This is a
// logical value codec only: no event wire ABI, owner lease or native IME routing.

} // namespace openq4::ui
