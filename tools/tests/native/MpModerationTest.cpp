// Quake 4 SDK EULA applies to the game headers this test compiles.
// Exercises the engine-free core of server moderation (MP-18): addresses and
// ranges, the ban list and its file lines, text mutes, the chat flood window,
// the name policy and the admin log's lines. Run by tools/tests/mp_moderation.py.
#include "../../../src/mpgame/mp/Moderation.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

static int failures = 0;

#define CHECK( condition ) \
	do { \
		if ( !( condition ) ) { \
			std::printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition ); \
			failures++; \
		} \
	} while ( 0 )

static std::string Range( const char *text ) {
	mpModAddress_t address;
	int prefix = -1;
	if ( !MPModParseAddress( text, address, prefix ) ) {
		return "<bad>";
	}
	char out[ MP_MOD_RANGE_TEXT_BYTES ];
	MPModFormatRange( address, prefix, out, sizeof( out ) );
	return out;
}

static mpModAddress_t Address( const char *text ) {
	mpModAddress_t address;
	int prefix = -1;
	CHECK( MPModParseAddress( text, address, prefix ) );
	return address;
}

static void TestAddresses( void ) {
	// the forms the engine prints for a connecting client
	CHECK( Range( "192.0.2.7:28004" ) == "192.0.2.7/32" );
	CHECK( Range( "192.0.2.7" ) == "192.0.2.7/32" );
	CHECK( Range( "localhost:28004" ) == "127.0.0.1/32" );
	CHECK( Range( "LocalHost" ) == "127.0.0.1/32" );
	CHECK( Range( "[2001:db8::1]:27650" ) == "2001:db8::1/128" );
	CHECK( Range( "[fe80::1%3]:27650" ) == "fe80::1/128" );
	CHECK( Range( "2001:0DB8:0000:0000:0000:0000:0000:0001" ) == "2001:db8::1/128" );
	CHECK( Range( "::" ) == "::/128" );
	CHECK( Range( "::1" ) == "::1/128" );
	CHECK( Range( "1::" ) == "1::/128" );
	CHECK( Range( "1:2:3:4:5:6:7::" ) == "1:2:3:4:5:6:7:0/128" );
	CHECK( Range( "2001:db8:0:0:1:0:0:1" ) == "2001:db8::1:0:0:1/128" );
	CHECK( Range( "2001:0:0:1:0:0:0:1" ) == "2001:0:0:1::1/128" );
	// an IPv4 peer through the mapped range is the IPv4 address
	CHECK( Range( "[::ffff:192.0.2.7]:28004" ) == "192.0.2.7/32" );
	CHECK( Range( "::ffff:192.0.2.0/120" ) == "192.0.2.0/24" );
	CHECK( Range( "::ffff:192.0.2.0/64" ) == "<bad>" );
	// what an operator types
	CHECK( Range( "192.0.2.0/24" ) == "192.0.2.0/24" );
	CHECK( Range( "  10.1.2.3/8 " ) == "10.1.2.3/8" );
	CHECK( Range( "2001:db8::/48" ) == "2001:db8::/48" );
	CHECK( Range( "[2001:db8::]/48" ) == "2001:db8::/48" );

	static const char *const bad[] = {
		"", " ", "bot", "bad", "broadcast:27650", "1.2.3", "1.2.3.4.5", "1.2.3.256", "1..3.4", "1.2.3.4.", ".1.2.3.4",
		"1.2.3.4:", "1.2.3.4:70000", "1.2.3.4:abc", "1.2.3.4/33", "1.2.3.4/", "/24", "1.2.3.4/2x", "0x1.2.3.4",
		"2001:db8::1/129", ":::", "1:::2", "1::2::3", "1:2:3:4:5:6:7", "1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7:8::",
		"12345::", "g::1", "1:", ":1", "[::1", "[::1]x", "[::1]:", "[::1]:99999", "::1.2.3", "1:2:3:4:5:6:7:1.2.3.4",
		"name", "localhost.example", "-1.2.3.4" };
	for ( size_t i = 0; i < sizeof( bad ) / sizeof( bad[ 0 ] ); i++ ) {
		if ( Range( bad[ i ] ) != "<bad>" ) {
			std::printf( "FAIL accepted '%s' as %s\n", bad[ i ], Range( bad[ i ] ).c_str() );
			failures++;
		}
	}
	// a text too long to be an address is refused, not read past
	std::string longText( 400, '1' );
	CHECK( Range( longText.c_str() ) == "<bad>" );
	CHECK( Range( 0 ) == "<bad>" );

	// ranges
	mpModAddress_t range = Address( "192.0.2.0" );
	CHECK( MPModAddressInRange( Address( "192.0.2.255" ), range, 24 ) );
	CHECK( !MPModAddressInRange( Address( "192.0.3.0" ), range, 24 ) );
	CHECK( MPModAddressInRange( Address( "192.0.3.0" ), range, 23 ) );
	CHECK( !MPModAddressInRange( Address( "192.0.4.0" ), range, 23 ) );
	CHECK( MPModAddressInRange( Address( "192.0.2.0" ), range, 32 ) );
	CHECK( !MPModAddressInRange( Address( "192.0.2.1" ), range, 32 ) );
	CHECK( MPModAddressInRange( Address( "203.0.113.9" ), range, 0 ) );
	CHECK( !MPModAddressInRange( Address( "192.0.2.1" ), range, 33 ) );
	// an IPv6 range never covers an IPv4 address, nor the reverse
	CHECK( !MPModAddressInRange( Address( "::1" ), range, 0 ) );
	mpModAddress_t range6 = Address( "2001:db8:aa00::" );
	CHECK( MPModAddressInRange( Address( "2001:db8:aaff::9" ), range6, 40 ) );
	CHECK( !MPModAddressInRange( Address( "2001:db8:ab00::9" ), range6, 40 ) );
	CHECK( MPModAddressInRange( Address( "2001:db8:aa80::" ), range6, 41 ) == false );
	CHECK( !MPModAddressInRange( Address( "192.0.2.1" ), range6, 0 ) );
	mpModAddress_t none;
	MPModClearAddress( none );
	CHECK( !MPModAddressInRange( none, none, 0 ) );
	CHECK( !MPModSameAddress( none, none ) );
	CHECK( MPModSameAddress( Address( "[2001:db8::1]:1" ), Address( "2001:db8::1" ) ) );

	// masking gives one spelling to one range
	mpModAddress_t masked = Address( "10.200.3.4" );
	MPModMaskAddress( masked, 12 );
	char text[ MP_MOD_RANGE_TEXT_BYTES ];
	MPModFormatRange( masked, 12, text, sizeof( text ) );
	CHECK( std::string( text ) == "10.192.0.0/12" );
	masked = Address( "2001:db8:ffff:ffff::1" );
	MPModMaskAddress( masked, 33 );
	MPModFormatRange( masked, 33, text, sizeof( text ) );
	CHECK( std::string( text ) == "2001:db8:8000::/33" );

	// limits on how wide a ban may be
	CHECK( MPModRangeIsAllowed( Address( "10.0.0.0" ), 8 ) );
	CHECK( !MPModRangeIsAllowed( Address( "10.0.0.0" ), 7 ) );
	CHECK( !MPModRangeIsAllowed( Address( "10.0.0.0" ), 0 ) );
	CHECK( MPModRangeIsAllowed( Address( "2001:db8::" ), 16 ) );
	CHECK( !MPModRangeIsAllowed( Address( "2001:db8::" ), 15 ) );
	CHECK( !MPModRangeIsAllowed( none, 32 ) );
}

