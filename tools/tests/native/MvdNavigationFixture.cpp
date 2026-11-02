// Executes the MVD 1.3 navigation code: marker records and their directory,
// keyframe records and their directory, the game baseline grammar check, marker
// stepping, the server's marker diff and playback auto-tracking. The Python
// driver tools/tests/mvd_navigation_contract.py compiles this against the
// production headers, then against single-reversal mutations of them.

#include "src/framework/async/MvdNavigation.h"
#include "src/mpgame/mp/MvdMarkers.h"
#include "src/mpgame/mp/DemoAutoTrack.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace openq4_mvd;

static int failures = 0;
#define CHECK( condition ) do { if ( !( condition ) ) { std::fprintf( stderr, "FAILED line %d: %s\n", __LINE__, #condition ); failures++; } } while ( 0 )

static const int KINDS = 9;
static const int CLIENTS = 32;
static const int INSTANCES = 8;

static Marker MakeMarker( int time, int kind, int client, const char *text ) {
	Marker marker;
	marker.gameTime = time;
	marker.kind = kind;
	marker.client = client;
	marker.instance = -1;
	marker.value0 = time / 10;
	marker.value1 = -time;
	for ( int i = 0; text[i] != '\0' && i < MARKER_TEXT_BYTES; i++ ) {
		marker.text[i] = text[i];
	}
	return marker;
}

static void TestMarkerRecords() {
	const Marker marker = MakeMarker( 12345, 8, -1, "round two: overtime" );
	std::vector<std::uint8_t> record;
	AppendU32( record, 777 );
	AppendMarkerBody( record, marker );
	Marker parsed;
	int frame = 0;
	CHECK( ParseMarkerRecord( record.data(), static_cast<int>( record.size() ), parsed, frame, KINDS, CLIENTS, INSTANCES ) );
	CHECK( frame == 777 && parsed.gameTime == 12345 && parsed.kind == 8 && parsed.client == -1 &&
		parsed.value0 == 1234 && parsed.value1 == -12345 && std::string( parsed.text ) == "round two: overtime" );

	// Trailing bytes, truncation, control bytes and out-of-range fields fail.
	std::vector<std::uint8_t> longer = record;
	longer.push_back( 0 );
	CHECK( !ParseMarkerRecord( longer.data(), static_cast<int>( longer.size() ), parsed, frame, KINDS, CLIENTS, INSTANCES ) );
	CHECK( !ParseMarkerRecord( record.data(), static_cast<int>( record.size() ) - 1, parsed, frame, KINDS, CLIENTS, INSTANCES ) );
	std::vector<std::uint8_t> control = record;
	control.back() = '\n';
	CHECK( !ParseMarkerRecord( control.data(), static_cast<int>( control.size() ), parsed, frame, KINDS, CLIENTS, INSTANCES ) );
	for ( int kind : { 0, KINDS } ) {
		std::vector<std::uint8_t> bad;
		AppendU32( bad, 1 );
		AppendMarkerBody( bad, MakeMarker( 5, kind, 0, "" ) );
		CHECK( !ParseMarkerRecord( bad.data(), static_cast<int>( bad.size() ), parsed, frame, KINDS, CLIENTS, INSTANCES ) );
	}
	for ( int client : { -2, CLIENTS } ) {
		std::vector<std::uint8_t> bad;
		AppendU32( bad, 1 );
		AppendMarkerBody( bad, MakeMarker( 5, 5, client, "" ) );
		CHECK( !ParseMarkerRecord( bad.data(), static_cast<int>( bad.size() ), parsed, frame, KINDS, CLIENTS, INSTANCES ) );
	}
	// The full text length round-trips; one more byte is refused.
	char full[ MARKER_TEXT_BYTES + 1 ];
	for ( int i = 0; i < MARKER_TEXT_BYTES; i++ ) { full[i] = 'a' + i % 26; }
	full[ MARKER_TEXT_BYTES ] = '\0';
	std::vector<std::uint8_t> maximum;
	AppendU32( maximum, 1 );
	AppendMarkerBody( maximum, MakeMarker( 5, 8, -1, full ) );
	CHECK( ParseMarkerRecord( maximum.data(), static_cast<int>( maximum.size() ), parsed, frame, KINDS, CLIENTS, INSTANCES ) );
	maximum[ 4 + 16 ] = static_cast<std::uint8_t>( MARKER_TEXT_BYTES + 1 );
	maximum.push_back( 'z' );
	CHECK( !ParseMarkerRecord( maximum.data(), static_cast<int>( maximum.size() ), parsed, frame, KINDS, CLIENTS, INSTANCES ) );
}

