// Executes the production bookkeeping behind a multiplayer client's predicted hit-scan
// shots: the per-shot spread seed the server and the shooting client share, and the
// notes that let the server's copy of a predicted shot add only the confirmed impacts.
// tools/tests/mp_predicted_hitscan_state.py splices the functions in verbatim from
// src/mpgame/Game_local.cpp and the member layout from src/mpgame/Game_local.h.

#include <cstdio>
#include <cstring>
#include <set>

template<class T> static T Max( T a, T b ) { return a > b ? a : b; }

static int fakeMsec = 1000;
static int Sys_Milliseconds( void ) { return fakeMsec; }

// idlib/math/Random.h: the generator the spread is drawn from (unsigned here only so a
// sanitizer build does not flag the wrap the engine's int arithmetic relies on).
class idRandom {
public:
	enum { MAX_RAND = 0x7fff };
	explicit idRandom( int s ) : seed( static_cast<unsigned int>( s ) ) {}
	int RandomInt( void ) { seed = 69069u * seed + 1u; return static_cast<int>( seed & MAX_RAND ); }
	float RandomFloat( void ) { return RandomInt() / static_cast<float>( MAX_RAND + 1 ); }
private:
	unsigned int seed;
};

class idGameLocal {
public:
	// PRODUCTION_MEMBERS
	void					NotePredictedHitscan( int hitscanDefIndex );
	bool					ConsumePredictedHitscan( int hitscanDefIndex );
	static int				HitscanSpreadSeed( int shooterEntityNumber, int shotTime );
};

// PRODUCTION_FUNCTIONS

static int failures = 0;

static void Check( bool condition, const char *what ) {
	if ( !condition ) {
		std::printf( "FAIL: %s\n", what );
		failures++;
	}
}

static void Reset( idGameLocal &game ) {
	memset( game.mpPredictedHitscans, 0, sizeof( game.mpPredictedHitscans ) );
	fakeMsec = 1000;
}

int main( void ) {
	idGameLocal game;
	const int railgun = 1749;
	const int bullet = 1815;

	// A copy with nothing waiting draws in full; a predicted shot's copy is consumed once.
	Reset( game );
	Check( !game.ConsumePredictedHitscan( railgun ), "a copy with no prediction finds a note" );
	game.NotePredictedHitscan( railgun );
	Check( game.ConsumePredictedHitscan( railgun ), "a predicted shot's copy finds its note" );
	Check( !game.ConsumePredictedHitscan( railgun ), "one note serves two copies" );

	// Notes are kept per hit-scan.
	Reset( game );
	game.NotePredictedHitscan( railgun );
	Check( !game.ConsumePredictedHitscan( bullet ), "a copy of another hit-scan takes the note" );
	Check( game.ConsumePredictedHitscan( railgun ), "a copy of another hit-scan consumed the note" );

	// Copies arrive in firing order, so a copy takes the oldest note: after it, the newer
	// note is the one still waiting once the older would have expired.
	Reset( game );
	game.NotePredictedHitscan( railgun );
	fakeMsec = 1010;
	game.NotePredictedHitscan( railgun );
	Check( game.ConsumePredictedHitscan( railgun ), "the first copy finds a note" );
	fakeMsec = 2005;
	Check( game.ConsumePredictedHitscan( railgun ), "the first copy took the newer note" );

	// A note waits one second, so a shot the server never fired cannot swallow a later copy.
	Reset( game );
	game.NotePredictedHitscan( railgun );
	fakeMsec = 2000;
	Check( game.ConsumePredictedHitscan( railgun ), "a note expired before its second was up" );
	Reset( game );
	game.NotePredictedHitscan( railgun );
	fakeMsec = 2001;
	Check( !game.ConsumePredictedHitscan( railgun ), "a note outlived its second" );
	fakeMsec = 2002;
	game.NotePredictedHitscan( bullet );
	Check( game.ConsumePredictedHitscan( bullet ), "an expired note's slot was not reused" );

	// A full ring gives up its oldest note, not its newest.
	Reset( game );
	const int capacity = idGameLocal::MP_PREDICTED_HITSCANS;
	for ( int i = 0; i <= capacity; i++ ) {
		fakeMsec = 1000 + i;
		game.NotePredictedHitscan( railgun );
	}
	// Bounded, so a copy that leaves its note behind fails instead of spinning.
	int consumed = 0;
	while ( consumed <= 2 * capacity && game.ConsumePredictedHitscan( railgun ) ) {
		consumed++;
	}
	Check( consumed == capacity, "a full ring did not keep one note per slot" );
	Reset( game );
	for ( int i = 0; i <= capacity; i++ ) {
		fakeMsec = 1000 + i;
		game.NotePredictedHitscan( railgun );
	}
	// The overflow replaced the oldest note, so the two newest (1000 + capacity - 1 and
	// 1000 + capacity) both wait until the first of them expires.
	fakeMsec = 1000 + capacity - 1 + 1000;
	const bool firstNewest = game.ConsumePredictedHitscan( railgun );
	const bool secondNewest = game.ConsumePredictedHitscan( railgun );
	Check( firstNewest && secondNewest, "a full ring dropped one of its newest notes" );

	// The seed is a function of the shooter and the shot's game time alone, so the server
	// and the shooting client derive the same pellets; neighbours must not share a seed.
	Check( idGameLocal::HitscanSpreadSeed( 4, 32016 ) == idGameLocal::HitscanSpreadSeed( 4, 32016 ), "the seed is not repeatable" );
	Check( idGameLocal::HitscanSpreadSeed( 4, 32016 ) >= 0, "the seed is negative" );
	std::set<int> seeds;
	for ( int slot = 0; slot < 16; slot++ ) {
		for ( int frame = 0; frame < 2048; frame++ ) {
			seeds.insert( idGameLocal::HitscanSpreadSeed( slot, frame * 16 ) );
		}
	}
	Check( seeds.size() > 16 * 2048 - 4, "neighbouring shooters or frames share seeds" );

	// idRandom is a linear congruence: seeds that step evenly give first draws that step
	// evenly, and consecutive shots would spread in step.  The mixed seed must not.
	int rising = 0;
	const int frames = 4096;
	float previous = idRandom( idGameLocal::HitscanSpreadSeed( 4, 0 ) ).RandomFloat();
	for ( int frame = 1; frame <= frames; frame++ ) {
		const float first = idRandom( idGameLocal::HitscanSpreadSeed( 4, frame * 16 ) ).RandomFloat();
		rising += first > previous ? 1 : 0;
		previous = first;
	}
	const float risingShare = static_cast<float>( rising ) / frames;
	Check( risingShare > 0.45f && risingShare < 0.55f, "consecutive shots' first pellets move in step" );

	if ( failures ) {
		std::printf( "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "mp predicted hitscan fixture: ok\n" );
	return 0;
}
