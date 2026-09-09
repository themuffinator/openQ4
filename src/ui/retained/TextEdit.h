// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "TextInput.h"
#include <vector>

namespace openq4::ui {

struct TextEditPolicy {
	std::size_t maxBytes = TextInputMaxBytes;
	bool multiline = false;
	bool allowTab = false;
};
struct TextEditState {
	std::string text;
	std::size_t anchor = 0, caret = 0;
};

// Local edit buffer only. It never changes an accepted setting, reads a device
// or clipboard, shapes text, or owns a native composition lease. Offsets are
// UTF-8 scalar boundaries. The layout/interaction adapter must supply legal
// grapheme and visual caret stops from the same text run used for drawing.
class TextEditBuffer {
public:
	static constexpr std::size_t MaxHistoryEntries = 64;
	static constexpr std::size_t MaxHistoryTextBytes = 1024 * 1024;
	bool Reset(std::string_view text, const TextEditPolicy& policy, std::string& error);
	const TextEditState& State() const { return state; }
	const TextEditPolicy& Policy() const { return policy; }
	const std::optional<TextInputEvent>& Composition() const { return composition; }
	// A non-consuming presentation string; selection/preedit ranges remain
	// available separately for a shared-run caret/selection renderer.
	std::string PresentedText() const;
	bool SetSelection(std::size_t anchor, std::size_t caret, std::string& error);
	// Explicit editing operation: an empty replacement deletes the selection.
	// Multiline replacements normalize CRLF/CR to LF and preserve Unicode NEL,
	// line separator and paragraph separator scalars. Single-line fields reject
	// line breaks atomically; no silent truncation or partial insertion.
	bool ReplaceSelection(std::string_view replacement, std::string& error);
	// Empty Commit is a no-op on text, clearing preedit only. Empty Preedit also
	// clears preedit. A nonempty commit replaces the captured selection once.
	bool Apply(const TextInputEvent& event, std::string& error);
	void CancelComposition();
	bool Undo(std::string& error);
	bool Redo(std::string& error);
	bool CanUndo() const { return !composition && !undo.empty(); }
	bool CanRedo() const { return !composition && !redo.empty(); }
	std::size_t HistoryEntries() const { return undo.size() + redo.size(); }
	// Payload accounting, not an allocator/resident-memory measurement.
	std::size_t HistoryTextBytes() const;
private:
	bool Replace(std::string_view replacement, bool fromCommit, std::string& error);
	void TrimHistory();
	TextEditPolicy policy;
	TextEditState state;
	std::optional<TextInputEvent> composition;
	std::vector<TextEditState> undo, redo;
};

enum class TextNumberStatus { Valid, Empty, Incomplete, Invalid, OutOfRange, InvalidPolicy };
struct TextNumberPolicy {
	double minimum = 0, maximum = 1;
	bool exponent = true;
};
// Locale-independent finite decimal parsing. Sign/dot/exponent prefixes remain
// editable but cannot become proposals. Values are never clamped or quantized;
// success preserves an in-range off-step value. The caller localizes status.
// Failure leaves value unchanged. It never changes the editor's buffer.
TextNumberStatus ParseTextNumber(std::string_view text, const TextNumberPolicy& policy, double& value);
// Formatting honors the exponent syntax policy. It preserves finite accepted
// readbacks even outside the current bounds, allowing the field to show/edit
// them instead of silently clamping them. It does not validate a proposal.
bool FormatTextNumber(double value, const TextNumberPolicy& policy, std::string& text, std::string& error);

} // namespace openq4::ui
