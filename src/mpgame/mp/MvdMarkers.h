// openQ4 multi-view demo markers. SDK EULA: see src/mpgame/LICENSE.
#ifndef OPENQ4_MP_MVD_MARKERS_H
#define OPENQ4_MP_MVD_MARKERS_H
#include <cstdint>

// The server publishes structured markers while it records an MVD by comparing
// the match state it observed last frame with this frame's. These kinds mirror
// serverMVDMarkerKind_t, which MultiplayerGame.cpp asserts.
enum {
	MP_MVD_MARKER_PHASE = 1,
	MP_MVD_MARKER_ROUND,
	MP_MVD_MARKER_PAUSE,
	MP_MVD_MARKER_SCORE,
	MP_MVD_MARKER_KILL,
	MP_MVD_MARKER_FLAG,
	MP_MVD_MARKER_POWERUP
};

static const int MP_MVD_MARKER_CLIENTS = 32;
static const int MP_MVD_MARKER_TEAMS = 2;
static const int MP_MVD_MARKER_FLAGS = 3;	// marine, strogg, One Flag's neutral flag

struct mpMVDMarkerState {
	int phase = 0;
	int round = 0;
	int pauseState = 0;
	int pauseKind = 0;
	int pauseOwnerSide = -1;
	int teamScore[ MP_MVD_MARKER_TEAMS ] = {};
	bool flags = false;
	int flagState[ MP_MVD_MARKER_FLAGS ] = {};
	int flagCarrier[ MP_MVD_MARKER_FLAGS ] = { -1, -1, -1 };
	bool present[ MP_MVD_MARKER_CLIENTS ] = {};
	std::uint32_t powerups[ MP_MVD_MARKER_CLIENTS ] = {};
};

struct mpMVDMarkerEvent {
	int kind = 0;
	int client = -1;
	int value0 = 0;
	int value1 = 0;
};

// The changes from previous to current, in a stable order, at most maxEvents.
// Team scores are reported only for team games, and a powerup only when a
// present player gains one of the tracked bits; a player who leaves or joins
// gains nothing.
inline int MPMVDMarkerDiff( const mpMVDMarkerState &previous, const mpMVDMarkerState &current,
		bool teamGame, std::uint32_t trackedPowerups, mpMVDMarkerEvent *events, int maxEvents ) {
	int count = 0;
	const auto add = [&]( int kind, int client, int value0, int value1 ) {
		if ( count < maxEvents ) {
			events[ count ].kind = kind;
			events[ count ].client = client;
			events[ count ].value0 = value0;
			events[ count ].value1 = value1;
			count++;
		}
	};
	if ( current.phase != previous.phase ) {
		add( MP_MVD_MARKER_PHASE, -1, previous.phase, current.phase );
	}
	if ( current.round != previous.round ) {
		add( MP_MVD_MARKER_ROUND, -1, previous.round, current.round );
	}
	if ( current.pauseState != previous.pauseState ) {
		add( MP_MVD_MARKER_PAUSE, -1, current.pauseState,
			( current.pauseKind & 0xff ) | ( ( current.pauseOwnerSide + 1 ) << 8 ) );
	}
	if ( teamGame ) {
		for ( int team = 0; team < MP_MVD_MARKER_TEAMS; team++ ) {
			if ( current.teamScore[ team ] != previous.teamScore[ team ] ) {
				add( MP_MVD_MARKER_SCORE, -1, team, current.teamScore[ team ] );
			}
		}
	}
	if ( current.flags && previous.flags ) {
		for ( int flag = 0; flag < MP_MVD_MARKER_FLAGS; flag++ ) {
			if ( current.flagState[ flag ] != previous.flagState[ flag ] ) {
				const int carrier = current.flagCarrier[ flag ] >= 0 ? current.flagCarrier[ flag ] : previous.flagCarrier[ flag ];
				add( MP_MVD_MARKER_FLAG, carrier, flag, current.flagState[ flag ] );
			}
		}
	}
	for ( int client = 0; client < MP_MVD_MARKER_CLIENTS; client++ ) {
		if ( !current.present[ client ] || !previous.present[ client ] ) {
			continue;
		}
		const std::uint32_t gained = current.powerups[ client ] & ~previous.powerups[ client ] & trackedPowerups;
		for ( int powerup = 0; powerup < 32; powerup++ ) {
			if ( gained & ( 1u << powerup ) ) {
				add( MP_MVD_MARKER_POWERUP, client, powerup, 0 );
			}
		}
	}
	return count;
}
#endif
