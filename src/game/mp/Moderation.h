//----------------------------------------------------------------
// Moderation.h
//
// openQ4 server administration and moderation (MP-18). Quake 4 keyed its ban
// list to a GUID the client never sends, so a ban matched nobody. This is the
// part of the replacement that owns no engine types:
//
//   - addresses and ranges: IPv4 and IPv6 text as the engine prints it
//     ("192.0.2.7:28004", "[2001:db8::1]:28004", "localhost:28004") or as an
//     operator types it ("192.0.2.0/24", "2001:db8::/48"), one canonical form
//   - the ban list: ranges with an expiry, a name and a reason, and the text
//     file they are kept in
//   - text mutes that follow an address across a reconnect
//   - the chat flood window (Quake II's flood_msgs / flood_persecond /
//     flood_waitdelay)
//   - the name policy: control characters, length, reserved names
//   - the NDJSON lines of the admin log
//
// A ban line names its kind: an address range, or (MP-17) a player identity,
// the sixteen hex digits of the key that player holds on this server.
// tools/tests/native/MpModerationTest.cpp compiles this alone.
//----------------------------------------------------------------

#ifndef __GAME_MP_MODERATION_H__
#define __GAME_MP_MODERATION_H__

#include <string.h>
#include <stdio.h>

// idlib replaces the C library's snprintf with idStr::snPrintf; the native test
// has the C library alone. Both truncate and terminate.
#ifdef snprintf
#define MPModPrintf idStr::snPrintf
#else
#define MPModPrintf snprintf
#endif

typedef long long mpModTime_t;		// seconds since 1970-01-01 UTC

static const int MP_MOD_NAME_BYTES = 64;
static const int MP_MOD_REASON_BYTES = 128;
static const int MP_MOD_RANGE_TEXT_BYTES = 64;		// "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128"
static const int MP_MOD_IDENTITY_BYTES = 17;		// MP-17: an identity's sixteen hex digits
#define MP_MOD_IDENTITY_PREFIX	"id:"				// how commands and menus write one: "id:00112233aabbccdd"
static const int MP_MOD_MAX_BANS = 512;
static const int MP_MOD_MAX_MUTES = 64;
static const int MP_MOD_LINE_BYTES = 512;
static const int MP_MOD_LOG_LINE_BYTES = 1024;
static const int MP_MOD_UTC_TEXT_BYTES = 24;		// "2026-11-22T10:15:00Z"
// a range wider than these would turn away a large part of the Internet on a typo
static const int MP_MOD_MIN_PREFIX_V4 = 8;
static const int MP_MOD_MIN_PREFIX_V6 = 16;
static const mpModTime_t MP_MOD_MAX_DURATION_SECONDS = 10LL * 365 * 24 * 3600;
// the first line of the ban file; Quake 4's binary banlist.txt never starts with it
#define MP_MOD_BANLIST_HEADER	"openq4-banlist 1"
// a name that shows nothing becomes this
#define MP_MOD_DEFAULT_NAME		"Player"
static const int MP_MOD_NAME_VISIBLE_MAX = 32;
static const int MP_MOD_FLOOD_MAX_MESSAGES = 16;

//----------------------------------------------------------------
// addresses and ranges
//----------------------------------------------------------------

typedef struct mpModAddress_s {
	unsigned char	bytes[ 16 ];	// IPv4 uses the first four
	int				family;			// 0 none, 4 or 6
} mpModAddress_t;

inline int MPModAddressBits( const mpModAddress_t &address ) {
	return address.family == 4 ? 32 : ( address.family == 6 ? 128 : 0 );
}

inline void MPModClearAddress( mpModAddress_t &address ) {
	memset( &address, 0, sizeof( address ) );
}

inline bool MPModParseIPv4( const char *text, int length, unsigned char out[ 4 ] ) {
	int octet = 0;
	int digits = 0;
	int value = 0;
	for ( int i = 0; i <= length; i++ ) {
		const char c = i < length ? text[ i ] : '.';
		if ( c >= '0' && c <= '9' ) {
			value = value * 10 + ( c - '0' );
			if ( ++digits > 3 || value > 255 ) {
				return false;
			}
		} else if ( c == '.' ) {
			if ( digits == 0 || octet >= 4 ) {
				return false;
			}
			out[ octet++ ] = static_cast<unsigned char>( value );
			value = 0;
			digits = 0;
		} else {
			return false;
		}
	}
	return octet == 4;
}

inline bool MPModParseIPv6( const char *text, int length, unsigned char out[ 16 ] ) {
	unsigned int groups[ 8 ];
	int count = 0;
	int compressAt = -1;
	int i = 0;

	if ( length < 2 ) {
		return false;
	}
	if ( text[ 0 ] == ':' ) {
		if ( text[ 1 ] != ':' ) {
			return false;
		}
		compressAt = 0;
		i = 2;
	}
	while ( i < length ) {
		if ( count >= 8 ) {
			return false;
		}
		const int start = i;
		bool dotted = false;
		while ( i < length && text[ i ] != ':' ) {
			if ( text[ i ] == '.' ) {
				dotted = true;
			}
			i++;
		}
		const int tokenLength = i - start;
		if ( dotted ) {
			// an embedded IPv4 address is the last 32 bits and nothing follows it
			unsigned char v4[ 4 ];
			if ( i != length || count > 6 || !MPModParseIPv4( text + start, tokenLength, v4 ) ) {
				return false;
			}
			groups[ count++ ] = ( static_cast<unsigned int>( v4[ 0 ] ) << 8 ) | v4[ 1 ];
			groups[ count++ ] = ( static_cast<unsigned int>( v4[ 2 ] ) << 8 ) | v4[ 3 ];
			break;
		}
		if ( tokenLength < 1 || tokenLength > 4 ) {
			return false;
		}
		unsigned int value = 0;
		for ( int k = start; k < i; k++ ) {
			const char c = text[ k ];
			int digit;
			if ( c >= '0' && c <= '9' ) {
				digit = c - '0';
			} else if ( c >= 'a' && c <= 'f' ) {
				digit = c - 'a' + 10;
			} else if ( c >= 'A' && c <= 'F' ) {
				digit = c - 'A' + 10;
			} else {
				return false;
			}
			value = ( value << 4 ) | static_cast<unsigned int>( digit );
		}
		groups[ count++ ] = value;
		if ( i < length ) {
			i++;	// the ':' after this group
			if ( i < length && text[ i ] == ':' ) {
				if ( compressAt >= 0 ) {
					return false;
				}
				compressAt = count;
				i++;
			} else if ( i >= length ) {
				return false;	// a single trailing ':'
			}
		}
	}

	unsigned int full[ 8 ];
	if ( compressAt < 0 ) {
		if ( count != 8 ) {
			return false;
		}
		for ( int g = 0; g < 8; g++ ) {
			full[ g ] = groups[ g ];
		}
	} else {
		if ( count > 7 ) {
			return false;	// "::" stands for at least one group
		}
		const int zeros = 8 - count;
		int g = 0;
		for ( int k = 0; k < compressAt; k++ ) {
			full[ g++ ] = groups[ k ];
		}
		for ( int k = 0; k < zeros; k++ ) {
			full[ g++ ] = 0;
		}
		for ( int k = compressAt; k < count; k++ ) {
			full[ g++ ] = groups[ k ];
		}
	}
	for ( int g = 0; g < 8; g++ ) {
		out[ g * 2 ] = static_cast<unsigned char>( full[ g ] >> 8 );
		out[ g * 2 + 1 ] = static_cast<unsigned char>( full[ g ] & 0xff );
	}
	return true;
}

// Zero the bits past the prefix, so one range has one spelling.
inline void MPModMaskAddress( mpModAddress_t &address, int prefixBits ) {
	const int bits = MPModAddressBits( address );
	for ( int bit = prefixBits < 0 ? 0 : prefixBits; bit < bits; bit++ ) {
		address.bytes[ bit >> 3 ] &= static_cast<unsigned char>( ~( 0x80 >> ( bit & 7 ) ) );
	}
}

inline bool MPModAddressInRange( const mpModAddress_t &address, const mpModAddress_t &range, int prefixBits ) {
	if ( address.family == 0 || address.family != range.family || prefixBits < 0 || prefixBits > MPModAddressBits( range ) ) {
		return false;
	}
	const int whole = prefixBits >> 3;
	if ( whole > 0 && memcmp( address.bytes, range.bytes, whole ) != 0 ) {
		return false;
	}
	const int rest = prefixBits & 7;
	if ( rest != 0 ) {
		const unsigned char mask = static_cast<unsigned char>( 0xff << ( 8 - rest ) );
		if ( ( address.bytes[ whole ] & mask ) != ( range.bytes[ whole ] & mask ) ) {
			return false;
		}
	}
	return true;
}

