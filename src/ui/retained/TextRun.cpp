// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "TextRun.h"
#include "TextInput.h"

#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <list>

namespace openq4::ui {

bool TextRun::CaretPosition(std::size_t byteOffset, float& position) const {
	const auto it = std::lower_bound(carets.begin(), carets.end(), byteOffset,
		[](const TextRunCaret& caret, std::size_t byte) { return caret.byteOffset < byte; });
	if (it == carets.end() || it->byteOffset != byteOffset) return false;
	position = it->x;
	return true;
}

std::optional<std::size_t> TextRun::HitTestLtr(float position) const {
	if (!monotonicLtr || !std::isfinite(position) || carets.empty()) return {};
	const auto right = std::upper_bound(carets.begin(), carets.end(), position,
		[](float x, const TextRunCaret& caret) { return x < caret.x; });
	if (right == carets.begin()) return right->byteOffset;
	if (right == carets.end()) return carets.back().byteOffset;
	const auto left = right - 1;
	if (position - left->x < right->x - position) return left->byteOffset;
	const auto afterEqual = std::upper_bound(right, carets.end(), right->x,
		[](float x, const TextRunCaret& caret) { return x < caret.x; });
	return (afterEqual - 1)->byteOffset;
}

float WalkTextRun(std::string_view text, float letterSpacing, const TextGlyphLookup& lookup,
	const TextGlyphVisitor& visitor) {
	float width = 0;
	// std::string_view's default data pointer may be null for an empty string.
	if (text.empty()) return width;
	for (Rml::StringIteratorU8 it(Rml::StringView(text.data(), text.data() + text.size())); it;) {
		TextRunGlyph record;
		record.byteStart = static_cast<std::size_t>(it.offset());
		record.codepoint = static_cast<std::uint32_t>(*it);
		record.glyph = lookup(record.codepoint);
		record.penX = width;
		++it;
		record.byteEnd = static_cast<std::size_t>(it.offset());
		if (visitor) visitor(record);
		width += record.glyph.advance + letterSpacing;
	}
	return width;
}

std::shared_ptr<const TextRun> MeasureTextRun(std::string_view text, float letterSpacing,
	const TextGlyphLookup& lookup, std::string& error) {
	if (!ValidateTextInputUtf8(text, error)) return {};
	if (!lookup || !std::isfinite(letterSpacing)) { error = "Invalid text run font or spacing"; return {}; }
	auto run = std::make_shared<TextRun>();
	run->text = text;
	run->carets.push_back({0, 0});
	bool finite = true;
	run->width = WalkTextRun(text, letterSpacing, lookup, [&](const TextRunGlyph& record) {
		const auto& g = record.glyph;
		for (const float value : {g.advance, g.left, g.top, g.width, g.height, g.u0, g.v0, g.u1, g.v1, record.penX})
			finite &= std::isfinite(value);
		const float next = record.penX + (g.advance + letterSpacing);
		finite &= std::isfinite(next);
		run->monotonicLtr &= next >= record.penX;
		run->glyphs.push_back(record);
		run->carets.push_back({record.byteEnd, next});
	});
	const double rounded = std::round(static_cast<double>(run->width));
	if (!finite || !std::isfinite(rounded) || rounded < (std::numeric_limits<int>::min)() || rounded > (std::numeric_limits<int>::max)()) {
		error = "Text run metrics or rounded width are not representable";
		return {};
	}
	run->roundedWidth = static_cast<int>(std::lround(run->width));
	error.clear();
	return run;
}

struct TextRunCache::Impl {
	struct Entry { std::uintptr_t face; float spacing; std::shared_ptr<const TextRun> run; std::size_t bytes; };
	std::list<Entry> entries;
	std::size_t maxEntries, maxBytes, bytes = 0;
	Impl(std::size_t count, std::size_t budget) : maxEntries(count), maxBytes(budget) {}
	static std::size_t Charge(const TextRun& run) {
		std::size_t result = sizeof(Entry) + sizeof(TextRun) + run.text.capacity() + 1 +
			run.glyphs.capacity() * sizeof(TextRunGlyph) + run.carets.capacity() * sizeof(TextRunCaret);
		for (const auto& record : run.glyphs) result += record.glyph.material.capacity() + 1;
		return result;
	}
};

TextRunCache::TextRunCache(std::size_t maxEntries, std::size_t maxResidentBytes) : impl(std::make_unique<Impl>(maxEntries, maxResidentBytes)) {}
TextRunCache::~TextRunCache() = default;
std::shared_ptr<const TextRun> TextRunCache::Get(std::uintptr_t face, std::string_view text, float letterSpacing,
	const TextGlyphLookup& lookup, std::string& error) {
	if (face == 0 || !lookup || !std::isfinite(letterSpacing)) { error = "Invalid text run font or spacing"; return {}; }
	for (auto it = impl->entries.begin(); it != impl->entries.end(); ++it) {
		if (it->face == face && it->spacing == letterSpacing && it->run->text == text) {
			impl->entries.splice(impl->entries.begin(), impl->entries, it);
			error.clear();
			return impl->entries.front().run;
		}
	}
	auto run = MeasureTextRun(text, letterSpacing, lookup, error);
	if (!run) return {};
	const auto bytes = Impl::Charge(*run);
	if (impl->maxEntries == 0 || bytes > impl->maxBytes) return run;
	while (!impl->entries.empty() && (impl->entries.size() >= impl->maxEntries || bytes > impl->maxBytes - impl->bytes)) {
		impl->bytes -= impl->entries.back().bytes;
		impl->entries.pop_back();
	}
	impl->entries.push_front({face, letterSpacing, run, bytes});
	impl->bytes += bytes;
	return run;
}
void TextRunCache::Clear() { impl->entries.clear(); impl->bytes = 0; }
std::size_t TextRunCache::ResidentEntries() const { return impl->entries.size(); }
std::size_t TextRunCache::ResidentBytes() const { return impl->bytes; }

} // namespace openq4::ui
