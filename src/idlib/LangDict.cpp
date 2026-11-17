




/*
============
idLangDict::idLangDict
============
*/
idLangDict::idLangDict( void ) {
	args.SetGranularity( 256 );
	hash.SetGranularity( 256 );
	hash.Clear( 4096, 8192 );
	baseID = 0;
}

/*
============
idLangDict::~idLangDict
============
*/
idLangDict::~idLangDict( void ) {
	Clear();
}

/*
============
idLangDict::Clear
============
*/
void idLangDict::Clear( void ) {
	args.Clear();
	hash.Clear();
}

/*
============
LANGDICT_CP1252_HIGH

Windows-1252 0x80-0x9F block - the only range where Windows-1252 diverges from
Latin-1. 0 marks the five unassigned CP1252 slots (0x81/0x8D/0x8F/0x90/0x9D).
============
*/
static const unsigned short LANGDICT_CP1252_HIGH[32] = {
	0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178
};

/*
============
LANGDICT_CP1250_HIGH

Windows-1250 0x80-0xFF. Unlike CP1252 this diverges from Latin-1 across the
whole upper half, so the entire range has to be tabulated rather than just the
0x80-0x9F band. 0 marks the five unassigned slots - 0x81/0x83/0x88/0x90/0x98,
which is a different set from the five CP1252 leaves empty.

This is the codepage idStr::printableCharacter and the case-folding tables were
built from - their comments mark 0x8C/0x8F/0xA3/0xA5/0xAF/0xB3/0xB9/0xBF as the
Polish letters, which are exactly the CP1250 assignments below.
============
*/
static const unsigned short LANGDICT_CP1250_HIGH[128] = {
	0x20AC, 0x0000, 0x201A, 0x0000, 0x201E, 0x2026, 0x2020, 0x2021,
	0x0000, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0000, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
	0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
	0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
	0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
	0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
	0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
	0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
	0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
	0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
	0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
	0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9
};

/*
============
LANGDICT_CP1251_HIGH

Windows-1251 0x80-0xFF, the codepage the retail Russian releases of the id
engines used. Like CP1250 it diverges from Latin-1 across the whole upper half.
0 marks the one unassigned slot, 0x98 - CP1252 and CP1250 leave five each.

Note 0xA0 is a non-breaking space and 0xB0-0xBF is a mix of punctuation and
Cyrillic letters; the alphabet proper is the contiguous 0xC0-0xFF run, which is
U+0410-U+044F - the reason a byte-indexed Russian font was ever workable.
============
*/
static const unsigned short LANGDICT_CP1251_HIGH[128] = {
	0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
	0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
	0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
	0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
	0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
	0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
	0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
	0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
	0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
	0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
	0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F
};

/*
============
LANGDICT_CP1254_HIGH

Windows-1254 0x80-0xFF. It is Windows-1252 with six Turkish letters in place
of six Icelandic ones - 0xD0/0xDD/0xDE/0xF0/0xFD/0xFE are G-breve, capital
dotted I, S-cedilla, g-breve, dotless i and s-cedilla - and with 0x8E/0x9E
(Z-caron in CP1252) left unassigned, so 0 marks seven slots here. Tabulated in
full like the other non-Western pages, so one lookup serves all three.
============
*/
static const unsigned short LANGDICT_CP1254_HIGH[128] = {
	0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x0000, 0x0000,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x0000, 0x0178,
	0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
	0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
	0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
	0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
	0x011E, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
	0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x0130, 0x015E, 0x00DF,
	0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
	0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
	0x011F, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
	0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x0131, 0x015F, 0x00FF
};

/*
============
LangDict_HighTableForCodePage

The tabulated 0x80-0xFF half of a non-Western codepage, or NULL for Windows-1252,
which only diverges from Latin-1 in its 0x80-0x9F band and is handled apart.
============
*/
static const unsigned short *LangDict_HighTableForCodePage( langCodePage_t codePage ) {
	switch ( codePage ) {
		case LANGCP_CENTRAL_EUROPEAN:	return LANGDICT_CP1250_HIGH;
		case LANGCP_CYRILLIC:			return LANGDICT_CP1251_HIGH;
		case LANGCP_TURKISH:			return LANGDICT_CP1254_HIGH;
		default:						return NULL;
	}
}