static void TestDurations( void ) {
	mpModTime_t seconds = -1;
	CHECK( MPModParseDuration( "30m", seconds ) && seconds == 1800 );
	CHECK( MPModParseDuration( "30", seconds ) && seconds == 1800 );
	CHECK( MPModParseDuration( "45s", seconds ) && seconds == 45 );
	CHECK( MPModParseDuration( "2H", seconds ) && seconds == 7200 );
	CHECK( MPModParseDuration( "7d", seconds ) && seconds == 7 * 86400 );
	CHECK( MPModParseDuration( "1w", seconds ) && seconds == 7 * 86400 );
	CHECK( MPModParseDuration( "perm", seconds ) && seconds == 0 );
	CHECK( MPModParseDuration( "Permanent", seconds ) && seconds == 0 );
	CHECK( MPModParseDuration( "forever", seconds ) && seconds == 0 );
	CHECK( MPModParseDuration( "0", seconds ) && seconds == 0 );
	CHECK( MPModParseDuration( "999999999w", seconds ) && seconds == MP_MOD_MAX_DURATION_SECONDS );
	// a reason is never read as a time
	static const char *const bad[] = { "", "spam", "3 strikes", "3x", "m", "-5m", "5mm", "1.5h", "00", "9999999999", "perm.", 0 };
	for ( int i = 0; bad[ i ] != 0; i++ ) {
		if ( MPModParseDuration( bad[ i ], seconds ) ) {
			std::printf( "FAIL read '%s' as a duration\n", bad[ i ] );
			failures++;
		}
	}
	CHECK( !MPModParseDuration( 0, seconds ) );

	char text[ 32 ];
	MPModFormatDuration( 0, text, sizeof( text ) );
	CHECK( std::string( text ) == "permanent" );
	MPModFormatDuration( 45, text, sizeof( text ) );
	CHECK( std::string( text ) == "45s" );
	MPModFormatDuration( 61, text, sizeof( text ) );
	CHECK( std::string( text ) == "2m" );
	MPModFormatDuration( 7500, text, sizeof( text ) );
	CHECK( std::string( text ) == "2h 5m" );
	MPModFormatDuration( 3 * 86400 + 4 * 3600 + 5, text, sizeof( text ) );
	CHECK( std::string( text ) == "3d 4h" );

	char utc[ MP_MOD_UTC_TEXT_BYTES ];
	MPModFormatUtc( 0, utc );
	CHECK( std::string( utc ) == "1970-01-01T00:00:00Z" );
	MPModFormatUtc( 951782400, utc );		// a leap day in a century year that is a leap year
	CHECK( std::string( utc ) == "2000-02-29T00:00:00Z" );
	MPModFormatUtc( 1795385700, utc );
	CHECK( std::string( utc ) == "2026-11-22T22:15:00Z" );
	MPModFormatUtc( 4102444799LL, utc );
	CHECK( std::string( utc ) == "2099-12-31T23:59:59Z" );
	MPModFormatUtc( -5, utc );
	CHECK( std::string( utc ) == "1970-01-01T00:00:00Z" );
}

static std::string JoinAddress( std::initializer_list<const char *> words, int first, int *used = 0 ) {
	const char *argv[ 32 ];
	int argc = 0;
	for ( const char *word : words ) {
		argv[ argc++ ] = word;
	}
	char out[ 64 ];
	std::memset( out, 'X', sizeof( out ) );
	const int count = MPModJoinAddressArgs( argc, argv, first, out, 48 );
	CHECK( out[ 48 ] == 'X' );
	if ( used ) {
		*used = count;
	}
	return out;
}