static void TestMarkerTable() {
	std::vector<Marker> markers = { MakeMarker( 100, 1, -1, "" ), MakeMarker( 100, 5, 3, "" ), MakeMarker( 900, 8, -1, "note" ) };
	std::vector<std::uint8_t> table;
	AppendU32( table, static_cast<std::uint32_t>( markers.size() ) );
	for ( const Marker &marker : markers ) { AppendMarkerBody( table, marker ); }
	std::vector<Marker> parsed;
	CHECK( ParseMarkerTable( table.data(), static_cast<int>( table.size() ), parsed, KINDS, CLIENTS, INSTANCES ) );
	CHECK( parsed.size() == 3 && parsed[2].gameTime == 900 && std::string( parsed[2].text ) == "note" );

	// Out of order, a wrong count and trailing bytes fail and leave nothing.
	std::vector<std::uint8_t> reversed;
	AppendU32( reversed, 2 );
	AppendMarkerBody( reversed, markers[2] );
	AppendMarkerBody( reversed, markers[0] );
	CHECK( !ParseMarkerTable( reversed.data(), static_cast<int>( reversed.size() ), parsed, KINDS, CLIENTS, INSTANCES ) && parsed.empty() );
	std::vector<std::uint8_t> miscounted = table;
	miscounted[0] = 4;
	CHECK( !ParseMarkerTable( miscounted.data(), static_cast<int>( miscounted.size() ), parsed, KINDS, CLIENTS, INSTANCES ) );
	std::vector<std::uint8_t> trailing = table;
	trailing.push_back( 1 );
	CHECK( !ParseMarkerTable( trailing.data(), static_cast<int>( trailing.size() ), parsed, KINDS, CLIENTS, INSTANCES ) );
	std::vector<std::uint8_t> oversized;
	AppendU32( oversized, MAX_MARKERS + 1 );
	CHECK( !ParseMarkerTable( oversized.data(), static_cast<int>( oversized.size() ), parsed, KINDS, CLIENTS, INSTANCES ) );
}

static void TestKeyframes() {
	std::vector<Keyframe> keyframes;
	for ( int i = 1; i <= 4; i++ ) {
		Keyframe keyframe;
		keyframe.gameTime = 1000 + i * 10000;
		keyframe.fileOffset = 64 + i * 5000;
		keyframe.sequence = i * 200;
		keyframes.push_back( keyframe );
	}
	std::vector<std::uint8_t> table;
	AppendKeyframeTable( table, keyframes );
	std::vector<Keyframe> parsed;
	CHECK( ParseKeyframeTable( table.data(), static_cast<int>( table.size() ), 64, 30000, parsed ) && parsed.size() == 4 );
	// Every entry must lie in the live stream before the directory, ascending.
	CHECK( !ParseKeyframeTable( table.data(), static_cast<int>( table.size() ), 64, 20064, parsed ) && parsed.empty() );
	CHECK( !ParseKeyframeTable( table.data(), static_cast<int>( table.size() ), 6000, 30000, parsed ) );
	for ( int field = 0; field < 3; field++ ) {
		std::vector<Keyframe> broken = keyframes;
		int *value = field == 0 ? &broken[2].gameTime : field == 1 ? &broken[2].fileOffset : &broken[2].sequence;
		*value = field == 0 ? broken[1].gameTime : field == 1 ? broken[1].fileOffset : broken[1].sequence;
		std::vector<std::uint8_t> bytes;
		AppendKeyframeTable( bytes, broken );
		CHECK( !ParseKeyframeTable( bytes.data(), static_cast<int>( bytes.size() ), 64, 30000, parsed ) );
	}
	std::vector<std::uint8_t> trailing = table;
	trailing.push_back( 0 );
	CHECK( !ParseKeyframeTable( trailing.data(), static_cast<int>( trailing.size() ), 64, 30000, parsed ) );

	CHECK( KeyframeAtOrBefore( keyframes, 10999 ) == -1 );
	CHECK( KeyframeAtOrBefore( keyframes, 11000 ) == 0 );
	CHECK( KeyframeAtOrBefore( keyframes, 30999 ) == 1 );
	CHECK( KeyframeAtOrBefore( keyframes, 99999 ) == 3 );
	CHECK( KeyframeAtOrBefore( std::vector<Keyframe>(), 5 ) == -1 );

	// The keyframe record: five counts, then three non-empty blocks, exactly.
	std::vector<std::uint8_t> record;
	for ( int value : { 600, 9600, 40, 12, 60 } ) { AppendU32( record, static_cast<std::uint32_t>( value ) ); }
	for ( int bytes : { 3, 5, 2 } ) {
		AppendU32( record, static_cast<std::uint32_t>( bytes ) );
		record.insert( record.end(), static_cast<size_t>( bytes ), 0xab );
	}
	KeyframeHeader header;
	CHECK( ParseKeyframeHeader( record.data(), static_cast<int>( record.size() ), header ) );
	CHECK( header.sequence == 40 && header.reliableCount == 12 && header.recordCount == 60 &&
		header.mapStateOffset == 24 && header.mapStateBytes == 3 &&
		header.networkInfoOffset == 31 && header.networkInfoBytes == 5 &&
		header.baselineOffset == 40 && header.baselineBytes == 2 );
	std::vector<std::uint8_t> trailingRecord = record;
	trailingRecord.push_back( 0 );
	CHECK( !ParseKeyframeHeader( trailingRecord.data(), static_cast<int>( trailingRecord.size() ), header ) );
	CHECK( !ParseKeyframeHeader( record.data(), static_cast<int>( record.size() ) - 1, header ) );
	std::vector<std::uint8_t> uncounted = record;
	uncounted[16] = 10;	// fewer records than snapshots plus reliables
	CHECK( !ParseKeyframeHeader( uncounted.data(), static_cast<int>( uncounted.size() ), header ) );
	std::vector<std::uint8_t> emptyBlock = record;
	emptyBlock[20] = 0;
	CHECK( !ParseKeyframeHeader( emptyBlock.data(), static_cast<int>( emptyBlock.size() ), header ) );
}

