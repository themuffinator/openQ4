
#ifndef __LANGDICT_H__
#define __LANGDICT_H__

/*
===============================================================================

	Simple dictionary specifically for the localized string tables.

===============================================================================
*/

/*
===============================================================================

	Text encoding.

	Displayable text inside the engine is UTF-8. Every string table is
	normalised to it as it loads - a table already authored in UTF-8 is kept
	byte for byte, and a legacy 8-bit table is decoded through the codepage its
	language was authored in and re-emitted as UTF-8. Nothing downstream of the
	loader has to ask which one a file was.

	The 8-bit codepages below are therefore an *input* format and a legacy font
	indexing scheme, not the engine's string encoding. They still matter in two
	places: reading a retail string table, and drawing through the retail
	bitmap atlases, whose 256 glyph slots are indexed by codepage byte rather
	than by code point.

===============================================================================
*/
typedef enum {
	LANGCP_WESTERN = 0,			// Windows-1252: English, French, Italian, Spanish, German, Brazilian Portuguese
	LANGCP_CENTRAL_EUROPEAN,	// Windows-1250: Polish, Czech, Hungarian
	LANGCP_CYRILLIC,			// Windows-1251: Russian, Ukrainian
	LANGCP_TURKISH				// Windows-1254: Turkish
} langCodePage_t;

						// Longest UTF-8 encoding of a single code point, and the
						// code point substituted for anything that cannot be decoded
						// or drawn.
const int				UTF8_MAX_BYTES = 4;
const unsigned int		UNICODE_REPLACEMENT = 0xFFFD;

						// Decodes one code point. Returns the number of bytes
						// consumed, or 0 when the sequence is not well-formed
						// shortest-form UTF-8 - callers that need to make progress
						// through arbitrary bytes should then consume one byte and
						// read it as Latin-1, which is what the text layer does.
int						LangDict_DecodeUtf8( const char *p, int available, unsigned int &codePoint );

						// Encodes one code point. Returns the number of bytes written,
						// or 0 if the code point is not encodable.
int						LangDict_EncodeUtf8( unsigned int codePoint, char out[UTF8_MAX_BYTES] );

						// Decodes one code point the way the text layer does: never
						// fails, and always advances. A byte that cannot start a
						// well-formed sequence is read through the active codepage
						// and consumes exactly one byte, so stray 8-bit text still
						// draws something sensible instead of stalling the caller.
int						LangDict_NextCodePoint( const char *p, int available, unsigned int &codePoint );

						// True when the whole buffer is well-formed UTF-8.
bool					LangDict_IsUtf8( const char *buffer, int length );

						// Re-encodes legacy 8-bit text written in codePage as UTF-8, with
						// the same ASCII folds a legacy string table gets. False, leaving
						// converted untouched, when the buffer is already UTF-8 (or ASCII).
bool					LangDict_ConvertCodePageToUtf8( const char *buffer, int length, langCodePage_t codePage, idStr &converted );

						// Which codepage a sys_lang value's legacy 8-bit tables are
						// authored in, and which one its retail bitmap atlases index by.
langCodePage_t			LangDict_CodePageForLanguage( const char *language );

						// Selected from sys_lang before the string tables load, so a legacy
						// table is decoded through the codepage it was written in. The
						// bitmap font path reads it back to map a code point onto one of
						// its 256 byte-indexed glyph slots.
void					LangDict_SetActiveCodePage( langCodePage_t codePage );
langCodePage_t			LangDict_GetActiveCodePage( void );

						// Bumped every time the active codepage actually changes, so the
						// font atlases can tell they are stale.
int						LangDict_GetCodePageGeneration( void );

						// The Unicode code point one codepage byte stands for. Returns 0
						// for the handful of slots the codepage leaves unassigned.
unsigned int			LangDict_UnicodeForByte( int byte );

						// The reverse: which byte of the active codepage draws a code
						// point, for the byte-indexed retail atlases. False when the
						// codepage has no such byte.
bool					LangDict_ByteForCodePoint( unsigned int codePoint, unsigned char &out );

						// The ASCII stand-in for punctuation the retail bitmap atlases
						// have no art for, or NULL when none is needed. A scalable face
						// draws all of it properly and does not consult this.
const char *			LangDict_AsciiFoldForCodePoint( unsigned int codePoint );

						// The Unicode blocks a language needs on top of Latin-1, as a
						// NULL-terminated list of inclusive [first,last] pairs. The font
						// rasteriser turns these into atlas pages, so a language is
						// readable even for text that never passed through a string table
						// - a player name typed at runtime, say. Returns NULL when Latin-1
						// already covers the language.
const unsigned int *	LangDict_ExtendedRangesForLanguage( const char *language );

						// True when the 256-slot retail bitmap atlases a language can find
						// have no art for its alphabet - none at all for Cyrillic, and the
						// wrong letters for a Central European or Turkish language with no
						// retail release of its own - so the legacy font path cannot draw it.
bool					LangDict_LanguageNeedsScalableFonts( const char *language );

class idLangKeyValue {
public:
	idStr					key;
	idStr					value;
};

class idLangDict {
public:
							idLangDict( void );
							~idLangDict( void );

	void					Clear( void );
							// gameDir reads that directory's copy of the file rather than the
							// search path's first one (layering a mod's table over the copies it shadows)
	bool					Load( const char *fileName, bool clear = true, const char *gameDir = NULL );
	void					Save( const char *fileName );

	const char *			AddString( const char *str );
	const char *			GetString( const char *str ) const;
	// Exact optional lookup: null for an absent key, without a console warning.
	const char *			FindString( const char *key ) const;

							// adds the value and key as passed (doesn't generate a "#str_xxxxxx" key or ensure the key/value pair is unique)
	void					AddKeyVal( const char *key, const char *val );

	int						GetNumKeyVals( void ) const;
	const idLangKeyValue *	GetKeyVal( int i ) const;

	void					SetBaseID(int id) { baseID = id; };

private:
	idList<idLangKeyValue>	args;
	idHashIndex				hash;

	bool					ExcludeString( const char *str ) const;
	int						GetNextId( void ) const;
	int						GetHashKey( const char *str ) const;

	int						baseID;
};

#endif /* !__LANGDICT_H__ */
