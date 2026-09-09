// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include "Runtime.h"
#include <functional>
#include <optional>
#include <string_view>

namespace openq4::ui {

struct TextRunGlyph {
	std::size_t byteStart = 0, byteEnd = 0;
	std::uint32_t codepoint = 0;
	float penX = 0;
	Glyph glyph;
};
struct TextRunCaret { std::size_t byteOffset = 0; float x = 0; };

// A single unshaped, left-to-right scalar run. These byte boundaries are NOT
// grapheme, bidi or shaping clusters. Returned runs are immutable measurements;
// glyph bearings, advances, spacing and final rounding match font submission.
struct TextRun {
	std::string text;
	std::vector<TextRunGlyph> glyphs;
	std::vector<TextRunCaret> carets;
	float width = 0;
	int roundedWidth = 0;
	bool monotonicLtr = true;
	bool CaretPosition(std::size_t byteOffset, float& position) const;
	// Nearest scalar caret; midpoint ties choose the later caret. Equal-position
	// carets choose the last boundary. Nonfinite/nonmonotonic runs are unavailable.
	std::optional<std::size_t> HitTestLtr(float position) const;
};
using TextGlyphLookup = std::function<Glyph(std::uint32_t)>;
using TextGlyphVisitor = std::function<void(const TextRunGlyph&)>;

// Existing Rml UTF-8 iteration/float placement, without retaining a whole run.
// This intentionally has no editing byte limit or new UTF-8 policy: legacy/raw
// markup rendering uses it when the strict editable run is unavailable.
float WalkTextRun(std::string_view text, float letterSpacing, const TextGlyphLookup& lookup,
	const TextGlyphVisitor& visitor = {});

// Strict bounded editing query: NUL-free scalar UTF-8 <= TextInputMaxBytes,
// finite glyph metrics/positions and representable rounded width. No partial
// run is published on failure. Error is diagnostic and must not alias text.
std::shared_ptr<const TextRun> MeasureTextRun(std::string_view text, float letterSpacing,
	const TextGlyphLookup& lookup, std::string& error);

// Engine-thread cache keyed by face identity, exact text and letter spacing.
// Clear with every face/font resource release. Returned shared runs can outlive
// eviction; callers must requery after resource changes. The budget charges
// retained run/vector/string capacities and entry storage, excluding allocator
// overhead and runs retained by callers after eviction. Oversize entries are
// measured transiently, never inserted. This is not a total process-memory cap.
class TextRunCache {
public:
	explicit TextRunCache(std::size_t maxEntries = 128, std::size_t maxResidentBytes = 4u * 1024u * 1024u);
	~TextRunCache();
	TextRunCache(const TextRunCache&) = delete;
	TextRunCache& operator=(const TextRunCache&) = delete;
	std::shared_ptr<const TextRun> Get(std::uintptr_t face, std::string_view text, float letterSpacing,
		const TextGlyphLookup& lookup, std::string& error);
	void Clear();
	std::size_t ResidentEntries() const;
	std::size_t ResidentBytes() const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace openq4::ui