static std::vector<std::uint8_t> Baseline( int sequence, std::vector<std::pair<int, int>> entries, int pvsWords ) {
	std::vector<std::uint8_t> bytes;
	AppendU32( bytes, BASELINE_VERSION );
	AppendU32( bytes, static_cast<std::uint32_t>( sequence ) );
	bytes.push_back( 1 );
	AppendU32( bytes, static_cast<std::uint32_t>( entries.size() ) );
	for ( const auto &entry : entries ) {
		AppendU16( bytes, static_cast<std::uint16_t>( entry.first ) );
		AppendU16( bytes, static_cast<std::uint16_t>( entry.second ) );
		bytes.insert( bytes.end(), static_cast<size_t>( entry.second ), 0x5a );
	}
	for ( int i = 0; i < pvsWords; i++ ) { AppendU32( bytes, 0xffffffffu ); }
	return bytes;
}

static void TestBaseline() {
	const int maxEntities = 4096, maxState = 512, pvsWords = 128;
	const auto valid = [&]( const std::vector<std::uint8_t> &bytes, int sequence ) {
		return ValidateBaseline( bytes.data(), static_cast<int>( bytes.size() ), sequence, maxEntities, maxState, pvsWords );
	};
	CHECK( valid( Baseline( 7, { { 0, 40 }, { 33, 512 }, { 4095, 12 } }, pvsWords ), 7 ) );
	CHECK( valid( Baseline( 7, {}, pvsWords ), 7 ) );
	CHECK( !valid( Baseline( 7, { { 0, 40 } }, pvsWords ), 8 ) );			// another snapshot's baseline
	CHECK( !valid( Baseline( 7, { { 33, 4 }, { 12, 4 } }, pvsWords ), 7 ) );	// descending entities
	CHECK( !valid( Baseline( 7, { { 33, 4 }, { 33, 4 } }, pvsWords ), 7 ) );	// duplicate entity
	CHECK( !valid( Baseline( 7, { { 4096, 4 } }, pvsWords ), 7 ) );			// past the entity range
	CHECK( !valid( Baseline( 7, { { 3, 513 } }, pvsWords ), 7 ) );			// oversized state
	CHECK( !valid( Baseline( 7, { { 3, 0 } }, pvsWords ), 7 ) );				// empty state
	CHECK( !valid( Baseline( 7, { { 3, 4 } }, pvsWords - 1 ), 7 ) );			// truncated PVS
	CHECK( !valid( Baseline( 7, { { 3, 4 } }, pvsWords + 1 ), 7 ) );			// trailing bytes
	std::vector<std::uint8_t> version = Baseline( 7, {}, pvsWords );
	version[0] = 2;
	CHECK( !valid( version, 7 ) );
}

