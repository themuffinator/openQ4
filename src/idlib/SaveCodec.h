#ifndef __SAVECODEC_H__
#define __SAVECODEC_H__

#include <type_traits>

/*
===============================================================================

	Portable savegame codecs (roadmap Q4-06).

	Each function writes one aggregate as explicit fields through the typed
	primitives of a writer, never as the compiler laid it out:

		w.WriteInt( int )      32-bit two's complement, little-endian
		w.WriteFloat( float )  IEEE-754 binary32, little-endian
		w.WriteBool( bool )    one byte, 0 or 1
		w.WriteByte( byte )    one byte

	idSaveGame, idRestoreGame and idFile already encode these primitives
	explicitly, so the bytes do not depend on padding, bitfield allocation,
	enum width or pointer width. Readers provide the matching ReadInt,
	ReadFloat, ReadBool and ReadByte, plus Error( fmt, ... ), which must not
	return. Counts are checked before they are used.

	The field orders below are a save format. Changing one is a payload
	version change; tools/tests/save_codec.py pins every order in golden
	bytes on each platform.

===============================================================================
*/

// Number of float components in a float-only vector type (idVec3, idVec4,
// idAngles, idQuat). Components are read through operator[], not memory.
template< class type >
struct SaveCodec_Components {
	static_assert( std::is_standard_layout<type>::value && sizeof( type ) % sizeof( float ) == 0 &&
		sizeof( type ) <= 16 * sizeof( float ), "SaveCodec vector types hold only floats" );
	static const int count = static_cast<int>( sizeof( type ) / sizeof( float ) );
};

template< class writer >
void SaveCodec_WriteValue( writer &w, float value ) {
	w.WriteFloat( value );
}

template< class reader >
void SaveCodec_ReadValue( reader &r, float &value ) {
	r.ReadFloat( value );
}

template< class writer, class type >
void SaveCodec_WriteValue( writer &w, const type &value ) {
	for ( int i = 0; i < SaveCodec_Components<type>::count; i++ ) {
		w.WriteFloat( value[ i ] );
	}
}

template< class reader, class type >
void SaveCodec_ReadValue( reader &r, type &value ) {
	for ( int i = 0; i < SaveCodec_Components<type>::count; i++ ) {
		r.ReadFloat( value[ i ] );
	}
}

// idMat3 and idBounds: rows in order, each row's components in order.
template< class writer, class type >
void SaveCodec_WriteRows( writer &w, const type &value, int rows ) {
	for ( int i = 0; i < rows; i++ ) {
		SaveCodec_WriteValue( w, value[ i ] );
	}
}

template< class reader, class type >
void SaveCodec_ReadRows( reader &r, type &value, int rows ) {
	for ( int i = 0; i < rows; i++ ) {
		SaveCodec_ReadValue( r, value[ i ] );
	}
}

template< class writer >
void SaveCodec_WriteInts( writer &w, const int *values, int count ) {
	for ( int i = 0; i < count; i++ ) {
		w.WriteInt( values[ i ] );
	}
}

template< class reader >
void SaveCodec_ReadInts( reader &r, int *values, int count ) {
	for ( int i = 0; i < count; i++ ) {
		r.ReadInt( values[ i ] );
	}
}

template< class writer >
void SaveCodec_WriteFloats( writer &w, const float *values, int count ) {
	for ( int i = 0; i < count; i++ ) {
		w.WriteFloat( values[ i ] );
	}
}

template< class reader >
void SaveCodec_ReadFloats( reader &r, float *values, int count ) {
	for ( int i = 0; i < count; i++ ) {
		r.ReadFloat( values[ i ] );
	}
}

/*
================
SaveCodec_WriteFlagBytes

A struct of one-bit bool fields as its listed flags: flag i in bit i % 8 of
byte i / 8, low bit first, in ( count + 7 ) / 8 bytes. MSVC, GCC and Clang all
lay such a struct out this way on little-endian targets, so these are the
bytes a raw copy of the struct wrote, with the unused bits cleared. The list
is the format and must follow the declaration order.

Readers ignore the bits beyond the list, because older saves copied whatever
those bits held.
================
*/
template< class writer, int count >
void SaveCodec_WriteFlagBytes( writer &w, const bool ( &flags )[ count ] ) {
	for ( int base = 0; base < count; base += 8 ) {
		unsigned char value = 0;
		for ( int i = base; i < count && i < base + 8; i++ ) {
			value = static_cast<unsigned char>( value | ( ( flags[ i ] ? 1u : 0u ) << ( i - base ) ) );
		}
		w.WriteByte( value );
	}
}

template< class reader, int count >
void SaveCodec_ReadFlagBytes( reader &r, bool ( &flags )[ count ] ) {
	for ( int base = 0; base < count; base += 8 ) {
		unsigned char value;
		r.ReadByte( value );
		for ( int i = base; i < count && i < base + 8; i++ ) {
			flags[ i ] = ( ( value >> ( i - base ) ) & 1u ) != 0;
		}
	}
}