static int JoinDuration( std::initializer_list<const char *> words, int first, mpModTime_t &seconds ) {
	const char *argv[ 32 ];
	int argc = 0;
	for ( const char *word : words ) {
		argv[ argc++ ] = word;
	}
	return MPModJoinDurationArgs( argc, argv, first, seconds );
}

// What the console's lexer makes of one typed word, put back together.
static void TestConsoleWords( void ) {
	int used = 0;
	CHECK( JoinAddress( { "ban", "192.0.2.0", "/24", "perm", "x" }, 1, &used ) == "192.0.2.0/24" && used == 2 );
	CHECK( JoinAddress( { "ban", "192.0.2.0", "/", "24", "x" }, 1, &used ) == "192.0.2.0/24" && used == 3 );
	CHECK( JoinAddress( { "ban", "192.0.2.7", "30", "spam" }, 1, &used ) == "192.0.2.7" && used == 1 );
	CHECK( JoinAddress( { "ban", "192.0.2.7" }, 1, &used ) == "192.0.2.7" && used == 1 );
	// a reason is never taken for a prefix
	CHECK( JoinAddress( { "ban", "192.0.2.7", "/24x" }, 1, &used ) == "192.0.2.7" && used == 1 );
	CHECK( JoinAddress( { "ban", "192.0.2.7", "/", "x" }, 1, &used ) == "192.0.2.7" && used == 1 );
	CHECK( JoinAddress( { "ban", "192.0.2.7", "/1234" }, 1, &used ) == "192.0.2.7" && used == 1 );
	CHECK( JoinAddress( { "ban", "192.0.2.7", "/" }, 1, &used ) == "192.0.2.7" && used == 1 );
	// IPv6 in brackets, however the lexer cut it
	CHECK( JoinAddress( { "ban", "[", "2001", ":", "db8", "::", "/48", "]", "2", "h" }, 1, &used ) == "2001:db8::/48" && used == 7 );
	CHECK( JoinAddress( { "ban", "[", "2001", ":", "db8", "::", "1", "]", "/64", "bad" }, 1, &used ) == "2001:db8::1/64" && used == 8 );
	CHECK( JoinAddress( { "ban", "[2001:db8::1]", "bad", "player" }, 1, &used ) == "2001:db8::1" && used == 1 );
	CHECK( JoinAddress( { "unban", "[", "::", "ffff", ":", "192.0.2.7", "]" }, 1, &used ) == "::ffff:192.0.2.7" && used == 6 );
	// an endpoint as the engine prints it keeps its brackets for the parser
	CHECK( JoinAddress( { "ban", "[", "::1", "]:27650" }, 1, &used ) == "[::1]:27650" && used == 3 );
	CHECK( Range( JoinAddress( { "ban", "[", "::1", "]:27650" }, 1 ).c_str() ) == "::1/128" );
	// no closing bracket is the first word alone, which is no address
	CHECK( JoinAddress( { "ban", "[", "2001", ":", "db8" }, 1, &used ) == "[" && used == 1 );
	// a word that is not an address is itself: a name, a slot
	CHECK( JoinAddress( { "ban", "Ada", "/24" }, 1, &used ) == "Ada/24" && used == 2 );
	CHECK( Range( "Ada/24" ) == "<bad>" );
	// bounds
	CHECK( JoinAddress( { "ban" }, 1, &used ) == "" && used == 1 );
	CHECK( JoinAddress( { "ban", "x" }, -1, &used ) == "" && used == 1 );
	const std::string longWord( 200, '7' );
	CHECK( JoinAddress( { "ban", longWord.c_str() }, 1, &used ) == std::string( 47, '7' ) );
	CHECK( JoinAddress( { "ban", "[", longWord.c_str(), "]" }, 1, &used ).size() <= 47 );

	mpModTime_t seconds = -1;
	CHECK( JoinDuration( { "ban", "3", "10", "m", "spam" }, 2, seconds ) == 2 && seconds == 600 );
	CHECK( JoinDuration( { "ban", "3", "2", "H" }, 2, seconds ) == 2 && seconds == 7200 );
	CHECK( JoinDuration( { "ban", "3", "10m", "spam" }, 2, seconds ) == 1 && seconds == 600 );
	CHECK( JoinDuration( { "ban", "3", "30", "spam" }, 2, seconds ) == 1 && seconds == 1800 );
	CHECK( JoinDuration( { "ban", "3", "30" }, 2, seconds ) == 1 && seconds == 1800 );
	CHECK( JoinDuration( { "ban", "3", "perm", "m" }, 2, seconds ) == 1 && seconds == 0 );
	// "0" never ends, and a letter after it is the reason's
	CHECK( JoinDuration( { "ban", "3", "0", "m" }, 2, seconds ) == 1 && seconds == 0 );
	// a one-letter word that is no unit leaves the number as minutes
	CHECK( JoinDuration( { "ban", "3", "5", "x" }, 2, seconds ) == 1 && seconds == 300 );
	CHECK( JoinDuration( { "ban", "3", "spam", "10", "m" }, 2, seconds ) == 0 );
	CHECK( JoinDuration( { "ban", "3" }, 2, seconds ) == 0 );
	CHECK( JoinDuration( { "ban", "3", "9999999999", "m" }, 2, seconds ) == 0 );
}

static mpModBanList_t banList;		// 120 KB: not on the stack