/*
============
LangDict_CodePageForLanguage
============
*/
langCodePage_t LangDict_CodePageForLanguage( const char *language ) {
	// sys_lang values whose legacy 8-bit tables are not Windows-1252. Raven's
	// retail Central European fonts carried the Polish and Czech alphabets
	// together, and Windows-1250 covers Hungarian too. Brazilian Portuguese
	// needs nothing outside Windows-1252, so it stays Western.
	static const char * const centralEuropean[] = { "polish", "czech", "hungarian", NULL };
	static const char * const cyrillic[] = { "russian", "ukrainian", NULL };
	static const char * const turkish[] = { "turkish", NULL };

	if ( language == NULL || language[0] == '\0' ) {
		return LANGCP_WESTERN;
	}
	for ( int i = 0; centralEuropean[i] != NULL; i++ ) {
		if ( idStr::Icmp( language, centralEuropean[i] ) == 0 ) {
			return LANGCP_CENTRAL_EUROPEAN;
		}
	}
	for ( int i = 0; cyrillic[i] != NULL; i++ ) {
		if ( idStr::Icmp( language, cyrillic[i] ) == 0 ) {
			return LANGCP_CYRILLIC;
		}
	}
	for ( int i = 0; turkish[i] != NULL; i++ ) {
		if ( idStr::Icmp( language, turkish[i] ) == 0 ) {
			return LANGCP_TURKISH;
		}
	}
	return LANGCP_WESTERN;
}

/*
============
LangDict_ExtendedRangesForLanguage

Whole Unicode blocks rather than the exact set a string table happens to use,
so text that never came from a table - a player name, a server name, a console
line - draws in the same alphabet as the menus around it.
============
*/
const unsigned int *LangDict_ExtendedRangesForLanguage( const char *language ) {
	// Cyrillic (U+0400-U+04FF) covers Russian, Ukrainian, Belarusian, Serbian
	// and Bulgarian in one block; the shipped faces carry all 256 of it.
	static const unsigned int cyrillicRanges[] = { 0x0400, 0x04FF, 0 };

	// Only non-Latin scripts appear here. The Latin extensions are built for
	// every language regardless - see Q4_TTF_UNIVERSAL_RANGES - because the
	// Western codepage itself reaches into them, so they are not something a
	// language gets to opt out of. Polish and Czech therefore need nothing of
	// their own even though their alphabets live above U+00FF.
	if ( LangDict_CodePageForLanguage( language ) == LANGCP_CYRILLIC ) {
		return cyrillicRanges;
	}
	return NULL;
}

/*
============
LangDict_LanguageNeedsScalableFonts
============
*/
bool LangDict_LanguageNeedsScalableFonts( const char *language ) {
	// A retail atlas has 256 byte-indexed slots, so it can draw a language only
	// if it carries art for that language's codepage. The English atlases are
	// Windows-1252 art, which every Western language shares, and the retail
	// Polish release shipped its own Central European set. Nothing shipped
	// Cyrillic art at all, so Windows-1251 bytes land on blank slots.
	//
	// The languages openQ4 added have no retail release and so only ever find
	// the English atlases. For Czech, Hungarian and Turkish that is worse than
	// a blank: their codepage bytes index Windows-1252 art, so c-caron would
	// draw as e-grave and s-cedilla as thorn - wrong letters, not missing ones.
	static const char * const textOnlyLanguages[] = { "czech", "hungarian", "turkish", NULL };

	const langCodePage_t codePage = LangDict_CodePageForLanguage( language );
	if ( codePage == LANGCP_CYRILLIC ) {
		return true;
	}
	if ( codePage != LANGCP_WESTERN ) {
		for ( int i = 0; textOnlyLanguages[i] != NULL; i++ ) {
			if ( idStr::Icmp( language, textOnlyLanguages[i] ) == 0 ) {
				return true;
			}
		}
	}
	return false;
}

