// openQ4 multiplayer air readout. SDK EULA: see src/game/LICENSE.
#ifndef OPENQ4_PLAYER_AIR_READOUT_H
#define OPENQ4_PLAYER_AIR_READOUT_H
#include <cstdint>

inline int MPAirPercent( int tics, int capacity ) {
	if ( capacity <= 0 ) { return 100; }
	if ( tics <= 0 ) { return 0; }
	if ( tics >= capacity ) { return 100; }
	return static_cast<int>( static_cast<std::int64_t>( tics ) * 100 / capacity );
}

// The enclosing entity event supplies the spawn identity. Init resets this
// optional presentation state on respawn, map reset and backward demo seek.
struct mpAirReadoutState {
	int percent = -1;
	int serverTime = -1;
	void Reset( int earliest = -1 ) { percent = -1; serverTime = earliest; }
	bool Apply( int stamp, int value ) {
		if ( stamp < 0 || value < 0 || value > 100 || stamp < serverTime ||
			( stamp == serverTime && percent >= 0 && percent != value ) ) { return false; }
		percent = value;
		serverTime = stamp;
		return true;
	}
};

struct mpAirPublishClock {
	int lastPercent = -1;
	std::int64_t nextSend = 0;
	std::int64_t refresh = 0;
	void Reset() { lastPercent = -1; nextSend = refresh = 0; }
	bool Admit( int now, int value ) {
		if ( now < 0 || value < 0 || value > 100 || now < nextSend ||
			( value == lastPercent && now < refresh ) ) { return false; }
		lastPercent = value;
		nextSend = static_cast<std::int64_t>( now ) + 100;
		refresh = static_cast<std::int64_t>( now ) + 1000;
		return true;
	}
};
#endif
