// Exercises src/mpgame/mp/LiveEvents.h alone: JSON line building over hostile
// names, the state diff and its order, the kill line and line overflow. With a
// path argument it also writes a sample match as a stream for
// tools/tests/mp_live_event_stream_contract.py to parse and replay through the
// sample overlay.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "src/mpgame/mp/LiveEvents.h"

static int failures = 0;
#define CHECK( condition ) do { if ( !( condition ) ) { ++failures; std::fprintf( stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition ); } } while ( 0 )

static const std::uint32_t TRACKED = ( 1u << 0 ) | ( 1u << 1 ) | ( 1u << 2 ) | ( 1u << 3 );

struct Collector {
	std::vector<std::string> lines;
	void operator()( const mpLiveEventLine &line ) { lines.emplace_back( line.Text(), line.Length() ); }
};

static bool Has( const std::string &text, const char *piece ) {
	return text.find( piece ) != std::string::npos;
}

static void Join( mpLiveEventState &state, int slot, const char *name, int team ) {
	mpLiveEventPlayer &player = state.players[ slot ];
	player = mpLiveEventPlayer();
	player.present = true;
	player.team = team;
	player.bot = 0;
	std::snprintf( player.name, sizeof( player.name ), "%s", name );
}

static void TestLines() {
	mpLiveEventLine line;
	line.Begin( 7, 1234, "sample" );
	line.Int( "n", -5 );
	line.Bool( "yes", true );
	line.Null( "none" );
	line.String( "text", "a\"b\\c\n\x01\x7f" );
	CHECK( line.End() );
	CHECK( std::string( line.Text() ) ==
		"{\"v\":1,\"seq\":7,\"t\":1234,\"type\":\"sample\",\"n\":-5,\"yes\":true,\"none\":null,"
		"\"text\":\"a\\\"b\\\\c\\u000a\\u0001\\u007f\"}\n" );
	CHECK( line.Length() == static_cast<int>( std::strlen( line.Text() ) ) );

	// Well-formed UTF-8 passes through; anything else becomes U+FFFD byte by byte.
	line.Begin( 0, 0, "utf8" );
	line.String( "ok", "Zo\xc3\xab \xe2\x82\xac \xf0\x9f\x98\x80" );
	line.String( "lone", "\x80x" );
	line.String( "overlong", "\xc0\xaf" );
	line.String( "surrogate", "\xed\xa0\x80" );
	line.String( "cut", "a\xe2\x82" );
	line.String( "high", "\xf4\x90\x80\x80" );
	CHECK( line.End() );
	const std::string utf8 = line.Text();
	CHECK( Has( utf8, "\"ok\":\"Zo\xc3\xab \xe2\x82\xac \xf0\x9f\x98\x80\"" ) );
	CHECK( Has( utf8, "\"lone\":\"\xef\xbf\xbdx\"" ) );
	CHECK( Has( utf8, "\"overlong\":\"\xef\xbf\xbd\xef\xbf\xbd\"" ) );
	CHECK( Has( utf8, "\"surrogate\":\"\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\"" ) );
	CHECK( Has( utf8, "\"cut\":\"a\xef\xbf\xbd\xef\xbf\xbd\"" ) );
	CHECK( Has( utf8, "\"high\":\"\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\"" ) );

	// A table lookup out of range is null, never a read past the table.
	line.Begin( 0, 0, "names" );
	line.Name( "a", mpLiveEventPhaseNames, MP_LIVE_EVENT_PHASES, 3 );
	line.Name( "b", mpLiveEventPhaseNames, MP_LIVE_EVENT_PHASES, MP_LIVE_EVENT_PHASES );
	line.Name( "c", mpLiveEventPhaseNames, MP_LIVE_EVENT_PHASES, -1 );
	CHECK( line.End() );
	CHECK( Has( line.Text(), "\"a\":\"live\",\"b\":null,\"c\":null" ) );

	// A line that does not fit is reported, and stays a terminated string.
	line.Begin( 0, 0, "big" );
	const std::string filler( 1000, 'x' );
	for ( int i = 0; i < 40; i++ ) {
		line.String( "k", filler.c_str() );
	}
	CHECK( !line.End() && line.Overflowed() );
	CHECK( line.Length() < MP_LIVE_EVENT_LINE_BYTES && line.Text()[ line.Length() ] == '\0' );
	// The next line starts clean.
	line.Begin( 1, 1, "after" );
	CHECK( line.End() && !line.Overflowed() );
}

