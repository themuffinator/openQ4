// openQ4 live match event stream. SDK EULA: see src/game/LICENSE.
#ifndef OPENQ4_MP_LIVE_EVENTS_H
#define OPENQ4_MP_LIVE_EVENTS_H
#include <cstdint>
#include <cstring>

// The typed events an overlay tool reads while a match is played: one JSON
// object per line, each with the schema version, a sequence number that never
// repeats within a stream, the game time and a type. The first line of a
// stream describes the match and everyone in it, so a reader that starts late
// rebuilds the present from the top of the file. Everything here is state a
// scoreboard or kill feed already shows; the stream carries no positions,
// health, armour or item timing, so it is the same on a server and on a
// spectating client. docs/dev/mp-live-event-stream.md is the reference.
//
// This header has no engine dependency: tools/tests/native/LiveEventsFixture.cpp
// compiles it alone. LiveEventStream.cpp asserts that the tables below match
// the game's enums.
static const int MP_LIVE_EVENT_SCHEMA = 1;
static const int MP_LIVE_EVENT_CLIENTS = 32;
static const int MP_LIVE_EVENT_TEAMS = 2;
static const int MP_LIVE_EVENT_FLAGS = 3;	// marine, strogg, One Flag's neutral flag
static const int MP_LIVE_EVENT_NAME_BYTES = 64;
static const int MP_LIVE_EVENT_LINE_BYTES = 24576;	// a full roster of worst-case names fits
static const int MP_LIVE_EVENT_PHASES = 7;
static const int MP_LIVE_EVENT_POWERUPS = 18;

static const char *const mpLiveEventPhaseNames[ MP_LIVE_EVENT_PHASES ] = {
	"inactive", "warmup", "countdown", "live", "suddenDeath", "review", "nextGame"
};
static const int MP_LIVE_PHASE_REVIEW = 5;	// the match is over and its standings are final
static const char *const mpLiveEventTeamNames[ MP_LIVE_EVENT_TEAMS ] = { "marine", "strogg" };
static const char *const mpLiveEventFlagNames[ MP_LIVE_EVENT_FLAGS ] = { "marine", "strogg", "neutral" };
static const char *const mpLiveEventPowerupNames[ MP_LIVE_EVENT_POWERUPS ] = {
	"quadDamage", "haste", "regeneration", "invisibility", "marineFlag", "stroggFlag", "neutralFlag",
	"ammoRegen", "guard", "doubler", "scout", "moderator", "deadZone",
	"teamAmmoRegen", "teamHealthRegen", "teamDamage", "adrenaline", "armorRegen"
};

// Flag states as the game numbers them (flagState_t).
enum {
	MP_LIVE_FLAG_AT_BASE = 0,
	MP_LIVE_FLAG_DROPPED,
	MP_LIVE_FLAG_TAKEN,
	MP_LIVE_FLAG_TAKEN_STROGG,
	MP_LIVE_FLAG_TAKEN_MARINE
};

struct mpLiveEventPlayer {
	bool present = false;
	bool spectator = false;
	int bot = -1;			// 1 a bot, 0 a person, -1 unknown (a client cannot tell)
	int team = -1;			// -1 outside team games
	int score = 0;
	std::uint32_t powerups = 0;
	char name[ MP_LIVE_EVENT_NAME_BYTES ] = {};	// colour escapes already removed
};

struct mpLiveEventState {
	int phase = 0;
	bool teamGame = false;
	int teamScore[ MP_LIVE_EVENT_TEAMS ] = {};
	bool flags = false;
	int flagState[ MP_LIVE_EVENT_FLAGS ] = {};
	int flagCarrier[ MP_LIVE_EVENT_FLAGS ] = { -1, -1, -1 };
	mpLiveEventPlayer players[ MP_LIVE_EVENT_CLIENTS ];
};

// One line of the stream. Text that does not fit marks the line overflowed and
// End() reports it, so a truncated object is never written.
class mpLiveEventLine {
public:
	mpLiveEventLine() { Reset(); }

