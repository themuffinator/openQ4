// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// The on-screen keyboard's layouts (PAD-05) without the engine: every layer
// is whole, shift gives capitals for each script and leaves the rest alone,
// and every letter each shipped language writes, small and capital, is on a
// key. ASCII source: the alphabets are \u escapes.
#include "src/framework/OnScreenKeyboard.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); std::exit(1); } } while (0)

static void Layers() {
	using namespace openq4;
	CHECK(OSK_KEYS == 44);
	for (int layer = 0; layer < OSK_LAYER_COUNT; ++layer) {
		std::set<char32_t> seen;
		for (int index = 0; index < OSK_KEYS; ++index) {
			const char32_t plain = OskKey(layer, index, false);
			// every key types something printable, once per layer, and encodes
			CHECK(plain >= 0x20 && plain != 0x7F);
			CHECK(seen.insert(plain).second);
			CHECK(!OskUtf8(plain).empty() && !OskUtf8(OskKey(layer, index, true)).empty());
		}
		CHECK(std::strlen(OskLayerName(layer)) > 0);
	}
	// out of range asks for nothing
	CHECK(OskKey(-1, 0, false) == 0 && OskKey(OSK_LAYER_COUNT, 0, false) == 0);
	CHECK(OskKey(OSK_LAYER_LATIN, -1, false) == 0 && OskKey(OSK_LAYER_LATIN, OSK_KEYS, false) == 0);
	CHECK(std::strlen(OskLayerName(OSK_LAYER_COUNT)) == 0);
	// the first row of Latin is the digits, the second starts qwerty
	CHECK(OskKey(OSK_LAYER_LATIN, 0, false) == U'1' && OskKey(OSK_LAYER_LATIN, 9, false) == U'0');
	CHECK(OskKey(OSK_LAYER_LATIN, OSK_COLUMNS, false) == U'q' && OskKey(OSK_LAYER_LATIN, OSK_COLUMNS, true) == U'Q');
	// the layers go round, and a stray value starts over
	int layer = OSK_LAYER_LATIN;
	for (int step = 0; step < OSK_LAYER_COUNT; ++step) {
		layer = OskNextLayer(layer);
	}
	CHECK(layer == OSK_LAYER_LATIN && OskNextLayer(99) == OSK_LAYER_LATIN);
}

static void Shift() {
	using namespace openq4;
	// digits and punctuation do not change with shift
	CHECK(OskUpper(U'1') == U'1' && OskUpper(U'.') == U'.' && OskUpper(U'@') == U'@');
	CHECK(OskUpper(U'a') == U'A' && OskUpper(U'z') == U'Z' && OskUpper(U'A') == U'A');
	CHECK(OskUpper(0x00E9) == 0x00C9);		// e-acute
	CHECK(OskUpper(0x00F7) == 0x00F7);		// the division sign is not a letter
	CHECK(OskUpper(0x00DF) == 0x00DF);		// sharp s keeps its form
	CHECK(OskUpper(0x0105) == 0x0104);		// a-ogonek
	CHECK(OskUpper(0x0142) == 0x0141);		// l-stroke, a pair with the capital odd
	CHECK(OskUpper(0x017A) == 0x0179);		// z-acute
	CHECK(OskUpper(0x0159) == 0x0158);		// r-caron
	CHECK(OskUpper(0x0151) == 0x0150);		// o-double-acute
	CHECK(OskUpper(0x0131) == 0x0130);		// dotless i gives the dotted capital, so Turkish has both
	CHECK(OskUpper(0x0153) == 0x0152);		// oe
	CHECK(OskUpper(0x0430) == 0x0410 && OskUpper(0x044F) == 0x042F);	// Cyrillic a, ya
	CHECK(OskUpper(0x0451) == 0x0401 && OskUpper(0x0456) == 0x0406 && OskUpper(0x0457) == 0x0407 && OskUpper(0x0454) == 0x0404);
	CHECK(OskUpper(0x0491) == 0x0490);
	// a capital stays a capital
	CHECK(OskUpper(0x0104) == 0x0104 && OskUpper(0x0141) == 0x0141 && OskUpper(0x0410) == 0x0410);
}

