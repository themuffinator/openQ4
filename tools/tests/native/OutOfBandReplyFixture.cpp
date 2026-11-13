// Cases for src/framework/async/OutOfBandReply.h and idConnectRoster::EncodeWithin.
// tools/tests/oob_reply_budget_state.py generates SERVERINFO_DEFAULTS from the
// source's CVAR_SERVERINFO declarations, builds this with ConnectRoster.cpp and
// also runs it against single-reversal copies of the sources.
#include "src/framework/async/OutOfBandReply.h"
#include "src/framework/async/ConnectRoster.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// GENERATED_SERVERINFO

static int failures = 0;
static void Check( bool passed, const char *label ) {
	if ( !passed ) {
		std::fprintf( stderr, "FAILED: %s\n", label );
		failures++;
	}
}

// The wire size of a key and value, counted here rather than by the header.
static int Wire( const std::string &key, const std::string &value ) {
	return static_cast<int>( key.size() + 1 + value.size() + 1 );
}

using dict_t = std::vector<std::pair<std::string, std::string>>;

static dict_t ServerInfo( int nameBytes, int extraKeys ) {
	dict_t dict;
	for ( const auto &pair : SERVERINFO_DEFAULTS ) {
		dict.emplace_back( pair[0], pair[1] );
	}
	for ( auto &pair : dict ) {
		if ( pair.first == "si_name" ) {
			pair.second = std::string( nameBytes, 'N' );
		} else if ( pair.first == "si_map" ) {
			pair.second = "mp/q4ctf1";
		} else if ( pair.first == "fs_game" ) {
			pair.second = "baseoq4";
		}
	}
	for ( int i = 0; i < extraKeys; i++ ) {
		dict.emplace_back( "si_modSetting" + std::to_string( i ), "a moderately long mod setting value" );
	}
	return dict;
}

// Chooses keys as the server does and returns the dictionary's wire bytes and
// whether every required key it has was kept.
static std::vector<std::string> lastKept;

static int Choose( const dict_t &dict, const char *const *required, int requiredCount, int budget, bool &requiredKept ) {
	lastKept.clear();
	std::vector<const char *> keys, values;
	for ( const auto &pair : dict ) {
		keys.push_back( pair.first.c_str() );
		values.push_back( pair.second.c_str() );
	}
	bool keepArray[ 512 ] = {};
	if ( dict.size() > sizeof( keepArray ) ) {
		Check( false, "the fixture dictionary fits its keep array" );
		return 0;
	}
	if ( !dict.empty() ) {
		idOutOfBandReply::ChooseKeys( keys.data(), values.data(), static_cast<int>( dict.size() ), required, requiredCount, budget, keepArray );
	}
	int bytes = 2;
	requiredKept = true;
	for ( size_t i = 0; i < dict.size(); i++ ) {
		bool isRequired = false;
		for ( int r = 0; r < requiredCount; r++ ) {
			isRequired = isRequired || dict[i].first == required[r];
		}
		if ( keepArray[i] ) {
			bytes += Wire( dict[i].first, dict[i].second );
			lastKept.push_back( dict[i].first );
		} else if ( isRequired ) {
			requiredKept = false;
		}
	}
	return bytes;
}

// The info reply the server writes (AsyncServer.cpp ProcessGetInfoMessage):
// header, the fitted dictionary, the player list, the OS mask.
static void CheckInfoReply( int players, int nameBytes, int serverNameBytes, int extraKeys ) {
	const dict_t dict = ServerInfo( serverNameBytes, extraKeys );
	const int header = 2 + static_cast<int>( std::strlen( "infoResponse" ) ) + 1 + 4 + 4;
	int requiredDict = 2;
	for ( const auto &pair : dict ) {
		if ( idOutOfBandReply::InList( pair.first.c_str(), idOutOfBandReply::BROWSER_KEYS, idOutOfBandReply::BROWSER_KEY_COUNT ) ) {
			requiredDict += Wire( pair.first, pair.second );
		}
	}
	std::vector<int> names( players, nameBytes );
	const int limit = idOutOfBandReply::NameLimit( names.data(), players, 1232 - header - requiredDict - 4 );
	int list = 1;
	for ( int i = 0; i < players; i++ ) {
		list += 8 + ( limit >= 0 && nameBytes > limit ? limit : nameBytes );
	}
	bool requiredKept = false;
	const int dictBytes = Choose( dict, idOutOfBandReply::BROWSER_KEYS, idOutOfBandReply::BROWSER_KEY_COUNT,
		idOutOfBandReply::BUDGET_BYTES - header - list - 4, requiredKept );
	const int total = header + dictBytes + list + 4;
	Check( total <= 1232, "an info reply fits one packet on a 1280-byte IPv6 path" );
	Check( requiredKept, "an info reply keeps every browser key the server has" );
	// What the browser's rows, filters and details card cannot do without
	// (ServerScan.cpp, Session_menu.cpp), whatever the required list says.
	for ( const char *key : { "si_name", "si_map", "si_gameType", "si_maxPlayers", "si_usePass", "si_pure", "fs_game",
			"net_serverDedicated", "si_fragLimit", "si_timeLimit", "si_captureLimit", "si_spectators", "si_teamDamage" } ) {
		bool present = false;
		for ( const auto &kept : lastKept ) {
			present = present || kept == key;
		}
		Check( present, "an info reply carries the keys the browser shows" );
	}
	Check( limit < 0 || limit >= 8 || players > 24, "names are cut only for a crowded server" );
	if ( players == 0 && extraKeys == 0 ) {
		Check( dictBytes + header + list + 4 > 1100, "an info reply fills the packet with further keys" );
	}
}