static void TestBans( void ) {
	mpModBanList_t &list = banList;
	std::memset( &list, 0, sizeof( list ) );
	const mpModTime_t now = 1795000000;

	CHECK( MPModBanFind( list, Address( "192.0.2.7" ), now ) == -1 );
	CHECK( MPModBanAdd( list, Address( "192.0.2.7" ), 32, now, 0, "Ada", "aimbot" ) == 0 );
	CHECK( MPModBanAdd( list, Address( "198.51.100.200" ), 24, now, 600, "^1Bo", "spam \"links\"" ) == 1 );
	CHECK( MPModBanAdd( list, Address( "2001:db8:1:2::9" ), 64, now, 0, "", "" ) == 2 );
	CHECK( list.count == 3 );
	// the range is stored masked
	char range[ MP_MOD_RANGE_TEXT_BYTES ];
	MPModFormatRange( list.bans[ 1 ].address, list.bans[ 1 ].prefixBits, range, sizeof( range ) );
	CHECK( std::string( range ) == "198.51.100.0/24" );

	CHECK( MPModBanFind( list, Address( "192.0.2.7:1234" ), now ) == 0 );
	CHECK( MPModBanFind( list, Address( "192.0.2.8" ), now ) == -1 );
	CHECK( MPModBanFind( list, Address( "198.51.100.1" ), now ) == 1 );
	CHECK( MPModBanFind( list, Address( "198.51.100.1" ), now + 599 ) == 1 );
	// an expired ban matches nobody, whether or not it was purged yet
	CHECK( MPModBanFind( list, Address( "198.51.100.1" ), now + 600 ) == -1 );
	CHECK( MPModBanFind( list, Address( "[2001:db8:1:2:ffff::1]:5" ), now ) == 2 );
	CHECK( MPModBanFind( list, Address( "2001:db8:1:3::1" ), now ) == -1 );
	// the mapped form of a banned IPv4 address is that address
	CHECK( MPModBanFind( list, Address( "[::ffff:192.0.2.7]:9" ), now ) == 0 );

	// banning the same range again replaces it
	CHECK( MPModBanAdd( list, Address( "198.51.100.77" ), 24, now + 10, 0, "Bo", "again" ) == 1 );
	CHECK( list.count == 3 && list.bans[ 1 ].expires == 0 && std::string( list.bans[ 1 ].reason ) == "again" );
	// a different prefix on the same address is another ban
	CHECK( MPModBanAdd( list, Address( "198.51.100.77" ), 32, now, 60, "Bo", "" ) == 3 );
	CHECK( MPModBanFindRange( list, Address( "198.51.100.1" ), 24 ) == 1 );
	CHECK( MPModBanFindRange( list, Address( "198.51.100.77" ), 32 ) == 3 );
	CHECK( MPModBanFindRange( list, Address( "198.51.100.78" ), 32 ) == -1 );

	// ranges too wide are refused
	CHECK( MPModBanAdd( list, Address( "10.0.0.0" ), 4, now, 0, "", "" ) == -1 );
	CHECK( MPModBanAdd( list, Address( "2001::" ), 8, now, 0, "", "" ) == -1 );
	mpModAddress_t none;
	MPModClearAddress( none );
	CHECK( MPModBanAdd( list, none, 32, now, 0, "", "" ) == -1 );
	CHECK( list.count == 4 );

	// control characters never reach the list
	CHECK( MPModBanAdd( list, Address( "203.0.113.5" ), 32, now, 0, "A\nB\tC", "line\r\none\x7f" ) == 4 );
	CHECK( std::string( list.bans[ 4 ].name ) == "ABC" && std::string( list.bans[ 4 ].reason ) == "lineone" );

	// lines round-trip
	for ( int i = 0; i < list.count; i++ ) {
		char line[ MP_MOD_LINE_BYTES ];
		CHECK( MPModBanFormatLine( list.bans[ i ], line, sizeof( line ) ) > 0 );
		mpModBan_t read;
		CHECK( MPModBanParseLine( line, read ) );
		CHECK( std::memcmp( &read, &list.bans[ i ], sizeof( read ) ) == 0 );
	}
	{
		char line[ MP_MOD_LINE_BYTES ];
		MPModBanFormatLine( list.bans[ 1 ], line, sizeof( line ) );
		CHECK( std::string( line ) == "ban address 198.51.100.0/24 1795000010 0 \"Bo\" \"again\"" );
		// the longest entry still fits the line
		mpModBan_t longest = list.bans[ 2 ];
		longest.address = Address( "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff" );
		longest.prefixBits = 128;
		longest.created = longest.expires = 999999999999999LL;
		std::memset( longest.name, '"', sizeof( longest.name ) - 1 );
		std::memset( longest.reason, '\\', sizeof( longest.reason ) - 1 );
		CHECK( MPModBanFormatLine( longest, line, sizeof( line ) ) > 0 );
		mpModBan_t read;
		CHECK( MPModBanParseLine( line, read ) && std::memcmp( &read, &longest, sizeof( read ) ) == 0 );
		// and a buffer that is too small is refused, not overrun
		char small[ 40 ];
		std::memset( small, 'X', sizeof( small ) );
		CHECK( MPModBanFormatLine( longest, small, 32 ) == 0 );
		CHECK( small[ 32 ] == 'X' );
	}

	// hand-written and damaged lines
	mpModBan_t read;
	CHECK( MPModBanParseLine( "ban address 192.0.2.0/24 0 0", read ) && read.prefixBits == 24 && read.name[ 0 ] == '\0' );
	CHECK( MPModBanParseLine( "  ban\taddress 192.0.2.9 5 0 \"x y\"\r\n", read ) && std::string( read.name ) == "x y" );
	CHECK( MPModBanParseLine( "ban address 192.0.2.99/24 0 0 a b", read ) );
	MPModFormatRange( read.address, read.prefixBits, range, sizeof( range ) );
	CHECK( std::string( range ) == "192.0.2.0/24" );
	static const char *const badLines[] = {
		"", "# a comment", MP_MOD_BANLIST_HEADER, "ban", "ban address", "ban address nonsense 0 0", "ban address 192.0.2.1",
		"ban address 192.0.2.1 x 0", "ban address 192.0.2.1 0", "ban address 192.0.2.1 0 -1", "ban address 0.0.0.0/0 0 0",
		"ban identity abcdef 0 0 \"n\" \"r\"", "ban address 192.0.2.1 0 0 \"unterminated", "unban address 192.0.2.1 0 0",
		"ban address 192.0.2.1 9999999999999999 0", 0 };
	for ( int i = 0; badLines[ i ] != 0; i++ ) {
		if ( MPModBanParseLine( badLines[ i ], read ) ) {
			// the last two fields are optional, so an unterminated name is the one damaged tail that still loads
			if ( std::strstr( badLines[ i ], "unterminated" ) != 0 ) {
				CHECK( read.name[ 0 ] == '\0' );
				continue;
			}
			std::printf( "FAIL read a ban from '%s'\n", badLines[ i ] );
			failures++;
		}
	}

	// purging and removing
	CHECK( MPModBanPurgeExpired( list, now + 30 ) == 0 );
	CHECK( MPModBanPurgeExpired( list, now + 60 ) == 1 );
	CHECK( list.count == 4 );
	CHECK( !MPModBanRemove( list, 4 ) && !MPModBanRemove( list, -1 ) );
	CHECK( MPModBanRemove( list, 0 ) );
	CHECK( list.count == 3 && MPModBanFind( list, Address( "192.0.2.7" ), now ) == -1 );
	CHECK( MPModBanFind( list, Address( "198.51.100.1" ), now ) == 0 );

	// a full list makes room from its expired bans, and refuses when there are none
	std::memset( &list, 0, sizeof( list ) );
	for ( int i = 0; i < MP_MOD_MAX_BANS; i++ ) {
		char text[ 32 ];
		std::snprintf( text, sizeof( text ), "10.%d.%d.1", i / 256, i % 256 );
		CHECK( MPModBanAdd( list, Address( text ), 32, now, i == 7 ? 100 : 0, "", "" ) == i );
	}
	CHECK( MPModBanAdd( list, Address( "172.16.0.1" ), 32, now + 50, 0, "", "" ) == -1 );
	CHECK( list.count == MP_MOD_MAX_BANS );
	CHECK( MPModBanAdd( list, Address( "172.16.0.1" ), 32, now + 100, 0, "", "" ) == MP_MOD_MAX_BANS - 1 );
	CHECK( list.count == MP_MOD_MAX_BANS && MPModBanFind( list, Address( "10.0.7.1" ), now ) == -1 );
	// replacing a ban in a full list needs no room
	CHECK( MPModBanAdd( list, Address( "10.0.0.1" ), 32, now, 0, "", "x" ) == 0 );
}