static void TestMarkerStepping() {
	std::vector<Marker> markers = {
		MakeMarker( 10000, 5, 1, "" ), MakeMarker( 20000, 1, -1, "" ), MakeMarker( 30000, 5, 2, "" ), MakeMarker( 40000, 8, -1, "x" )
	};
	const int lead = 2000;
	// From the start, next lands on the first marker; stepping walks the list.
	int index = FindMarker( markers, 0, 1, 0, lead, -1 );
	CHECK( index == 0 );
	index = FindMarker( markers, 10000 - lead, 1, 0, lead, index );
	CHECK( index == 1 );
	index = FindMarker( markers, 20000 - lead, 1, 0, lead, index );
	CHECK( index == 2 );
	index = FindMarker( markers, 30000 - lead, -1, 0, lead, index );
	CHECK( index == 1 );
	// A kind filter skips other markers in both directions.
	CHECK( FindMarker( markers, 0, 1, 5, lead, -1 ) == 0 );
	CHECK( FindMarker( markers, 10000 - lead, 1, 5, lead, 0 ) == 2 );
	CHECK( FindMarker( markers, 50000, -1, 8, lead, -1 ) == 3 );
	CHECK( FindMarker( markers, 50000, 1, 0, lead, -1 ) == -1 );
	// Away from the last selection, the search uses the playback time instead.
	CHECK( FindMarker( markers, 25000, 1, 0, lead, 0 ) == 2 );
	CHECK( FindMarker( markers, 25000, -1, 0, lead, 3 ) == 1 );
	CHECK( FindMarker( std::vector<Marker>(), 0, 1, 0, lead, -1 ) == -1 );
}

static void TestMarkerDiff() {
	mpMVDMarkerState previous;
	previous.phase = 1;
	previous.flags = true;
	previous.flagCarrier[0] = -1;
	previous.present[3] = previous.present[4] = true;
	mpMVDMarkerState current = previous;
	mpMVDMarkerEvent events[16];
	const std::uint32_t tracked = 0x0fu;
	CHECK( MPMVDMarkerDiff( previous, current, true, tracked, events, 16 ) == 0 );

	current.phase = 2;
	current.round = 3;
	current.pauseState = 2;
	current.pauseKind = 1;
	current.pauseOwnerSide = 1;
	current.teamScore[1] = 4;
	current.flagState[0] = 2;
	current.flagCarrier[0] = 3;
	current.powerups[3] = 0x1u | 0x10u;	// a tracked and an untracked powerup
	current.powerups[5] = 0x1u;			// a player who just joined
	current.present[5] = true;
	const int count = MPMVDMarkerDiff( previous, current, true, tracked, events, 16 );
	CHECK( count == 6 );
	CHECK( events[0].kind == MP_MVD_MARKER_PHASE && events[0].value0 == 1 && events[0].value1 == 2 );
	CHECK( events[1].kind == MP_MVD_MARKER_ROUND && events[1].value1 == 3 );
	CHECK( events[2].kind == MP_MVD_MARKER_PAUSE && events[2].value0 == 2 && events[2].value1 == ( 1 | ( 2 << 8 ) ) );
	CHECK( events[3].kind == MP_MVD_MARKER_SCORE && events[3].value0 == 1 && events[3].value1 == 4 );
	CHECK( events[4].kind == MP_MVD_MARKER_FLAG && events[4].client == 3 && events[4].value0 == 0 && events[4].value1 == 2 );
	CHECK( events[5].kind == MP_MVD_MARKER_POWERUP && events[5].client == 3 && events[5].value0 == 0 );
	// Free-for-all scores are kills, not team score markers; the cap holds.
	CHECK( MPMVDMarkerDiff( previous, current, false, tracked, events, 16 ) == 5 );
	CHECK( MPMVDMarkerDiff( previous, current, true, tracked, events, 2 ) == 2 );
	// A returned flag names the carrier who had it.
	mpMVDMarkerState returned = current;
	returned.flagState[0] = 0;
	returned.flagCarrier[0] = -1;
	CHECK( MPMVDMarkerDiff( current, returned, true, tracked, events, 16 ) == 1 && events[0].client == 3 );
}