static langCodePage_t	langDictActiveCodePage = LANGCP_WESTERN;
static int				langDictCodePageGeneration = 0;

/*
============
LangDict_SetActiveCodePage
============
*/
void LangDict_SetActiveCodePage( langCodePage_t codePage ) {
	if ( codePage == langDictActiveCodePage ) {
		return;
	}
	langDictActiveCodePage = codePage;
	langDictCodePageGeneration++;
}

langCodePage_t LangDict_GetActiveCodePage( void ) {
	return langDictActiveCodePage;
}

int LangDict_GetCodePageGeneration( void ) {
	return langDictCodePageGeneration;
}

/*
============
LangDict_UnicodeForByte
============
*/
static unsigned int LangDict_UnicodeForByteInCodePage( int byte, langCodePage_t codePage ) {
	if ( byte < 0 || byte > 0xFF ) {
		return 0;
	}
	if ( byte < 0x80 ) {
		return (unsigned int)byte;
	}
	const unsigned short *table = LangDict_HighTableForCodePage( codePage );
	if ( table != NULL ) {
		return table[byte - 0x80];
	}
	if ( byte <= 0x9F ) {
		return LANGDICT_CP1252_HIGH[byte - 0x80];
	}
	// CP1252 agrees with Latin-1 from 0xA0 up.
	return (unsigned int)byte;
}

unsigned int LangDict_UnicodeForByte( int byte ) {
	return LangDict_UnicodeForByteInCodePage( byte, langDictActiveCodePage );
}

/*
============
LANGDICT_GLYPH_FOLD

The stock Quake 4 .fontdat atlases hold 256 byte-indexed glyphs, of which only
83 of the 128 high slots carry real art, and the missing set is identical in
every shipped Latin font. These codes land on a 2x2 .notdef cell with zero
horizontal advance, so a non-breaking space would not merely draw nothing, it
would delete the word gap. Fold them to ASCII equivalents that do have art.

Keyed by code point rather than by byte, because a code point is what the
engine carries now. All three Windows codepages agree on this punctuation - they
differ only in their letters - so the byte column is unambiguous and one table
serves every legacy table and every byte-indexed atlas.

Only the legacy paths need this. A scalable face has real art for all of it.
============
*/
typedef struct langDictGlyphFold_s {
	unsigned char	byte;
	unsigned short	codePoint;
	const char *	ascii;
} langDictGlyphFold_t;

static const langDictGlyphFold_t LANGDICT_GLYPH_FOLD[] = {
	{ 0x82, 0x201A, "," },  { 0x84, 0x201E, "\"" }, { 0x85, 0x2026, "..." },
	{ 0x91, 0x2018, "'" },  { 0x92, 0x2019, "'" },  { 0x93, 0x201C, "\"" },
	{ 0x94, 0x201D, "\"" }, { 0x96, 0x2013, "-" },  { 0x97, 0x2014, "-" },
	{ 0xA0, 0x00A0, " " },
	{ 0x00, 0x0000, NULL }
};

static const char *LangDict_FoldForByte( unsigned char c ) {
	for ( int i = 0; LANGDICT_GLYPH_FOLD[i].ascii != NULL; i++ ) {
		if ( LANGDICT_GLYPH_FOLD[i].byte == c ) {
			return LANGDICT_GLYPH_FOLD[i].ascii;
		}
	}
	return NULL;
}

/*
============
LangDict_AsciiFoldForCodePoint
============
*/
const char *LangDict_AsciiFoldForCodePoint( unsigned int codePoint ) {
	if ( codePoint < 0x80 ) {
		return NULL;
	}
	for ( int i = 0; LANGDICT_GLYPH_FOLD[i].ascii != NULL; i++ ) {
		if ( LANGDICT_GLYPH_FOLD[i].codePoint == codePoint ) {
			return LANGDICT_GLYPH_FOLD[i].ascii;
		}
	}
	return NULL;
}