static void Languages() {
	using namespace openq4;
	static const struct { const char *language; const char32_t *letters; } alphabets[] = {
		{ "german", U"\u00E4\u00F6\u00FC\u00DF" },
		{ "french", U"\u00E0\u00E2\u00E7\u00E9\u00E8\u00EA\u00EB\u00EE\u00EF\u00F4\u00F9\u00FB\u00FC\u0153" },
		{ "italian", U"\u00E0\u00E8\u00E9\u00EC\u00F2\u00F9" },
		{ "spanish", U"\u00E1\u00E9\u00ED\u00F1\u00F3\u00FA\u00FC\u00BF\u00A1" },
		{ "brazilian", U"\u00E1\u00E2\u00E3\u00E0\u00E7\u00E9\u00EA\u00ED\u00F3\u00F4\u00F5\u00FA" },
		{ "polish", U"\u0105\u0107\u0119\u0142\u0144\u00F3\u015B\u017A\u017C" },
		{ "czech", U"\u00E1\u010D\u010F\u00E9\u011B\u00ED\u0148\u00F3\u0159\u0161\u0165\u00FA\u016F\u00FD\u017E" },
		{ "hungarian", U"\u00E1\u00E9\u00ED\u00F3\u00F6\u0151\u00FA\u00FC\u0171" },
		{ "turkish", U"\u00E7\u011F\u0131\u00F6\u015F\u00FC\u00E2\u00EE\u00FB" },
		{ "russian", U"\u0430\u0431\u0432\u0433\u0434\u0435\u0451\u0436\u0437\u0438\u0439\u043A\u043B\u043C\u043D\u043E\u043F\u0440\u0441\u0442\u0443\u0444\u0445\u0446\u0447\u0448\u0449\u044A\u044B\u044C\u044D\u044E\u044F" },
		{ "ukrainian", U"\u0430\u0431\u0432\u0433\u0491\u0434\u0435\u0454\u0436\u0437\u0438\u0456\u0457\u0439\u043A\u043B\u043C\u043D\u043E\u043F\u0440\u0441\u0442\u0443\u0444\u0445\u0446\u0447\u0448\u0449\u044C\u044E\u044F" },
	};
	for (const auto &alphabet : alphabets) {
		for (const char32_t *letter = alphabet.letters; *letter != 0; ++letter) {
			if (!OskReaches(*letter) || !OskReaches(OskUpper(*letter))) {
				std::fprintf(stderr, "%s: U+%04X is on no key\n", alphabet.language, static_cast<unsigned>(*letter));
				std::exit(1);
			}
		}
	}
	// ASCII letters, digits and the punctuation an address or a password needs
	for (char32_t c = U'a'; c <= U'z'; ++c) {
		CHECK(OskReaches(c) && OskReaches(OskUpper(c)));
	}
	for (char32_t c = U'0'; c <= U'9'; ++c) {
		CHECK(OskReaches(c));
	}
	for (const char *c = ".:-_@/!?#$%&*+=()[]{}<>'\",;\\|~^`"; *c != 0; ++c) {
		CHECK(OskReaches(static_cast<char32_t>(*c)));
	}
	// a field opens on the layer its text most likely needs
	CHECK(OskFirstLayer("english", false) == OSK_LAYER_LATIN && OskFirstLayer("polish", false) == OSK_LAYER_LATIN);
	CHECK(OskFirstLayer("russian", false) == OSK_LAYER_CYRILLIC && OskFirstLayer("ukrainian", false) == OSK_LAYER_CYRILLIC);
	CHECK(OskFirstLayer("russian", true) == OSK_LAYER_SYMBOLS && OskFirstLayer(nullptr, false) == OSK_LAYER_LATIN);
}

static void Encoding() {
	using namespace openq4;
	CHECK(OskUtf8(U'A') == "A");
	CHECK(OskUtf8(0x00E9) == "\xC3\xA9");
	CHECK(OskUtf8(0x0151) == "\xC5\x91");
	CHECK(OskUtf8(0x0436) == "\xD0\xB6");
	CHECK(OskUtf8(0x20AC) == "\xE2\x82\xAC");
	CHECK(OskUtf8(0x1F600) == "\xF0\x9F\x98\x80");
	CHECK(OskUtf8(0xD800).empty() && OskUtf8(0x110000).empty());
}

int main() {
	Layers();
	Shift();
	Languages();
	Encoding();
	std::puts("on-screen keyboard layouts: PASS");
	return 0;
}