	void Begin( int sequence, int timeMsec, const char *type ) {
		Reset();
		Raw( "{" );
		Int( "v", MP_LIVE_EVENT_SCHEMA );
		Int( "seq", sequence );
		Int( "t", timeMsec );
		String( "type", type );
	}

	void Int( const char *key, int value ) {
		// By hand: inside the game module idlib redefines snprintf and strcmp.
		char number[ 16 ];
		char *end = number + sizeof( number ) - 1;
		char *digit = end;
		*digit = '\0';
		unsigned magnitude = value < 0 ? 0u - static_cast<unsigned>( value ) : static_cast<unsigned>( value );
		do {
			*--digit = static_cast<char>( '0' + magnitude % 10u );
			magnitude /= 10u;
		} while ( magnitude != 0u );
		if ( value < 0 ) {
			*--digit = '-';
		}
		Key( key );
		Raw( digit );
	}

	void Bool( const char *key, bool value ) {
		Key( key );
		Raw( value ? "true" : "false" );
	}

	void Null( const char *key ) {
		Key( key );
		Raw( "null" );
	}

	void String( const char *key, const char *value ) {
		Key( key );
		Quoted( value );
	}

	// A table entry by index, or null when the game reports a value the table
	// does not know: a reader sees null, never an out-of-range read.
	void Name( const char *key, const char *const *table, int count, int index ) {
		if ( index >= 0 && index < count ) {
			String( key, table[ index ] );
		} else {
			Null( key );
		}
	}

	void BeginArray( const char *key ) {
		Key( key );
		Raw( "[" );
		needComma = false;
	}

	void EndArray() {
		Raw( "]" );
		needComma = true;
	}

	void BeginObject() {
		if ( needComma ) {
			Raw( "," );
		}
		Raw( "{" );
		needComma = false;
	}

	void EndObject() {
		Raw( "}" );
		needComma = true;
	}

	bool End() {
		Raw( "}\n" );
		return !overflowed;
	}

	const char *Text() const { return text; }
	int Length() const { return length; }
	bool Overflowed() const { return overflowed; }

private:
	char text[ MP_LIVE_EVENT_LINE_BYTES ];
	int length;
	bool needComma;
	bool overflowed;

	void Reset() {
		length = 0;
		text[ 0 ] = '\0';
		needComma = false;
		overflowed = false;
	}

	void Raw( const char *value ) {
		const int bytes = static_cast<int>( std::strlen( value ) );
		if ( overflowed || bytes >= MP_LIVE_EVENT_LINE_BYTES - length ) {
			overflowed = true;
			return;
		}
		std::memcpy( text + length, value, bytes + 1 );
		length += bytes;
	}

	void Key( const char *key ) {
		if ( needComma ) {
			Raw( "," );
		}
		Quoted( key );
		Raw( ":" );
		needComma = true;
	}

	// The length of the well-formed UTF-8 sequence at s, or 0. Overlong forms,
	// surrogates and values past U+10FFFF are not well formed.
	static int Utf8Length( const unsigned char *s ) {
		if ( s[0] < 0x80 ) {
			return 1;
		}
		if ( s[0] >= 0xc2 && s[0] <= 0xdf ) {
			return ( s[1] & 0xc0 ) == 0x80 ? 2 : 0;
		}
		if ( s[0] >= 0xe0 && s[0] <= 0xef ) {
			if ( ( s[1] & 0xc0 ) != 0x80 || ( s[2] & 0xc0 ) != 0x80 ) {
				return 0;
			}
			if ( ( s[0] == 0xe0 && s[1] < 0xa0 ) || ( s[0] == 0xed && s[1] > 0x9f ) ) {
				return 0;
			}
			return 3;
		}
		if ( s[0] >= 0xf0 && s[0] <= 0xf4 ) {
			if ( ( s[1] & 0xc0 ) != 0x80 || ( s[2] & 0xc0 ) != 0x80 || ( s[3] & 0xc0 ) != 0x80 ) {
				return 0;
			}
			if ( ( s[0] == 0xf0 && s[1] < 0x90 ) || ( s[0] == 0xf4 && s[1] > 0x8f ) ) {
				return 0;
			}
			return 4;
		}
		return 0;
	}