static void TestRoster() {
	// Thirty-two players with the worst names the escaper can be handed still fit one line.
	mpLiveEventState full;
	full.teamGame = true;
	char worst[ MP_LIVE_EVENT_NAME_BYTES ];
	std::memset( worst, 0x01, sizeof( worst ) - 1 );	// every byte becomes six
	worst[ sizeof( worst ) - 1 ] = '\0';
	for ( int slot = 0; slot < MP_LIVE_EVENT_CLIENTS; slot++ ) {
		Join( full, slot, worst, slot % 2 );
	}
	mpLiveEventLine line;
	CHECK( MPLiveEventStreamLine( line, 0, 0, "server", "mp/q4ctf1", "CTF", full ) );
	CHECK( !line.Overflowed() && line.Text()[ line.Length() - 1 ] == '\n' );
	CHECK( std::strchr( line.Text(), '\n' ) == line.Text() + line.Length() - 1 );
	// The live file's opening line has no wall-clock time. The match log's has
	// one, after the game type, and a full roster still fits beside it.
	CHECK( std::strstr( line.Text(), "\"utc\"" ) == 0 );
	CHECK( MPLiveEventStreamLine( line, 0, 0, "server", "mp/q4ctf1", "CTF", full, "2026-11-22T10:15:00Z" ) );
	CHECK( !line.Overflowed() && line.Text()[ line.Length() - 1 ] == '\n' );
	CHECK( std::strstr( line.Text(), "\"gameType\":\"CTF\",\"utc\":\"2026-11-22T10:15:00Z\",\"" ) != 0 );
	CHECK( MPLiveEventStreamLine( line, 0, 0, "server", "mp/q4ctf1", "CTF", full, "" ) );
	CHECK( std::strstr( line.Text(), "\"utc\"" ) == 0 );
}

