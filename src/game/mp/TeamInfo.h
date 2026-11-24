//----------------------------------------------------------------
// TeamInfo.h
//
// openQ4 team overlay data (MP-15). A snapshot carries only the players its
// recipient can see, so a team mate across the map reads stale. Twice a second
// the server tells each player in a team mode where every team mate is and what
// they have: health, armour, the weapon in hand and a position the client turns
// into a location name.
//
// The message goes to the team member alone, holding their own team. Someone
// who chose to watch gets every player's record on a casual server, for the
// one thing no snapshot carries for players they do not follow: how much
// ammunition each has for the weapon in hand (the Competitive scoreline's
// bars). It is not recorded.
//
// The ammunition travels as a trailer of one byte an entry after the entries,
// so a 0.15.0 client, which stops reading after the entries, takes the message
// as before, and a record from an older server has no ammunition.
//
// The packing core below owns no engine types, so tools/tests/native can
// compile it alone.
//----------------------------------------------------------------

#ifndef __GAME_MP_TEAMINFO_H__
#define __GAME_MP_TEAMINFO_H__

static const int MP_TEAMINFO_SEND_MSEC = 500;
// a record older than this is no better than the stale entity it replaces
static const int MP_TEAMINFO_FRESH_MSEC = 2500;
static const int MP_TEAMINFO_NO_WEAPON = 0xff;
// ammunition for the weapon in hand as a share of what it can hold, 0..250
static const int MP_TEAMINFO_AMMO_FULL = 250;
static const int MP_TEAMINFO_AMMO_UNKNOWN = 0xff;

struct mpTeamInfoEntry_t {
	int		clientNum;
	int		health;			// 0 when dead
	int		armor;
	int		weapon;			// slot, -1 for none
	int		origin[ 3 ];	// whole world units
	int		ammo;			// 0..MP_TEAMINFO_AMMO_FULL, or MP_TEAMINFO_AMMO_UNKNOWN
};

// Health and armour travel as unsigned bytes and positions as signed 16 bit
// world units: Quake 4's maps fit, and a unit is far finer than a place name.
inline int MPTeamInfoPackVital( int value ) {
	return value < 0 ? 0 : ( value > 255 ? 255 : value );
}

inline int MPTeamInfoPackCoord( float value ) {
	const float rounded = value < 0.0f ? value - 0.5f : value + 0.5f;
	if ( rounded <= -32768.0f ) {
		return -32768;
	}
	if ( rounded >= 32767.0f ) {
		return 32767;
	}
	return (int)rounded;
}

inline int MPTeamInfoPackWeapon( int weapon, int maxWeapons ) {
	return ( weapon >= 0 && weapon < maxWeapons && weapon < MP_TEAMINFO_NO_WEAPON ) ? weapon : MP_TEAMINFO_NO_WEAPON;
}

// A weapon that uses none, or holds unlimited ammunition, reads full.
inline int MPTeamInfoPackAmmo( int ammo, int maxAmmo, bool unlimited ) {
	if ( unlimited ) {
		return MP_TEAMINFO_AMMO_FULL;
	}
	if ( maxAmmo <= 0 ) {
		return MP_TEAMINFO_AMMO_UNKNOWN;
	}
	if ( ammo <= 0 ) {
		return 0;
	}
	if ( ammo >= maxAmmo ) {
		return MP_TEAMINFO_AMMO_FULL;
	}
	const int share = ( ammo * MP_TEAMINFO_AMMO_FULL + maxAmmo / 2 ) / maxAmmo;
	// a weapon with a shot left never reads empty
	return share < 1 ? 1 : share;
}

inline int MPTeamInfoUnpackAmmo( int packed ) {
	return ( packed >= 0 && packed <= MP_TEAMINFO_AMMO_FULL ) ? packed : MP_TEAMINFO_AMMO_UNKNOWN;
}

inline int MPTeamInfoUnpackWeapon( int packed, int maxWeapons ) {
	return ( packed >= 0 && packed < maxWeapons ) ? packed : -1;
}

inline bool MPTeamInfoFresh( int receivedTime, int now ) {
	return receivedTime != 0 && now - receivedTime >= 0 && now - receivedTime <= MP_TEAMINFO_FRESH_MSEC;
}

#ifndef MP_CLIENT_OPTIONS_CORE_ONLY
class idPlayer;

/*
===============================================================================

	rvTeamInfo

===============================================================================
*/
class rvTeamInfo {
public:
							rvTeamInfo( void );

	void					Clear( void );

	// server, once a game frame
	void					ServerFrame( void );

	// client
	void					ClientReceive( const idBitMsg &msg );

	// What the HUD should show for a player: the server's record when this end
	// is a client holding a fresh one, and otherwise the entity itself, which is
	// exact on a server and in a demo and current for anyone in view.
	void					Resolve( const idPlayer *player, mpTeamInfoEntry_t &entry ) const;
	// true when Resolve would answer from the server's record
	bool					UsesRecord( const idPlayer *player ) const;

private:
	mpTeamInfoEntry_t		entries[ MAX_CLIENTS ];
	int						received[ MAX_CLIENTS ];	// realClientTime, 0 for never
	int						nextSendTime;
};
#endif

#endif	/* !__GAME_MP_TEAMINFO_H__ */