inline bool MPModSameAddress( const mpModAddress_t &a, const mpModAddress_t &b ) {
	return a.family != 0 && MPModAddressInRange( a, b, MPModAddressBits( b ) );
}

// The range one player is taken to hold: an IPv4 address, or an IPv6 /64. A home
// connection is given a whole IPv6 /64 and a device takes new addresses inside it
// whenever it likes, so one IPv6 address names nobody for longer than they choose.
// A ban of a connected player, a mute that outlasts a reconnect and the test for
// "the same address" all use it. The engine's connection limit takes the same view
// (AsyncServer_SameSubscriber).
static const int MP_MOD_PLAYER_PREFIX_V6 = 64;

inline int MPModPlayerPrefixBits( const mpModAddress_t &address ) {
	return address.family == 6 ? MP_MOD_PLAYER_PREFIX_V6 : MPModAddressBits( address );
}

inline bool MPModSamePlayer( const mpModAddress_t &a, const mpModAddress_t &b ) {
	return a.family != 0 && MPModAddressInRange( a, b, MPModPlayerPrefixBits( b ) );
}

// Reads an address with an optional port and an optional "/prefix". Without a
// prefix the range is the one address. An IPv4 peer that arrives through the
// mapped IPv6 range is the IPv4 address, as the engine has it. False for
// anything else, including "bot", "bad" and an empty string.
inline bool MPModParseAddress( const char *text, mpModAddress_t &address, int &prefixBits ) {
	MPModClearAddress( address );
	prefixBits = 0;
	if ( text == 0 ) {
		return false;
	}
	while ( *text == ' ' || *text == '\t' ) {
		text++;
	}
	int length = static_cast<int>( strlen( text ) );
	while ( length > 0 && ( text[ length - 1 ] == ' ' || text[ length - 1 ] == '\t' || text[ length - 1 ] == '\r' || text[ length - 1 ] == '\n' ) ) {
		length--;
	}
	if ( length <= 0 || length >= 128 ) {
		return false;
	}

	// "/prefix" closes the text
	int prefix = -1;
	for ( int i = length - 1; i >= 0 && i >= length - 4; i-- ) {
		if ( text[ i ] == '/' ) {
			if ( i == length - 1 ) {
				return false;
			}
			prefix = 0;
			for ( int k = i + 1; k < length; k++ ) {
				if ( text[ k ] < '0' || text[ k ] > '9' ) {
					return false;
				}
				prefix = prefix * 10 + ( text[ k ] - '0' );
			}
			length = i;
			break;
		}
		if ( text[ i ] < '0' || text[ i ] > '9' ) {
			break;
		}
	}
	if ( length <= 0 ) {
		return false;
	}

	const char *host = text;
	int hostLength = length;
	const char *port = 0;
	int portLength = 0;
	bool v6 = false;

	if ( text[ 0 ] == '[' ) {
		int close = -1;
		for ( int i = 1; i < length; i++ ) {
			if ( text[ i ] == ']' ) {
				close = i;
				break;
			}
		}
		if ( close < 0 ) {
			return false;
		}
		host = text + 1;
		hostLength = close - 1;
		v6 = true;
		if ( close + 1 < length ) {
			if ( text[ close + 1 ] != ':' ) {
				return false;
			}
			port = text + close + 2;
			portLength = length - close - 2;
		}
	} else {
		int colons = 0;
		int lastColon = -1;
		for ( int i = 0; i < length; i++ ) {
			if ( text[ i ] == ':' ) {
				colons++;
				lastColon = i;
			}
		}
		if ( colons > 1 ) {
			v6 = true;
		} else if ( colons == 1 ) {
			hostLength = lastColon;
			port = text + lastColon + 1;
			portLength = length - lastColon - 1;
		}
	}
	if ( port != 0 ) {
		if ( portLength < 1 || portLength > 5 ) {
			return false;
		}
		int value = 0;
		for ( int i = 0; i < portLength; i++ ) {
			if ( port[ i ] < '0' || port[ i ] > '9' ) {
				return false;
			}
			value = value * 10 + ( port[ i ] - '0' );
		}
		if ( value > 65535 ) {
			return false;
		}
	}

	if ( v6 ) {
		// a link-local scope ("%3") names an interface, not an address
		for ( int i = 0; i < hostLength; i++ ) {
			if ( host[ i ] == '%' ) {
				hostLength = i;
				break;
			}
		}
		if ( !MPModParseIPv6( host, hostLength, address.bytes ) ) {
			return false;
		}
		address.family = 6;
		static const unsigned char mapped[ 12 ] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
		if ( memcmp( address.bytes, mapped, sizeof( mapped ) ) == 0 ) {
			if ( prefix >= 0 ) {
				if ( prefix < 96 ) {
					MPModClearAddress( address );
					return false;
				}
				prefix -= 96;
			}
			memmove( address.bytes, address.bytes + 12, 4 );
			memset( address.bytes + 4, 0, 12 );
			address.family = 4;
		}
	} else {
		static const char localhost[] = "localhost";
		bool isLocalhost = hostLength == static_cast<int>( sizeof( localhost ) - 1 );
		for ( int i = 0; isLocalhost && i < hostLength; i++ ) {
			const char c = host[ i ];
			if ( ( c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c ) != localhost[ i ] ) {
				isLocalhost = false;
			}
		}
		if ( isLocalhost ) {
			address.bytes[ 0 ] = 127;
			address.bytes[ 3 ] = 1;
		} else if ( !MPModParseIPv4( host, hostLength, address.bytes ) ) {
			MPModClearAddress( address );
			return false;
		}
		address.family = 4;
	}

	const int bits = MPModAddressBits( address );
	if ( prefix < 0 ) {
		prefix = bits;
	}
	if ( prefix > bits ) {
		MPModClearAddress( address );
		return false;
	}
	prefixBits = prefix;
	return true;
}

inline bool MPModRangeIsAllowed( const mpModAddress_t &address, int prefixBits ) {
	if ( address.family == 4 ) {
		return prefixBits >= MP_MOD_MIN_PREFIX_V4 && prefixBits <= 32;
	}
	if ( address.family == 6 ) {
		return prefixBits >= MP_MOD_MIN_PREFIX_V6 && prefixBits <= 128;
	}
	return false;
}

// "192.0.2.7" or RFC 5952 text ("2001:db8::1"): lower case, the longest run of
// zero groups compressed, the first one when two are as long.
inline void MPModFormatAddress( const mpModAddress_t &address, char *out, int outSize ) {
	if ( outSize <= 0 ) {
		return;
	}
	out[ 0 ] = '\0';
	if ( address.family == 4 ) {
		MPModPrintf( out, outSize, "%d.%d.%d.%d", address.bytes[ 0 ], address.bytes[ 1 ], address.bytes[ 2 ], address.bytes[ 3 ] );
		return;
	}
	if ( address.family != 6 ) {
		return;
	}
	unsigned int groups[ 8 ];
	for ( int g = 0; g < 8; g++ ) {
		groups[ g ] = ( static_cast<unsigned int>( address.bytes[ g * 2 ] ) << 8 ) | address.bytes[ g * 2 + 1 ];
	}
	int bestStart = -1;
	int bestLength = 0;
	for ( int g = 0; g < 8; ) {
		if ( groups[ g ] != 0 ) {
			g++;
			continue;
		}
		const int start = g;
		while ( g < 8 && groups[ g ] == 0 ) {
			g++;
		}
		if ( g - start > bestLength ) {
			bestStart = start;
			bestLength = g - start;
		}
	}
	if ( bestLength < 2 ) {
		bestStart = -1;
	}
	char text[ 48 ];
	int length = 0;
	for ( int g = 0; g < 8; ) {
		if ( g == bestStart ) {
			text[ length++ ] = ':';
			text[ length++ ] = ':';
			g += bestLength;
			continue;
		}
		if ( length > 0 && text[ length - 1 ] != ':' ) {
			text[ length++ ] = ':';
		}
		length += MPModPrintf( text + length, static_cast<int>( sizeof( text ) ) - length, "%x", groups[ g ] );
		g++;
	}
	text[ length ] = '\0';
	MPModPrintf( out, outSize, "%s", text );
}

inline void MPModFormatRange( const mpModAddress_t &address, int prefixBits, char *out, int outSize ) {
	char text[ 48 ];
	MPModFormatAddress( address, text, sizeof( text ) );
	if ( outSize > 0 ) {
		MPModPrintf( out, outSize, "%s/%d", text, prefixBits );
	}
}

//----------------------------------------------------------------
// text an operator or a player supplied
//----------------------------------------------------------------