/*
============
LangDict_DecodeUtf8

Returns the number of bytes consumed, or 0 if the sequence is not well-formed
shortest-form UTF-8.
============
*/
int LangDict_DecodeUtf8( const char *buffer, int available, unsigned int &codePoint ) {
	const unsigned char *p = (const unsigned char *)buffer;
	if ( p == NULL || available <= 0 ) {
		return 0;
	}

	const unsigned char c0 = p[0];
	if ( c0 < 0x80 ) {
		codePoint = c0;
		return 1;
	}

	int extra;
	unsigned int cp;
	unsigned int minimum;
	if ( ( c0 & 0xE0 ) == 0xC0 ) {
		extra = 1; cp = c0 & 0x1Fu; minimum = 0x80;
	} else if ( ( c0 & 0xF0 ) == 0xE0 ) {
		extra = 2; cp = c0 & 0x0Fu; minimum = 0x800;
	} else if ( ( c0 & 0xF8 ) == 0xF0 ) {
		extra = 3; cp = c0 & 0x07u; minimum = 0x10000;
	} else {
		return 0;
	}

	if ( available < extra + 1 ) {
		return 0;
	}
	for ( int i = 1; i <= extra; i++ ) {
		if ( ( p[i] & 0xC0 ) != 0x80 ) {
			return 0;
		}
		cp = ( cp << 6 ) | ( p[i] & 0x3Fu );
	}
	if ( cp < minimum || cp > 0x10FFFF || ( cp >= 0xD800 && cp <= 0xDFFF ) ) {
		return 0;
	}

	codePoint = cp;
	return extra + 1;
}

/*
============
LangDict_EncodeUtf8
============
*/
int LangDict_EncodeUtf8( unsigned int codePoint, char out[UTF8_MAX_BYTES] ) {
	if ( out == NULL || codePoint > 0x10FFFF || ( codePoint >= 0xD800 && codePoint <= 0xDFFF ) ) {
		return 0;
	}
	if ( codePoint < 0x80 ) {
		out[0] = (char)codePoint;
		return 1;
	}
	if ( codePoint < 0x800 ) {
		out[0] = (char)( 0xC0 | ( codePoint >> 6 ) );
		out[1] = (char)( 0x80 | ( codePoint & 0x3F ) );
		return 2;
	}
	if ( codePoint < 0x10000 ) {
		out[0] = (char)( 0xE0 | ( codePoint >> 12 ) );
		out[1] = (char)( 0x80 | ( ( codePoint >> 6 ) & 0x3F ) );
		out[2] = (char)( 0x80 | ( codePoint & 0x3F ) );
		return 3;
	}
	out[0] = (char)( 0xF0 | ( codePoint >> 18 ) );
	out[1] = (char)( 0x80 | ( ( codePoint >> 12 ) & 0x3F ) );
	out[2] = (char)( 0x80 | ( ( codePoint >> 6 ) & 0x3F ) );
	out[3] = (char)( 0x80 | ( codePoint & 0x3F ) );
	return 4;
}

/*
============
LangDict_NextCodePoint

The text layer cannot stall on a byte it does not understand: a GUI, a cvar or
a network name can hold anything, and a draw loop that refuses to advance hangs
the frame. Anything that is not well-formed UTF-8 is therefore read as a single
byte, which is a guaranteed one byte of progress.

That byte is decoded through the *active codepage* rather than as Latin-1,
because a buffer that is not valid UTF-8 is by definition legacy 8-bit text, and
the codepage is this session's best statement about which one. It matters both
ways round: a stray Windows-1252 smart quote becomes U+201C, which every face
has art for, instead of U+0093, which is a control code no face covers; and a
stray Windows-1251 byte in a Russian session becomes the Cyrillic letter it
stands for instead of an unrelated accented Latin one. For a Western session
outside the 0x80-0x9F band this is Latin-1 anyway.
============
*/
int LangDict_NextCodePoint( const char *p, int available, unsigned int &codePoint ) {
	if ( p == NULL || available <= 0 ) {
		codePoint = 0;
		return 0;
	}
	const int used = LangDict_DecodeUtf8( p, available, codePoint );
	if ( used > 0 ) {
		return used;
	}

	const unsigned char byte = (unsigned char)p[0];
	const unsigned int mapped = LangDict_UnicodeForByte( byte );
	// A slot the codepage leaves unassigned has no character at all; keep the
	// raw byte rather than returning 0, which callers read as end of string.
	codePoint = ( mapped != 0 ) ? mapped : byte;
	return 1;
}

