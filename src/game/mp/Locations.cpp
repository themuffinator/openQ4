//----------------------------------------------------------------
// Locations.cpp
//
// openQ4 location names for multiplayer (MP-15): builds the landmark table
// from the map's items. The lookup is the engine-free core in Locations.h.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "Locations.h"
#include "ItemTimers.h"

/*
================
LocationLandmarkName

The name a map item lends to the place it stands in, or NULL when the item is
too common to say where anything is. Weapons, the timed items and flags name
places; ammunition, shards and small health do not.
================
*/
static const char *LocationLandmarkName( const idItem *item ) {
	const idDict &args = item->spawnArgs;
	if ( args.GetBool( "dropped" ) || args.GetInt( "givenToPlayer", "-1" ) != -1 ) {
		return NULL;
	}
	const char *name = args.GetString( "inv_name" );
	if ( name[ 0 ] == '\0' ) {
		return NULL;
	}
	const itemTimerKind_t kind = rvItemTimers::KindForItem( item );
	if ( kind != ITEMTIMER_NONE && kind != ITEMTIMER_SMALL_ARMOR ) {
		return name;
	}
	if ( args.FindKey( "inv_weapon" ) != NULL || args.FindKey( "weaponclass" ) != NULL ) {
		return name;
	}
	if ( idStr::Icmpn( args.GetString( "classname" ), "mp_ctf_", 7 ) == 0 ) {
		return name;
	}
	return NULL;
}

/*
================
rvMapLocations::rvMapLocations
================
*/
rvMapLocations::rvMapLocations( void ) {
	built = false;
}

/*
================
rvMapLocations::Clear
================
*/
void rvMapLocations::Clear( void ) {
	built = false;
	points.Clear();
	names.Clear();
	result.Clear();
}

/*
================
rvMapLocations::Build

An item's spawn origin is where the map placed it: orgOrigin survives a pickup
and a respawn, and is the same on the server and on every client.
================
*/
void rvMapLocations::Build( void ) {
	points.Clear();
	names.Clear();
	built = true;

	for ( int entityNumber = MAX_CLIENTS; entityNumber < MAX_GENTITIES; entityNumber++ ) {
		idEntity *ent = gameLocal.entities[ entityNumber ];
		if ( ent == NULL || !ent->IsType( idItem::GetClassType() ) ) {
			continue;
		}
		const idItem *item = static_cast< const idItem * >( ent );
		const char *name = LocationLandmarkName( item );
		if ( name == NULL ) {
			continue;
		}
		const idVec3 origin = item->spawnArgs.GetVector( "origin", "0 0 0" );
		points.Append( origin.x );
		points.Append( origin.y );
		points.Append( origin.z );
		names.Append( name );
	}
}

/*
================
rvMapLocations::NumLandmarks
================
*/
int rvMapLocations::NumLandmarks( void ) {
	if ( !built ) {
		Build();
	}
	return names.Num();
}

/*
================
rvMapLocations::GetLandmark
================
*/
bool rvMapLocations::GetLandmark( int index, idVec3 &origin, idStr &name ) {
	if ( index < 0 || index >= NumLandmarks() ) {
		return false;
	}
	origin.Set( points[ index * 3 + 0 ], points[ index * 3 + 1 ], points[ index * 3 + 2 ] );
	name = common->GetLocalizedString( names[ index ] );
	return true;
}

/*
================
rvMapLocations::NameForPoint
================
*/
const char *rvMapLocations::NameForPoint( const idVec3 &point ) {
	result.Clear();
	if ( !gameLocal.isMultiplayer || gameRenderWorld == NULL ) {
		return result.c_str();
	}

	// a map that names its own places is believed first
	const int area = gameRenderWorld->PointInArea( point );
	if ( area >= 0 && area < gameRenderWorld->NumAreas() ) {
		idLocationEntity *location = gameLocal.LocationForPoint( point );
		if ( location != NULL && location->GetLocation()[ 0 ] != '\0' ) {
			result = common->GetLocalizedString( location->GetLocation() );
			return result.c_str();
		}
	}

	if ( !built ) {
		Build();
	}
	const float where[ 3 ] = { point.x, point.y, point.z };
	const int nearest = MPLocationNearest( points.Ptr(), names.Num(), where,
		MP_LOCATION_MAX_DISTANCE, MP_LOCATION_VERTICAL_WEIGHT );
	if ( nearest >= 0 ) {
		result = common->GetLocalizedString( names[ nearest ] );
	}
	return result.c_str();
}