// The length in bytes of the well-formed UTF-8 character at s and its code point,
// or 0 when the byte there begins none: a stray continuation byte, a lead byte
// short of its continuation bytes, an overlong form, a surrogate, or a value past
// U+10FFFF. An ASCII byte, the terminating NUL included, is one byte.
inline int MPModUtf8Decode( const char *s, unsigned int &value ) {
	const unsigned char c = static_cast<unsigned char>( s[ 0 ] );
	value = c;
	if ( c < 0x80 ) {
		return 1;
	}
	int length;
	if ( c >= 0xc2 && c <= 0xdf ) {
		length = 2;
		value = c & 0x1fu;
	} else if ( c >= 0xe0 && c <= 0xef ) {
		length = 3;
		value = c & 0x0fu;
	} else if ( c >= 0xf0 && c <= 0xf4 ) {
		length = 4;
		value = c & 0x07u;
	} else {
		return 0;
	}
	for ( int k = 1; k < length; k++ ) {
		const unsigned char next = static_cast<unsigned char>( s[ k ] );
		if ( ( next & 0xc0 ) != 0x80 ) {
			return 0;
		}
		value = ( value << 6 ) | ( next & 0x3fu );
	}
	if ( ( length == 3 && ( value < 0x800 || ( value >= 0xd800 && value <= 0xdfff ) ) ) ||
		( length == 4 && ( value < 0x10000 || value > 0x10ffff ) ) ) {
		return 0;
	}
	return length;
}

// Copies text for a list or a log: no control characters, only well-formed
// UTF-8, bounded, terminated, and never cut inside a character.
inline void MPModCopyText( char *out, int outSize, const char *in ) {
	if ( outSize <= 0 ) {
		return;
	}
	int length = 0;
	while ( in != 0 && *in != '\0' ) {
		unsigned int value = 0;
		const int bytes = MPModUtf8Decode( in, value );
		if ( bytes == 0 || value < 0x20 || value == 0x7f || ( value >= 0x80 && value <= 0x9f ) ) {
			in += bytes > 0 ? bytes : 1;
			continue;
		}
		if ( length + bytes > outSize - 1 ) {
			break;
		}
		for ( int k = 0; k < bytes; k++ ) {
			out[ length++ ] = in[ k ];
		}
		in += bytes;
	}
	out[ length ] = '\0';
}

// A command's line cut into words at spaces and tabs and nowhere else. The
// console's tokenizer is a script lexer: it cuts "192.0.2.0/24" and "10m" in
// two, reads "3rd" as a number and a word, splits an IPv6 address at every
// colon, and takes "1e0f" inside one for a float, losing the "f". A command
// that reads addresses, lengths of time and free text reads its line with this
// instead, when the engine provides the line (cmd_rawText). A word in double
// quotes keeps its spaces and loses the quotes; a word that begins "//" ends
// the line, as a comment does in a script. False when the line does not fit.
static const int MP_MOD_MAX_WORDS = 64;
static const int MP_MOD_WORDS_BYTES = 1024;

typedef struct mpModWords_s {
	int				count;
	const char *	word[ MP_MOD_MAX_WORDS ];
	char			text[ MP_MOD_WORDS_BYTES ];
} mpModWords_t;

inline bool MPModSplitWords( const char *line, mpModWords_t &words ) {
	words.count = 0;
	int used = 0;
	const char *at = line != 0 ? line : "";
	for ( ;; ) {
		while ( *at == ' ' || *at == '\t' || *at == '\n' || *at == '\r' ) {
			at++;
		}
		if ( *at == '\0' || ( at[ 0 ] == '/' && at[ 1 ] == '/' ) ) {
			return true;
		}
		if ( words.count >= MP_MOD_MAX_WORDS ) {
			return false;
		}
		words.word[ words.count ] = words.text + used;
		const bool quoted = *at == '"';
		if ( quoted ) {
			at++;
		}
		while ( *at != '\0' && ( quoted ? *at != '"' : ( *at != ' ' && *at != '\t' && *at != '\n' && *at != '\r' ) ) ) {
			if ( used >= MP_MOD_WORDS_BYTES - 1 ) {
				return false;
			}
			words.text[ used++ ] = *at++;
		}
		if ( quoted && *at == '"' ) {
			at++;
		}
		if ( used >= MP_MOD_WORDS_BYTES ) {
			return false;
		}
		words.text[ used++ ] = '\0';
		words.count++;
	}
}

// "30m", "2h", "7d", "1w", "45s"; a bare number is minutes. "perm", "permanent",
// "forever" and "0" are no expiry and give 0 seconds. False for anything else,
// so a reason is never read as a time.
inline bool MPModParseDuration( const char *text, mpModTime_t &seconds ) {
	seconds = 0;
	if ( text == 0 || *text == '\0' ) {
		return false;
	}
	static const char *const never[] = { "perm", "permanent", "forever", "0" };
	for ( int i = 0; i < 4; i++ ) {
		const char *a = text;
		const char *b = never[ i ];
		while ( *a != '\0' && *b != '\0' && ( *a >= 'A' && *a <= 'Z' ? *a - 'A' + 'a' : *a ) == *b ) {
			a++;
			b++;
		}
		if ( *a == '\0' && *b == '\0' ) {
			return true;
		}
	}
	mpModTime_t value = 0;
	int digits = 0;
	while ( *text >= '0' && *text <= '9' ) {
		value = value * 10 + ( *text - '0' );
		if ( ++digits > 9 ) {
			return false;
		}
		text++;
	}
	if ( digits == 0 || value <= 0 ) {
		return false;
	}
	mpModTime_t unit = 60;
	if ( *text != '\0' ) {
		switch ( *text ) {
			case 's': case 'S': unit = 1; break;
			case 'm': case 'M': unit = 60; break;
			case 'h': case 'H': unit = 3600; break;
			case 'd': case 'D': unit = 86400; break;
			case 'w': case 'W': unit = 7 * 86400; break;
			default: return false;
		}
		if ( text[ 1 ] != '\0' ) {
			return false;
		}
	}
	seconds = value * unit;
	if ( seconds > MP_MOD_MAX_DURATION_SECONDS ) {
		seconds = MP_MOD_MAX_DURATION_SECONDS;
	}
	return true;
}

// The console's lexer splits what an operator typed as one word: "192.0.2.0/24"
// arrives as "192.0.2.0" "/24", "10m" as "10" "m", and an IPv6 address in
// pieces at its colons; rcon then sends the pieces on with spaces between. The
// two functions below put a word back together from the arguments it became.

// An address at argv[first]: one argument, with a "/prefix" argument after it,
// or everything from "[" to "]" for an IPv6 address ("[2001:db8::/48]",
// "[2001:db8::1]/64"). Returns how many arguments it took, at least one.
inline int MPModJoinAddressArgs( int argc, const char *const *argv, int first, char *out, int outSize ) {
	if ( outSize <= 0 ) {
		return 1;
	}
	out[ 0 ] = '\0';
	if ( first < 0 || first >= argc ) {
		return 1;
	}
	int used = 1;
	int length = 0;
	const char *text = argv[ first ];
	bool bracketed = text[ 0 ] == '[';
	bool closed = false;
	for ( int arg = first; arg < argc; arg++ ) {
		for ( const char *in = argv[ arg ]; *in != '\0' && length < outSize - 1; in++ ) {
			out[ length++ ] = *in;
			if ( *in == ']' ) {
				closed = true;
			}
		}
		used = arg - first + 1;
		if ( !bracketed || closed ) {
			break;
		}
	}
	out[ length ] = '\0';
	if ( bracketed && !closed ) {
		// no closing bracket: the first argument alone, which no parser accepts
		MPModPrintf( out, outSize, "%s", argv[ first ] );
		return 1;
	}
	if ( bracketed ) {
		// "[address]:port" keeps its brackets; anything else is the text between them
		const char *close = strchr( out, ']' );
		if ( close[ 1 ] != ':' ) {
			int kept = 0;
			for ( int i = 0; i < length; i++ ) {
				if ( out[ i ] != '[' && out[ i ] != ']' ) {
					out[ kept++ ] = out[ i ];
				}
			}
			length = kept;
			out[ length ] = '\0';
		}
	}
	// "/24", or "/" and "24"
	int next = first + used;
	if ( next < argc && argv[ next ][ 0 ] == '/' ) {
		const char *digits = argv[ next ] + 1;
		int taken = 1;
		if ( *digits == '\0' && next + 1 < argc ) {
			digits = argv[ next + 1 ];
			taken = 2;
		}
		int count = 0;
		while ( digits[ count ] >= '0' && digits[ count ] <= '9' ) {
			count++;
		}
		if ( count >= 1 && count <= 3 && digits[ count ] == '\0' && length + count + 1 < outSize ) {
			out[ length++ ] = '/';
			for ( int i = 0; i < count; i++ ) {
				out[ length++ ] = digits[ i ];
			}
			out[ length ] = '\0';
			used += taken;
		}
	}
	return used;
}