	// A JSON string. Player names are untrusted: quotes, backslashes and control
	// bytes are escaped and a byte that is not well-formed UTF-8 becomes U+FFFD,
	// so every line stays one valid JSON object on one line.
	void Quoted( const char *value ) {
		Raw( "\"" );
		const unsigned char *s = reinterpret_cast<const unsigned char *>( value != NULL ? value : "" );
		while ( *s != '\0' ) {
			char piece[ 8 ];
			if ( *s == '"' || *s == '\\' ) {
				piece[ 0 ] = '\\';
				piece[ 1 ] = static_cast<char>( *s );
				piece[ 2 ] = '\0';
				s++;
			} else if ( *s < 0x20 || *s == 0x7f ) {
				static const char hex[] = "0123456789abcdef";
				piece[ 0 ] = '\\';
				piece[ 1 ] = 'u';
				piece[ 2 ] = '0';
				piece[ 3 ] = '0';
				piece[ 4 ] = hex[ *s >> 4 ];
				piece[ 5 ] = hex[ *s & 15 ];
				piece[ 6 ] = '\0';
				s++;
			} else {
				const int bytes = Utf8Length( s );
				if ( bytes == 0 ) {
					std::memcpy( piece, "\xef\xbf\xbd", 4 );
					s++;
				} else {
					std::memcpy( piece, s, bytes );
					piece[ bytes ] = '\0';
					s += bytes;
				}
			}
			Raw( piece );
		}
		Raw( "\"" );
	}
};

inline bool MPLiveEventSameText( const char *a, const char *b ) {
	while ( *a != '\0' && *a == *b ) {
		a++;
		b++;
	}
	return *a == *b;
}

// "team": a team's name inside team games, null outside them.
inline void MPLiveEventTeam( mpLiveEventLine &line, const char *key, bool teamGame, int team ) {
	if ( teamGame ) {
		line.Name( key, mpLiveEventTeamNames, MP_LIVE_EVENT_TEAMS, team );
	} else {
		line.Null( key );
	}
}

// "slot" and "name" of a player, or nulls for the world.
inline void MPLiveEventActor( mpLiveEventLine &line, const char *slotKey, const char *nameKey,
		const mpLiveEventState &state, int slot ) {
	if ( slot >= 0 && slot < MP_LIVE_EVENT_CLIENTS && state.players[ slot ].present ) {
		line.Int( slotKey, slot );
		line.String( nameKey, state.players[ slot ].name );
	} else {
		line.Null( slotKey );
		line.Null( nameKey );
	}
}

// The body shared by the "stream" line that opens a stream and the "standings"
// line that closes a match: the phase, the scores and everyone present.
inline void MPLiveEventRoster( mpLiveEventLine &line, const mpLiveEventState &state ) {
	line.Name( "phase", mpLiveEventPhaseNames, MP_LIVE_EVENT_PHASES, state.phase );
	line.Bool( "teamGame", state.teamGame );
	if ( state.teamGame ) {
		line.BeginArray( "teams" );
		for ( int team = 0; team < MP_LIVE_EVENT_TEAMS; team++ ) {
			line.BeginObject();
			line.String( "team", mpLiveEventTeamNames[ team ] );
			line.Int( "score", state.teamScore[ team ] );
			line.EndObject();
		}
		line.EndArray();
	}
	line.BeginArray( "players" );
	for ( int slot = 0; slot < MP_LIVE_EVENT_CLIENTS; slot++ ) {
		const mpLiveEventPlayer &player = state.players[ slot ];
		if ( !player.present ) {
			continue;
		}
		line.BeginObject();
		line.Int( "slot", slot );
		line.String( "name", player.name );
		MPLiveEventTeam( line, "team", state.teamGame, player.team );
		line.Bool( "spectator", player.spectator );
		line.Int( "score", player.score );
		if ( player.bot >= 0 ) {
			line.Bool( "bot", player.bot != 0 );
		}
		line.EndObject();
	}
	line.EndArray();
}