static void TestMutes( void ) {
	static mpModMuteList_t list;
	std::memset( &list, 0, sizeof( list ) );
	const mpModTime_t now = 1795000000;
	MPModMuteAdd( list, Address( "192.0.2.7:1" ), now, 0 );
	MPModMuteAdd( list, Address( "2001:db8::1" ), now, 300 );
	CHECK( list.count == 2 );
	CHECK( MPModMuteFind( list, Address( "192.0.2.7:999" ), now + 100000 ) == 0 );
	CHECK( MPModMuteFind( list, Address( "192.0.2.8" ), now ) == -1 );
	CHECK( MPModMuteFind( list, Address( "[2001:db8::1]:5" ), now + 299 ) == 1 );
	CHECK( MPModMuteFind( list, Address( "[2001:db8::1]:5" ), now + 300 ) == -1 );
	// muting again replaces the first mute
	MPModMuteAdd( list, Address( "2001:db8::1" ), now, 0 );
	CHECK( list.count == 2 && MPModMuteFind( list, Address( "2001:db8::1" ), now + 100000 ) >= 0 );
	CHECK( MPModMuteRemove( list, Address( "192.0.2.7" ) ) );
	CHECK( !MPModMuteRemove( list, Address( "192.0.2.7" ) ) );
	CHECK( list.count == 1 );
	// an address the engine cannot name (a bot, the host) is never listed
	mpModAddress_t none;
	MPModClearAddress( none );
	MPModMuteAdd( list, none, now, 0 );
	CHECK( list.count == 1 );
	CHECK( MPModMuteFind( list, none, now ) == -1 );

	// a full list gives up its expired mutes, then the one that ends soonest
	std::memset( &list, 0, sizeof( list ) );
	for ( int i = 0; i < MP_MOD_MAX_MUTES; i++ ) {
		char text[ 32 ];
		std::snprintf( text, sizeof( text ), "10.0.0.%d", i + 1 );
		MPModMuteAdd( list, Address( text ), now, i == 3 ? 50 : ( i == 5 ? 500 : 0 ) );
	}
	CHECK( list.count == MP_MOD_MAX_MUTES );
	MPModMuteAdd( list, Address( "10.0.1.1" ), now + 60, 0 );
	CHECK( list.count == MP_MOD_MAX_MUTES );
	CHECK( MPModMuteFind( list, Address( "10.0.1.1" ), now + 60 ) >= 0 );
	CHECK( MPModMuteFind( list, Address( "10.0.0.6" ), now + 60 ) >= 0 );
	MPModMuteAdd( list, Address( "10.0.1.2" ), now + 60, 0 );
	CHECK( list.count == MP_MOD_MAX_MUTES );
	CHECK( MPModMuteFind( list, Address( "10.0.0.6" ), now + 60 ) == -1 );
	CHECK( MPModMuteFind( list, Address( "10.0.1.2" ), now + 60 ) >= 0 );
}