// A time at argv[first]: one argument ("perm", "30"), or a number and its unit
// as two ("10" "m"). Returns how many arguments it took, 0 when there is no
// time there.
inline int MPModJoinDurationArgs( int argc, const char *const *argv, int first, mpModTime_t &seconds ) {
	seconds = 0;
	if ( first < 0 || first >= argc ) {
		return 0;
	}
	const char *number = argv[ first ];
	if ( first + 1 < argc && argv[ first + 1 ][ 0 ] != '\0' && argv[ first + 1 ][ 1 ] == '\0' ) {
		int digits = 0;
		while ( number[ digits ] >= '0' && number[ digits ] <= '9' ) {
			digits++;
		}
		if ( digits >= 1 && digits <= 9 && number[ digits ] == '\0' ) {
			char joined[ 16 ];
			MPModPrintf( joined, static_cast<int>( sizeof( joined ) ), "%s%s", number, argv[ first + 1 ] );
			if ( MPModParseDuration( joined, seconds ) ) {
				return 2;
			}
		}
	}
	return MPModParseDuration( number, seconds ) ? 1 : 0;
}

// "permanent", "3d 4h", "2h 5m", "12m", "45s"
inline void MPModFormatDuration( mpModTime_t seconds, char *out, int outSize ) {
	if ( outSize <= 0 ) {
		return;
	}
	if ( seconds <= 0 ) {
		MPModPrintf( out, outSize, "permanent" );
	} else if ( seconds >= 86400 ) {
		MPModPrintf( out, outSize, "%lldd %lldh", seconds / 86400, ( seconds % 86400 ) / 3600 );
	} else if ( seconds >= 3600 ) {
		MPModPrintf( out, outSize, "%lldh %lldm", seconds / 3600, ( seconds % 3600 ) / 60 );
	} else if ( seconds >= 60 ) {
		MPModPrintf( out, outSize, "%lldm", ( seconds + 59 ) / 60 );
	} else {
		MPModPrintf( out, outSize, "%llds", seconds );
	}
}

// "2026-11-22T10:15:00Z" from seconds since 1970, without the C library's
// time zone state.
inline void MPModFormatUtc( mpModTime_t unixSeconds, char out[ MP_MOD_UTC_TEXT_BYTES ] ) {
	if ( unixSeconds < 0 ) {
		unixSeconds = 0;
	}
	const mpModTime_t days = unixSeconds / 86400;
	const int daySeconds = static_cast<int>( unixSeconds % 86400 );
	// days to a civil date (Howard Hinnant's algorithm)
	const mpModTime_t z = days + 719468;
	const mpModTime_t era = z / 146097;
	const int doe = static_cast<int>( z - era * 146097 );
	const int yoe = ( doe - doe / 1460 + doe / 36524 - doe / 146096 ) / 365;
	const int doy = doe - ( 365 * yoe + yoe / 4 - yoe / 100 );
	const int mp = ( 5 * doy + 2 ) / 153;
	const int day = doy - ( 153 * mp + 2 ) / 5 + 1;
	const int month = mp < 10 ? mp + 3 : mp - 9;
	const mpModTime_t year = static_cast<mpModTime_t>( yoe ) + era * 400 + ( month <= 2 ? 1 : 0 );
	MPModPrintf( out, MP_MOD_UTC_TEXT_BYTES, "%04lld-%02d-%02dT%02d:%02d:%02dZ", year, month, day,
		daySeconds / 3600, ( daySeconds % 3600 ) / 60, daySeconds % 60 );
}

//----------------------------------------------------------------
// the ban list
//----------------------------------------------------------------

typedef struct mpModBan_s {
	mpModAddress_t	address;		// host bits zero
	int				prefixBits;
	mpModTime_t		created;
	mpModTime_t		expires;		// 0 never
	char			name[ MP_MOD_NAME_BYTES ];		// who it was when banned; a label, never matched
	char			reason[ MP_MOD_REASON_BYTES ];
	char			identity[ MP_MOD_IDENTITY_BYTES ];	// MP-17: an identity ban; its address is empty (family 0)
	mpModTime_t		refusedLogged;	// not in the file: when a connection this ban refused was last put in the log
} mpModBan_t;

typedef struct mpModBanList_s {
	mpModBan_t		bans[ MP_MOD_MAX_BANS ];
	int				count;
} mpModBanList_t;

inline bool MPModBanExpired( const mpModBan_t &ban, mpModTime_t now ) {
	return ban.expires != 0 && ban.expires <= now;
}

// MP-17: "id:" in front of an identity. (idlib forbids strncmp in the game.)
inline bool MPModHasIdentityPrefix( const char *text ) {
	return text != 0 && text[ 0 ] == 'i' && text[ 1 ] == 'd' && text[ 2 ] == ':';
}

// MP-17: a ban on a player identity rather than an address range.
inline bool MPModBanIsIdentity( const mpModBan_t &ban ) {
	return ban.address.family == 0 && ban.identity[ 0 ] != '\0';
}

// Sixteen hex digits, with or without "id:" in front; read back in lower case.
inline bool MPModParseIdentity( const char *text, char out[ MP_MOD_IDENTITY_BYTES ] ) {
	if ( text == 0 ) {
		return false;
	}
	if ( MPModHasIdentityPrefix( text ) ) {
		text += sizeof( MP_MOD_IDENTITY_PREFIX ) - 1;
	}
	if ( strlen( text ) != MP_MOD_IDENTITY_BYTES - 1 ) {
		return false;
	}
	for ( int i = 0; i < MP_MOD_IDENTITY_BYTES - 1; i++ ) {
		char c = text[ i ];
		if ( c >= 'A' && c <= 'F' ) {
			c = static_cast<char>( c - 'A' + 'a' );
		}
		if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) ) ) {
			return false;
		}
		out[ i ] = c;
	}
	out[ MP_MOD_IDENTITY_BYTES - 1 ] = '\0';
	return true;
}

// The identity ban in force for this identity, or -1.
inline int MPModBanFindIdentity( const mpModBanList_t &list, const char *identity, mpModTime_t now ) {
	if ( identity == 0 || identity[ 0 ] == '\0' ) {
		return -1;
	}
	for ( int i = 0; i < list.count; i++ ) {
		const mpModBan_t &ban = list.bans[ i ];
		if ( MPModBanIsIdentity( ban ) && !MPModBanExpired( ban, now ) && strcmp( ban.identity, identity ) == 0 ) {
			return i;
		}
	}
	return -1;
}

// The first ban in force that covers the address, or -1.
inline int MPModBanFind( const mpModBanList_t &list, const mpModAddress_t &address, mpModTime_t now ) {
	for ( int i = 0; i < list.count; i++ ) {
		const mpModBan_t &ban = list.bans[ i ];
		if ( !MPModBanExpired( ban, now ) && MPModAddressInRange( address, ban.address, ban.prefixBits ) ) {
			return i;
		}
	}
	return -1;
}

inline int MPModBanFindRange( const mpModBanList_t &list, const mpModAddress_t &address, int prefixBits ) {
	mpModAddress_t masked = address;
	MPModMaskAddress( masked, prefixBits );
	for ( int i = 0; i < list.count; i++ ) {
		const mpModBan_t &ban = list.bans[ i ];
		if ( masked.family != 0 && ban.prefixBits == prefixBits && ban.address.family == masked.family &&
			memcmp( ban.address.bytes, masked.bytes, sizeof( masked.bytes ) ) == 0 ) {
			return i;
		}
	}
	return -1;
}

inline bool MPModBanRemove( mpModBanList_t &list, int index ) {
	if ( index < 0 || index >= list.count ) {
		return false;
	}
	for ( int i = index; i < list.count - 1; i++ ) {
		list.bans[ i ] = list.bans[ i + 1 ];
	}
	list.count--;
	return true;
}

inline int MPModBanPurgeExpired( mpModBanList_t &list, mpModTime_t now ) {
	int removed = 0;
	for ( int i = list.count - 1; i >= 0; i-- ) {
		if ( MPModBanExpired( list.bans[ i ], now ) ) {
			MPModBanRemove( list, i );
			removed++;
		}
	}
	return removed;
}