template< class reader >
int SaveCodec_ReadCount( reader &r, int maximum, const char *context, const char *field ) {
	int count;
	r.ReadInt( count );
	if ( count < 0 || count > maximum ) {
		r.Error( "%s: invalid %s %d (maximum %d)", context, field, count, maximum );
	}
	return count;
}

/*
================
SaveCodec_WriteTraceModel

Only the used vertices, edges and polygons are written. Edge 0 is unused; edge
indices are signed to give direction.
================
*/
template< class writer, class traceModel >
void SaveCodec_WriteTraceModel( writer &w, const traceModel &trm ) {
	w.WriteInt( static_cast<int>( trm.type ) );
	w.WriteInt( trm.numVerts );
	for ( int i = 0; i < trm.numVerts; i++ ) {
		SaveCodec_WriteValue( w, trm.verts[ i ] );
	}
	w.WriteInt( trm.numEdges );
	for ( int i = 1; i <= trm.numEdges; i++ ) {
		SaveCodec_WriteInts( w, trm.edges[ i ].v, 2 );
		SaveCodec_WriteValue( w, trm.edges[ i ].normal );
	}
	w.WriteInt( trm.numPolys );
	for ( int i = 0; i < trm.numPolys; i++ ) {
		SaveCodec_WriteValue( w, trm.polys[ i ].normal );
		w.WriteFloat( trm.polys[ i ].dist );
		SaveCodec_WriteRows( w, trm.polys[ i ].bounds, 2 );
		w.WriteInt( trm.polys[ i ].numEdges );
		SaveCodec_WriteInts( w, trm.polys[ i ].edges, trm.polys[ i ].numEdges );
	}
	SaveCodec_WriteValue( w, trm.offset );
	SaveCodec_WriteRows( w, trm.bounds, 2 );
	w.WriteBool( trm.isConvex );
}

template< class reader, class traceModel >
void SaveCodec_ReadTraceModel( reader &r, traceModel &trm, int typeCount, int maxVerts, int maxEdges, int maxPolys, int maxPolyEdges ) {
	int type;
	r.ReadInt( type );
	if ( type < 0 || type >= typeCount ) {
		r.Error( "SaveCodec_ReadTraceModel: invalid trace model type %d", type );
	}
	trm.type = static_cast<decltype( trm.type )>( type );
	trm.numVerts = SaveCodec_ReadCount( r, maxVerts, "SaveCodec_ReadTraceModel", "vertex count" );
	for ( int i = 0; i < trm.numVerts; i++ ) {
		SaveCodec_ReadValue( r, trm.verts[ i ] );
	}
	trm.numEdges = SaveCodec_ReadCount( r, maxEdges, "SaveCodec_ReadTraceModel", "edge count" );
	for ( int i = 1; i <= trm.numEdges; i++ ) {
		SaveCodec_ReadInts( r, trm.edges[ i ].v, 2 );
		SaveCodec_ReadValue( r, trm.edges[ i ].normal );
	}
	trm.numPolys = SaveCodec_ReadCount( r, maxPolys, "SaveCodec_ReadTraceModel", "polygon count" );
	for ( int i = 0; i < trm.numPolys; i++ ) {
		SaveCodec_ReadValue( r, trm.polys[ i ].normal );
		r.ReadFloat( trm.polys[ i ].dist );
		SaveCodec_ReadRows( r, trm.polys[ i ].bounds, 2 );
		trm.polys[ i ].numEdges = SaveCodec_ReadCount( r, maxPolyEdges, "SaveCodec_ReadTraceModel", "polygon edge count" );
		SaveCodec_ReadInts( r, trm.polys[ i ].edges, trm.polys[ i ].numEdges );
	}
	SaveCodec_ReadValue( r, trm.offset );
	SaveCodec_ReadRows( r, trm.bounds, 2 );
	r.ReadBool( trm.isConvex );
}

/*
================
SaveCodec_WriteInterpolate

idInterpolateAccelDecelLinear by its defining values. The reader rebuilds it
through Init, so its derived speed and extrapolation cache are never saved.
================
*/
template< class writer, class interpolate >
void SaveCodec_WriteInterpolate( writer &w, const interpolate &lerp ) {
	w.WriteFloat( lerp.GetStartTime() );
	w.WriteFloat( lerp.GetAcceleration() );
	w.WriteFloat( lerp.GetDeceleration() );
	w.WriteFloat( lerp.GetDuration() );
	SaveCodec_WriteValue( w, lerp.GetStartValue() );
	SaveCodec_WriteValue( w, lerp.GetEndValue() );
}

template< class reader, class interpolate >
void SaveCodec_ReadInterpolate( reader &r, interpolate &lerp ) {
	float startTime, accelTime, decelTime, duration;
	r.ReadFloat( startTime );
	r.ReadFloat( accelTime );
	r.ReadFloat( decelTime );
	r.ReadFloat( duration );
	typename std::remove_cv<typename std::remove_reference<decltype( lerp.GetStartValue() )>::type>::type startValue, endValue;
	SaveCodec_ReadValue( r, startValue );
	SaveCodec_ReadValue( r, endValue );
	lerp.Init( startTime, accelTime, decelTime, duration, startValue, endValue );
}