static void CheckDeferredConnectReply( int players, int nameBytes ) {
	const dict_t dict = ServerInfo( 63, 0 );
	const int header = 2 + static_cast<int>( std::strlen( "connectResponse" ) ) + 1 + 4 * 4;
	bool requiredKept = false;
	const int dictBytes = Choose( dict, idOutOfBandReply::LOADING_KEYS, idOutOfBandReply::LOADING_KEY_COUNT, 0, requiredKept );
	Check( requiredKept, "a deferred connect reply names the server and map" );
	Check( dictBytes < 200, "a deferred connect reply carries only the loading keys" );

	idConnectRoster::roster_t roster;
	std::memset( &roster, 0, sizeof( roster ) );
	roster.count = players;
	for ( int i = 0; i < players; i++ ) {
		roster.entries[i].slot = static_cast<std::uint8_t>( i );
		std::memset( roster.entries[i].name, 'p', nameBytes );
		roster.entries[i].name[ nameBytes ] = '\0';
	}
	std::uint8_t block[ idConnectRoster::MAX_BYTES ];
	const int used = header + dictBytes;
	const std::size_t size = idConnectRoster::EncodeWithin( roster, block, sizeof( block ), static_cast<std::size_t>( 1232 - used - 6 ) );
	Check( size > 0 && used + 6 + static_cast<int>( size ) <= 1232, "a deferred connect reply with its roster fits one packet" );
	idConnectRoster::roster_t decoded;
	Check( idConnectRoster::Decode( block, size, decoded ) && decoded.count == roster.count, "the trimmed roster decodes" );
	if ( players <= 12 && nameBytes <= 20 ) {
		Check( roster.count == players, "a roster that fits keeps every player" );
	} else if ( players == 32 && nameBytes == idConnectRoster::MAX_NAME_BYTES ) {
		Check( roster.count > 0 && roster.count < players, "a roster too large for the packet lists the first players" );
	}
}

int main() {
	using namespace idOutOfBandReply;
	Check( BUDGET_BYTES == 1280 - 40 - 8, "the budget is a 1280-byte path less IPv6 and UDP headers" );
	Check( StringBytes( "abc" ) == 4 && StringBytes( "" ) == 1 && PairBytes( "k", "vv" ) == 5, "strings count their NUL" );

	{
		const char *keys[] = { "a", "big", "b", "c" };
		const char *values[] = { "1", "0123456789", "2", "3" };
		const char *required[] = { "c" };
		bool keep[4];
		const int used = ChooseKeys( keys, values, 4, required, 1, 2 + 4 + 4 + 4, keep );
		Check( keep[3] && keep[0] && !keep[1] && keep[2] && used == 14, "required keys first, then the rest in order while they fit" );
		const int over = ChooseKeys( keys, values, 4, required, 1, 0, keep );
		Check( keep[3] && !keep[0] && !keep[1] && !keep[2] && over == 6, "required keys are kept over budget" );
		const int all = ChooseKeys( keys, values, 4, required, 1, 1000, keep );
		Check( keep[0] && keep[1] && keep[2] && keep[3] && all == 2 + 4 + 15 + 4 + 4, "a small dictionary is written whole" );
	}
	{
		const int names[] = { 10, 30, 5 };
		Check( NameLimit( names, 3, 1 + 24 + 45 ) == -1, "names that fit are not cut" );
		const int limit = NameLimit( names, 3, 1 + 24 + 30 );
		Check( limit == 15, "the longest names are cut just enough" );
		Check( NameLimit( names, 3, 10 ) == 0, "no room cuts every name" );
	}

	for ( int players : { 0, 1, 12, 24, 32 } ) {
		for ( int name : { 6, 20, idConnectRoster::MAX_NAME_BYTES } ) {
			CheckInfoReply( players, name, 63, 0 );
			CheckDeferredConnectReply( players, name );
		}
	}
	CheckInfoReply( 32, idConnectRoster::MAX_NAME_BYTES, 63, 40 );
	CheckInfoReply( 0, 6, 12, 40 );

	{
		idConnectRoster::roster_t roster;
		std::memset( &roster, 0, sizeof( roster ) );
		roster.count = 2;
		std::strcpy( roster.entries[0].name, "one" );
		std::strcpy( roster.entries[1].name, "two" );
		std::uint8_t block[ idConnectRoster::MAX_BYTES ];
		Check( idConnectRoster::EncodeWithin( roster, block, sizeof( block ), 2 ) == 0, "a limit below the header writes nothing" );
		roster.count = 2;
		roster.entries[1].team = 9;
		Check( idConnectRoster::EncodeWithin( roster, block, sizeof( block ), 1000 ) == 0 && roster.count == 2,
			"a malformed roster is refused, not trimmed" );
	}

	std::printf( "out-of-band reply cases: %s\n", failures ? "FAILED" : "ok" );
	return failures ? 1 : 0;
}