/*
============
LangDict_IsUtf8
============
*/
bool LangDict_IsUtf8( const char *buffer, int length ) {
	if ( buffer == NULL ) {
		return false;
	}
	for ( int i = 0; i < length; ) {
		unsigned int cp = 0;
		const int used = LangDict_DecodeUtf8( buffer + i, length - i, cp );
		if ( used == 0 ) {
			return false;
		}
		i += used;
	}
	return true;
}

/*
============
LangDict_ByteForCodePoint
============
*/
bool LangDict_ByteForCodePoint( unsigned int codePoint, unsigned char &out ) {
	if ( codePoint < 0x80 ) {
		out = (unsigned char)codePoint;
		return true;
	}
	const unsigned short *table = LangDict_HighTableForCodePage( LangDict_GetActiveCodePage() );
	if ( table != NULL ) {
		for ( int i = 0; i < 128; i++ ) {
			if ( table[i] != 0 && table[i] == codePoint ) {
				out = (unsigned char)( 0x80 + i );
				return true;
			}
		}
		return false;
	}
	if ( codePoint >= 0xA0 && codePoint <= 0xFF ) {
		out = (unsigned char)codePoint;
		return true;
	}
	for ( int i = 0; i < 32; i++ ) {
		if ( LANGDICT_CP1252_HIGH[i] != 0 && LANGDICT_CP1252_HIGH[i] == codePoint ) {
			out = (unsigned char)( 0x80 + i );
			return true;
		}
	}
	return false;
}

/*
============
LangDict_ConvertCodePageToUtf8

Normalises a legacy 8-bit string table to UTF-8, which is the encoding the rest
of the engine works in. Retail Quake 4 tables are an 8-bit codepage - Western
for the shipped languages, Central European for the retail Polish release - and
a table like that is never valid UTF-8 end to end, because a byte in the
0xC0-0xFF range is read as a lead byte and the ASCII letter after it fails the
continuation-byte test. So "is this well-formed UTF-8?" is a reliable test for
which of the two a file is, and a pure-ASCII table answers yes and is passed
through untouched.

Which codepage a legacy table is read as follows sys_lang, and it has to,
because the byte values overlap completely: 0xB9 is superscript one in
Windows-1252, a-ogonek in Windows-1250 and a soft-hyphen-adjacent punctuation
mark in Windows-1251. There is no way to tell from the bytes. Callers pass it:
string tables the active codepage, and text whose codepage is known regardless
of the language, such as the stock sound shaders' English script lines, that
one.

This is the inverse of what the engine used to do. Converting the other way -
UTF-8 down to a codepage - was forced by 256 byte-indexed font glyphs, and it
capped the engine at whatever one 8-bit page could express. Going up instead
means a table can hold any script the fonts have art for.
============
*/
bool LangDict_ConvertCodePageToUtf8( const char *buffer, int length, langCodePage_t codePage, idStr &converted ) {
	const unsigned char *p = (const unsigned char *)buffer;

	if ( LangDict_IsUtf8( buffer, length ) ) {
		// Already UTF-8, or pure ASCII, which is the same thing.
		return false;
	}

	bool sawHighByte = false;
	for ( int i = 0; i < length; i++ ) {
		if ( p[i] >= 0x80 ) {
			sawHighByte = true;
			break;
		}
	}
	if ( !sawHighByte ) {
		// Not valid UTF-8 yet has no high bytes at all: nothing this function
		// knows how to repair. Leave it alone rather than guess.
		return false;
	}

	converted.Clear();
	converted.Fill( ' ', length * UTF8_MAX_BYTES );

	int outIndex = 0;
	for ( int i = 0; i < length; i++ ) {
		const unsigned char c = p[i];
		if ( c < 0x80 ) {
			converted[outIndex++] = (char)c;
			continue;
		}

		// A slot the codepage leaves unassigned has no character to encode.
		// Substitute rather than emit U+0000, which would truncate the table.
		unsigned int cp = LangDict_UnicodeForByteInCodePage( c, codePage );
		if ( cp == 0 ) {
			cp = UNICODE_REPLACEMENT;
		}

		// The retail atlases have no art for most of the 0x80-0x9F punctuation
		// band, so those code points were historically folded to ASCII on the way
		// in. Keep the fold: it is the difference between a non-breaking space
		// drawing as a word gap and drawing as a zero-advance blank that deletes
		// the gap entirely.
		const char *fold = LangDict_FoldForByte( c );
		if ( fold != NULL ) {
			for ( int k = 0; fold[k] != '\0'; k++ ) {
				converted[outIndex++] = fold[k];
			}
			continue;
		}

		char encoded[UTF8_MAX_BYTES];
		const int used = LangDict_EncodeUtf8( cp, encoded );
		for ( int k = 0; k < used; k++ ) {
			converted[outIndex++] = encoded[k];
		}
	}

	converted.CapLength( outIndex );
	return true;
}