static void TestDiff() {
	mpLiveEventState before;
	mpLiveEventState after;
	mpLiveEventLine line;
	Collector out;
	int sequence = 10;

	// Nothing changed, nothing written.
	CHECK( MPLiveEventDiff( before, after, TRACKED, sequence, 0, line, out ) == 0 && sequence == 10 && out.lines.empty() );

	// Phase first, then the roster, scores, flags and powerups.
	before.teamGame = after.teamGame = true;
	before.flags = after.flags = true;
	Join( before, 1, "Kane", 0 );
	Join( after, 1, "Kane", 0 );
	Join( before, 2, "Voss", 1 );
	Join( after, 2, "Voss", 1 );
	Join( after, 5, "New \"guy\"", 1 );
	after.players[ 5 ].powerups = 2u;	// arrives with haste: joining gains nothing
	after.phase = 3;
	before.phase = 2;
	after.teamScore[ 1 ] = 2;
	after.players[ 1 ].score = 4;
	after.players[ 2 ].powerups = 1u | ( 1u << 4 );	// quad, and an untracked flag bit
	after.flagState[ 0 ] = MP_LIVE_FLAG_TAKEN;
	after.flagCarrier[ 0 ] = 2;
	const int count = MPLiveEventDiff( before, after, TRACKED, sequence, 5000, line, out );
	CHECK( count == 6 && sequence == 16 && out.lines.size() == 6 );
	if ( out.lines.size() == 6 ) {
		CHECK( Has( out.lines[ 0 ], "\"seq\":10,\"t\":5000,\"type\":\"phase\",\"from\":\"countdown\",\"to\":\"live\"" ) );
		CHECK( Has( out.lines[ 1 ], "\"type\":\"player\",\"event\":\"join\",\"slot\":5,\"name\":\"New \\\"guy\\\"\",\"team\":\"strogg\"" ) );
		CHECK( Has( out.lines[ 2 ], "\"type\":\"score\",\"team\":\"strogg\",\"score\":2,\"was\":0" ) );
		CHECK( Has( out.lines[ 3 ], "\"type\":\"playerScore\",\"slot\":1,\"name\":\"Kane\",\"score\":4,\"was\":0" ) );
		CHECK( Has( out.lines[ 4 ], "\"type\":\"flag\",\"flag\":\"marine\",\"state\":\"taken\",\"carrier\":2,\"carrierName\":\"Voss\",\"wasCarrier\":null" ) );
		CHECK( Has( out.lines[ 5 ], "\"type\":\"powerup\",\"event\":\"gain\",\"slot\":2,\"name\":\"Voss\",\"powerup\":\"quadDamage\"" ) );
	}

	// Leaving, renaming, spectating and changing team are each one roster line;
	// someone who leaves carrying a powerup loses nothing.
	out.lines.clear();
	before = after;
	after.players[ 5 ].present = false;
	std::snprintf( after.players[ 1 ].name, sizeof( after.players[ 1 ].name ), "Kane2" );
	after.players[ 2 ].spectator = true;
	after.players[ 2 ].powerups = 0;
	CHECK( MPLiveEventDiff( before, after, TRACKED, sequence, 6000, line, out ) == 4 );
	if ( out.lines.size() == 4 ) {
		CHECK( Has( out.lines[ 0 ], "\"event\":\"name\",\"slot\":1,\"name\":\"Kane2\",\"was\":\"Kane\"" ) );
		CHECK( Has( out.lines[ 1 ], "\"event\":\"spectator\",\"slot\":2" ) && Has( out.lines[ 1 ], "\"spectator\":true" ) );
		CHECK( Has( out.lines[ 2 ], "\"event\":\"leave\",\"slot\":5,\"name\":\"New \\\"guy\\\"\"" ) );
		CHECK( Has( out.lines[ 3 ], "\"type\":\"powerup\",\"event\":\"end\",\"slot\":2" ) );
	}
	out.lines.clear();
	before = after;
	after.players[ 1 ].team = 1;
	CHECK( MPLiveEventDiff( before, after, TRACKED, sequence, 6100, line, out ) == 1 &&
		Has( out.lines[ 0 ], "\"event\":\"team\",\"slot\":1,\"name\":\"Kane2\",\"team\":\"strogg\"" ) );

	// One Flag names the holding team; a capture names who carried it home.
	out.lines.clear();
	before = after;
	after.flagState[ 2 ] = MP_LIVE_FLAG_TAKEN_STROGG;
	after.flagCarrier[ 2 ] = 1;
	after.flagState[ 0 ] = MP_LIVE_FLAG_AT_BASE;
	after.flagCarrier[ 0 ] = -1;
	CHECK( MPLiveEventDiff( before, after, TRACKED, sequence, 7000, line, out ) == 2 );
	if ( out.lines.size() == 2 ) {
		CHECK( Has( out.lines[ 0 ], "\"flag\":\"marine\",\"state\":\"base\",\"carrier\":null,\"carrierName\":null,\"wasCarrier\":2,\"wasCarrierName\":\"Voss\"" ) );
		CHECK( Has( out.lines[ 1 ], "\"flag\":\"neutral\",\"state\":\"taken\",\"by\":\"strogg\",\"carrier\":1" ) );
	}

	// A mode without flags, or a map that just loaded them, reports no flag change.
	out.lines.clear();
	before = after;
	before.flags = false;
	after.flagState[ 1 ] = MP_LIVE_FLAG_DROPPED;
	CHECK( MPLiveEventDiff( before, after, TRACKED, sequence, 7100, line, out ) == 0 );

	// Free-for-all: no team score lines, and teams are null.
	mpLiveEventState ffaBefore;
	mpLiveEventState ffaAfter;
	Join( ffaBefore, 0, "Solo", -1 );
	Join( ffaAfter, 0, "Solo", -1 );
	ffaAfter.teamScore[ 0 ] = 9;
	ffaAfter.players[ 0 ].score = 1;
	out.lines.clear();
	CHECK( MPLiveEventDiff( ffaBefore, ffaAfter, TRACKED, sequence, 100, line, out ) == 1 &&
		Has( out.lines[ 0 ], "\"type\":\"playerScore\"" ) );

	// Reaching review closes the match with its standings.
	out.lines.clear();
	ffaBefore = ffaAfter;
	ffaBefore.phase = 3;
	ffaAfter.phase = MP_LIVE_PHASE_REVIEW;
	CHECK( MPLiveEventDiff( ffaBefore, ffaAfter, TRACKED, sequence, 200, line, out ) == 2 );
	if ( out.lines.size() == 2 ) {
		CHECK( Has( out.lines[ 0 ], "\"to\":\"review\"" ) );
		CHECK( Has( out.lines[ 1 ], "\"type\":\"standings\",\"phase\":\"review\",\"teamGame\":false,\"players\":[{\"slot\":0,\"name\":\"Solo\",\"team\":null,\"spectator\":false,\"score\":1,\"bot\":false}]" ) );
	}
}

