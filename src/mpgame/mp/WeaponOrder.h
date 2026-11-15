//----------------------------------------------------------------
// WeaponOrder.h
//
// openQ4 weapon preference order (MP-15). A player lists the weapons they
// would rather hold, best first, in ui_weaponOrder. The server then switches
// on a pickup only to a weapon the player ranks above the one in hand, and
// falls back to the best ranked weapon that still has ammunition when the
// one in hand runs dry.
//
// This core owns no engine types, so tools/tests/native can compile it alone.
//----------------------------------------------------------------

#ifndef __GAME_MP_WEAPONORDER_H__
#define __GAME_MP_WEAPONORDER_H__

static const int MP_WEAPON_ORDER_SLOTS = 32;		// at least MAX_WEAPONS
static const int MP_WEAPON_ORDER_UNRANKED = 0x7fff;

struct mpWeaponOrder_t {
	short	rank[ MP_WEAPON_ORDER_SLOTS ];	// 0 is the most wanted; UNRANKED when not listed
	int		listed;							// how many weapons the order names
};

inline void MPWeaponOrderClear( mpWeaponOrder_t &order ) {
	for ( int i = 0; i < MP_WEAPON_ORDER_SLOTS; i++ ) {
		order.rank[ i ] = MP_WEAPON_ORDER_UNRANKED;
	}
	order.listed = 0;
}

inline bool MPWeaponOrderActive( const mpWeaponOrder_t &order ) {
	return order.listed > 0;
}

inline char MPWeaponOrderLower( char c ) {
	return ( c >= 'A' && c <= 'Z' ) ? (char)( c - 'A' + 'a' ) : c;
}

// Compares a token of tokenLength characters with a whole name, without case.
inline bool MPWeaponOrderSame( const char *token, int tokenLength, const char *name, int nameLength ) {
	if ( tokenLength != nameLength ) {
		return false;
	}
	for ( int i = 0; i < tokenLength; i++ ) {
		if ( MPWeaponOrderLower( token[ i ] ) != MPWeaponOrderLower( name[ i ] ) ) {
			return false;
		}
	}
	return true;
}

inline int MPWeaponOrderLength( const char *text ) {
	int length = 0;
	while ( text[ length ] != '\0' ) {
		length++;
	}
	return length;
}

// The short names players already use for Quake 4's weapons.
inline const char *MPWeaponOrderAlias( const char *token, int tokenLength ) {
	static const char * const aliases[][ 2 ] = {
		{ "rl", "rocketlauncher" }, { "rg", "railgun" }, { "lg", "lightninggun" },
		{ "gl", "grenadelauncher" }, { "mg", "machinegun" }, { "sg", "shotgun" },
		{ "hb", "hyperblaster" }, { "ng", "nailgun" }, { "nap", "napalmgun" },
		{ "bfg", "dmg" }, { "g", "gauntlet" }, { "bl", "blaster" }
	};
	for ( int i = 0; i < (int)( sizeof( aliases ) / sizeof( aliases[ 0 ] ) ); i++ ) {
		if ( MPWeaponOrderSame( token, tokenLength, aliases[ i ][ 0 ], MPWeaponOrderLength( aliases[ i ][ 0 ] ) ) ) {
			return aliases[ i ][ 1 ];
		}
	}
	return 0;
}

