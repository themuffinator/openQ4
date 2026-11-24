//----------------------------------------------------------------
// ChatTokens.h
//
// openQ4 chat tokens (MP-15): #h #a #w #l #n written in a chat line are
// replaced on the sender with their health, armour, weapon and ammunition,
// location and nearby team mates before the line leaves the client.
//
// The expansion core below owns no engine types, so tools/tests/native can
// compile it alone. The adapter that reads a player lives in ChatTokens.cpp.
//----------------------------------------------------------------

#ifndef __GAME_MP_CHATTOKENS_H__
#define __GAME_MP_CHATTOKENS_H__

// The stock message-mode control takes 128 characters; an expanded line is
// held to the same budget in bytes, terminator included, so nothing downstream
// of the say command sees a longer line than it could before.
static const int MP_CHAT_TOKEN_TEXT_BYTES = 128;

// What each token stands for. A NULL or empty value leaves "-" in its place:
// a token never vanishes, so a reader can tell the sender meant to say it.
struct mpChatTokenValues_t {
	const char *health;		// #h
	const char *armor;		// #a
	const char *weapon;		// #w
	const char *location;	// #l
	const char *nearby;		// #n
};

inline bool MPChatTokenIsWordChar( char c ) {
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_';
}

inline const char *MPChatTokenValue( char letter, const mpChatTokenValues_t &values, bool &known ) {
	const char *value = 0;
	known = true;
	switch ( letter ) {
		case 'h': case 'H': value = values.health; break;
		case 'a': case 'A': value = values.armor; break;
		case 'w': case 'W': value = values.weapon; break;
		case 'l': case 'L': value = values.location; break;
		case 'n': case 'N': value = values.nearby; break;
		default: known = false; return 0;
	}
	return ( value != 0 && value[ 0 ] != '\0' ) ? value : "-";
}

/*
================
MPChatExpandTokens

Copies text to out with every token replaced. "##" is a literal '#'. A '#'
followed by anything that is not a token letter stays as typed, and so does a
token letter that begins a longer word ("#help", "#str_100000"), so a line
that merely contains a hash is never rewritten.

The result is always terminated and never longer than outBytes - 1. A line
that would overflow is cut at a UTF-8 character boundary, and a colour escape
is not left half written at the end. Returns the number of tokens replaced.
================
*/
inline int MPChatExpandTokens( const char *text, const mpChatTokenValues_t &values, char *out, int outBytes ) {
	int length = 0;
	int expanded = 0;
	bool full = false;

	if ( out == 0 || outBytes <= 0 ) {
		return 0;
	}
	out[ 0 ] = '\0';
	if ( text == 0 ) {
		return 0;
	}

	for ( const char *scan = text; *scan != '\0' && !full; ) {
		const char *piece = scan;
		int pieceLength = 1;
		bool token = false;

		if ( scan[ 0 ] == '#' && scan[ 1 ] == '#' ) {
			scan += 2;
		} else if ( scan[ 0 ] == '#' && scan[ 1 ] != '\0' && !MPChatTokenIsWordChar( scan[ 2 ] ) ) {
			bool known;
			const char *value = MPChatTokenValue( scan[ 1 ], values, known );
			if ( known ) {
				piece = value;
				pieceLength = 0;
				while ( piece[ pieceLength ] != '\0' ) {
					pieceLength++;
				}
				token = true;
				scan += 2;
			} else {
				scan += 1;
			}
		} else {
			scan += 1;
		}

		for ( int i = 0; i < pieceLength; i++ ) {
			// a value is one line of text: control characters in it become spaces
			const unsigned char c = (unsigned char)piece[ i ];
			if ( length >= outBytes - 1 ) {
				full = true;
				break;
			}
			out[ length++ ] = ( c < 0x20 ) ? ' ' : (char)c;
		}
		if ( token ) {
			expanded++;
		}
	}

	if ( full ) {
		// back out of a UTF-8 sequence the cut went through
		int start = length;
		while ( start > 0 && ( (unsigned char)out[ start - 1 ] & 0xC0 ) == 0x80 ) {
			start--;
		}
		if ( start > 0 ) {
			const unsigned char lead = (unsigned char)out[ start - 1 ];
			const int need = ( lead >= 0xF0 ) ? 4 : ( lead >= 0xE0 ) ? 3 : ( lead >= 0xC0 ) ? 2 : 1;
			if ( length - ( start - 1 ) < need ) {
				length = start - 1;
			}
		} else {
			length = 0;
		}
		// and never end on the first half of a colour escape
		if ( length > 0 && out[ length - 1 ] == '^' && ( length < 2 || out[ length - 2 ] != '^' ) ) {
			length--;
		}
	}
	out[ length ] = '\0';
	return expanded;
}

/*
================
MPChatRejoinTokens

The console splits a line into words and puts them back with single spaces
between, so "#h" typed in the console or bound to a key reaches the say
command as "# h". This puts such a pair back together, and only such a pair:
a lone '#' at the start of the line or after a space, one space, a token
letter, and then the end of a word. Text sent from the chat box is never
split and does not pass through here.
================
*/
inline void MPChatRejoinTokens( const char *text, char *out, int outBytes ) {
	int length = 0;

	if ( out == 0 || outBytes <= 0 ) {
		return;
	}
	if ( text != 0 ) {
		for ( int i = 0; text[ i ] != '\0' && length < outBytes - 1; i++ ) {
			out[ length++ ] = text[ i ];
			if ( text[ i ] != '#' || ( i > 0 && text[ i - 1 ] != ' ' ) || text[ i + 1 ] != ' ' ) {
				continue;
			}
			bool known;
			const mpChatTokenValues_t none = { 0, 0, 0, 0, 0 };
			MPChatTokenValue( text[ i + 2 ], none, known );
			if ( known && !MPChatTokenIsWordChar( text[ i + 3 ] ) ) {
				i++;	// drop the space the console put between the hash and its letter
			}
		}
	}
	out[ length ] = '\0';
}

#ifndef MP_CLIENT_OPTIONS_CORE_ONLY
class idPlayer;
class idStr;

// Expands text for the local player who is about to send it. A spectator, a
// dedicated server's console and a line without tokens come back unchanged.
// consoleWords says the line was typed as separate console words, which the
// console has put back together with spaces. Returns the number of tokens
// replaced.
int MPChatExpandForPlayer( idPlayer *player, const char *text, bool consoleWords, idStr &expanded );
#endif

#endif	/* !__GAME_MP_CHATTOKENS_H__ */