static void TestKill() {
	mpLiveEventState state;
	state.teamGame = true;
	Join( state, 1, "Kane", 0 );
	Join( state, 2, "Voss", 1 );
	Join( state, 3, "Rhodes", 0 );
	mpLiveEventLine line;
	CHECK( MPLiveEventKillLine( line, 3, 900, state, 1, 2, "rocketlauncher", "weapon", true ) );
	CHECK( Has( line.Text(), "\"type\":\"kill\",\"killer\":1,\"killerName\":\"Kane\",\"victim\":2,\"victimName\":\"Voss\","
		"\"weapon\":\"rocketlauncher\",\"cause\":\"weapon\",\"suicide\":false,\"teamKill\":false,\"quad\":true" ) );
	CHECK( MPLiveEventKillLine( line, 4, 900, state, 1, 3, "railgun", "weapon", false ) );
	CHECK( Has( line.Text(), "\"teamKill\":true" ) );
	// The world and a suicide have no killer; neither is a team kill.
	CHECK( MPLiveEventKillLine( line, 5, 900, state, -1, 2, NULL, "world", false ) );
	CHECK( Has( line.Text(), "\"killer\":null,\"killerName\":null,\"victim\":2" ) && Has( line.Text(), "\"weapon\":null,\"cause\":\"world\",\"suicide\":true,\"teamKill\":false" ) );
	CHECK( MPLiveEventKillLine( line, 6, 900, state, 2, 2, "rocketlauncher", "weapon", false ) );
	CHECK( Has( line.Text(), "\"killer\":null" ) && Has( line.Text(), "\"suicide\":true" ) );
	// Slots the roster does not hold are null, including out-of-range ones.
	CHECK( MPLiveEventKillLine( line, 7, 900, state, 9, 40, NULL, "telefrag", false ) );
	CHECK( Has( line.Text(), "\"killer\":null,\"killerName\":null,\"victim\":null,\"victimName\":null" ) && Has( line.Text(), "\"teamKill\":false" ) );
}

// A short CTF match, written exactly as the game would: the opening line, then
// the diff of each frame and the kills between them.
static bool WriteSample( const char *path ) {
	std::ofstream file( path, std::ios::binary | std::ios::trunc );
	if ( !file ) {
		return false;
	}
	mpLiveEventLine line;
	int sequence = 0;
	const auto write = [&]( const mpLiveEventLine &complete ) {
		file.write( complete.Text(), complete.Length() );
	};
	mpLiveEventState state;
	state.teamGame = true;
	state.flags = true;
	state.phase = 1;
	Join( state, 0, "Kane", 0 );
	Join( state, 1, "Voss \"the\\Wall\"", 1 );
	Join( state, 2, "Zo\xc3\xab", 0 );
	Join( state, 3, "Watcher", 1 );
	state.players[ 3 ].spectator = true;
	state.players[ 1 ].bot = 1;
	if ( MPLiveEventStreamLine( line, sequence, 1000, "server", "mp/q4ctf1", "CTF", state ) ) {
		write( line );
	}
	sequence++;

	mpLiveEventState next = state;
	const auto step = [&]( int time ) {
		MPLiveEventDiff( state, next, TRACKED, sequence, time, line, write );
		state = next;
	};
	const auto kill = [&]( int time, int killer, int victim, const char *weapon, const char *cause, bool quad ) {
		if ( MPLiveEventKillLine( line, sequence, time, state, killer, victim, weapon, cause, quad ) ) {
			write( line );
		}
		sequence++;
	};
	next.phase = 2; step( 2000 );
	next.phase = 3; step( 5000 );
	next.players[ 0 ].powerups = 1u; step( 9000 );
	kill( 9500, 0, 1, "rocketlauncher", "weapon", true );
	next.players[ 0 ].score = 1; step( 9516 );
	next.flagState[ 1 ] = MP_LIVE_FLAG_TAKEN; next.flagCarrier[ 1 ] = 2; step( 12000 );
	kill( 13000, 1, 1, "grenadelauncher", "weapon", false );
	next.players[ 1 ].score = -1; step( 13016 );
	next.flagState[ 1 ] = MP_LIVE_FLAG_AT_BASE; next.flagCarrier[ 1 ] = -1;
	next.teamScore[ 0 ] = 1; next.players[ 2 ].score = 5; step( 15000 );
	next.players[ 0 ].powerups = 0; step( 39000 );
	Join( next, 4, "Late", 1 ); step( 40000 );
	kill( 41000, -1, 4, NULL, "world", false );
	next.players[ 3 ].present = false; step( 42000 );
	next.phase = MP_LIVE_PHASE_REVIEW; step( 60000 );
	file.flush();
	return file.good();
}

int main( int argc, char **argv ) {
	TestLines();
	TestRoster();
	TestDiff();
	TestKill();
	if ( argc > 1 && !WriteSample( argv[ 1 ] ) ) {
		std::fprintf( stderr, "live events fixture: could not write %s\n", argv[ 1 ] );
		return 1;
	}
	if ( failures != 0 ) {
		std::fprintf( stderr, "live events fixture: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "live events fixture: ok\n" );
	return 0;
}