// A token names a weapon by its definition ("weapon_railgun"), by that name
// without the "weapon_" prefix and without a "_mp" suffix ("railgun"), by an
// alias ("rg") or by its slot number ("7").
inline int MPWeaponOrderFind( const char *token, int tokenLength, const char * const *names, int count ) {
	bool number = tokenLength > 0;
	int slot = 0;
	for ( int i = 0; i < tokenLength; i++ ) {
		if ( token[ i ] < '0' || token[ i ] > '9' ) {
			number = false;
			break;
		}
		slot = slot * 10 + ( token[ i ] - '0' );
		if ( slot > MP_WEAPON_ORDER_SLOTS ) {
			slot = MP_WEAPON_ORDER_SLOTS;
		}
	}
	if ( number ) {
		return ( slot < count && names[ slot ] != 0 && names[ slot ][ 0 ] != '\0' ) ? slot : -1;
	}

	const char *alias = MPWeaponOrderAlias( token, tokenLength );
	if ( alias != 0 ) {
		token = alias;
		tokenLength = MPWeaponOrderLength( alias );
	}
	for ( int i = 0; i < count; i++ ) {
		const char *name = names[ i ];
		if ( name == 0 || name[ 0 ] == '\0' ) {
			continue;
		}
		int nameLength = MPWeaponOrderLength( name );
		if ( MPWeaponOrderSame( token, tokenLength, name, nameLength ) ) {
			return i;
		}
		const char *shortName = name;
		int shortLength = nameLength;
		if ( shortLength > 7 && MPWeaponOrderSame( shortName, 7, "weapon_", 7 ) ) {
			shortName += 7;
			shortLength -= 7;
		}
		if ( shortLength > 3 && MPWeaponOrderSame( shortName + shortLength - 3, 3, "_mp", 3 ) ) {
			shortLength -= 3;
		}
		if ( MPWeaponOrderSame( token, tokenLength, shortName, shortLength ) ) {
			return i;
		}
	}
	return -1;
}

/*
================
MPWeaponOrderParse

names[ slot ] is the weapon definition in that slot, NULL or empty when the
slot is unused. Tokens are separated by spaces, commas or semicolons. A token
that names nothing is skipped and a weapon named twice keeps its first rank,
so a list written for another weapon set still orders the weapons it shares.
================
*/
inline void MPWeaponOrderParse( const char *text, const char * const *names, int count, mpWeaponOrder_t &order ) {
	MPWeaponOrderClear( order );
	if ( text == 0 || names == 0 ) {
		return;
	}
	if ( count > MP_WEAPON_ORDER_SLOTS ) {
		count = MP_WEAPON_ORDER_SLOTS;
	}
	for ( const char *scan = text; *scan != '\0'; ) {
		while ( *scan == ' ' || *scan == '\t' || *scan == ',' || *scan == ';' ) {
			scan++;
		}
		const char *token = scan;
		while ( *scan != '\0' && *scan != ' ' && *scan != '\t' && *scan != ',' && *scan != ';' ) {
			scan++;
		}
		const int tokenLength = (int)( scan - token );
		if ( tokenLength <= 0 ) {
			continue;
		}
		const int slot = MPWeaponOrderFind( token, tokenLength, names, count );
		if ( slot >= 0 && order.rank[ slot ] == MP_WEAPON_ORDER_UNRANKED ) {
			order.rank[ slot ] = (short)order.listed++;
		}
	}
}

inline int MPWeaponOrderRank( const mpWeaponOrder_t &order, int slot ) {
	return ( slot >= 0 && slot < MP_WEAPON_ORDER_SLOTS ) ? order.rank[ slot ] : MP_WEAPON_ORDER_UNRANKED;
}

// A pickup switches only to a weapon ranked above the one in hand. A weapon
// the order does not name is never switched to; any named weapon beats one
// that is not named, and beats holding nothing.
inline bool MPWeaponOrderSwitchOnPickup( const mpWeaponOrder_t &order, int current, int picked ) {
	const int pickedRank = MPWeaponOrderRank( order, picked );
	if ( pickedRank == MP_WEAPON_ORDER_UNRANKED ) {
		return false;
	}
	return pickedRank < MPWeaponOrderRank( order, current );
}

// The best ranked weapon among the usable ones (bit per slot: owned, with
// ammunition), or -1 when the order names none of them.
inline int MPWeaponOrderBest( const mpWeaponOrder_t &order, unsigned int usable ) {
	int best = -1;
	for ( int slot = 0; slot < MP_WEAPON_ORDER_SLOTS; slot++ ) {
		if ( ( usable & ( 1u << slot ) ) == 0 || order.rank[ slot ] == MP_WEAPON_ORDER_UNRANKED ) {
			continue;
		}
		if ( best < 0 || order.rank[ slot ] < order.rank[ best ] ) {
			best = slot;
		}
	}
	return best;
}

#endif	/* !__GAME_MP_WEAPONORDER_H__ */