static void TestFlood( void ) {
	mpModFlood_t flood;
	MPModFloodReset( flood );
	// four lines in any four seconds; the fifth silences for ten
	CHECK( MPModFloodCheck( flood, 1000, 4, 4000, 10000 ) == 0 );
	CHECK( MPModFloodCheck( flood, 1100, 4, 4000, 10000 ) == 0 );
	CHECK( MPModFloodCheck( flood, 1200, 4, 4000, 10000 ) == 0 );
	CHECK( MPModFloodCheck( flood, 1300, 4, 4000, 10000 ) == 0 );
	CHECK( MPModFloodCheck( flood, 1400, 4, 4000, 10000 ) == 10000 );
	// lines sent while silenced are refused and do not extend the silence
	CHECK( MPModFloodCheck( flood, 5000, 4, 4000, 10000 ) == 6400 );
	CHECK( MPModFloodCheck( flood, 11399, 4, 4000, 10000 ) == 1 );
	CHECK( MPModFloodCheck( flood, 11400, 4, 4000, 10000 ) == 0 );
	// a steady talker is never silenced
	MPModFloodReset( flood );
	for ( int i = 0; i < 200; i++ ) {
		CHECK( MPModFloodCheck( flood, 1000 + i * 1001, 4, 4000, 10000 ) == 0 );
	}
	// exactly at the window's edge the oldest line no longer counts
	MPModFloodReset( flood );
	CHECK( MPModFloodCheck( flood, 0, 2, 1000, 5000 ) == 0 );
	CHECK( MPModFloodCheck( flood, 10, 2, 1000, 5000 ) == 0 );
	CHECK( MPModFloodCheck( flood, 1000, 2, 1000, 5000 ) == 0 );
	CHECK( MPModFloodCheck( flood, 1009, 2, 1000, 5000 ) == 5000 );
	// off
	MPModFloodReset( flood );
	for ( int i = 0; i < 100; i++ ) {
		CHECK( MPModFloodCheck( flood, i, 0, 4000, 10000 ) == 0 );
		CHECK( MPModFloodCheck( flood, i, 4, 0, 10000 ) == 0 );
	}
	// more lines than the ring holds is the ring's size
	MPModFloodReset( flood );
	for ( int i = 0; i < MP_MOD_FLOOD_MAX_MESSAGES; i++ ) {
		CHECK( MPModFloodCheck( flood, i, 1000, 60000, 1000 ) == 0 );
	}
	CHECK( MPModFloodCheck( flood, 20, 1000, 60000, 1000 ) == 1000 );
	// no wait still refuses the line that broke the window
	MPModFloodReset( flood );
	CHECK( MPModFloodCheck( flood, 0, 1, 1000, 0 ) == 0 );
	CHECK( MPModFloodCheck( flood, 1, 1, 1000, 0 ) > 0 );
	CHECK( MPModFloodCheck( flood, 1000, 1, 1000, 0 ) == 0 );
	// a map restart winds game time back: old stamps and an old silence are dropped
	MPModFloodReset( flood );
	for ( int i = 0; i < 5; i++ ) {
		MPModFloodCheck( flood, 500000 + i, 4, 4000, 10000 );
	}
	CHECK( MPModFloodCheck( flood, 500010, 4, 4000, 10000 ) > 0 );
	CHECK( MPModFloodCheck( flood, 100, 4, 4000, 10000 ) == 0 );
	CHECK( flood.count == 1 && !flood.silenced );
}

static std::string Clean( const char *in, int maxVisible = MP_MOD_NAME_VISIBLE_MAX, bool *changed = 0, int size = MP_MOD_NAME_BYTES ) {
	char out[ 96 ];
	std::memset( out, 'X', sizeof( out ) );
	const bool differs = MPModCleanName( in, out, size, maxVisible );
	if ( changed ) {
		*changed = differs;
	}
	CHECK( out[ size ] == 'X' );
	return out;
}

