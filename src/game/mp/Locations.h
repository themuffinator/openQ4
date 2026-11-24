//----------------------------------------------------------------
// Locations.h
//
// openQ4 location names for multiplayer (MP-15). Quake 4's multiplayer maps
// carry no location entities, so a place is named after the nearest landmark
// the map does have: a weapon, a powerup, mega health, armour or a flag. A map
// that does place info_location entities is named by them instead.
//
// Both ends build the same table from the map's own items, so a name is
// always computed where it is shown and only positions cross the network.
//
// The lookup core below owns no engine types, so tools/tests/native can
// compile it alone.
//----------------------------------------------------------------

#ifndef __GAME_MP_LOCATIONS_H__
#define __GAME_MP_LOCATIONS_H__

// A landmark names a place out to this far; a floor apart counts double.
static const float MP_LOCATION_MAX_DISTANCE = 1400.0f;
static const float MP_LOCATION_VERTICAL_WEIGHT = 2.0f;

/*
================
MPLocationNearest

points holds count landmarks as x, y, z triples. Returns the index of the
landmark nearest point, or -1 when none lies within maxDistance. Height is
stretched by verticalWeight so the landmark on the same floor wins over one
straight above or below. Ties keep the lower index, which makes the answer
the same on every machine.
================
*/
inline int MPLocationNearest( const float *points, int count, const float *point, float maxDistance, float verticalWeight ) {
	int best = -1;
	float bestDistance = maxDistance * maxDistance;

	if ( points == 0 || point == 0 ) {
		return -1;
	}
	for ( int i = 0; i < count; i++ ) {
		const float dx = points[ i * 3 + 0 ] - point[ 0 ];
		const float dy = points[ i * 3 + 1 ] - point[ 1 ];
		const float dz = ( points[ i * 3 + 2 ] - point[ 2 ] ) * verticalWeight;
		const float distance = dx * dx + dy * dy + dz * dz;
		if ( distance < bestDistance ) {
			bestDistance = distance;
			best = i;
		}
	}
	return best;
}

#ifndef MP_CLIENT_OPTIONS_CORE_ONLY
/*
===============================================================================

	rvMapLocations

	The current map's landmarks. Built on first use after a map load, from
	the items the map spawned; dropped weapons and thrown items never count.

===============================================================================
*/
class rvMapLocations {
public:
							rvMapLocations( void );

	// map load: the next lookup rebuilds the table
	void					Clear( void );

	// The localized name of the place at point, or "" when the map has nothing
	// to name it by. The string is valid until the next call.
	const char *			NameForPoint( const idVec3 &point );

	int						NumLandmarks( void );
	// for the listLocations command
	bool					GetLandmark( int index, idVec3 &origin, idStr &name );

private:
	void					Build( void );

	bool					built;
	idList<float>			points;		// x, y, z per landmark
	idStrList				names;		// the item's inv_name, usually a #str_ key
	idStr					result;
};
#endif

#endif	/* !__GAME_MP_LOCATIONS_H__ */