// The line that opens a stream. `source` is "server" or "client". `utc` is the
// wall-clock time the stream began ("2026-11-22T10:15:00Z"); the live file
// leaves it out, and the match log, which keeps every stream, gives it.
inline bool MPLiveEventStreamLine( mpLiveEventLine &line, int sequence, int timeMsec, const char *source,
		const char *map, const char *gameType, const mpLiveEventState &state, const char *utc = 0 ) {
	line.Begin( sequence, timeMsec, "stream" );
	line.String( "game", "openQ4" );
	line.String( "source", source );
	line.String( "map", map );
	line.String( "gameType", gameType );
	if ( utc != 0 && utc[ 0 ] != '\0' ) {
		line.String( "utc", utc );
	}
	MPLiveEventRoster( line, state );
	return line.End();
}

// A death. `killer` is -1 for the world; `weapon` is NULL when the death has
// no weapon, and `cause` then says what it was ("world", "telefrag").
inline bool MPLiveEventKillLine( mpLiveEventLine &line, int sequence, int timeMsec, const mpLiveEventState &state,
		int killer, int victim, const char *weapon, const char *cause, bool quad ) {
	const bool suicide = killer < 0 || killer == victim;
	line.Begin( sequence, timeMsec, "kill" );
	MPLiveEventActor( line, "killer", "killerName", state, suicide ? -1 : killer );
	MPLiveEventActor( line, "victim", "victimName", state, victim );
	if ( weapon != NULL ) {
		line.String( "weapon", weapon );
	} else {
		line.Null( "weapon" );
	}
	line.String( "cause", cause );
	line.Bool( "suicide", suicide );
	const bool teamKill = !suicide && state.teamGame && killer < MP_LIVE_EVENT_CLIENTS &&
		victim >= 0 && victim < MP_LIVE_EVENT_CLIENTS &&
		state.players[ killer ].present && state.players[ victim ].present &&
		state.players[ killer ].team == state.players[ victim ].team;
	line.Bool( "teamKill", teamKill );
	line.Bool( "quad", quad );
	return line.End();
}