// Adds a range, or replaces the ban already on exactly that range. Returns its
// index, or -1 for a range that is not allowed or a list that is full of bans
// still in force.
inline int MPModBanAdd( mpModBanList_t &list, const mpModAddress_t &address, int prefixBits, mpModTime_t now,
		mpModTime_t durationSeconds, const char *name, const char *reason ) {
	if ( !MPModRangeIsAllowed( address, prefixBits ) ) {
		return -1;
	}
	int index = MPModBanFindRange( list, address, prefixBits );
	if ( index < 0 ) {
		if ( list.count >= MP_MOD_MAX_BANS ) {
			MPModBanPurgeExpired( list, now );
		}
		if ( list.count >= MP_MOD_MAX_BANS ) {
			return -1;
		}
		index = list.count++;
	}
	mpModBan_t &ban = list.bans[ index ];
	memset( &ban, 0, sizeof( ban ) );
	ban.address = address;
	MPModMaskAddress( ban.address, prefixBits );
	ban.prefixBits = prefixBits;
	ban.created = now;
	if ( durationSeconds > MP_MOD_MAX_DURATION_SECONDS ) {
		durationSeconds = MP_MOD_MAX_DURATION_SECONDS;
	}
	ban.expires = durationSeconds > 0 ? now + durationSeconds : 0;
	MPModCopyText( ban.name, sizeof( ban.name ), name );
	MPModCopyText( ban.reason, sizeof( ban.reason ), reason );
	return index;
}

// MP-17: adds an identity ban, or replaces the one already on that identity.
// Returns its index, or -1 for no identity or a list full of bans in force.
inline int MPModBanAddIdentity( mpModBanList_t &list, const char *identity, mpModTime_t now,
		mpModTime_t durationSeconds, const char *name, const char *reason ) {
	char canonical[ MP_MOD_IDENTITY_BYTES ];
	if ( !MPModParseIdentity( identity, canonical ) ) {
		return -1;
	}
	int index = -1;
	for ( int i = 0; i < list.count && index < 0; i++ ) {
		if ( MPModBanIsIdentity( list.bans[ i ] ) && strcmp( list.bans[ i ].identity, canonical ) == 0 ) {
			index = i;
		}
	}
	if ( index < 0 ) {
		if ( list.count >= MP_MOD_MAX_BANS ) {
			MPModBanPurgeExpired( list, now );
		}
		if ( list.count >= MP_MOD_MAX_BANS ) {
			return -1;
		}
		index = list.count++;
	}
	mpModBan_t &ban = list.bans[ index ];
	memset( &ban, 0, sizeof( ban ) );
	memcpy( ban.identity, canonical, sizeof( canonical ) );
	ban.created = now;
	if ( durationSeconds > MP_MOD_MAX_DURATION_SECONDS ) {
		durationSeconds = MP_MOD_MAX_DURATION_SECONDS;
	}
	ban.expires = durationSeconds > 0 ? now + durationSeconds : 0;
	MPModCopyText( ban.name, sizeof( ban.name ), name );
	MPModCopyText( ban.reason, sizeof( ban.reason ), reason );
	return index;
}

// What a ban covers, as the commands and menus write it: "id:<identity>" or
// the range.
inline void MPModBanTarget( const mpModBan_t &ban, char *out, int outSize ) {
	if ( MPModBanIsIdentity( ban ) ) {
		MPModPrintf( out, outSize, "%s%s", MP_MOD_IDENTITY_PREFIX, ban.identity );
	} else {
		MPModFormatRange( ban.address, ban.prefixBits, out, outSize );
	}
}

// The row of the ban on exactly this target, as the commands and menus write
// one: "id:<identity>", or an address and prefix. -1 for none.
inline int MPModBanFindTarget( const mpModBanList_t &list, const char *text ) {
	char identity[ MP_MOD_IDENTITY_BYTES ];
	if ( text != 0 && MPModHasIdentityPrefix( text ) ) {
		if ( !MPModParseIdentity( text, identity ) ) {
			return -1;
		}
		for ( int i = 0; i < list.count; i++ ) {
			if ( MPModBanIsIdentity( list.bans[ i ] ) && strcmp( list.bans[ i ].identity, identity ) == 0 ) {
				return i;
			}
		}
		return -1;
	}
	mpModAddress_t address;
	int prefixBits = 0;
	if ( text == 0 || !MPModParseAddress( text, address, prefixBits ) ) {
		return -1;
	}
	return MPModBanFindRange( list, address, prefixBits );
}

// One ban as a line of the file, without the line end:
//   ban address 192.0.2.0/24 1795000000 0 "name" "reason"
//   ban identity 00112233aabbccdd 1795000000 0 "name" "reason"
// Returns the length, or 0 when it does not fit.
inline int MPModBanFormatLine( const mpModBan_t &ban, char *out, int outSize ) {
	char range[ MP_MOD_RANGE_TEXT_BYTES ];
	int length = 0;
	if ( MPModBanIsIdentity( ban ) ) {
		length = MPModPrintf( out, outSize, "ban identity %s %lld %lld ", ban.identity, ban.created, ban.expires );
	} else {
		MPModFormatRange( ban.address, ban.prefixBits, range, sizeof( range ) );
		length = MPModPrintf( out, outSize, "ban address %s %lld %lld ", range, ban.created, ban.expires );
	}
	if ( length <= 0 || length >= outSize ) {
		return 0;
	}
	const char *const fields[ 2 ] = { ban.name, ban.reason };
	for ( int field = 0; field < 2; field++ ) {
		if ( length + 2 >= outSize ) {
			return 0;
		}
		out[ length++ ] = '"';
		for ( const char *in = fields[ field ]; *in != '\0'; in++ ) {
			if ( length + 3 >= outSize ) {
				return 0;
			}
			if ( *in == '"' || *in == '\\' ) {
				out[ length++ ] = '\\';
			}
			out[ length++ ] = *in;
		}
		out[ length++ ] = '"';
		if ( field == 0 ) {
			out[ length++ ] = ' ';
		}
	}
	if ( length >= outSize ) {
		return 0;
	}
	out[ length ] = '\0';
	return length;
}

// Reads one token of a ban line: a quoted string with \" and \\, or a run
// without spaces. Returns the text after it, or 0 at the end or on an
// unterminated quote.
inline const char *MPModReadToken( const char *text, char *out, int outSize ) {
	while ( *text == ' ' || *text == '\t' ) {
		text++;
	}
	if ( *text == '\0' || *text == '\r' || *text == '\n' || outSize <= 0 ) {
		return 0;
	}
	int length = 0;
	if ( *text == '"' ) {
		text++;
		while ( *text != '"' ) {
			if ( *text == '\0' || *text == '\r' || *text == '\n' ) {
				return 0;
			}
			if ( *text == '\\' && ( text[ 1 ] == '"' || text[ 1 ] == '\\' ) ) {
				text++;
			}
			if ( length < outSize - 1 ) {
				out[ length++ ] = *text;
			}
			text++;
		}
		text++;
	} else {
		while ( *text != '\0' && *text != ' ' && *text != '\t' && *text != '\r' && *text != '\n' ) {
			if ( length < outSize - 1 ) {
				out[ length++ ] = *text;
			}
			text++;
		}
	}
	out[ length ] = '\0';
	return text;
}

inline bool MPModParseTime( const char *text, mpModTime_t &value ) {
	value = 0;
	int digits = 0;
	for ( ; *text >= '0' && *text <= '9'; text++ ) {
		if ( ++digits > 15 ) {
			return false;
		}
		value = value * 10 + ( *text - '0' );
	}
	return digits > 0 && *text == '\0';
}

// True when the line is a ban this build understands. A comment, a blank line,
// a kind from a later build and a damaged line are all false: the caller counts
// them and keeps going.
inline bool MPModBanParseLine( const char *line, mpModBan_t &ban ) {
	char token[ MP_MOD_REASON_BYTES ];
	memset( &ban, 0, sizeof( ban ) );
	const char *at = MPModReadToken( line, token, sizeof( token ) );
	if ( at == 0 || strcmp( token, "ban" ) != 0 ) {
		return false;
	}
	at = MPModReadToken( at, token, sizeof( token ) );
	if ( at == 0 ) {
		return false;
	}
	if ( strcmp( token, "identity" ) == 0 ) {
		// MP-17: the identity is written bare, sixteen lower-case hex digits
		at = MPModReadToken( at, token, sizeof( token ) );
		if ( at == 0 || MPModHasIdentityPrefix( token ) ||
			!MPModParseIdentity( token, ban.identity ) ) {
			return false;
		}
	} else if ( strcmp( token, "address" ) == 0 ) {
		at = MPModReadToken( at, token, sizeof( token ) );
		if ( at == 0 || !MPModParseAddress( token, ban.address, ban.prefixBits ) || !MPModRangeIsAllowed( ban.address, ban.prefixBits ) ) {
			return false;
		}
		MPModMaskAddress( ban.address, ban.prefixBits );
	} else {
		return false;
	}
	at = MPModReadToken( at, token, sizeof( token ) );
	if ( at == 0 || !MPModParseTime( token, ban.created ) ) {
		return false;
	}
	at = MPModReadToken( at, token, sizeof( token ) );
	if ( at == 0 || !MPModParseTime( token, ban.expires ) ) {
		return false;
	}
	// a hand-written line may leave the name and the reason out
	at = MPModReadToken( at, token, sizeof( token ) );
	if ( at != 0 ) {
		MPModCopyText( ban.name, sizeof( ban.name ), token );
		at = MPModReadToken( at, token, sizeof( token ) );
		if ( at != 0 ) {
			MPModCopyText( ban.reason, sizeof( ban.reason ), token );
		}
	}
	return true;
}

