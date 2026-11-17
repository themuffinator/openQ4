// openQ4 additions to the SDK game sources; see src/game/LICENSE.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// The caller measures actual font advances and decodes UTF-8/legacy text.
// Pages contain at most two lines. Timing is proportional to text coverage,
// not word alignment: retail lipSync declarations do not supply word times.
namespace OpenQ4Subtitles {
// Cinematic letterboxes occupy y=0..60 and 420..480 in the centred 640x480
// artwork below 4:3, and captions keep to the lower third of that picture. A
// narrower screen (portrait) has room under the artwork, which spans 360 +/-
// 270 * aspect of the 720-high canvas. That picture is too short for enlarged
// captions, which then covered the title text cinematics draw in it, so when
// two lines of the given pitch fit there captions hang from the artwork's
// lower edge instead. The panel is 12 + pitch * lines high; deciding on two
// lines keeps the position still while a caption's line count changes. When
// only one line fits, the overlay pages a lone caption a line at a time so it
// can stay there, and a second speaker takes the same room above the artwork.
inline int LinesBelowArtwork(float aspect, float pitch) {
	if (!(aspect <= 4.0f / 3.0f) || !std::isfinite(pitch) || pitch <= 0.0f) return 0;
	const float room = 708.0f - (360.0f + 270.0f * aspect + 12.0f) - 12.0f;
	return room >= 2.0f * pitch ? 2 : room >= pitch ? 1 : 0;
}
inline float CanvasBottom(float aspect, bool cinematic, float pitch = 0.0f, int lines = 1) {
	if (!cinematic) return 570.0f;
	if (!(aspect <= 4.0f / 3.0f)) return 640.0f;
	const int fit = LinesBelowArtwork(aspect, pitch);
	if (fit == 2 || (fit == 1 && lines <= 1))
		return 360.0f + 270.0f * aspect + 24.0f + pitch * std::clamp(lines, 1, 2);
	return 348.0f + 202.5f * aspect;
}
// The bottom of the one-line panel above a portrait cinematic's artwork, 12 above
// it; the room there matches the room below.
inline float CanvasAboveBottom(float aspect) {
	return 360.0f - 270.0f * aspect - 12.0f;
}
// The indent a speaker label takes: its whole measured width, so the words never
// run under it, or none, leaving the label out, where it would take more than half
// the line (the largest text on a narrow screen).
inline float SpeakerIndent(float label, float width) {
	if (!std::isfinite(label) || !std::isfinite(width) || label <= 0.0f || label > width * 0.5f) return 0.0f;
	return label;
}
// Quake 4's dialogue text carries the facial-animation cues its lip-sync
// system reads, such as "{furrow}" or "{scared}", sometimes mid-sentence. A
// caption shows each as a capitalised stage direction, "(Furrow)", set off by
// one space, drops the neutral "{idle}" (it only returns the face to rest),
// and collapses the runs of spaces the stock lines carry (#225). Only a
// brace pair around 1-24 ASCII letters or underscores is a cue; any other
// brace text is kept as written, and so are line breaks.
inline std::string FormatCues(const char* text) {
	std::string out;
	if (!text) return out;
	const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
	const auto closing = [](char c) { return c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':' || c == ')'; };
	// Whether the text so far ends a word, rather than at its start, a space or a line break.
	const auto afterWord = [&out]() { return !out.empty() && out.back() != ' ' && out.back() != '\n' && out.back() != '\r'; };
	bool spaceBefore = false; // the next word after a cue keeps its distance from it
	for (std::size_t i = 0; i < 4096 && text[i];) {
		const char c = text[i];
		if (c == '{') {
			std::size_t letters = 0;
			while (letters < 25 && letter(text[i + 1 + letters])) ++letters;
			if (letters >= 1 && letters <= 24 && text[i + 1 + letters] == '}') {
				std::string cue(text + i + 1, letters);
				for (char& ch : cue) ch = ch == '_' ? ' ' : (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
				i += letters + 2;
				if (cue == "idle") { spaceBefore = spaceBefore || afterWord(); continue; }
				if (afterWord()) out += ' ';
				out += '(';
				out += static_cast<char>(cue[0] >= 'a' && cue[0] <= 'z' ? cue[0] - 'a' + 'A' : cue[0]);
				out.append(cue, 1, std::string::npos);
				out += ')';
				spaceBefore = true;
				continue;
			}
		}
		++i;
		if (c == ' ' || c == '\t') {
			if (afterWord()) out += ' ';
			spaceBefore = false;
			continue;
		}
		if (c == '\n' || c == '\r') {
			while (!out.empty() && out.back() == ' ') out.pop_back();
			out += c;
			spaceBefore = false;
			continue;
		}
		if (spaceBefore && !closing(c) && afterWord()) out += ' ';
		spaceBefore = false;
		out += c;
	}
	while (!out.empty() && out.back() == ' ') out.pop_back();
	return out;
}
struct Page { std::string lines[2]; int end = 0; };
template<class Glyph> std::vector<Page> Paginate(const char* text, float width, int rows, Glyph glyph) {
	std::vector<Page> pages;
	if (!text || !std::isfinite(width) || width <= 0) return pages;
	std::size_t length = 0;
	while (length < 4096 && text[length]) ++length;
	std::string source(text, length);
	rows = std::clamp(rows, 1, 2);
	std::size_t pos = 0;
	while (pos < source.size() && pages.size() < 256) {
		Page page;
		for (int row = 0; row < rows && pos < source.size(); ++row) {
			while (pos < source.size() && static_cast<unsigned char>(source[pos]) <= ' ') ++pos;
			const auto begin = pos;
			auto lastSpace = pos;
			float used = 0;
			while (pos < source.size()) {
				if (source[pos] == '\n' || source[pos] == '\r') break;
				float advance = 0;
				int bytes = glyph(source.c_str() + pos, static_cast<int>(source.size() - pos), advance);
				bytes = std::clamp(bytes, 1, static_cast<int>(source.size() - pos));
				advance = std::isfinite(advance) ? (std::max)(0.0f, advance) : width;
				if (used + advance > width && pos > begin) {
					if (lastSpace > begin && source[pos] != ' ' && source[pos] != '\t') pos = lastSpace;
					break;
				}
				if (source[pos] == ' ' || source[pos] == '\t') lastSpace = pos;
				used += advance;
				pos += bytes;
			}
			auto end = pos;
			while (end > begin && static_cast<unsigned char>(source[end - 1]) <= ' ') --end;
			page.lines[row] = source.substr(begin, end - begin);
		}
		page.end = static_cast<int>(pos);
		if (!page.lines[0].empty() || !page.lines[1].empty()) pages.push_back(page);
	}
	return pages;
}
inline int PageAt(const std::vector<Page>& pages, int time, int duration) {
	if (pages.empty() || duration <= 0) return -1;
	const long long offset = static_cast<long long>(std::clamp(time, 0, duration - 1)) * pages.back().end / duration;
	for (std::size_t i = 0; i < pages.size(); ++i) if (offset < pages[i].end) return static_cast<int>(i);
	return static_cast<int>(pages.size() - 1);
}
}
