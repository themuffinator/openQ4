/*
===========================================================================

openQ4 network endpoint parsing helpers.

These helpers intentionally have no engine dependencies so the address grammar
used by the platform socket layers can also be covered by the native tests.

===========================================================================
*/

#ifndef __NETWORK_ENDPOINT_H__
#define __NETWORK_ENDPOINT_H__

#include <cstddef>
#include <cstring>

namespace idNetworkEndpoint {

struct endpointParts_t {
	bool			hasPort;
	unsigned short	port;
};

inline bool ParsePort( const char *begin, const char *end, unsigned short &port ) {
	if ( begin == NULL || end == NULL || begin == end ) {
		return false;
	}

	unsigned int parsedPort = 0;
	for ( const char *cursor = begin; cursor != end; ++cursor ) {
		if ( *cursor < '0' || *cursor > '9' ) {
			return false;
		}
		const unsigned int digit = static_cast<unsigned int>( *cursor - '0' );
		// Check before multiplying so an arbitrarily long decimal string cannot
		// wrap the accumulator back into the valid 16-bit range.
		if ( parsedPort > 6553 || ( parsedPort == 6553 && digit > 5 ) ) {
			return false;
		}
		parsedPort = parsedPort * 10 + digit;
	}

	port = static_cast<unsigned short>( parsedPort );
	return true;
}

inline bool CopyHost( const char *begin, const char *end, char *host, size_t hostSize ) {
	if ( begin == NULL || end == NULL || host == NULL || hostSize == 0 || begin == end || end < begin ) {
		return false;
	}

	const size_t hostLength = static_cast<size_t>( end - begin );
	if ( hostLength >= hostSize ) {
		return false;
	}

	memcpy( host, begin, hostLength );
	host[hostLength] = '\0';
	return true;
}

// FormatIPv6 emits at most the 39 character all-hex form, but this size covers
// any RFC 4291 textual form including the 45 character mixed
// "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255", so a buffer sized from this
// constant stays correct if the mixed form is ever accepted more widely.
const size_t IPV6_TEXT_SIZE = 46;

// A formatted endpoint additionally carries "[", "]", ":", a five digit port,
// and a "%" with a ten digit 32-bit scope id.
const size_t IPV6_ENDPOINT_TEXT_SIZE = IPV6_TEXT_SIZE + 19;

// FormatLocalServerEndpoint echoes the configured interface, which may be a
// host name rather than a literal, so its callers need considerably more room
// than an address alone. Overflow fails closed, and a buffer sized from an
// address constant would silently produce no endpoint at all for a long name.
const size_t LOCAL_ENDPOINT_TEXT_SIZE = 256;

// A bounded text builder. Formatting IPv6 literals without the engine's string
// library keeps this header usable from the platform socket layers, the native
// tests, and Windows builds whose WINVER predates inet_ntop.
struct textBuffer_t {
	char *		data;
	size_t		size;
	size_t		length;
	bool		overflow;
};

inline void TextInit( textBuffer_t &buffer, char *data, size_t size ) {
	buffer.data = data;
	buffer.size = size;
	buffer.length = 0;
	buffer.overflow = ( data == NULL || size == 0 );
	if ( !buffer.overflow ) {
		data[0] = '\0';
	}
}

inline void TextAppendChar( textBuffer_t &buffer, char character ) {
	if ( buffer.overflow ) {
		return;
	}
	if ( buffer.length + 1 >= buffer.size ) {
		buffer.overflow = true;
		return;
	}
	buffer.data[buffer.length++] = character;
	buffer.data[buffer.length] = '\0';
}

inline void TextAppendString( textBuffer_t &buffer, const char *text ) {
	if ( text == NULL ) {
		return;
	}
	for ( const char *cursor = text; *cursor != '\0'; ++cursor ) {
		TextAppendChar( buffer, *cursor );
	}
}

inline void TextAppendUnsigned( textBuffer_t &buffer, unsigned int value ) {
	char digits[10];
	size_t count = 0;
	do {
		digits[count++] = static_cast<char>( '0' + ( value % 10 ) );
		value /= 10;
	} while ( value != 0 && count < sizeof( digits ) );
	while ( count > 0 ) {
		TextAppendChar( buffer, digits[--count] );
	}
}

inline void TextAppendHex( textBuffer_t &buffer, unsigned int value ) {
	static const char hexDigits[] = "0123456789abcdef";
	char digits[4];
	size_t count = 0;
	do {
		digits[count++] = hexDigits[value & 0xf];
		value >>= 4;
	} while ( value != 0 && count < sizeof( digits ) );
	while ( count > 0 ) {
		TextAppendChar( buffer, digits[--count] );
	}
}

// ::ffff:0:0/96, the range that carries an IPv4 peer over an IPv6 socket.
inline bool IsIPv4Mapped( const unsigned char *ip6 ) {
	if ( ip6 == NULL ) {
		return false;
	}
	for ( int i = 0; i < 10; i++ ) {
		if ( ip6[i] != 0 ) {
			return false;
		}
	}
	return ip6[10] == 0xff && ip6[11] == 0xff;
}

inline bool IsIPv6Loopback( const unsigned char *ip6 ) {
	if ( ip6 == NULL ) {
		return false;
	}
	for ( int i = 0; i < 15; i++ ) {
		if ( ip6[i] != 0 ) {
			return false;
		}
	}
	return ip6[15] == 1;
}

inline bool IsIPv6Unspecified( const unsigned char *ip6 ) {
	if ( ip6 == NULL ) {
		return false;
	}
	for ( int i = 0; i < 16; i++ ) {
		if ( ip6[i] != 0 ) {
			return false;
		}
	}
	return true;
}

// fe80::/10
inline bool IsIPv6LinkLocal( const unsigned char *ip6 ) {
	return ip6 != NULL && ip6[0] == 0xfe && ( ip6[1] & 0xc0 ) == 0x80;
}

// fc00::/7, the unique local addresses that replaced the deprecated site-local
// fec0::/10 block.
inline bool IsIPv6UniqueLocal( const unsigned char *ip6 ) {
	return ip6 != NULL && ( ip6[0] & 0xfe ) == 0xfc;
}

inline bool IsIPv6SiteLocal( const unsigned char *ip6 ) {
	return ip6 != NULL && ip6[0] == 0xfe && ( ip6[1] & 0xc0 ) == 0xc0;
}

// ff00::/8
inline bool IsIPv6Multicast( const unsigned char *ip6 ) {
	return ip6 != NULL && ip6[0] == 0xff;
}

// ff02::1, the link-local all-nodes group. Every IPv6 node is a permanent
// member, and Linux delivers it to every socket, so the POSIX layer joins only
// other discovery groups. Windows delivers a group only to sockets that joined
// it, so win_net joins this one as well.
inline bool IsIPv6AllNodes( const unsigned char *ip6 ) {
	if ( ip6 == NULL || ip6[0] != 0xff || ip6[1] != 0x02 ) {
		return false;
	}
	for ( int i = 2; i < 15; i++ ) {
		if ( ip6[i] != 0 ) {
			return false;
		}
	}
	return ip6[15] == 1;
}

// Canonical RFC 5952 text: lowercase hex, no leading zeros in a group, "::"
// over the longest run of zero groups with the leftmost run winning a tie, no
// "::" for a single zero group, and a dotted-quad tail for IPv4-mapped
// addresses. Producing this in shared code keeps Windows and POSIX logs, ban
// lists, and server lists byte-identical.
inline bool FormatIPv6( const unsigned char *ip6, char *out, size_t outSize ) {
	if ( ip6 == NULL ) {
		if ( out != NULL && outSize > 0 ) {
			out[0] = '\0';
		}
		return false;
	}

	textBuffer_t buffer;
	TextInit( buffer, out, outSize );

	unsigned int groups[8];
	for ( int i = 0; i < 8; i++ ) {
		groups[i] = ( static_cast<unsigned int>( ip6[i * 2] ) << 8 ) | static_cast<unsigned int>( ip6[i * 2 + 1] );
	}

	int bestStart = -1;
	int bestLength = 0;
	int runStart = -1;
	int runLength = 0;
	for ( int i = 0; i < 8; i++ ) {
		if ( groups[i] != 0 ) {
			runStart = -1;
			runLength = 0;
			continue;
		}
		if ( runStart < 0 ) {
			runStart = i;
			runLength = 0;
		}
		runLength++;
		// A strict comparison keeps the leftmost of two equally long runs.
		if ( runLength > bestLength ) {
			bestStart = runStart;
			bestLength = runLength;
		}
	}
	if ( bestLength < 2 ) {
		bestStart = -1;
		bestLength = 0;
	}

	const bool mapped = IsIPv4Mapped( ip6 );
	const int textGroups = mapped ? 6 : 8;

	bool wroteGroup = false;
	int index = 0;
	while ( index < textGroups ) {
		if ( index == bestStart ) {
			TextAppendChar( buffer, ':' );
			TextAppendChar( buffer, ':' );
			index += bestLength;
			// The "::" already separates what follows from what came before.
			wroteGroup = false;
			continue;
		}
		if ( wroteGroup ) {
			TextAppendChar( buffer, ':' );
		}
		TextAppendHex( buffer, groups[index] );
		wroteGroup = true;
		index++;
	}

	if ( mapped ) {
		TextAppendChar( buffer, ':' );
		for ( int octet = 12; octet < 16; octet++ ) {
			if ( octet > 12 ) {
				TextAppendChar( buffer, '.' );
			}
			TextAppendUnsigned( buffer, static_cast<unsigned int>( ip6[octet] ) );
		}
	}

	// A truncated literal is worse than none: it would alias two distinct hosts
	// onto one server-browser key and one ban-list entry, so fail closed rather
	// than hand back a prefix.
	if ( buffer.overflow ) {
		if ( out != NULL && outSize > 0 ) {
			out[0] = '\0';
		}
		return false;
	}
	return true;
}

// Renders the address the way the engine consumes it again: a port forces the
// bracketed form so "[::1]:27650" round-trips through Split, and a zone index
// is appended only when the kernel reported one.
inline bool FormatIPv6Endpoint( const unsigned char *ip6, unsigned int scopeId, unsigned short port, char *out, size_t outSize ) {
	char address[IPV6_TEXT_SIZE];
	if ( !FormatIPv6( ip6, address, sizeof( address ) ) ) {
		if ( out != NULL && outSize > 0 ) {
			out[0] = '\0';
		}
		return false;
	}

	textBuffer_t buffer;
	TextInit( buffer, out, outSize );
	if ( port != 0 ) {
		TextAppendChar( buffer, '[' );
	}
	TextAppendString( buffer, address );
	if ( scopeId != 0 ) {
		TextAppendChar( buffer, '%' );
		TextAppendUnsigned( buffer, scopeId );
	}
	if ( port != 0 ) {
		TextAppendChar( buffer, ']' );
		TextAppendChar( buffer, ':' );
		TextAppendUnsigned( buffer, port );
	}

	if ( buffer.overflow ) {
		if ( out != NULL && outSize > 0 ) {
			out[0] = '\0';
		}
		return false;
	}
	return true;
}

inline bool EqualIgnoreCase( const char *a, const char *b ) {
	if ( a == NULL || b == NULL ) {
		return a == b;
	}
	for ( ; *a != '\0' && *b != '\0'; ++a, ++b ) {
		char left = *a;
		char right = *b;
		if ( left >= 'A' && left <= 'Z' ) {
			left = static_cast<char>( left - 'A' + 'a' );
		}
		if ( right >= 'A' && right <= 'Z' ) {
			right = static_cast<char>( right - 'A' + 'a' );
		}
		if ( left != right ) {
			return false;
		}
	}
	return *a == '\0' && *b == '\0';
}

// An empty interface name, the inherited "localhost" default, or either
// family's wildcard literal selects every local interface rather than one
// specific address.
inline bool IsAnyInterfaceName( const char *name ) {
	return name == NULL || name[0] == '\0' || EqualIgnoreCase( name, "localhost" ) ||
		EqualIgnoreCase( name, "0.0.0.0" ) || EqualIgnoreCase( name, "::" ) || EqualIgnoreCase( name, "[::]" );
}

// Distinguishes an IPv6 interface literal from the legacy "address" and
// "address:port" IPv4 forms. Two or more colons cannot appear in either of
// those, and brackets are reserved for IP literals.
inline bool LooksLikeIPv6Literal( const char *name ) {
	if ( name == NULL || name[0] == '\0' ) {
		return false;
	}
	if ( name[0] == '[' ) {
		return true;
	}
	int colons = 0;
	for ( const char *cursor = name; *cursor != '\0'; ++cursor ) {
		if ( *cursor == ':' && ++colons > 1 ) {
			return true;
		}
	}
	return false;
}

struct bindPlan_t {
	bool			bindIPv4;
	bool			bindIPv6;
	const char *	ipv4Interface;
	const char *	ipv6Interface;
	// Set when net_ip carried an IPv6 literal, which older configurations used
	// before net_ip6 existed.
	bool			legacyIPv6Interface;
};

// Decides which interface text drives which family. Keeping the decision in
// shared code stops the two platform socket layers from drifting apart, and
// makes the table directly testable without opening a socket.
inline void PlanBind( const char *ipv4Text, const char *ipv6Text, bool enableIPv4, bool enableIPv6, bindPlan_t &plan ) {
	plan.bindIPv4 = enableIPv4;
	plan.bindIPv6 = enableIPv6;
	plan.ipv4Interface = ipv4Text;
	plan.ipv6Interface = ipv6Text;
	plan.legacyIPv6Interface = false;

	if ( IsAnyInterfaceName( ipv4Text ) || !LooksLikeIPv6Literal( ipv4Text ) ) {
		return;
	}

	// net_ip names an IPv6 interface. Honour it as the IPv6 selection and open
	// no IPv4 socket, which is exactly the single-socket outcome the setting
	// produced before net_ip6 existed.
	plan.legacyIPv6Interface = true;
	if ( IsAnyInterfaceName( ipv6Text ) ) {
		plan.ipv6Interface = ipv4Text;
	}
	plan.bindIPv4 = false;
	plan.ipv4Interface = NULL;
}

// Builds the address text shown for a locally hosted server from the
// configured interface and port. The wildcard interface has no useful text, so
// it becomes the matching loopback address for whichever family was requested.
// Shared so the loading screen and the network system cannot describe the same
// server two different ways.
inline bool FormatLocalServerEndpoint( const char *netIP, int netPort, char *out, size_t outSize ) {
	if ( out == NULL || outSize == 0 ) {
		return false;
	}
	out[0] = '\0';

	const bool hasInterface = netIP != NULL && netIP[0] != '\0';
	const bool ipv6Interface = hasInterface && LooksLikeIPv6Literal( netIP );
	// "localhost" and either family's wildcard name every interface rather than
	// one address, so the endpoint a player would actually dial is the matching
	// loopback.
	const bool wildcard = IsAnyInterfaceName( netIP );

	// With neither a chosen interface nor a chosen port there is nothing
	// truthful to display.
	if ( wildcard && netPort <= 0 ) {
		return false;
	}

	textBuffer_t buffer;
	TextInit( buffer, out, outSize );

	if ( wildcard ) {
		TextAppendString( buffer, ipv6Interface ? "[::1]" : "127.0.0.1" );
	} else if ( ipv6Interface && netIP[0] != '[' ) {
		// A bare IPv6 literal has to be bracketed before a port is appended, or
		// the result reads as a longer address with no port at all.
		TextAppendChar( buffer, '[' );
		TextAppendString( buffer, netIP );
		TextAppendChar( buffer, ']' );
	} else {
		TextAppendString( buffer, netIP );
	}

	// Port 0 means "choose one automatically", and the chosen port is not known
	// here, so naming it would point players at a port nothing listens on.
	if ( netPort > 0 ) {
		TextAppendChar( buffer, ':' );
		TextAppendUnsigned( buffer, static_cast<unsigned int>( netPort ) );
	}

	if ( buffer.overflow ) {
		out[0] = '\0';
		return false;
	}
	return true;
}

// Two addresses share a subnet when their leading prefixLength bits match.
// IPv6 deployments use /64 for on-link subnets, which is what the platform
// layers pass when they classify a LAN neighbour.
inline bool IPv6PrefixMatches( const unsigned char *a, const unsigned char *b, unsigned int prefixLength ) {
	if ( a == NULL || b == NULL || prefixLength > 128 ) {
		return false;
	}
	const unsigned int wholeBytes = prefixLength / 8;
	if ( wholeBytes > 0 && memcmp( a, b, wholeBytes ) != 0 ) {
		return false;
	}
	const unsigned int remainingBits = prefixLength % 8;
	if ( remainingBits == 0 ) {
		return true;
	}
	const unsigned char mask = static_cast<unsigned char>( 0xff << ( 8 - remainingBits ) );
	return ( a[wholeBytes] & mask ) == ( b[wholeBytes] & mask );
}

// Accepts host, host:port, an unbracketed IPv6 literal without a port, or the
// standard [IPv6]:port form. A single colon always introduces a port, which
// keeps the legacy IPv4/hostname grammar strict and unambiguous.
inline bool Split( const char *text, char *host, size_t hostSize, endpointParts_t &parts ) {
	parts.hasPort = false;
	parts.port = 0;
	if ( host != NULL && hostSize > 0 ) {
		host[0] = '\0';
	}
	if ( text == NULL || text[0] == '\0' || host == NULL || hostSize == 0 ) {
		return false;
	}

	const char *textEnd = text + strlen( text );
	if ( text[0] == '[' ) {
		const char *closeBracket = strchr( text + 1, ']' );
		if ( closeBracket == NULL || !CopyHost( text + 1, closeBracket, host, hostSize ) ) {
			return false;
		}
		if ( closeBracket + 1 == textEnd ) {
			return true;
		}
		if ( closeBracket[1] != ':' || !ParsePort( closeBracket + 2, textEnd, parts.port ) ) {
			host[0] = '\0';
			return false;
		}
		parts.hasPort = true;
		return true;
	}

	const char *firstColon = strchr( text, ':' );
	const char *lastColon = strrchr( text, ':' );
	if ( firstColon != NULL && firstColon == lastColon ) {
		if ( !CopyHost( text, firstColon, host, hostSize ) || !ParsePort( firstColon + 1, textEnd, parts.port ) ) {
			host[0] = '\0';
			return false;
		}
		parts.hasPort = true;
		return true;
	}

	return CopyHost( text, textEnd, host, hostSize );
}

// The resolvers report families with the socket headers' constants; this
// header has no socket dependency, so callers translate to these.
const int ADDRESS_FAMILY_IPV4 = 4;
const int ADDRESS_FAMILY_IPV6 = 6;

// The most resolver records ordered, and the most of them a connection tries.
// A host name with more records still connects; the extra records are simply
// never tried.
const int MAX_RESOLVER_RESULTS = 32;
const int MAX_RESOLVED_CANDIDATES = 8;

// Orders resolver results for connection attempts. The first entry keeps the
// historical single-address choice - the first IPv4 record when preferIPv4 is
// set, otherwise the resolver's first record - so a host that answers on its
// first address connects exactly as before. The rest alternate address
// families (RFC 8305 section 4) and keep the resolver's order, which already
// follows RFC 6724 destination selection, within each family, so one
// unreachable family cannot hold every attempt on the other behind it.
// Returns how many indices were written to order.
inline int OrderCandidates( const int *families, int count, bool preferIPv4, int *order, int maxOrder ) {
	if ( families == NULL || order == NULL || count <= 0 || maxOrder <= 0 ) {
		return 0;
	}
	if ( count > MAX_RESOLVER_RESULTS ) {
		count = MAX_RESOLVER_RESULTS;
	}

	bool used[MAX_RESOLVER_RESULTS] = {};
	int first = 0;
	if ( preferIPv4 ) {
		for ( int i = 0; i < count; i++ ) {
			if ( families[i] == ADDRESS_FAMILY_IPV4 ) {
				first = i;
				break;
			}
		}
	}

	int written = 0;
	int lastFamily = families[first];
	order[written++] = first;
	used[first] = true;
	while ( written < maxOrder ) {
		const int wanted = ( lastFamily == ADDRESS_FAMILY_IPV4 ) ? ADDRESS_FAMILY_IPV6 : ADDRESS_FAMILY_IPV4;
		int pick = -1;
		for ( int i = 0; i < count && pick < 0; i++ ) {
			if ( !used[i] && families[i] == wanted ) {
				pick = i;
			}
		}
		for ( int i = 0; i < count && pick < 0; i++ ) {
			if ( !used[i] ) {
				pick = i;
			}
		}
		if ( pick < 0 ) {
			break;
		}
		order[written++] = pick;
		used[pick] = true;
		lastFamily = families[pick];
	}
	return written;
}

// How many unanswered challenges a connection spends on one resolved address
// before it moves to the next. Challenges go out once a second, so a slow but
// live server still has three seconds to answer.
const int CONNECT_CHALLENGES_PER_CANDIDATE = 3;

struct connectFailover_t {
	int		count;		// resolved candidates for this connection
	int		index;		// the candidate currently being challenged
	int		sent;		// challenges already sent to it
};

inline void FailoverReset( connectFailover_t &state, int count ) {
	state.count = count > 0 ? count : 1;
	state.index = 0;
	state.sent = 0;
}

// Call once before each challenge is sent. Returns true when this challenge
// goes to a different candidate than the previous one. After the last
// candidate it starts over at the first, so a connection that was never
// answered keeps trying as the single-address loop always did, but no address
// holds it for longer than its share of the cycle.
inline bool FailoverBeforeChallenge( connectFailover_t &state ) {
	bool moved = false;
	if ( state.count > 1 && state.sent >= CONNECT_CHALLENGES_PER_CANDIDATE ) {
		state.index = ( state.index + 1 ) % state.count;
		state.sent = 0;
		moved = true;
	}
	state.sent++;
	return moved;
}

} // namespace idNetworkEndpoint

#endif /* !__NETWORK_ENDPOINT_H__ */