//----------------------------------------------------------------
// text mutes that outlast a reconnect
//----------------------------------------------------------------

typedef struct mpModMute_s {
	mpModAddress_t	address;
	mpModTime_t		expires;		// 0 until the server restarts
} mpModMute_t;

typedef struct mpModMuteList_s {
	mpModMute_t		mutes[ MP_MOD_MAX_MUTES ];
	int				count;
} mpModMuteList_t;

inline int MPModMuteFind( const mpModMuteList_t &list, const mpModAddress_t &address, mpModTime_t now ) {
	for ( int i = 0; i < list.count; i++ ) {
		const mpModMute_t &mute = list.mutes[ i ];
		if ( ( mute.expires == 0 || mute.expires > now ) && MPModSamePlayer( address, mute.address ) ) {
			return i;
		}
	}
	return -1;
}

inline bool MPModMuteRemove( mpModMuteList_t &list, const mpModAddress_t &address ) {
	bool removed = false;
	for ( int i = list.count - 1; i >= 0; i-- ) {
		if ( MPModSamePlayer( address, list.mutes[ i ].address ) ) {
			list.mutes[ i ] = list.mutes[ list.count - 1 ];
			list.count--;
			removed = true;
		}
	}
	return removed;
}

// A full list drops its expired mutes, then the timed one that ends soonest.
// When every mute in it is permanent, a new timed mute is not listed (it still
// holds for the player's connection) and a new permanent one replaces the first.
inline void MPModMuteAdd( mpModMuteList_t &list, const mpModAddress_t &address, mpModTime_t now, mpModTime_t durationSeconds ) {
	if ( address.family == 0 ) {
		return;
	}
	MPModMuteRemove( list, address );
	if ( list.count >= MP_MOD_MAX_MUTES ) {
		for ( int i = list.count - 1; i >= 0; i-- ) {
			if ( list.mutes[ i ].expires != 0 && list.mutes[ i ].expires <= now ) {
				list.mutes[ i ] = list.mutes[ list.count - 1 ];
				list.count--;
			}
		}
	}
	if ( list.count >= MP_MOD_MAX_MUTES ) {
		int soonest = 0;
		for ( int i = 1; i < list.count; i++ ) {
			const mpModTime_t a = list.mutes[ i ].expires;
			const mpModTime_t b = list.mutes[ soonest ].expires;
			if ( a != 0 && ( b == 0 || a < b ) ) {
				soonest = i;
			}
		}
		if ( list.mutes[ soonest ].expires == 0 && durationSeconds > 0 ) {
			return;
		}
		list.mutes[ soonest ] = list.mutes[ list.count - 1 ];
		list.count--;
	}
	mpModMute_t &mute = list.mutes[ list.count++ ];
	mute.address = address;
	mute.expires = durationSeconds > 0 ? now + durationSeconds : 0;
}

//----------------------------------------------------------------
// the chat flood window
//----------------------------------------------------------------

typedef struct mpModFlood_s {
	int				times[ MP_MOD_FLOOD_MAX_MESSAGES ];	// game time of the last accepted lines, a ring
	int				count;
	int				next;
	int				silencedUntil;
	bool			silenced;
} mpModFlood_t;

inline void MPModFloodReset( mpModFlood_t &flood ) {
	memset( &flood, 0, sizeof( flood ) );
}

// A sender may put `messages` lines into any `windowMsec`; the line after that
// silences them for `waitMsec`. Returns 0 when this line may pass, otherwise
// the milliseconds of silence left. Lines sent while silenced do not extend it.
// messages <= 0 turns the window off.
inline int MPModFloodCheck( mpModFlood_t &flood, int now, int messages, int windowMsec, int waitMsec ) {
	if ( messages <= 0 || windowMsec <= 0 ) {
		return 0;
	}
	if ( messages > MP_MOD_FLOOD_MAX_MESSAGES ) {
		messages = MP_MOD_FLOOD_MAX_MESSAGES;
	}
	// a map restart winds game time back: stamps from the future are stale
	if ( flood.count > 0 ) {
		const int last = flood.times[ ( flood.next + MP_MOD_FLOOD_MAX_MESSAGES - 1 ) % MP_MOD_FLOOD_MAX_MESSAGES ];
		if ( now < last || ( flood.silenced && flood.silencedUntil - now > waitMsec ) ) {
			MPModFloodReset( flood );
		}
	}
	if ( flood.silenced ) {
		if ( now < flood.silencedUntil ) {
			return flood.silencedUntil - now;
		}
		flood.silenced = false;
	}
	if ( flood.count >= messages ) {
		const int oldest = flood.times[ ( flood.next + MP_MOD_FLOOD_MAX_MESSAGES - messages ) % MP_MOD_FLOOD_MAX_MESSAGES ];
		if ( now - oldest < windowMsec ) {
			flood.silenced = true;
			flood.silencedUntil = now + ( waitMsec > 0 ? waitMsec : 0 );
			return waitMsec > 0 ? waitMsec : 1;
		}
	}
	flood.times[ flood.next ] = now;
	flood.next = ( flood.next + 1 ) % MP_MOD_FLOOD_MAX_MESSAGES;
	if ( flood.count < MP_MOD_FLOOD_MAX_MESSAGES ) {
		flood.count++;
	}
	return 0;
}

//----------------------------------------------------------------
// the name policy
//----------------------------------------------------------------

// How many bytes the engine's text code takes as one escape at s. This mirrors
// idStr::IsEscape (idlib/Str.cpp), which this engine-free header cannot call:
// "^0".."^9", "^:", "^-", "^+" and "^r" are two bytes; "^n" and one more byte is
// three; "^c" (a colour) and "^i" (an icon) with three more bytes are five.
// Anything else, "^^" included, is no escape and is drawn as it is written.
inline int MPModEscapeLength( const char *s ) {
	if ( s == 0 || s[ 0 ] != '^' ) {
		return 0;
	}
	const char c = s[ 1 ];
	if ( ( c >= '0' && c <= '9' ) || c == ':' || c == '-' || c == '+' || c == 'r' || c == 'R' ) {
		return 2;
	}
	if ( c == 'n' || c == 'N' ) {
		return s[ 2 ] != '\0' ? 3 : 0;
	}
	if ( c == 'c' || c == 'C' || c == 'i' || c == 'I' ) {
		return ( s[ 2 ] != '\0' && s[ 3 ] != '\0' && s[ 4 ] != '\0' ) ? 5 : 0;
	}
	return 0;
}

// What a name shows: no escapes ("^1", "^c900"), ASCII lower case. Used to
// compare names, so "^1Bob", "^c900BOB" and "bob" are one name.
inline void MPModVisibleName( const char *in, char *out, int outSize ) {
	if ( outSize <= 0 ) {
		return;
	}
	int length = 0;
	while ( in != 0 && *in != '\0' && length < outSize - 1 ) {
		const int escape = MPModEscapeLength( in );
		if ( escape > 0 ) {
			in += escape;
			continue;
		}
		out[ length++ ] = ( *in >= 'A' && *in <= 'Z' ) ? static_cast<char>( *in - 'A' + 'a' ) : *in;
		in++;
	}
	out[ length ] = '\0';
}

// The escapes a name or a clan tag may keep: the colours. "^c" must be followed
// by its three digits, so no escape takes bytes of a character after it. An icon
// ("^i") or a repeat ("^n") is no part of a name: an icon the game does not know
// draws nothing, and a repeat draws dozens.
inline int MPModNameEscapeLength( const char *s ) {
	const int length = MPModEscapeLength( s );
	if ( length == 2 ) {
		return 2;
	}
	if ( length == 5 && ( s[ 1 ] == 'c' || s[ 1 ] == 'C' ) && s[ 2 ] >= '0' && s[ 2 ] <= '9' &&
		s[ 3 ] >= '0' && s[ 3 ] <= '9' && s[ 4 ] >= '0' && s[ 4 ] <= '9' ) {
		return 5;
	}
	return 0;
}