// jointMod_t: joint, rotation, position, both transform modes, the angular
// velocity interpolator, its last update time and the collapse joint.
template< class writer, class jointMod >
void SaveCodec_WriteJointMod( writer &w, const jointMod &mod ) {
	w.WriteInt( static_cast<int>( mod.jointnum ) );
	SaveCodec_WriteRows( w, mod.mat, 3 );
	SaveCodec_WriteValue( w, mod.pos );
	w.WriteInt( static_cast<int>( mod.transform_pos ) );
	w.WriteInt( static_cast<int>( mod.transform_axis ) );
	SaveCodec_WriteInterpolate( w, mod.angularVelocity );
	w.WriteInt( mod.lastTime );
	w.WriteInt( static_cast<int>( mod.collapseJoint ) );
}

template< class reader, class jointMod >
void SaveCodec_ReadJointMod( reader &r, jointMod &mod, int transformCount ) {
	int value;
	r.ReadInt( value );
	mod.jointnum = static_cast<decltype( mod.jointnum )>( value );
	SaveCodec_ReadRows( r, mod.mat, 3 );
	SaveCodec_ReadValue( r, mod.pos );
	r.ReadInt( value );
	if ( value < 0 || value >= transformCount ) {
		r.Error( "SaveCodec_ReadJointMod: invalid position transform %d", value );
	}
	mod.transform_pos = static_cast<decltype( mod.transform_pos )>( value );
	r.ReadInt( value );
	if ( value < 0 || value >= transformCount ) {
		r.Error( "SaveCodec_ReadJointMod: invalid axis transform %d", value );
	}
	mod.transform_axis = static_cast<decltype( mod.transform_axis )>( value );
	SaveCodec_ReadInterpolate( r, mod.angularVelocity );
	r.ReadInt( mod.lastTime );
	r.ReadInt( value );
	mod.collapseJoint = static_cast<decltype( mod.collapseJoint )>( value );
}

// idAFPoseJointMod: modifier type, axis rows, origin.
template< class writer, class poseMod >
void SaveCodec_WritePoseJointMod( writer &w, const poseMod &mod ) {
	w.WriteInt( static_cast<int>( mod.mod ) );
	SaveCodec_WriteRows( w, mod.axis, 3 );
	SaveCodec_WriteValue( w, mod.origin );
}

template< class reader, class poseMod >
void SaveCodec_ReadPoseJointMod( reader &r, poseMod &mod, int modCount ) {
	int value;
	r.ReadInt( value );
	if ( value < 0 || value >= modCount ) {
		r.Error( "SaveCodec_ReadPoseJointMod: invalid modifier type %d", value );
	}
	mod.mod = static_cast<decltype( mod.mod )>( value );
	SaveCodec_ReadRows( r, mod.axis, 3 );
	SaveCodec_ReadValue( r, mod.origin );
}

// idJointQuat: rotation quaternion, translation, then w.
template< class writer, class jointQuat >
void SaveCodec_WriteJointQuat( writer &w, const jointQuat &joint ) {
	SaveCodec_WriteValue( w, joint.q );
	SaveCodec_WriteValue( w, joint.t );
	w.WriteFloat( joint.w );
}

template< class reader, class jointQuat >
void SaveCodec_ReadJointQuat( reader &r, jointQuat &joint ) {
	SaveCodec_ReadValue( r, joint.q );
	SaveCodec_ReadValue( r, joint.t );
	r.ReadFloat( joint.w );
}

// idJointMat: its twelve matrix floats in storage order.
template< class writer, class jointMat >
void SaveCodec_WriteJointMat( writer &w, const jointMat &joint ) {
	SaveCodec_WriteFloats( w, joint.ToFloatPtr(), 12 );
}

template< class reader, class jointMat >
void SaveCodec_ReadJointMat( reader &r, jointMat &joint ) {
	SaveCodec_ReadFloats( r, joint.ToFloatPtr(), 12 );
}

/*
================
SaveCodec_WritePathTarget

rvVehicleDriver::PathTargetInfo. The target entity goes through the caller's
typed reference writer, so its restore keeps the spawn-reference checks.
================
*/
template< class writer, class pathTarget, class writeReference >
void SaveCodec_WritePathTarget( writer &w, const pathTarget &target, writeReference writeNode ) {
	writeNode( target.node );
	w.WriteFloat( target.initialDistance );
	w.WriteFloat( target.minDistance );
	w.WriteFloat( target.throttle );
	w.WriteBool( target.exitVehicle );
}

template< class reader, class pathTarget, class readReference >
void SaveCodec_ReadPathTarget( reader &r, pathTarget &target, readReference readNode ) {
	readNode( target.node );
	r.ReadFloat( target.initialDistance );
	r.ReadFloat( target.minDistance );
	r.ReadFloat( target.throttle );
	r.ReadBool( target.exitVehicle );
}

#endif /* !__SAVECODEC_H__ */