static void TestNames( void ) {
	bool changed = true;
	CHECK( Clean( "Ada", MP_MOD_NAME_VISIBLE_MAX, &changed ) == "Ada" && !changed );
	CHECK( Clean( "^1A^2da Lovelace", MP_MOD_NAME_VISIBLE_MAX, &changed ) == "^1A^2da Lovelace" && !changed );
	// control characters go; a tab or a newline in a name breaks every list that shows it
	CHECK( Clean( "A\nd\ta\x7f", MP_MOD_NAME_VISIBLE_MAX, &changed ) == "Ada" && changed );
	CHECK( Clean( "\x01\x02" ) == MP_MOD_DEFAULT_NAME );
	// spaces at either end
	CHECK( Clean( "  Ada  " ) == "Ada" );
	CHECK( Clean( "^1  Ada" ) == "^1Ada" );
	// nothing to show
	CHECK( Clean( "" ) == MP_MOD_DEFAULT_NAME );
	CHECK( Clean( "   " ) == MP_MOD_DEFAULT_NAME );
	CHECK( Clean( "^1^2 ^3" ) == MP_MOD_DEFAULT_NAME );
	CHECK( Clean( 0 ) == MP_MOD_DEFAULT_NAME );
	// a caret with nothing to colour would swallow what follows the name
	CHECK( Clean( "Ada^" ) == "Ada" );
	CHECK( Clean( "Ada^1" ) == "Ada" );
	CHECK( Clean( "A^\x01" "da" ) == "Ada" );
	// length counts what shows
	CHECK( Clean( "abcdefghij", 4 ) == "abcd" );
	CHECK( Clean( "^1a^2b^3c^4d^5e", 3 ) == "^1a^2b^3c" );
	CHECK( Clean( "ab  cd", 3 ) == "ab" );
	CHECK( Clean( "abc", 0 ) == "a" );
	// a UTF-8 character counts once and is never cut in half
	CHECK( Clean( "\xc3\x84" "da", 2 ) == "\xc3\x84" "d" );
	CHECK( Clean( "\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac", 2 ) == "\xe2\x82\xac\xe2\x82\xac" );
	CHECK( Clean( "ab\xe2\x82\xac", MP_MOD_NAME_VISIBLE_MAX, 0, 5 ) == "ab" );
	CHECK( Clean( "\xc3", 4 ) == "\xc3" );
	// the output never passes its buffer
	const std::string longName( 300, 'n' );
	CHECK( Clean( longName.c_str(), 1000, 0, 16 ) == std::string( 15, 'n' ) );
	CHECK( Clean( longName.c_str() ) == std::string( MP_MOD_NAME_VISIBLE_MAX, 'n' ) );

	char visible[ MP_MOD_NAME_BYTES ];
	MPModVisibleName( "^1B^2o^0B", visible, sizeof( visible ) );
	CHECK( std::string( visible ) == "bob" );
	MPModVisibleName( "x^", visible, sizeof( visible ) );
	CHECK( std::string( visible ) == "x^" );

	const char *reserved = "admin; Console ,referee;;";
	CHECK( MPModNameIsReserved( "admin", reserved ) );
	CHECK( MPModNameIsReserved( "^1A^2dmin", reserved ) );
	CHECK( MPModNameIsReserved( "CONSOLE", reserved ) );
	CHECK( MPModNameIsReserved( "referee", reserved ) );
	CHECK( !MPModNameIsReserved( "admin2", reserved ) );
	CHECK( !MPModNameIsReserved( "adm", reserved ) );
	CHECK( !MPModNameIsReserved( "", reserved ) );
	CHECK( !MPModNameIsReserved( "admin", "" ) );
	CHECK( !MPModNameIsReserved( "admin", 0 ) );
	CHECK( !MPModNameIsReserved( "admin", ";;, ," ) );
}

static void TestLog( void ) {
	mpModLogLine_t line;
	MPModLogBegin( line, 1795385700, "ban" );
	MPModLogString( line, "by", "console" );
	MPModLogString( line, "name", "A\"d\\a\n\x01" );
	MPModLogString( line, "range", "192.0.2.0/24" );
	MPModLogString( line, "reason", "" );		// an empty value leaves the field out
	MPModLogString( line, "skip", 0 );
	MPModLogNumber( line, "seconds", 600 );
	MPModLogEnd( line );
	CHECK( std::string( line.text ) ==
		"{\"time\":\"2026-11-22T22:15:00Z\",\"event\":\"ban\",\"by\":\"console\",\"name\":\"A\\\"d\\\\a\\u000a\\u0001\","
		"\"range\":\"192.0.2.0/24\",\"seconds\":600}\n" );
	CHECK( !line.truncated && line.length == static_cast<int>( std::strlen( line.text ) ) );

	// a value too long for the line is cut on a whole escape and the line stays one valid object
	const std::string quotes( 2000, '"' );
	MPModLogBegin( line, 0, "kick" );
	MPModLogString( line, "reason", quotes.c_str() );
	MPModLogString( line, "after", "x" );
	MPModLogNumber( line, "n", 5 );
	MPModLogEnd( line );
	CHECK( line.truncated );
	CHECK( line.length < MP_MOD_LOG_LINE_BYTES && line.text[ line.length ] == '\0' );
	const std::string text( line.text );
	CHECK( text.size() >= 5 && text.substr( text.size() - 5 ) == "\\\"\"}\n" );
	// every quote inside the value is escaped: the unescaped quotes are the structural ones
	int structural = 0;
	for ( size_t i = 0; i < text.size(); i++ ) {
		if ( text[ i ] == '\\' ) {
			i++;
		} else if ( text[ i ] == '"' ) {
			structural++;
		}
	}
	CHECK( structural % 2 == 0 );
	CHECK( text.find( "after" ) == std::string::npos );
	CHECK( text.find( '\n' ) == text.size() - 1 );
}