// Characters that take no room or show nothing: soft hyphen, zero-width and
// joining characters, direction marks, fillers, variation selectors and tags.
// With one of these "adm<zero width>in" would read "admin" and match nothing.
inline bool MPModCodeIsInvisible( unsigned int code ) {
	return code == 0x00ad || code == 0x034f || code == 0x061c || code == 0x115f || code == 0x1160 ||
		code == 0x17b4 || code == 0x17b5 || ( code >= 0x180b && code <= 0x180e ) || ( code >= 0x200b && code <= 0x200f ) ||
		( code >= 0x202a && code <= 0x202e ) || ( code >= 0x2060 && code <= 0x206f ) || code == 0x3164 ||
		( code >= 0xfe00 && code <= 0xfe0f ) || code == 0xfeff || code == 0xffa0 || ( code >= 0xe0000 && code <= 0xe0fff );
}

// Characters that show as a space: they are read as one, so they are trimmed
// from a name's ends and cannot stand in for a name.
inline bool MPModCodeIsSpace( unsigned int code ) {
	return code == 0x00a0 || code == 0x1680 || ( code >= 0x2000 && code <= 0x200a ) || code == 0x2028 ||
		code == 0x2029 || code == 0x202f || code == 0x205f || code == 0x2800 || code == 0x3000;
}

// True when the text shows anything: a character that is not a space.
inline bool MPModTextShows( const char *text ) {
	while ( text != 0 && *text != '\0' ) {
		const int escape = MPModEscapeLength( text );
		if ( escape > 0 ) {
			text += escape;
			continue;
		}
		if ( *text != ' ' ) {
			return true;
		}
		text++;
	}
	return false;
}

// A name or a clan tag as the server keeps it: well-formed UTF-8 with no
// control or invisible characters, every kind of space an ordinary one, spaces
// at either end trimmed, at most maxVisible characters that show (a colour
// escape is kept whole and not counted; a character counts once and is never
// cut). Icons and repeats are removed. A caret that starts no colour is
// removed: left at the end it would join whatever text is printed after the
// name ("name^" then ": hello" hides the colon; "name^c" hides three
// characters and recolours the rest). May leave nothing. Returns true when out
// differs from in.
inline bool MPModCleanText( const char *in, char *out, int outSize, int maxVisible ) {
	if ( outSize <= 0 ) {
		return false;
	}
	if ( in == 0 ) {
		in = "";
	}
	if ( maxVisible < 1 ) {
		maxVisible = 1;
	}
	// What will be kept of the bytes, so an escape is measured on that.
	char text[ 256 ];
	int textLength = 0;
	for ( const char *at = in; *at != '\0'; ) {
		unsigned int code = 0;
		const int bytes = MPModUtf8Decode( at, code );
		if ( bytes == 0 || code < 0x20 || code == 0x7f || ( code >= 0x80 && code <= 0x9f ) || MPModCodeIsInvisible( code ) ) {
			at += bytes > 0 ? bytes : 1;
			continue;
		}
		if ( MPModCodeIsSpace( code ) ) {
			if ( textLength + 1 > static_cast<int>( sizeof( text ) ) - 1 ) {
				break;
			}
			text[ textLength++ ] = ' ';
			at += bytes;
			continue;
		}
		if ( textLength + bytes > static_cast<int>( sizeof( text ) ) - 1 ) {
			break;
		}
		for ( int k = 0; k < bytes; k++ ) {
			text[ textLength++ ] = at[ k ];
		}
		at += bytes;
	}
	text[ textLength ] = '\0';

	int length = 0;
	int visible = 0;
	int lastShown = 0;		// length after the last character that shows
	for ( const char *at = text; *at != '\0'; ) {
		const unsigned char c = static_cast<unsigned char>( *at );
		if ( c == '^' ) {
			const int escape = MPModNameEscapeLength( at );
			if ( escape == 0 ) {
				// an icon or a repeat goes whole; any other caret goes alone
				const int other = MPModEscapeLength( at );
				at += ( other > 0 && ( at[ 1 ] == 'i' || at[ 1 ] == 'I' || at[ 1 ] == 'n' || at[ 1 ] == 'N' ) ) ? other : 1;
				continue;
			}
			if ( length + escape >= outSize ) {
				break;
			}
			for ( int k = 0; k < escape; k++ ) {
				out[ length++ ] = at[ k ];
			}
			at += escape;
			continue;
		}
		if ( c == ' ' && visible == 0 ) {
			at++;
			continue;
		}
		unsigned int code = 0;
		const int bytes = MPModUtf8Decode( at, code );		// well formed: the pass above kept nothing else
		if ( visible >= maxVisible || length + bytes >= outSize ) {
			break;
		}
		for ( int k = 0; k < bytes; k++ ) {
			out[ length++ ] = at[ k ];
		}
		at += bytes;
		visible++;
		if ( c != ' ' ) {
			lastShown = length;
		}
	}
	// trailing spaces go, and so does a colour that then colours nothing
	length = lastShown;
	out[ length ] = '\0';
	return strcmp( in, out ) != 0;
}

// A name is never empty: one that shows nothing is replaced.
inline bool MPModCleanName( const char *in, char *out, int outSize, int maxVisible ) {
	if ( outSize <= 0 ) {
		return false;
	}
	MPModCleanText( in, out, outSize, maxVisible );
	if ( !MPModTextShows( out ) ) {
		MPModPrintf( out, outSize, "%s", MP_MOD_DEFAULT_NAME );
	}
	return strcmp( in != 0 ? in : "", out ) != 0;
}

// True when what a name shows reads as a number to the commands that take "a
// slot or a name" (idStr::IsNumeric: an optional minus, digits, at most one
// point). Such a name would be taken for a slot, so nobody may have one,
// whatever colours it is written in.
inline bool MPModNameIsNumeric( const char *name ) {
	char visible[ MP_MOD_NAME_BYTES ];
	MPModVisibleName( name, visible, sizeof( visible ) );
	const char *at = visible;
	if ( *at == '\0' ) {
		return false;
	}
	if ( *at == '-' ) {
		at++;
	}
	bool point = false;
	int digits = 0;
	for ( ; *at != '\0'; at++ ) {
		if ( *at == '.' && !point ) {
			point = true;
			continue;
		}
		if ( *at < '0' || *at > '9' ) {
			return false;
		}
		digits++;
	}
	return digits > 0;
}

// The list is names separated by ';' or ','. A reserved name matches whatever
// its colours and case.
inline bool MPModNameIsReserved( const char *name, const char *reservedList ) {
	char visible[ MP_MOD_NAME_BYTES ];
	MPModVisibleName( name, visible, sizeof( visible ) );
	if ( visible[ 0 ] == '\0' || reservedList == 0 ) {
		return false;
	}
	const char *at = reservedList;
	while ( *at != '\0' ) {
		while ( *at == ';' || *at == ',' || *at == ' ' ) {
			at++;
		}
		char entry[ MP_MOD_NAME_BYTES ];
		int length = 0;
		while ( *at != '\0' && *at != ';' && *at != ',' ) {
			if ( length < static_cast<int>( sizeof( entry ) ) - 1 ) {
				entry[ length++ ] = *at;
			}
			at++;
		}
		while ( length > 0 && entry[ length - 1 ] == ' ' ) {
			length--;
		}
		entry[ length ] = '\0';
		char entryVisible[ MP_MOD_NAME_BYTES ];
		MPModVisibleName( entry, entryVisible, sizeof( entryVisible ) );
		if ( entryVisible[ 0 ] != '\0' && strcmp( visible, entryVisible ) == 0 ) {
			return true;
		}
	}
	return false;
}

//----------------------------------------------------------------
// the admin log: one JSON object a line
//----------------------------------------------------------------

typedef struct mpModLogLine_s {
	char			text[ MP_MOD_LOG_LINE_BYTES ];
	int				length;
	bool			truncated;		// a value was cut short or a field left out; the line is still valid
} mpModLogLine_t;

// Room for this many more bytes ahead of the closing brace, the line end and
// the terminator.
inline bool MPModLogRoom( const mpModLogLine_t &line, int bytes ) {
	return line.length + bytes <= MP_MOD_LOG_LINE_BYTES - 3;
}

inline void MPModLogRaw( mpModLogLine_t &line, const char *text ) {
	for ( ; *text != '\0' && MPModLogRoom( line, 1 ); text++ ) {
		line.text[ line.length++ ] = *text;
	}
}