/*
============
idLangDict::Load
============
*/
bool idLangDict::Load( const char *fileName, bool clear, const char *gameDir ) {
	if ( clear ) {
		Clear();
	}

	const char *buffer = NULL;
	// 'transcoded' must be declared before 'src': idLexer::LoadMemory() stores
	// the pointer without copying, so the buffer has to outlive the lexer.
	idStr transcoded;
	idLexer src( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT );

	// One directory's copy: read it whole, NUL-terminated as ReadFile's buffer is.
	idList<char> dirCopy;
	int len;
	if ( gameDir != NULL && gameDir[0] != '\0' ) {
		idFile *f = idLib::fileSystem->OpenFileRead( fileName, false, gameDir );
		if ( f == NULL ) {
			return false;
		}
		len = f->Length();
		if ( len > 0 ) {
			dirCopy.SetNum( len + 1 );
			len = f->Read( dirCopy.Ptr(), len );
			dirCopy[ len > 0 ? len : 0 ] = '\0';
			buffer = dirCopy.Ptr();
		}
		idLib::fileSystem->CloseFile( f );
	} else {
		len = idLib::fileSystem->ReadFile( fileName, (void**)&buffer );
	}
	if ( len <= 0 ) {
		// let whoever called us deal with the failure (so sys_lang can be reset)
		return false;
	}

	const char *parseText = buffer;
	int parseLength = (int)strlen( buffer );
	if ( parseLength >= 3 && (unsigned char)parseText[0] == 0xEF &&
		 (unsigned char)parseText[1] == 0xBB && (unsigned char)parseText[2] == 0xBF ) {
		// a UTF-8 BOM would break ExpectTokenString( "{" )
		parseText += 3;
		parseLength -= 3;
	}
	if ( LangDict_ConvertCodePageToUtf8( parseText, parseLength, langDictActiveCodePage, transcoded ) ) {
		idLib::common->DPrintf( "%s: converted legacy 8-bit string table to UTF-8\n", fileName );
		parseText = transcoded.c_str();
		parseLength = transcoded.Length();
	}

	src.LoadMemory( parseText, parseLength, fileName );
	if ( !src.IsLoaded() ) {
		return false;
	}

	idToken tok, tok2;
	src.ExpectTokenString( "{" );
	while ( src.ReadToken( &tok ) ) {
		if ( tok == "}" ) {
			break;
		}

		if ( src.ReadToken( &tok2 ) ) {
			if ( tok2 == "}" ) {
				break;
			}
			idLangKeyValue kv;
			kv.key = tok;
			kv.value = tok2;
// RAVEN BEGIN
			if( kv.key.CmpPrefix( STRTABLE_ID ) ) {
				common->Warning( "Invalid token id \'%s\' in \'%s\' line %d", kv.key.c_str(), fileName, src.GetLineNum() );
			} else {
				hash.Add( GetHashKey( kv.key ), args.Append( kv ) );
			}
// RAVEN END
		}
	}
	if ( gameDir != NULL && gameDir[0] != '\0' ) {
		idLib::common->Printf( "%i strings read from %s (%s)\n", args.Num(), fileName, gameDir );
	} else {
		idLib::common->Printf( "%i strings read from %s\n", args.Num(), fileName );
		idLib::fileSystem->FreeFile( (void*)buffer );
	}
	
	return true;
}