static void TestAdmins( void ) {
	// an admin's ban always ends, and never outlasts the host's limit
	CHECK( MPModAdminBanSeconds( 0, 1440 ) == 86400 );
	CHECK( MPModAdminBanSeconds( -5, 60 ) == 3600 );
	CHECK( MPModAdminBanSeconds( 3600, 1440 ) == 3600 );
	CHECK( MPModAdminBanSeconds( 86400, 1440 ) == 86400 );
	CHECK( MPModAdminBanSeconds( 86401, 1440 ) == 86400 );
	CHECK( MPModAdminBanSeconds( 10LL * 24 * 3600, 5 ) == 300 );
	CHECK( MPModAdminBanSeconds( 3600, 0 ) == 0 );
	CHECK( MPModAdminBanSeconds( 3600, -1 ) == 0 );
	CHECK( MPModAdminBanSeconds( 0, 0x7fffffff ) == MP_MOD_MAX_DURATION_SECONDS );

	CHECK( MPModAdminVerbHasTarget( MP_MOD_ADMIN_KICK ) && MPModAdminVerbHasTarget( MP_MOD_ADMIN_MUTE ) );
	CHECK( MPModAdminVerbHasTarget( MP_MOD_ADMIN_UNMUTE ) && MPModAdminVerbHasTarget( MP_MOD_ADMIN_BAN ) );
	CHECK( !MPModAdminVerbHasTarget( MP_MOD_ADMIN_LOGOUT ) && !MPModAdminVerbHasTarget( MP_MOD_ADMIN_NONE ) );
	CHECK( !MPModAdminVerbHasTarget( MP_MOD_ADMIN_VERB_COUNT ) && !MPModAdminVerbHasTarget( 255 ) );
	CHECK( std::strcmp( MPModAdminVerbName( MP_MOD_ADMIN_BAN ), "ban" ) == 0 );
	CHECK( std::strcmp( MPModAdminVerbName( 99 ), "" ) == 0 );
	// the wire carries these numbers: they are appended to, never renumbered
	CHECK( MP_MOD_ADMIN_KICK == 1 && MP_MOD_ADMIN_MUTE == 2 && MP_MOD_ADMIN_UNMUTE == 3 && MP_MOD_ADMIN_BAN == 4 &&
		MP_MOD_ADMIN_LOGOUT == 5 );

	mpModAdminTarget_t player;
	std::memset( &player, 0, sizeof( player ) );
	player.connected = true;
	player.current = true;
	for ( int verb = MP_MOD_ADMIN_KICK; verb <= MP_MOD_ADMIN_BAN; verb++ ) {
		CHECK( MPModAdminCheck( verb, 3, 5, player, 1440 ) == MP_MOD_ADMIN_OK );
		// nobody there, or somebody else by now
		mpModAdminTarget_t gone = player;
		gone.connected = false;
		CHECK( MPModAdminCheck( verb, 3, 5, gone, 1440 ) == MP_MOD_ADMIN_REFUSED_TARGET );
		mpModAdminTarget_t replaced = player;
		replaced.current = false;
		CHECK( MPModAdminCheck( verb, 3, 5, replaced, 1440 ) == MP_MOD_ADMIN_REFUSED_TARGET );
		CHECK( MPModAdminCheck( verb, 3, -1, player, 1440 ) == MP_MOD_ADMIN_REFUSED_TARGET );
		CHECK( MPModAdminCheck( verb, 3, 3, player, 1440 ) == MP_MOD_ADMIN_REFUSED_SELF );
		mpModAdminTarget_t host = player;
		host.host = true;
		CHECK( MPModAdminCheck( verb, 3, 5, host, 1440 ) == MP_MOD_ADMIN_REFUSED_HOST );
		mpModAdminTarget_t admin = player;
		admin.admin = true;
		CHECK( MPModAdminCheck( verb, 3, 5, admin, 1440 ) == MP_MOD_ADMIN_REFUSED_ADMIN );
		mpModAdminTarget_t bot = player;
		bot.bot = true;
		CHECK( MPModAdminCheck( verb, 3, 5, bot, 1440 ) == MP_MOD_ADMIN_REFUSED_BOT );
	}
	// only a ban answers to the host's limit and to the admin's own address
	CHECK( MPModAdminCheck( MP_MOD_ADMIN_BAN, 3, 5, player, 0 ) == MP_MOD_ADMIN_REFUSED_BANS_OFF );
	CHECK( MPModAdminCheck( MP_MOD_ADMIN_KICK, 3, 5, player, 0 ) == MP_MOD_ADMIN_OK );
	CHECK( MPModAdminCheck( MP_MOD_ADMIN_MUTE, 3, 5, player, 0 ) == MP_MOD_ADMIN_OK );
	mpModAdminTarget_t housemate = player;
	housemate.sameAddress = true;
	CHECK( MPModAdminCheck( MP_MOD_ADMIN_BAN, 3, 5, housemate, 1440 ) == MP_MOD_ADMIN_REFUSED_SELF );
	CHECK( MPModAdminCheck( MP_MOD_ADMIN_KICK, 3, 5, housemate, 1440 ) == MP_MOD_ADMIN_OK );
	CHECK( MPModAdminCheck( MP_MOD_ADMIN_MUTE, 3, 5, housemate, 1440 ) == MP_MOD_ADMIN_OK );
	// a logout needs no target; anything else is not a request
	mpModAdminTarget_t nobody;
	std::memset( &nobody, 0, sizeof( nobody ) );
	CHECK( MPModAdminCheck( MP_MOD_ADMIN_LOGOUT, 3, 0, nobody, 0 ) == MP_MOD_ADMIN_OK );
	CHECK( MPModAdminCheck( MP_MOD_ADMIN_NONE, 3, 5, player, 1440 ) == MP_MOD_ADMIN_REFUSED_VERB );
	CHECK( MPModAdminCheck( MP_MOD_ADMIN_VERB_COUNT, 3, 5, player, 1440 ) == MP_MOD_ADMIN_REFUSED_VERB );
	CHECK( MPModAdminCheck( 200, 3, 5, player, 1440 ) == MP_MOD_ADMIN_REFUSED_VERB );
}

int main( void ) {
	TestAddresses();
	TestDurations();
	TestConsoleWords();
	TestBans();
	TestMutes();
	TestFlood();
	TestNames();
	TestLog();
	TestAdmins();
	if ( failures != 0 ) {
		std::printf( "%d moderation core check(s) failed\n", failures );
		return 1;
	}
	std::printf( "moderation core: all checks passed\n" );
	return 0;
}
