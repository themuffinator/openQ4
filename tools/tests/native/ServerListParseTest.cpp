/*
 * openQ4 HTTPS server list reader (src/framework/async/ServerListParse.h,
 * roadmap MP-19) compiled alone: the list tools/master/openq4_master.py
 * writes, unknown keys and nesting skipped, and damaged or foreign text
 * refused. Built and run by tools/tests/mp_server_discovery.py.
 */
#include "../../../src/framework/async/ServerListParse.h"

#include <cstdio>

static int failures = 0;

static void Check( bool condition, const char *what ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAIL: %s\n", what );
		++failures;
	}
}

int main() {
	std::vector<std::string> addresses;
	std::string error;
	const std::string list =
		"{\n \"format\": \"openq4-server-list\",\n \"version\": 1,\n \"generated\": \"2026-11-23T12:00:00Z\",\n"
		" \"servers\": [\n  {\"address\": \"192.0.2.7:28004\", \"protocol\": 131114, \"game\": \"baseoq4\", \"name\": \"A \\\"quoted\\\" name\","
		" \"map\": \"mp/q4dm1\", \"players\": 3, \"maxPlayers\": 8, \"seen\": 1795000000},\n"
		"  {\"address\": \"[2001:db8::7]:28004\", \"extra\": {\"nested\": [1, 2, {\"deep\": true}]}, \"name\": \"\\u00e9t\\u00e9\"},\n"
		"  {\"name\": \"no address\"}\n ],\n \"future\": [null, false, -1.5e3]\n}\n";
	Check( serverListParse::ParseServerList( list, addresses, error ), "the master's list reads" );
	Check( addresses.size() == 2 && addresses[ 0 ] == "192.0.2.7:28004" && addresses[ 1 ] == "[2001:db8::7]:28004",
		"each server's address, in order; an entry without one is skipped" );

	Check( serverListParse::ParseServerList( "{\"format\":\"openq4-server-list\",\"servers\":[]}", addresses, error ) && addresses.empty(),
		"an empty list reads as no servers" );
	Check( !serverListParse::ParseServerList( "{\"servers\":[{\"address\":\"192.0.2.7:1\"}]}", addresses, error ) && addresses.empty(),
		"a list that does not say it is an openQ4 list is refused" );
	Check( !serverListParse::ParseServerList( "{\"format\":\"something-else\",\"servers\":[]}", addresses, error ),
		"another format is refused" );
	const char *const damaged[] = {
		"", "[]", "{", "{\"format\":\"openq4-server-list\",\"servers\":[{\"address\":\"192.0.2.7:1\"}", "{\"format\":\"openq4-server-list\"} trailing",
		"{\"format\":\"openq4-server-list\",\"servers\":[{\"address\":\"192.0.2.7:1\" \"x\":1}]}",
		"{\"format\":\"openq4-server-list\",\"servers\":[{\"address\":\"bad\\q\"}]}", "{\"format\":\"openq4-server-list\",\"a\":\"\x01\"}",
		"<html>404</html>",
	};
	for ( const char *text : damaged ) {
		Check( !serverListParse::ParseServerList( text, addresses, error ) && !error.empty(), "damaged text is refused with a reason" );
	}
	std::string deep = "{\"format\":\"openq4-server-list\",\"x\":";
	for ( int i = 0; i < 40; i++ ) deep += "[";
	for ( int i = 0; i < 40; i++ ) deep += "]";
	deep += "}";
	Check( !serverListParse::ParseServerList( deep, addresses, error ), "nesting past the limit is refused" );
	std::string many = "{\"format\":\"openq4-server-list\",\"servers\":[";
	for ( int i = 0; i < 5000; i++ ) many += std::string( i ? "," : "" ) + "{\"address\":\"192.0.2.7:" + std::to_string( 1 + i ) + "\"}";
	many += "]}";
	Check( serverListParse::ParseServerList( many, addresses, error ) && addresses.size() == serverListParse::MAX_SERVERS,
		"a list longer than the browser holds is cut to its limit" );
	std::string longAddress = "{\"format\":\"openq4-server-list\",\"servers\":[{\"address\":\"" + std::string( 100, 'a' ) + "\"}]}";
	Check( serverListParse::ParseServerList( longAddress, addresses, error ) && addresses.empty(), "an address longer than any address is skipped" );
	if ( failures != 0 ) {
		std::fprintf( stderr, "%d failures\n", failures );
		return 1;
	}
	std::printf( "server list reader: all checks passed\n" );
	return 0;
}