/*
============
idLangDict::Save
============
*/
void idLangDict::Save( const char *fileName ) {
	idFile *outFile = idLib::fileSystem->OpenFileWrite( fileName );
// RAVEN BEGIN
	if( !outFile ) {
		common->Printf( "Could not open file \'%s\'for writing\n", fileName );
		return;
	}
// RAVEN END
	outFile->WriteFloatString( "// string table" NEWLINE "// english" NEWLINE "//" NEWLINE NEWLINE "{" NEWLINE );
	for ( int j = 0; j < args.Num(); j++ ) {
		outFile->WriteFloatString( "\t\"%s\"\t\"", args[j].key.c_str() );
		int l = args[j].value.Length();
		char slash = '\\';
		char tab = 't';
		char nl = 'n';
		for ( int k = 0; k < l; k++ ) {
			char ch = args[j].value[k];
			if ( ch == '\t' ) {
				outFile->Write( &slash, 1 );
				outFile->Write( &tab, 1 );
			} else if ( ch == '\n' || ch == '\r' ) {
				outFile->Write( &slash, 1 );
				outFile->Write( &nl, 1 );
			} else {
				outFile->Write( &ch, 1 );
			}
		}
		outFile->WriteFloatString( "\"" NEWLINE );
	}
	outFile->WriteFloatString( NEWLINE "}" NEWLINE );
	idLib::fileSystem->CloseFile( outFile );
}

/*
============
idLangDict::GetString
============
*/
const char *idLangDict::GetString( const char *str ) const {

	if ( str == NULL || str[0] == '\0' ) {
		return "";
	}

	if ( idStr::Cmpn( str, STRTABLE_ID, STRTABLE_ID_LENGTH ) != 0 ) {
		return str;
	}

	int hashKey = GetHashKey( str );
	for ( int i = hash.First( hashKey ); i != -1; i = hash.Next( i ) ) {
		if ( args[i].key.Cmp( str ) == 0 ) {
			return args[i].value;
		}
	}

	// Quake 4 content uses "#str_1xxxxx" while some legacy engine paths still
	// request Doom 3-era "#str_xxxxx" ids. If the legacy id misses, retry with
	// a Quake 4-style remap before reporting an unknown string.
	const int legacyDigits = 5;
	const int legacyLength = STRTABLE_ID_LENGTH + legacyDigits;
	bool triedLegacyRemap = false;
	if ( idStr::Length( str ) == legacyLength ) {
		bool hasOnlyDigits = true;
		for ( int i = STRTABLE_ID_LENGTH; i < legacyLength; ++i ) {
			if ( str[i] < '0' || str[i] > '9' ) {
				hasOnlyDigits = false;
				break;
			}
		}

		if ( hasOnlyDigits ) {
			triedLegacyRemap = true;
			char remapped[STRTABLE_ID_LENGTH + legacyDigits + 2];
			idStr::snPrintf( remapped, sizeof( remapped ), "%s1%s", STRTABLE_ID, str + STRTABLE_ID_LENGTH );
			hashKey = GetHashKey( remapped );
			for ( int i = hash.First( hashKey ); i != -1; i = hash.Next( i ) ) {
				if ( args[i].key.Cmp( remapped ) == 0 ) {
					return args[i].value;
				}
			}
		}
	}

	// Keep compatibility paths quiet for stale Doom 3 id requests that don't
	// have a Quake 4 remap in the active language dictionary.
	if ( triedLegacyRemap ) {
		return str;
	}

	idLib::common->Warning( "Unknown string id %s", str );
	return str;
}