// The inside of a JSON string. An escape is written whole or not at all, and a
// byte is always left for the closing quote.
// A character is written whole or not at all, and a byte that is no part of
// well-formed UTF-8 is written as U+FFFD, so the line is always valid JSON in
// valid UTF-8 whatever a name or a reason held.
inline void MPModLogValue( mpModLogLine_t &line, const char *text ) {
	char piece[ 8 ];
	while ( text != 0 && *text != '\0' ) {
		unsigned int code = 0;
		const int bytes = MPModUtf8Decode( text, code );
		if ( bytes == 0 ) {
			MPModPrintf( piece, static_cast<int>( sizeof( piece ) ), "\\ufffd" );
		} else if ( code == '"' || code == '\\' ) {
			piece[ 0 ] = '\\';
			piece[ 1 ] = static_cast<char>( code );
			piece[ 2 ] = '\0';
		} else if ( code < 0x20 || code == 0x7f ) {
			MPModPrintf( piece, static_cast<int>( sizeof( piece ) ), "\\u%04x", code );
		} else {
			for ( int k = 0; k < bytes; k++ ) {
				piece[ k ] = text[ k ];
			}
			piece[ bytes ] = '\0';
		}
		if ( !MPModLogRoom( line, static_cast<int>( strlen( piece ) ) + 1 ) ) {
			line.truncated = true;
			return;
		}
		MPModLogRaw( line, piece );
		text += bytes > 0 ? bytes : 1;
	}
}

inline void MPModLogBegin( mpModLogLine_t &line, mpModTime_t unixSeconds, const char *event ) {
	char utc[ MP_MOD_UTC_TEXT_BYTES ];
	MPModFormatUtc( unixSeconds, utc );
	line.length = 0;
	line.truncated = false;
	MPModLogRaw( line, "{\"time\":\"" );
	MPModLogRaw( line, utc );
	MPModLogRaw( line, "\",\"event\":\"" );
	MPModLogValue( line, event );
	MPModLogRaw( line, "\"" );
}

// An empty value leaves the field out.
inline void MPModLogString( mpModLogLine_t &line, const char *key, const char *value ) {
	if ( value == 0 || *value == '\0' ) {
		return;
	}
	if ( !MPModLogRoom( line, static_cast<int>( strlen( key ) ) + 6 ) ) {
		line.truncated = true;
		return;
	}
	MPModLogRaw( line, ",\"" );
	MPModLogRaw( line, key );
	MPModLogRaw( line, "\":\"" );
	MPModLogValue( line, value );
	MPModLogRaw( line, "\"" );
}

inline void MPModLogNumber( mpModLogLine_t &line, const char *key, mpModTime_t value ) {
	char number[ 32 ];
	MPModPrintf( number, static_cast<int>( sizeof( number ) ), "%lld", value );
	if ( !MPModLogRoom( line, static_cast<int>( strlen( key ) + strlen( number ) ) + 4 ) ) {
		line.truncated = true;
		return;
	}
	MPModLogRaw( line, ",\"" );
	MPModLogRaw( line, key );
	MPModLogRaw( line, "\":" );
	MPModLogRaw( line, number );
}

// Closes the object and ends the line.
inline void MPModLogEnd( mpModLogLine_t &line ) {
	line.text[ line.length++ ] = '}';
	line.text[ line.length++ ] = '\n';
	line.text[ line.length ] = '\0';
}

//----------------------------------------------------------------
// Admins: players the host gave a second password (net_serverAdminPassword). They log
// in through the referee's challenge and response and then ask the server to
// kick, mute or ban a connected player. The request is a typed message, never
// a console line, so an admin cannot reach a command the host did not grant:
// no addresses, no ranges, no lifting of bans, no shutdown.
//----------------------------------------------------------------

typedef enum {
	MP_MOD_ADMIN_NONE,
	MP_MOD_ADMIN_KICK,
	MP_MOD_ADMIN_MUTE,
	MP_MOD_ADMIN_UNMUTE,
	MP_MOD_ADMIN_BAN,
	MP_MOD_ADMIN_LOGOUT,
	MP_MOD_ADMIN_VERB_COUNT
} mpModAdminVerb_t;

typedef enum {
	MP_MOD_ADMIN_OK,
	MP_MOD_ADMIN_REFUSED_VERB,			// not a request this build knows
	MP_MOD_ADMIN_REFUSED_TARGET,		// nobody is in that slot, or somebody else is by now
	MP_MOD_ADMIN_REFUSED_SELF,
	MP_MOD_ADMIN_REFUSED_HOST,			// a listen server's own player
	MP_MOD_ADMIN_REFUSED_ADMIN,			// another admin: only the host removes one
	MP_MOD_ADMIN_REFUSED_BOT,			// bots are the host's choice
	MP_MOD_ADMIN_REFUSED_BANS_OFF,		// g_adminMaxBanMinutes is 0
	MP_MOD_ADMIN_RESULT_COUNT
} mpModAdminResult_t;

typedef struct mpModAdminTarget_s {
	bool	connected;		// a player is in the slot
	bool	current;		// and is the player the admin chose (same spawn identity)
	bool	host;
	bool	admin;
	bool	bot;
	bool	coversAdmin;	// a ban or a mute of the target's range would also reach an admin, the asking one included
} mpModAdminTarget_t;

inline bool MPModAdminVerbHasTarget( int verb ) {
	return verb == MP_MOD_ADMIN_KICK || verb == MP_MOD_ADMIN_MUTE || verb == MP_MOD_ADMIN_UNMUTE || verb == MP_MOD_ADMIN_BAN;
}

inline const char *MPModAdminVerbName( int verb ) {
	switch ( verb ) {
		case MP_MOD_ADMIN_KICK:		return "kick";
		case MP_MOD_ADMIN_MUTE:		return "mute";
		case MP_MOD_ADMIN_UNMUTE:	return "unmute";
		case MP_MOD_ADMIN_BAN:		return "ban";
		case MP_MOD_ADMIN_LOGOUT:	return "logout";
		default:					return "";
	}
}

/*
================
MPModAdminBanSeconds

How long a ban an admin asked for lasts. An admin's ban always ends: asking
for none, for ever, or for longer than the host's limit gives the limit. Zero
when the host lets admins ban nobody (g_adminMaxBanMinutes 0).
================
*/
inline mpModTime_t MPModAdminBanSeconds( mpModTime_t requestedSeconds, int maxMinutes ) {
	if ( maxMinutes <= 0 ) {
		return 0;
	}
	mpModTime_t limit = static_cast<mpModTime_t>( maxMinutes ) * 60;
	if ( limit > MP_MOD_MAX_DURATION_SECONDS ) {
		limit = MP_MOD_MAX_DURATION_SECONDS;
	}
	if ( requestedSeconds <= 0 || requestedSeconds > limit ) {
		return limit;
	}
	return requestedSeconds;
}

/*
================
MPModAdminCheck

Whether an admin in actorSlot may do this to the player in targetSlot. The
checks are ordered so the answer never tells an admin more than the scoreboard
does.
================
*/
inline mpModAdminResult_t MPModAdminCheck( int verb, int actorSlot, int targetSlot, const mpModAdminTarget_t &target, int maxBanMinutes ) {
	if ( verb == MP_MOD_ADMIN_LOGOUT ) {
		return MP_MOD_ADMIN_OK;
	}
	if ( !MPModAdminVerbHasTarget( verb ) ) {
		return MP_MOD_ADMIN_REFUSED_VERB;
	}
	if ( targetSlot < 0 || !target.connected || !target.current ) {
		return MP_MOD_ADMIN_REFUSED_TARGET;
	}
	if ( targetSlot == actorSlot ) {
		return MP_MOD_ADMIN_REFUSED_SELF;
	}
	if ( target.host ) {
		return MP_MOD_ADMIN_REFUSED_HOST;
	}
	if ( target.admin ) {
		return MP_MOD_ADMIN_REFUSED_ADMIN;
	}
	if ( target.bot ) {
		return MP_MOD_ADMIN_REFUSED_BOT;
	}
	if ( verb == MP_MOD_ADMIN_BAN && maxBanMinutes <= 0 ) {
		return MP_MOD_ADMIN_REFUSED_BANS_OFF;
	}
	// A ban and a mute are of a range (MPModPlayerPrefixBits), not of a player. One
	// that also holds an admin, the asking one or another, would act on that admin.
	if ( ( verb == MP_MOD_ADMIN_BAN || verb == MP_MOD_ADMIN_MUTE ) && target.coversAdmin ) {
		return MP_MOD_ADMIN_REFUSED_ADMIN;
	}
	return MP_MOD_ADMIN_OK;
}

#endif /* !__GAME_MP_MODERATION_H__ */