static void TestAutoTrack() {
	mpAutoTrackPlayer players[ MP_AUTOTRACK_CLIENTS ];
	for ( int i = 0; i < 4; i++ ) {
		players[i].eligible = true;
		players[i].alive = true;
		players[i].team = i % 2;
		players[i].score = 5;
	}
	mpDemoAutoTrack track;
	// Off keeps the camera; with nobody eligible on the team nothing is chosen.
	CHECK( track.Choose( MP_AUTOTRACK_OFF, -1, 2, players, 0 ) == 2 );
	CHECK( track.Choose( MP_AUTOTRACK_ACTION, 5, 2, players, 0 ) == -1 );

	// Action: a flag carrier wins, but only after the dwell and with a clear lead.
	CHECK( track.Choose( MP_AUTOTRACK_ACTION, -1, -1, players, 1000 ) == 0 );
	players[3].flag = true;
	CHECK( track.Choose( MP_AUTOTRACK_ACTION, -1, 0, players, 2000 ) == 0 );
	CHECK( track.Choose( MP_AUTOTRACK_ACTION, -1, 0, players, 4000 ) == 3 );
	players[3].flag = false;
	players[1].powerups = 1;	// 300 more, but the camera just moved
	CHECK( track.Choose( MP_AUTOTRACK_ACTION, -1, 3, players, 5000 ) == 3 );
	CHECK( track.Choose( MP_AUTOTRACK_ACTION, -1, 3, players, 7000 ) == 1 );
	players[1].powerups = 0;
	// A recent kill earns the camera, and counts for ten seconds.
	track.NoteKill( 2, 0, 1, 8000 );
	CHECK( track.Choose( MP_AUTOTRACK_ACTION, -1, 1, players, 10500 ) == 2 );
	CHECK( track.RecentKills( 2, 18000 ) == 1 && track.RecentKills( 2, 18001 ) == 0 );
	// A suicide is no kill.
	track.NoteKill( 3, 3, -1, 19000 );
	CHECK( track.RecentKills( 3, 19000 ) == 0 );
	// A team filter keeps the camera on that team.
	CHECK( track.Choose( MP_AUTOTRACK_ACTION, 1, 2, players, 20000 ) % 2 == 1 );

	// Killer: the followed player's killer is followed after the beat.
	mpDemoAutoTrack killer;
	CHECK( killer.Choose( MP_AUTOTRACK_KILLER, -1, 1, players, 0 ) == 1 );
	killer.NoteKill( 2, 1, 1, 100 );
	CHECK( killer.Choose( MP_AUTOTRACK_KILLER, -1, 1, players, 100 + MP_AUTOTRACK_KILLER_DELAY_MS - 1 ) == 1 );
	CHECK( killer.Choose( MP_AUTOTRACK_KILLER, -1, 1, players, 100 + MP_AUTOTRACK_KILLER_DELAY_MS ) == 2 );
	// A kill of someone else, a suicide or a dead killer moves nothing.
	killer.NoteKill( 3, 0, 2, 5000 );
	killer.NoteKill( 2, 2, 2, 5000 );
	CHECK( killer.Choose( MP_AUTOTRACK_KILLER, -1, 2, players, 9000 ) == 2 );
	killer.NoteKill( 0, 2, 2, 10000 );
	players[0].alive = false;
	CHECK( killer.Choose( MP_AUTOTRACK_KILLER, -1, 2, players, 12000 ) == 2 );
	players[0].alive = true;
	// A kill noted while action mode ran is not followed later in killer mode.
	mpDemoAutoTrack switched;
	switched.Choose( MP_AUTOTRACK_KILLER, -1, 1, players, 0 );
	switched.NoteKill( 3, 1, 1, 1000 );
	CHECK( switched.Choose( MP_AUTOTRACK_ACTION, -1, 3, players, 1000 ) == 3 );
	CHECK( switched.Choose( MP_AUTOTRACK_KILLER, -1, 1, players, 2600 ) == 1 );
	// Nor is one that waited past the dwell for killer mode to run again.
	switched.NoteKill( 3, 1, 1, 7000 );
	CHECK( switched.Choose( MP_AUTOTRACK_KILLER, -1, 1, players, 7000 + MP_AUTOTRACK_KILLER_DELAY_MS + MP_AUTOTRACK_DWELL_MS + 1 ) == 1 );
	// A target who left is replaced at once.
	players[2].eligible = false;
	CHECK( killer.Choose( MP_AUTOTRACK_KILLER, -1, 2, players, 12100 ) != 2 );
}

int main() {
	TestMarkerRecords();
	TestMarkerTable();
	TestKeyframes();
	TestBaseline();
	TestMarkerStepping();
	TestMarkerDiff();
	TestAutoTrack();
	if ( failures != 0 ) {
		std::fprintf( stderr, "mvd navigation fixture: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "mvd navigation fixture: ok\n" );
	return 0;
}