const char *idLangDict::FindString( const char *key ) const {
	if (key == NULL || key[0] == '\0') return NULL;
	for (int i = hash.First(GetHashKey(key)); i != -1; i = hash.Next(i)) {
		if (args[i].key.Cmp(key) == 0) return args[i].value.c_str();
	}
	return NULL;
}

/*
============
idLangDict::AddString
============
*/
const char *idLangDict::AddString( const char *str ) {
	
	if ( ExcludeString( str ) ) {
		return str;
	}

	int c = args.Num();
	for ( int j = 0; j < c; j++ ) {
		if ( idStr::Cmp( args[j].value, str ) == 0 ){
			return args[j].key;
		}
	}

	int id = GetNextId();
	idLangKeyValue kv;
	kv.key = va( "#str_%06i", id );
	kv.value = str;
	c = args.Append( kv );
	assert( kv.key.CmpPrefix( STRTABLE_ID ) == 0 );
	hash.Add( GetHashKey( kv.key ), c );
	return args[c].key;
}

/*
============
idLangDict::GetNumKeyVals
============
*/
int idLangDict::GetNumKeyVals( void ) const {
	return args.Num();
}

/*
============
idLangDict::GetKeyVal
============
*/
const idLangKeyValue * idLangDict::GetKeyVal( int i ) const {
	return &args[i];
}

/*
============
idLangDict::AddKeyVal
============
*/
void idLangDict::AddKeyVal( const char *key, const char *val ) {
	idLangKeyValue kv;
	kv.key = key;
	kv.value = val;
	assert( kv.key.CmpPrefix( STRTABLE_ID ) == 0 );
	hash.Add( GetHashKey( kv.key ), args.Append( kv ) );
}

/*
============
idLangDict::ExcludeString
============
*/
bool idLangDict::ExcludeString( const char *str ) const {
	if ( str == NULL ) {
		return true;
	}

	int c = idLib::SizeToInt( strlen( str ), "idLangDict::ExcludeString" );
	if ( c <= 1 ) {
		return true;
	}

	if ( idStr::Cmpn( str, STRTABLE_ID, STRTABLE_ID_LENGTH ) == 0 ) {
		return true;
	}

	if ( idStr::Icmpn( str, "gui::", static_cast<int>( sizeof( "gui::" ) - 1 ) ) == 0 ) {
		return true;
	}

	if ( str[0] == '$' ) {
		return true;
	}

	int i;
	for ( i = 0; i < c; i++ ) {
		// isalpha() is only defined for unsigned char values and EOF; passing a
		// plain signed char is undefined for any byte >= 0x80, which every
		// accented CP1252 character is
		if ( isalpha( (unsigned char)str[i] ) ) {
			break;
		}
	}
	if ( i == c ) {
		return true;
	}
	
	return false;
}

/*
============
idLangDict::GetNextId
============
*/
int idLangDict::GetNextId( void ) const {
	int c = args.Num();
	//Let and external user supply the base id for this dictionary
	int id = baseID;

	if ( c == 0 ) {
		return id;
	}

	idStr work;
	for ( int j = 0; j < c; j++ ) {
		work = args[j].key;
		work.StripLeading( STRTABLE_ID );
		int test = atoi( work );
		if ( test > id ) {
			id = test;
		}
	}
	return id + 1;
}

/*
============
idLangDict::GetHashKey
============
*/
int idLangDict::GetHashKey( const char *str ) const {
	int hashKey = 0;
	for ( str += STRTABLE_ID_LENGTH; str[0] != '\0'; str++ ) {
		assert( str[0] >= '0' && str[0] <= '9' );
		hashKey = hashKey * 10 + str[0] - '0';
	}
	return hashKey;
}
