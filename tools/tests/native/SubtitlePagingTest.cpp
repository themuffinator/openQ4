// Copyright (C) 2026 openQ4 contributors.
#include "src/game/gamesys/SubtitlePaging.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace OpenQ4Subtitles;
static int Glyph(const char* p, int n, float& width) {
	width = 1;
	return static_cast<unsigned char>(*p) >= 0xc0 && n > 1 ? 2 : 1;
}
int main() {
	assert(CanvasBottom(16.0f/9, false) == 570 && CanvasBottom(16.0f/9, true) == 640);
	for (float aspect : {4.0f/3, 9.0f/16}) {
		const float bottom = CanvasBottom(aspect, true);
		assert(bottom < 360 + 202.5f * aspect && bottom > 360);
		assert(bottom - (12 + 2 * 34 * 1.4f * 2) > 360 - 202.5f * aspect);
	}
	// Portrait cinematics: when two lines fit under the artwork, the panel hangs
	// 12 below it, clear of every title row Awakening's intro draws in the
	// picture (GUI y 270-310) and in its lower band (GUI y 430-470). When only
	// one fits, a one-line panel (a lone caption paged a line at a time) does too.
	for (float aspect : {9.0f/16, 10.0f/16, 3.0f/4, 0.9f}) {
		const float artworkTop = 360 - 270 * aspect, artworkBottom = 360 + 270 * aspect;
		for (float pitch : {17 * 1.4f, 17 * 1.6f * 1.4f, 17 * 2 * 1.4f, 17 * 2 * 2 * 1.4f}) {
			const int fit = artworkBottom + 24 + 2 * pitch <= 708 ? 2 : artworkBottom + 24 + pitch <= 708 ? 1 : 0;
			assert(LinesBelowArtwork(aspect, pitch) == fit);
			for (int lines : {1, 2}) {
				const float bottom = CanvasBottom(aspect, true, pitch, lines);
				const float top = bottom - (12 + pitch * lines);
				if (fit == 2 || (fit == 1 && lines == 1)) {
					assert(std::fabs(top - (artworkBottom + 12)) < 0.01f && bottom <= 708.01f);
					for (float y : {270.0f, 310.0f, 430.0f, 470.0f})
						assert(artworkTop + (artworkBottom - artworkTop) * y / 480 < top);
				} else {
					assert(bottom == CanvasBottom(aspect, true)); // too tall below: stays in the picture
				}
			}
		}
	}
	assert(CanvasBottom(9.0f/16, true, 17 * 1.4f, 2) <= 708 && CanvasBottom(9.0f/16, true, 17 * 2 * 2 * 1.4f, 2) == CanvasBottom(9.0f/16, true));
	// Two speakers where one line fits below: the second panel sits 12 above the artwork,
	// on screen, in room equal to the room below.
	for (float aspect : {9.0f/16, 3.0f/4}) {
		const float pitch = 17 * 2 * 2 * 1.4f, artworkTop = 360 - 270 * aspect, artworkBottom = 360 + 270 * aspect;
		const float above = CanvasAboveBottom(aspect);
		assert(std::fabs(above - (artworkTop - 12)) < 0.01f && above - (12 + pitch) >= 12);
		assert(std::fabs((above - 24) - (708 - artworkBottom - 24)) < 0.01f); // text room: margin and padding each side
	}
	// A speaker label indents the words by its whole width, or is left out past half the line.
	assert(SpeakerIndent(100, 340) == 100 && SpeakerIndent(170, 340) == 170 && SpeakerIndent(171, 340) == 0);
	assert(SpeakerIndent(0, 340) == 0 && SpeakerIndent(std::numeric_limits<float>::quiet_NaN(), 340) == 0 &&
	       SpeakerIndent(100, std::numeric_limits<float>::infinity()) == 0);
	// The largest text (both scales at 2) at 9:16 and 3:4 fits one line below the picture, not two.
	assert(LinesBelowArtwork(9.0f/16, 17 * 2 * 2 * 1.4f) == 1 && LinesBelowArtwork(3.0f/4, 17 * 2 * 2 * 1.4f) == 1);
	assert(CanvasBottom(9.0f/16, true, 17 * 2 * 2 * 1.4f, 1) <= 708 && CanvasBottom(9.0f/16, true, 17 * 2 * 2 * 1.4f, 1) > 360 + 270 * 9.0f/16);
	assert(LinesBelowArtwork(4.0f/3, 17 * 1.4f) == 0 && LinesBelowArtwork(16.0f/9, 17 * 1.4f) == 0);
	assert(LinesBelowArtwork(9.0f/16, std::numeric_limits<float>::quiet_NaN()) == 0 && LinesBelowArtwork(9.0f/16, -5) == 0);
	// 4:3 and wider, gameplay, and an unusable pitch keep the earlier placement.
	assert(CanvasBottom(4.0f/3, true, 17 * 1.4f, 2) == CanvasBottom(4.0f/3, true));
	assert(CanvasBottom(16.0f/9, true, 17 * 1.4f, 2) == 640 && CanvasBottom(9.0f/16, false, 17 * 1.4f, 2) == 570);
	assert(CanvasBottom(9.0f/16, true, std::numeric_limits<float>::quiet_NaN(), 2) == CanvasBottom(9.0f/16, true));
	assert(CanvasBottom(9.0f/16, true, -5, 2) == CanvasBottom(9.0f/16, true));
	assert(CanvasBottom(std::numeric_limits<float>::quiet_NaN(), true, 17 * 1.4f, 2) == 640);
	auto pages = Paginate("one two three four five six", 7, 2, Glyph);
	assert(pages.size() == 3 && pages[0].lines[0] == "one two" && pages[0].lines[1] == "three");
	assert(pages[1].lines[0] == "four" && pages[1].lines[1] == "five");
	assert(PageAt(pages, -5, 1000) == 0 && PageAt(pages, 1000, 1000) == 2);
	int last = 0;
	for (int time = 0; time < 1000; ++time) { int at = PageAt(pages,time,1000); assert(at >= last); last=at; }
	assert(Paginate(" \n\t\r ", 8, 2, Glyph).empty());
	assert(Paginate("text", 0, 1, Glyph).empty());
	assert(Paginate("text", std::numeric_limits<float>::quiet_NaN(), 1, Glyph).empty());
	assert(PageAt({}, 5, 100) == -1 && PageAt(pages, 5, 0) == -1);
	pages = Paginate("abc\ndef\r\nghi", 20, 2, Glyph);
	assert(pages.size() == 2 && pages[0].lines[1] == "def" && pages[1].lines[0] == "ghi");
	pages = Paginate("abcdefghijk", 3, 1, Glyph);
	assert(pages.size() == 4 && pages[0].lines[0] == "abc" && pages[3].lines[0] == "jk");
	pages = Paginate("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", 2, 1, Glyph);
	assert(pages.size() == 3 && pages[0].lines[0].size() == 4 && pages[2].lines[0].size() == 2);
	pages = Paginate("abc", .1f, 2, Glyph); // Even an oversized glyph makes progress.
	assert(pages.size() == 2 && pages[0].lines[0] == "a" && pages[0].lines[1] == "b");
	pages = Paginate(std::string(10000, 'x').c_str(), 80, 2, Glyph);
	assert(pages.back().end == 4096 && pages.size() < 256);
	pages = Paginate("abcdef", 2, 1, [](const char*,int,float& w) { w=1;return 0; });
	assert(pages.size() == 3); // Malformed input cannot stall pagination.
	// Stock lip-sync text: facial-animation cues read as stage directions (#225),
	// {idle} drops out, and the stock lines' runs of spaces close up.
	const auto cues = [](const char* text) { return FormatCues(text); };
	assert(cues("{furrow}Shit. Okay, let's...") == "(Furrow) Shit. Okay, let's...");
	assert(cues("{puzzled}You're Kane, right? {idle}I'm Anderson.") == "(Puzzled) You're Kane, right? I'm Anderson.");
	assert(cues("What the hell?  {scared}They got the main power!") == "What the hell? (Scared) They got the main power!");
	assert(cues("Hey, {puzzled}didn't I see you?") == "Hey, (Puzzled) didn't I see you?");
	assert(cues("You are{confused}... Corporal Kane") == "You are (Confused)... Corporal Kane");
	assert(cues("{Puzzled}What?") == "(Puzzled) What?" && cues("{PAIN}Ugh") == "(Pain) Ugh");
	assert(cues("{idle}Hello {idle}") == "Hello" && cues("word{idle}word") == "word word" && cues("Wait{idle}.") == "Wait.");
	assert(cues("{happy}{idle}Done") == "(Happy) Done" && cues("Fine {happy}") == "Fine (Happy)");
	assert(cues("{wry_smile}Sure") == "(Wry smile) Sure");
	assert(cues("Okay . . .  so") == "Okay . . . so" && cues("  padded  ") == "padded");
	// Only a short run of letters is a cue; other brace text and line breaks stay.
	assert(cues("{} {1} {two words} {a-b}") == "{} {1} {two words} {a-b}");
	assert(cues("{abcdefghijklmnopqrstuvwxyz}") == "{abcdefghijklmnopqrstuvwxyz}");
	assert(cues("{abcdefghijklmnopqrstuvwx}") == "(Abcdefghijklmnopqrstuvwx)");
	assert(cues("{furrow") == "{furrow" && cues("{") == "{");
	assert(cues("one  \ntwo {scared}three") == "one\ntwo (Scared) three");
	assert(cues("caf\xc3\xa9 {happy}ol\xc3\xa9") == "caf\xc3\xa9 (Happy) ol\xc3\xa9");
	assert(cues(nullptr).empty() && cues("").empty() && cues("{idle}").empty());
	assert(cues(std::string(10000, 'x').c_str()).size() == 4096);
	std::puts("Subtitle pagination: wrapping, timing, overlap rows, UTF-8, bounds, cue formatting and cinematic placement passed");
}
