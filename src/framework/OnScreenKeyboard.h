// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#ifndef OPENQ4_ON_SCREEN_KEYBOARD_H
#define OPENQ4_ON_SCREEN_KEYBOARD_H

// openQ4 (PAD-05): the on-screen keyboard's layouts, for typing with only a
// controller or a touch screen. Five layers of four rows of eleven keys cover
// what every shipped language writes: Latin letters and digits; the accented
// letters of French, German, Italian, Spanish and Portuguese; those of Polish,
// Czech, Hungarian and Turkish; Russian and Ukrainian Cyrillic; and symbols.
// Shift gives a letter's capital and leaves every other key as it is.
//
// Pure policy: no engine, no device, no locale. The characters are Unicode
// scalar values; the session types them into the focused field, which stores
// UTF-8. This source is ASCII: the layouts are \u escapes, each layer's four
// rows in one string, so no compiler's source encoding changes a key.

#include <string>

namespace openq4 {

enum { OSK_COLUMNS = 11, OSK_ROWS = 4, OSK_KEYS = OSK_COLUMNS * OSK_ROWS };
enum { OSK_LAYER_LATIN = 0, OSK_LAYER_WEST, OSK_LAYER_CENTRAL, OSK_LAYER_CYRILLIC, OSK_LAYER_SYMBOLS, OSK_LAYER_COUNT };

// A letter's capital, for the scripts the layers hold; anything else unchanged.
inline char32_t OskUpper(char32_t c) {
	if (c >= U'a' && c <= U'z') {
		return c - 0x20;
	}
	if ((c >= 0x00E0 && c <= 0x00FE && c != 0x00F7)) {
		return c - 0x20;				// Latin-1: a-grave to thorn
	}
	if (c == 0x00FF) {
		return 0x0178;					// y-diaeresis
	}
	if ((c >= 0x0100 && c <= 0x0137) || (c >= 0x014A && c <= 0x0177)) {
		return (c & 1) ? c - 1 : c;		// Latin Extended-A pairs, the capital even; dotless i gives dotted I
	}
	if ((c >= 0x0139 && c <= 0x0148) || (c >= 0x0179 && c <= 0x017E)) {
		return (c & 1) ? c : c - 1;		// and the pairs with the capital odd
	}
	if (c >= 0x0430 && c <= 0x044F) {
		return c - 0x20;				// Cyrillic a to ya
	}
	if (c >= 0x0450 && c <= 0x045F) {
		return c - 0x50;				// io, Ukrainian ie, i, yi
	}
	if (c == 0x0491) {
		return 0x0490;					// ghe with upturn
	}
	return c;
}

inline bool OskValidLayer(int layer) {
	return layer >= 0 && layer < OSK_LAYER_COUNT;
}

// The character on a key, or 0 for no such key.
inline char32_t OskKey(int layer, int index, bool shift) {
	static const char32_t *const layers[OSK_LAYER_COUNT] = {
		U"1234567890-qwertyuiop'asdfghjkl.@zxcvbnm,?!_",	// latin
		U"\u00E0\u00E1\u00E2\u00E3\u00E4\u00E7\u00E8\u00E9\u00EA\u00EB\u00EC\u00ED\u00EE\u00EF\u00F1\u00F2\u00F3\u00F4\u00F5\u00F6\u00F9\u00FA\u00FB\u00FC\u0153\u00DF\u00BF\u00A1\u00BA\u00AA\u00AB\u00BB'\u2019\u201C\u201D\u201E\u2013\u2026\u00B7-.,?",	// west
		U"\u0105\u0107\u0119\u0142\u0144\u00F3\u015B\u017A\u017C\u00E1\u00E9\u00ED\u00FA\u00FD\u010D\u010F\u011B\u0148\u0159\u0161\u0165\u016F\u017E\u00F6\u00FC\u0151\u0171\u00E7\u011F\u0131\u015F\u00E2\u00EE\u00FB\u201E\u201D\u00AB\u00BB\u2019-.,?!",	// central
		U"\u0439\u0446\u0443\u043A\u0435\u043D\u0433\u0448\u0449\u0437\u0445\u0444\u044B\u0432\u0430\u043F\u0440\u043E\u043B\u0434\u0436\u044D\u044F\u0447\u0441\u043C\u0438\u0442\u044C\u0431\u044E\u044A\u0451\u0456\u0457\u0454\u0491'.,-!?:",	// cyrillic
		U"1234567890=!?.,:;'\"@#&+-*/\\_()[]%<>{}~^|$`\u00B0\u00A7",	// symbols
	};
	if (!OskValidLayer(layer) || index < 0 || index >= OSK_KEYS) {
		return 0;
	}
	const char32_t c = layers[layer][index];
	return shift ? OskUpper(c) : c;
}

// What the layer key shows for a layer, in UTF-8.
inline const char *OskLayerName(int layer) {
	static const char *const names[OSK_LAYER_COUNT] = { "abc", "\xC3\xA0\xC3\xA9", "\xC4\x85\xC5\x99", "\xD0\xB0\xD0\xB1\xD0\xB2", "#+=" };
	return OskValidLayer(layer) ? names[layer] : "";
}

inline int OskNextLayer(int layer) {
	return OskValidLayer(layer) ? (layer + 1) % OSK_LAYER_COUNT : OSK_LAYER_LATIN;
}

// The layer a field opens on: digits for a number, Cyrillic for the languages
// that write it, Latin otherwise.
inline int OskFirstLayer(const char *language, bool number) {
	if (number) {
		return OSK_LAYER_SYMBOLS;
	}
	const std::string name = language != nullptr ? language : "";
	return name == "russian" || name == "ukrainian" ? OSK_LAYER_CYRILLIC : OSK_LAYER_LATIN;
}

// Whether some key, shifted or not, types this character.
inline bool OskReaches(char32_t c) {
	for (int layer = 0; layer < OSK_LAYER_COUNT; ++layer) {
		for (int index = 0; index < OSK_KEYS; ++index) {
			if (OskKey(layer, index, false) == c || OskKey(layer, index, true) == c) {
				return true;
			}
		}
	}
	return false;
}

// A scalar value as UTF-8; empty for a surrogate or a value past U+10FFFF.
inline std::string OskUtf8(char32_t c) {
	std::string out;
	if (c < 0x80) {
		out += static_cast<char>(c);
	} else if (c < 0x800) {
		out += static_cast<char>(0xC0 | (c >> 6));
		out += static_cast<char>(0x80 | (c & 0x3F));
	} else if (c < 0x10000) {
		if (c >= 0xD800 && c <= 0xDFFF) {
			return out;
		}
		out += static_cast<char>(0xE0 | (c >> 12));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (c & 0x3F));
	} else if (c <= 0x10FFFF) {
		out += static_cast<char>(0xF0 | (c >> 18));
		out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (c & 0x3F));
	}
	return out;
}

}	// namespace openq4

#endif