// Writes the changes from previous to current as lines, in a stable order:
// phase, roster, team scores, player scores, flags, powerups, and the standings
// once a match reaches review. `emit( line )` is called for each complete
// line; `sequence` advances once per line offered. Returns the lines offered.
template< class Emit >
int MPLiveEventDiff( const mpLiveEventState &previous, const mpLiveEventState &current,
		std::uint32_t trackedPowerups, int &sequence, int timeMsec, mpLiveEventLine &line, Emit &&emit ) {
	int count = 0;
	const auto finish = [&]() {
		if ( line.End() ) {
			emit( line );
		}
		sequence++;
		count++;
	};

	if ( current.phase != previous.phase ) {
		line.Begin( sequence, timeMsec, "phase" );
		line.Name( "from", mpLiveEventPhaseNames, MP_LIVE_EVENT_PHASES, previous.phase );
		line.Name( "to", mpLiveEventPhaseNames, MP_LIVE_EVENT_PHASES, current.phase );
		finish();
	}

	for ( int slot = 0; slot < MP_LIVE_EVENT_CLIENTS; slot++ ) {
		const mpLiveEventPlayer &was = previous.players[ slot ];
		const mpLiveEventPlayer &now = current.players[ slot ];
		if ( !was.present && !now.present ) {
			continue;
		}
		const char *change = NULL;
		if ( !was.present ) {
			change = "join";
		} else if ( !now.present ) {
			change = "leave";
		} else if ( !MPLiveEventSameText( was.name, now.name ) ) {
			change = "name";
		} else if ( was.spectator != now.spectator ) {
			change = "spectator";
		} else if ( current.teamGame && was.team != now.team ) {
			change = "team";
		}
		if ( change == NULL ) {
			continue;
		}
		const mpLiveEventPlayer &shown = now.present ? now : was;
		line.Begin( sequence, timeMsec, "player" );
		line.String( "event", change );
		line.Int( "slot", slot );
		line.String( "name", shown.name );
		if ( change[ 0 ] == 'n' ) {
			line.String( "was", was.name );
		}
		MPLiveEventTeam( line, "team", current.teamGame, shown.team );
		line.Bool( "spectator", shown.spectator );
		line.Int( "score", shown.score );
		if ( shown.bot >= 0 ) {
			line.Bool( "bot", shown.bot != 0 );
		}
		finish();
	}

	if ( current.teamGame ) {
		for ( int team = 0; team < MP_LIVE_EVENT_TEAMS; team++ ) {
			if ( current.teamScore[ team ] != previous.teamScore[ team ] ) {
				line.Begin( sequence, timeMsec, "score" );
				line.String( "team", mpLiveEventTeamNames[ team ] );
				line.Int( "score", current.teamScore[ team ] );
				line.Int( "was", previous.teamScore[ team ] );
				finish();
			}
		}
	}

	for ( int slot = 0; slot < MP_LIVE_EVENT_CLIENTS; slot++ ) {
		const mpLiveEventPlayer &was = previous.players[ slot ];
		const mpLiveEventPlayer &now = current.players[ slot ];
		// a join reports the score it arrives with
		if ( was.present && now.present && was.score != now.score ) {
			line.Begin( sequence, timeMsec, "playerScore" );
			line.Int( "slot", slot );
			line.String( "name", now.name );
			line.Int( "score", now.score );
			line.Int( "was", was.score );
			finish();
		}
	}

	if ( current.flags && previous.flags ) {
		for ( int flag = 0; flag < MP_LIVE_EVENT_FLAGS; flag++ ) {
			if ( current.flagState[ flag ] == previous.flagState[ flag ] &&
				 current.flagCarrier[ flag ] == previous.flagCarrier[ flag ] ) {
				continue;
			}
			const int state = current.flagState[ flag ];
			line.Begin( sequence, timeMsec, "flag" );
			line.String( "flag", mpLiveEventFlagNames[ flag ] );
			line.String( "state", state == MP_LIVE_FLAG_AT_BASE ? "base" :
				state == MP_LIVE_FLAG_DROPPED ? "dropped" : "taken" );
			// One Flag reports which team holds the neutral flag
			if ( state == MP_LIVE_FLAG_TAKEN_STROGG || state == MP_LIVE_FLAG_TAKEN_MARINE ) {
				line.String( "by", mpLiveEventTeamNames[ state == MP_LIVE_FLAG_TAKEN_STROGG ? 1 : 0 ] );
			}
			MPLiveEventActor( line, "carrier", "carrierName", current, current.flagCarrier[ flag ] );
			// who had it: the player who dropped, lost or captured it
			MPLiveEventActor( line, "wasCarrier", "wasCarrierName", previous, previous.flagCarrier[ flag ] );
			finish();
		}
	}

	for ( int slot = 0; slot < MP_LIVE_EVENT_CLIENTS; slot++ ) {
		const mpLiveEventPlayer &was = previous.players[ slot ];
		const mpLiveEventPlayer &now = current.players[ slot ];
		// a player who joins or leaves gains nothing
		if ( !was.present || !now.present ) {
			continue;
		}
		const std::uint32_t gained = now.powerups & ~was.powerups & trackedPowerups;
		const std::uint32_t lost = was.powerups & ~now.powerups & trackedPowerups;
		for ( int powerup = 0; powerup < MP_LIVE_EVENT_POWERUPS; powerup++ ) {
			const std::uint32_t bit = 1u << powerup;
			if ( ( ( gained | lost ) & bit ) == 0 ) {
				continue;
			}
			line.Begin( sequence, timeMsec, "powerup" );
			line.String( "event", ( gained & bit ) != 0 ? "gain" : "end" );
			line.Int( "slot", slot );
			line.String( "name", now.name );
			line.String( "powerup", mpLiveEventPowerupNames[ powerup ] );
			finish();
		}
	}

	if ( current.phase != previous.phase && current.phase == MP_LIVE_PHASE_REVIEW ) {
		line.Begin( sequence, timeMsec, "standings" );
		MPLiveEventRoster( line, current );
		finish();
	}
	return count;
}
#endif
