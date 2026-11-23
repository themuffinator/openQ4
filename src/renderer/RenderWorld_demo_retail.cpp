/*
===========================================================================

openQ4 retail Quake 4 render demo reader

Copyright (C) 2026 openQ4 contributors

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

/*
Retail Quake 4 render demos (MP-22).

Retail Quake 4 1.4.2 keeps the Doom 3 render-demo wrapper (DS_VERSION, DS_RENDER,
DS_SOUND) but writes every token and command as one byte, and its records follow
its own structures rather than openQ4's. The layout below was read from the
retail executable's writers and checked against recordings it made; the
independent decoder tools/tests/retail_render_demo.py walks the same layout.

	DS_RENDER (byte 1), then one command byte:
	 1 RENDERVIEW / 2 PORTALSKY_RENDERVIEW: viewID, x, y, width, height, fov_x,
	   fov_y, vieworg, viewaxis, cramZNear (byte), forceUpdate (byte), time,
	   12 shader parms, int globalMaterial; its name follows when non-zero
	 3 UPDATE_ENTITYDEF, 6 UPDATE_LIGHTDEF, 9 UPDATE_EFFECTDEF: a handle (no-ops)
	 4 DELETE_ENTITYDEF, 7 DELETE_LIGHTDEF, 10 STOP_EFFECTDEF, 11 DELETE_EFFECTDEF
	 5 ENTITYDEF: the renderEntity_t (pointers as presence words; its bounds
	   maximum first, as retail's idFile::WriteBounds writes), the names of
	   its model, shaders and skin, its joints (12 floats each), then each GUI it
	   carries: an int id and the GUI's whole state (state dictionary, window tree,
	   cursor)
	 8 LIGHTDEF: the renderLight_t, its four Doom 3 flags only
	12 EFFECTDEF: the renderEffect_t, the effect's name, then the effect's live
	   state: the effect, its segments, every live particle. Retail writes no
	   segment count and no pool flag: playback finds both by starting the effect.
	13 LOADMAP: int 0, ints 224 and 216 (retail's renderEntity_t/renderLight_t
	   record sizes), an unexplained int, the 256-byte map name, start time
	14 CROP_RENDER, 15 UNCROP_RENDER, 16 CAPTURE_RENDER (name)
	17 END_FRAME
	18 DEFINE_MODEL: retail never writes it
	19 SET_PORTAL_STATE: portal, blocking bits
	20 GUI_MODEL: the frame's 2D drawing (the legacy openQ4 layout)

Nothing read here is trusted: every count, index and length is bounded before
it is used, and a record that does not fit stops playback with a warning.
*/

#include "tr_local.h"
#include "../bse/BSEInterface.h"
#include "../bse/BSE_Envelope.h"
#include "../bse/BSE_Particle.h"

namespace {

enum {
	RDC_RENDERVIEW = 1,
	RDC_PORTALSKY_RENDERVIEW,
	RDC_UPDATE_ENTITYDEF,
	RDC_DELETE_ENTITYDEF,
	RDC_ENTITYDEF,
	RDC_UPDATE_LIGHTDEF,
	RDC_DELETE_LIGHTDEF,
	RDC_LIGHTDEF,
	RDC_UPDATE_EFFECTDEF,
	RDC_STOP_EFFECTDEF,
	RDC_DELETE_EFFECTDEF,
	RDC_EFFECTDEF,
	RDC_LOADMAP,
	RDC_CROP_RENDER,
	RDC_UNCROP_RENDER,
	RDC_CAPTURE_RENDER,
	RDC_END_FRAME,
	RDC_DEFINE_MODEL,
	RDC_SET_PORTAL_STATE,
	RDC_GUI_MODEL
};

const int RETAIL_RENDER_ENTITY_BYTES	= 224;
const int RETAIL_RENDER_LIGHT_BYTES		= 216;
const int RETAIL_MAP_NAME_BYTES			= 256;
const int RETAIL_MAX_HANDLE				= LUDICROUS_INDEX;
const int RETAIL_MAX_JOINTS				= 65536;
const int RETAIL_MAX_STRING				= MAX_STRING_CHARS;
const int RETAIL_MAX_WINDOW_DEPTH		= 64;
const int RETAIL_MAX_WINDOW_CHILDREN	= 4096;
const int RETAIL_MAX_WINDOWS			= 1 << 16;
const int RETAIL_MAX_REGISTERS			= 1 << 16;
const int RETAIL_MAX_PARTICLE_INDEX		= 1 << 16;
const int RETAIL_MAX_EFFECT_SEGMENTS	= 512;

// retail's particle record kinds, by openQ4 PTYPE_*
enum retailParticleKind_t {
	RPK_NONE = -1,
	RPK_SPRITE,			// sprites and decals
	RPK_LINE,
	RPK_ORIENTED,
	RPK_MODEL,
	RPK_LIGHT,
	RPK_ELECTRICITY,
	RPK_LINKED,
	RPK_DEBRIS
};

retailParticleKind_t RetailParticleKind( int particleType ) {
	switch ( particleType ) {
	case PTYPE_SPRITE:
	case PTYPE_DECAL:		return RPK_SPRITE;
	case PTYPE_LINE:		return RPK_LINE;
	case PTYPE_ORIENTED:	return RPK_ORIENTED;
	case PTYPE_MODEL:		return RPK_MODEL;
	case PTYPE_LIGHT:		return RPK_LIGHT;
	case PTYPE_ELECTRICITY:	return RPK_ELECTRICITY;
	case PTYPE_LINKED:		return RPK_LINKED;
	case PTYPE_DEBRIS:		return RPK_DEBRIS;
	default:				return RPK_NONE;
	}
}

/*
Checked reads. The first failure closes the demo and warns once; every later
read on a closed demo fails without a second warning, so a record can be read
straight through and checked once at its end.
*/
class idRetailDemoReader {
public:
	explicit idRetailDemoReader( idDemoFile *file ) : demo( file ), failed( file == NULL || !file->IsOpen() ) {}

	bool	Ok() const { return !failed; }

	bool	Fail( const char *reason ) {
		if ( !failed ) {
			failed = true;
			common->Warning( "Malformed retail render demo: %s; playback stopped safely", reason );
			if ( demo != NULL ) {
				demo->Close();
			}
		}
		return false;
	}

	bool	Bytes( void *out, int count, const char *what ) {
		if ( failed ) {
			return false;
		}
		if ( demo->Read( out, count ) != count ) {
			return Fail( va( "truncated %s", what ) );
		}
		return true;
	}

	bool	Skip( int count, const char *what ) {
		byte scratch[256];
		while ( count > 0 ) {
			const int step = Min( count, static_cast<int>( sizeof( scratch ) ) );
			if ( !Bytes( scratch, step, what ) ) {
				return false;
			}
			count -= step;
		}
		return true;
	}

	bool	Byte( byte &value, const char *what ) { return Bytes( &value, 1, what ); }

	bool	Bool( bool &value, const char *what ) {
		byte b = 0;
		if ( !Byte( b, what ) ) {
			return false;
		}
		value = b != 0;
		return true;
	}

	bool	Int( int &value, const char *what ) {
		byte b[4];
		if ( !Bytes( b, 4, what ) ) {
			return false;
		}
		value = static_cast<int>( static_cast<unsigned int>( b[0] ) | ( static_cast<unsigned int>( b[1] ) << 8 ) |
			( static_cast<unsigned int>( b[2] ) << 16 ) | ( static_cast<unsigned int>( b[3] ) << 24 ) );
		return true;
	}

	bool	Bounded( int &value, int low, int high, const char *what ) {
		if ( !Int( value, what ) ) {
			return false;
		}
		if ( value < low || value > high ) {
			return Fail( va( "%s %d is out of range", what, value ) );
		}
		return true;
	}

	bool	Float( float &value, const char *what ) {
		int bits;
		if ( !Int( bits, what ) ) {
			return false;
		}
		memcpy( &value, &bits, sizeof( value ) );
		return true;
	}

	bool	Finite( float &value, const char *what ) {
		if ( !Float( value, what ) ) {
			return false;
		}
		if ( FLOAT_IS_NAN( value ) || FLOAT_IS_INF( value ) ) {
			return Fail( va( "non-finite %s", what ) );
		}
		return true;
	}

	bool	Vec3( idVec3 &value, const char *what ) {
		return Finite( value.x, what ) && Finite( value.y, what ) && Finite( value.z, what );
	}

	bool	Mat3( idMat3 &value, const char *what ) {
		for ( int i = 0; i < 3; i++ ) {
			if ( !Vec3( value[i], what ) ) {
				return false;
			}
		}
		return true;
	}

	bool	String( idStr &value, const char *what ) {
		int length = 0;
		if ( !Bounded( length, 0, RETAIL_MAX_STRING - 1, va( "%s length", what ) ) ) {
			return false;
		}
		value.Fill( ' ', length );
		return length == 0 || Bytes( &value[0], length, what );
	}

	bool	HashString( idStr &value, const char *what ) {
		if ( failed ) {
			return false;
		}
		value = demo->ReadHashString();
		if ( !demo->IsOpen() ) {
			// ReadHashString has closed the demo and said why
			failed = true;
			return false;
		}
		(void)what;
		return true;
	}

	idDemoFile *	File() const { return demo; }

private:
	idDemoFile *	demo;
	bool			failed;
};

bool RetailReadRenderView( idRetailDemoReader &r, renderView_t &view ) {
	renderView_t read = {};
	int globalMaterial = 0;
	r.Int( read.viewID, "view id" );
	r.Int( read.x, "view rectangle" );
	r.Int( read.y, "view rectangle" );
	r.Int( read.width, "view rectangle" );
	r.Int( read.height, "view rectangle" );
	r.Finite( read.fov_x, "field of view" );
	r.Finite( read.fov_y, "field of view" );
	r.Vec3( read.vieworg, "view origin" );
	r.Mat3( read.viewaxis, "view axis" );
	r.Bool( read.cramZNear, "view flag" );
	r.Bool( read.forceUpdate, "view flag" );
	r.Int( read.time, "view time" );
	for ( int i = 0; i < MAX_GLOBAL_SHADER_PARMS; i++ ) {
		r.Float( read.shaderParms[i], "view shader parm" );
	}
	r.Int( globalMaterial, "view material flag" );
	if ( !r.Ok() ) {
		return false;
	}
	if ( read.width < 1 || read.width > 32768 || read.height < 1 || read.height > 32768 ) {
		return r.Fail( va( "view size %d x %d", read.width, read.height ) );
	}
	if ( read.fov_x <= 0.0f || read.fov_x >= 180.0f || read.fov_y <= 0.0f || read.fov_y >= 180.0f ) {
		return r.Fail( va( "field of view %g x %g", read.fov_x, read.fov_y ) );
	}
	for ( int i = 0; i < 3; i++ ) {
		if ( read.viewaxis[i].LengthSqr() < 0.25f ) {
			return r.Fail( "degenerate view axis" );
		}
	}
	if ( globalMaterial != 0 ) {
		idStr name;
		if ( !r.HashString( name, "view material" ) ) {
			return false;
		}
		read.globalMaterial = declManager->FindMaterial( name );
	}
	view = read;
	return true;
}

/*
---------------------------------------------------------------------------
In-world GUIs. Retail serializes a GUI's whole state: its state dictionary,
then its desktop window and every window under it, then the cursor. Playback
needs only the dictionary: the GUI it names is loaded and given that state.
The window tree is read through and checked.
---------------------------------------------------------------------------
*/

// window variables: a name, an evaluation flag, then the value by type
bool RetailSkipWinVars( idRetailDemoReader &r, const char *kinds ) {
	idStr name;
	for ( const char *kind = kinds; *kind && r.Ok(); kind++ ) {
		bool eval;
		r.HashString( name, "gui variable name" );
		r.Bool( eval, "gui variable flag" );
		switch ( *kind ) {
		case 'b': r.Skip( 1, "gui variable" ); break;
		case 'f': r.Skip( 4, "gui variable" ); break;
		case '2': r.Skip( 8, "gui variable" ); break;
		case '4': r.Skip( 16, "gui variable" ); break;
		case 's': r.HashString( name, "gui variable" ); break;
		}
	}
	return r.Ok();
}

bool RetailSkipSimpleWindow( idRetailDemoReader &r ) {
	idStr name;
	r.HashString( name, "gui window name" );
	// flags, three rectangles, origin, font, matScale x/y, border, align,
	// align x/y, spacing, style
	r.Skip( 4 + 4 * 12 + 8 + 4 + 4 * 3 + 4 + 4 * 2 + 4 + 4, "gui window" );
	// text, visible, rect, back/mat/fore/border colors, scale, rotate,
	// shear, background, hideCursor
	return RetailSkipWinVars( r, "sb44444ff2sb" );
}

bool RetailSkipWindow( idRetailDemoReader &r, int depth, int &windows ) {
	if ( depth > RETAIL_MAX_WINDOW_DEPTH ) {
		return r.Fail( "gui windows nest too deeply" );
	}
	if ( ++windows > RETAIL_MAX_WINDOWS ) {
		return r.Fail( "gui holds too many windows" );
	}
	// actual x/y, child id, flags, last run, two rectangles, origin, time line,
	// offsets, aspect, matScale, border, align x/y, shear, align, style,
	// font, cursor (bytes), spacing
	r.Skip( 4 * 2 + 4 + 4 + 4 + 4 * 8 + 8 + 4 + 4 * 9 + 8 + 4 * 3 + 1 + 1 + 4, "gui window" );
	// noTime, visible, noEvents, hideCursor, rect, back/mat/fore/hover/border
	// colors, text scale, rotate, text, background
	RetailSkipWinVars( r, "bbbb444444ffss" );
	r.Skip( 4 * 4, "gui window text rectangle" );
	idStr name;
	r.HashString( name, "gui window name" );
	int registers = 0;
	r.Bounded( registers, 0, RETAIL_MAX_REGISTERS, "gui register count" );
	for ( int i = 0; i < registers && r.Ok(); i++ ) {
		// enabled, type, count, four register indexes, name
		r.Skip( 1 + 2 + 4 + 2 * 4, "gui register" );
		r.HashString( name, "gui register name" );
	}
	int children = 0;
	r.Bounded( children, 0, RETAIL_MAX_WINDOW_CHILDREN, "gui child count" );
	for ( int i = 0; i < children && r.Ok(); i++ ) {
		bool isWindow = false;
		r.Bool( isWindow, "gui child kind" );
		if ( isWindow ) {
			RetailSkipWindow( r, depth + 1, windows );
		} else {
			if ( ++windows > RETAIL_MAX_WINDOWS ) {
				return r.Fail( "gui holds too many windows" );
			}
			RetailSkipSimpleWindow( r );
		}
	}
	return r.Ok();
}

bool RetailReadGuiState( idRetailDemoReader &r, idDict &state ) {
	int guiId = 0;
	r.Int( guiId, "gui id" );
	if ( !r.Ok() ) {
		return false;
	}
	int keys = 0;
	r.Bounded( keys, 0, 4096, "gui state count" );
	for ( int i = 0; i < keys && r.Ok(); i++ ) {
		idStr key, value;
		if ( r.HashString( key, "gui state key" ) && r.HashString( value, "gui state value" ) ) {
			state.Set( key, value );
		}
	}
	int windows = 0;
	RetailSkipWindow( r, 0, windows );
	float cursor;
	r.Float( cursor, "gui cursor" );
	r.Float( cursor, "gui cursor" );
	return r.Ok();
}

/*
---------------------------------------------------------------------------
Effects. After the effect's own record retail writes rvBSE's live state:
the effect, then each segment with the particles it holds.
---------------------------------------------------------------------------
*/

bool RetailSkipEnvelope( idRetailDemoReader &r, int dimensions ) {
	if ( dimensions == 1 ) {
		r.Skip( 4 * 4, "particle envelope" );
	} else {
		bool flag;
		r.Bool( flag, "particle envelope flag" );
		r.Skip( 4 * dimensions * 4, "particle envelope" );
	}
	int table = 0;
	r.Int( table, "particle envelope table" );
	if ( table != 0 ) {
		idStr name;
		r.String( name, "particle envelope table name" );
	}
	return r.Ok();
}

bool RetailReadLightPayload( idRetailDemoReader &r, renderLight_t &light );

bool RetailSkipParticle( idRetailDemoReader &r, retailParticleKind_t kind ) {
	int link, flags;
	r.Bounded( link, -1, RETAIL_MAX_PARTICLE_INDEX, "particle link" );
	r.Skip( 4, "particle" );
	r.Int( flags, "particle flags" );
	r.Skip( 4 + 12 + 12, "particle" );
	if ( !r.Ok() ) {
		return false;
	}
	const bool full = ( flags & 8 ) == 0;	// a particle's fixed half is written once
	if ( full ) {
		r.Skip( 4 * 3 + 4 + 4 * 2 + 12 + 36 + 12 * 3, "particle" );
		RetailSkipEnvelope( r, 3 );
		RetailSkipEnvelope( r, 1 );
		RetailSkipEnvelope( r, 3 );
		RetailSkipEnvelope( r, 3 );
	}
	if ( kind == RPK_LIGHT ) {
		int lightHandle;
		renderLight_t light;
		r.Int( lightHandle, "particle light" );
		RetailReadLightPayload( r, light );
	}
	if ( full ) {
		switch ( kind ) {
		case RPK_SPRITE:		RetailSkipEnvelope( r, 2 ); RetailSkipEnvelope( r, 1 ); break;
		case RPK_LINE:			RetailSkipEnvelope( r, 1 ); RetailSkipEnvelope( r, 3 ); break;
		case RPK_ORIENTED:		RetailSkipEnvelope( r, 2 ); RetailSkipEnvelope( r, 3 ); break;
		case RPK_MODEL: {
			idStr model;
			RetailSkipEnvelope( r, 3 );
			RetailSkipEnvelope( r, 3 );
			r.String( model, "particle model" );
			break;
		}
		case RPK_LIGHT:			RetailSkipEnvelope( r, 3 ); break;
		case RPK_ELECTRICITY:	RetailSkipEnvelope( r, 1 ); RetailSkipEnvelope( r, 3 ); break;
		case RPK_LINKED:		RetailSkipEnvelope( r, 1 ); break;
		case RPK_DEBRIS:		RetailSkipEnvelope( r, 3 ); break;
		default:				return r.Fail( "unknown particle kind" );
		}
	}
	if ( kind == RPK_ELECTRICITY ) {
		int bolts;
		r.Int( bolts, "electricity bolts" );
		if ( full ) {
			idStr name;
			r.Skip( 4 * 2 + 12 * 3 + 4 * 2, "electricity" );
			r.String( name, "electricity" );
		}
	}
	return r.Ok();
}

bool RetailSkipEffectState( idRetailDemoReader &r, const idList<int> &layout, float &startTime ) {
	int flags = 0;
	r.Int( flags, "effect flags" );
	// state floats, tint, brightness, view origin and axis, gravity, wind,
	// origins, spawn time, velocity, end origins, axes, bounds
	r.Skip( 4 * 3 + 16 + 4 * 2 + 12 + 36 + 12 * 3 + 4 + 12 * 3 + 36 * 2 + 24 * 2, "effect state" );
	if ( !r.Ok() ) {
		return false;
	}
	if ( ( flags & 0x40 ) == 0 ) {
		// written once per effect: its origin, axis and start
		r.Skip( 4 + 12 * 2 + 36, "effect state" );
		r.Float( startTime, "effect start" );
		r.Skip( 4 * 2, "effect state" );
	}
	for ( int segment = 0; segment < layout.Num() && r.Ok(); segment++ ) {
		int segmentFlags = 0;
		r.Int( segmentFlags, "effect segment flags" );
		r.Skip( 4 + 4, "effect segment" );
		if ( ( segmentFlags & 8 ) == 0 ) {
			r.Skip( 4 + 4 * 2 + 8 * 2 + 4 * 2 + 4 * 2, "effect segment" );
		}
		if ( layout[segment] < 0 ) {
			continue;
		}
		const retailParticleKind_t kind = RetailParticleKind( layout[segment] );
		if ( kind == RPK_NONE ) {
			return r.Fail( va( "effect segment %d holds particles retail cannot write", segment ) );
		}
		int index;
		r.Bounded( index, -1, RETAIL_MAX_PARTICLE_INDEX, "effect free particle" );
		r.Bounded( index, -1, RETAIL_MAX_PARTICLE_INDEX, "effect particle" );
		int particles = 0;
		while ( r.Ok() ) {
			r.Bounded( index, -1, RETAIL_MAX_PARTICLE_INDEX, "effect particle" );
			if ( !r.Ok() || index < 0 ) {
				break;
			}
			if ( ++particles > RETAIL_MAX_PARTICLE_INDEX ) {
				return r.Fail( "effect particle list does not end" );
			}
			RetailSkipParticle( r, kind );
		}
	}
	return r.Ok();
}

bool RetailReadLightPayload( idRetailDemoReader &r, renderLight_t &light ) {
	light = renderLight_t();
	light.detailLevel = DEFAULT_LIGHT_DETAIL_LEVEL;
	int prelight = 0, shader = 0;
	r.Mat3( light.axis, "light axis" );
	r.Vec3( light.origin, "light origin" );
	r.Int( light.suppressLightInViewID, "light view id" );
	r.Int( light.allowLightInViewID, "light view id" );
	float detail = DEFAULT_LIGHT_DETAIL_LEVEL;
	r.Float( detail, "light detail" );
	if ( !FLOAT_IS_NAN( detail ) && !FLOAT_IS_INF( detail ) ) {
		light.detailLevel = detail;
	}
	r.Bool( light.noShadows, "light flag" );
	r.Bool( light.noSpecular, "light flag" );
	r.Bool( light.pointLight, "light flag" );
	r.Bool( light.parallel, "light flag" );
	r.Vec3( light.lightRadius, "light radius" );
	r.Vec3( light.lightCenter, "light center" );
	r.Vec3( light.target, "light target" );
	r.Vec3( light.right, "light right" );
	r.Vec3( light.up, "light up" );
	r.Vec3( light.start, "light start" );
	r.Vec3( light.end, "light end" );
	r.Int( prelight, "light prelight flag" );
	r.Int( light.lightId, "light id" );
	r.Int( shader, "light shader flag" );
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ ) {
		r.Float( light.shaderParms[i], "light shader parm" );
	}
	r.Int( light.referenceSoundHandle, "light sound" );
	idStr name;
	if ( prelight != 0 && r.HashString( name, "light prelight model" ) ) {
		light.prelightModel = renderModelManager->FindModel( name );
	}
	if ( shader != 0 && r.HashString( name, "light shader" ) ) {
		light.shader = declManager->FindMaterial( name );
	}
	return r.Ok();
}

} // namespace

/*
================
idRenderWorldLocal::ReadRetailRenderLight
================
*/
bool idRenderWorldLocal::ReadRetailRenderLight( idDemoFile *demo ) {
	idRetailDemoReader r( demo );
	int index = 0;
	renderLight_t light;
	if ( !r.Bounded( index, 0, RETAIL_MAX_HANDLE, "light handle" ) || !RetailReadLightPayload( r, light ) ) {
		return false;
	}
	UpdateLightDef( index, &light );
	if ( r_showDemo.GetBool() ) {
		common->Printf( "retail DC_LIGHTDEF: %i\n", index );
	}
	return true;
}

/*
================
idRenderWorldLocal::ReadRetailRenderEntity
================
*/
bool idRenderWorldLocal::ReadRetailRenderEntity( idDemoFile *demo ) {
	idRetailDemoReader r( demo );
	renderEntity_t ent = {};
	int index = 0;
	int model = 0, callback = 0, callbackData = 0;
	int customShader = 0, referenceShader = 0, overlayShader = 0, customSkin = 0;
	int guis[ MAX_RENDERENTITY_GUI ] = {};
	int remoteView = 0, jointsPointer = 0;

	r.Bounded( index, 0, RETAIL_MAX_HANDLE, "entity handle" );
	r.Int( model, "entity model flag" );
	r.Int( ent.entityNum, "entity number" );
	r.Int( ent.bodyId, "entity body" );
	// retail's idFile::WriteBounds writes the maximum first
	r.Vec3( ent.bounds[1], "entity bounds" );
	r.Vec3( ent.bounds[0], "entity bounds" );
	r.Int( callback, "entity callback flag" );
	r.Int( callbackData, "entity callback flag" );
	r.Int( ent.suppressSurfaceInViewID, "entity view id" );
	r.Int( ent.suppressShadowInViewID, "entity view id" );
	r.Int( ent.suppressShadowInLightID, "entity light id" );
	r.Int( ent.allowSurfaceInViewID, "entity view id" );
	r.Int( ent.suppressSurfaceMask, "entity surface mask" );
	r.Vec3( ent.origin, "entity origin" );
	r.Mat3( ent.axis, "entity axis" );
	r.Int( customShader, "entity shader flag" );
	r.Int( referenceShader, "entity shader flag" );
	r.Int( overlayShader, "entity shader flag" );
	r.Int( customSkin, "entity skin flag" );
	r.Int( ent.referenceSoundHandle, "entity sound" );
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ ) {
		r.Float( ent.shaderParms[i], "entity shader parm" );
	}
	for ( int i = 0; i < MAX_RENDERENTITY_GUI; i++ ) {
		r.Int( guis[i], "entity gui flag" );
	}
	r.Int( remoteView, "entity remote view" );
	r.Bounded( ent.numJoints, 0, RETAIL_MAX_JOINTS, "entity joint count" );
	r.Int( jointsPointer, "entity joints flag" );
	r.Finite( ent.modelDepthHack, "entity depth hack" );
	r.Bool( ent.noSelfShadow, "entity flag" );
	r.Bool( ent.noShadow, "entity flag" );
	r.Bool( ent.noDynamicInteractions, "entity flag" );
	r.Bool( ent.forceUpdate, "entity flag" );
	r.Int( ent.weaponDepthHackInViewID, "entity depth hack view" );
	r.Float( ent.shadowLODDistance, "entity shadow distance" );
	r.Int( ent.suppressLOD, "entity LOD" );
	if ( !r.Ok() ) {
		return false;
	}

	idStr name;
	if ( model != 0 && r.HashString( name, "entity model" ) ) {
		ent.hModel = renderModelManager->FindModel( name );
	}
	if ( customShader != 0 && r.HashString( name, "entity shader" ) ) {
		ent.customShader = declManager->FindMaterial( name );
	}
	if ( referenceShader != 0 && r.HashString( name, "entity shader" ) ) {
		ent.referenceShader = declManager->FindMaterial( name );
	}
	if ( overlayShader != 0 && r.HashString( name, "entity shader" ) ) {
		ent.overlayShader = declManager->FindMaterial( name );
	}
	if ( customSkin != 0 && r.HashString( name, "entity skin" ) ) {
		ent.customSkin = declManager->FindSkin( name );
	}
	if ( !r.Ok() ) {
		return false;
	}

	if ( ent.numJoints > 0 ) {
		ent.joints = (idJointMat *)Mem_Alloc16( ent.numJoints * sizeof( ent.joints[0] ) );
		for ( int i = 0; i < ent.numJoints && r.Ok(); i++ ) {
			float *words = ent.joints[i].ToFloatPtr();
			for ( int j = 0; j < 12; j++ ) {
				r.Finite( words[j], "entity joint" );
			}
		}
		if ( !r.Ok() ) {
			Mem_Free16( ent.joints );
			return false;
		}
	}

	// Each GUI the entity carries: the one its state names, loaded and given
	// that state. A GUI already on this entity with the same file is reused:
	// playback frees the GUIs of the definition it replaces.
	idRenderEntityLocal *def = index < entityDefs.Num() ? entityDefs[index] : NULL;
	for ( int i = 0; i < MAX_RENDERENTITY_GUI; i++ ) {
		if ( guis[i] == 0 ) {
			continue;
		}
		idDict state;
		if ( !RetailReadGuiState( r, state ) ) {
			for ( int j = 0; j < i; j++ ) {
				delete ent.gui[j];
			}
			if ( ent.joints != NULL ) {
				Mem_Free16( ent.joints );
			}
			return false;
		}
		const char *guiName = state.GetString( "name" );
		idUserInterface *gui = NULL;
		if ( def != NULL && def->parms.gui[i] != NULL && !idStr::Icmp( def->parms.gui[i]->Name(), guiName ) ) {
			gui = def->parms.gui[i];
			def->parms.gui[i] = NULL;
		} else if ( guiName[0] != '\0' ) {
			gui = uiManager->FindGui( guiName, true, true );
		}
		if ( gui != NULL ) {
			for ( int k = 0; k < state.GetNumKeyVals(); k++ ) {
				const idKeyValue *kv = state.GetKeyVal( k );
				gui->SetStateString( kv->GetKey(), kv->GetValue() );
			}
			gui->StateChanged( retailDemoViewTime );
		}
		ent.gui[i] = gui;
	}

	if ( ent.hModel == NULL ) {
		// nothing to draw: playback keeps no definition for it
		for ( int i = 0; i < MAX_RENDERENTITY_GUI; i++ ) {
			delete ent.gui[i];
		}
		if ( ent.joints != NULL ) {
			Mem_Free16( ent.joints );
		}
		if ( def != NULL ) {
			FreeEntityDef( index );
		}
		return true;
	}
	if ( ent.joints != NULL && ent.hModel->IsDynamicModel() == DM_STATIC ) {
		Mem_Free16( ent.joints );
		ent.joints = NULL;
		ent.numJoints = 0;
	}
	ent.callback = NULL;
	ent.callbackData = NULL;
	ent.remoteRenderView = NULL;

	UpdateEntityDef( index, &ent );
	if ( r_showDemo.GetBool() ) {
		common->Printf( "retail DC_ENTITYDEF: %i = %s\n", index, ent.hModel->Name() );
	}
	return true;
}

/*
================
idRenderWorldLocal::ReadRetailRenderEffect
================
*/
bool idRenderWorldLocal::ReadRetailRenderEffect( idDemoFile *demo, const renderView_t *demoRenderView ) {
	idRetailDemoReader r( demo );
	renderEffect_t effect = {};
	int index = 0;
	int decl = 0;
	r.Bounded( index, 0, RETAIL_MAX_HANDLE, "effect handle" );
	r.Int( decl, "effect flag" );
	r.Int( effect.suppressSurfaceInViewID, "effect view id" );
	r.Int( effect.allowSurfaceInViewID, "effect view id" );
	r.Int( effect.groupID, "effect group" );
	r.Vec3( effect.origin, "effect origin" );
	r.Mat3( effect.axis, "effect axis" );
	r.Vec3( effect.gravity, "effect gravity" );
	r.Vec3( effect.endOrigin, "effect end" );
	r.Finite( effect.attenuation, "effect attenuation" );
	r.Bool( effect.hasEndOrigin, "effect flag" );
	r.Bool( effect.loop, "effect flag" );
	r.Bool( effect.ambient, "effect flag" );
	r.Bool( effect.inConnectedArea, "effect flag" );
	r.Int( effect.weaponDepthHackInViewID, "effect depth hack view" );
	r.Finite( effect.modelDepthHack, "effect depth hack" );
	r.Int( effect.referenceSoundHandle, "effect sound" );
	for ( int i = 0; i < MAX_ENTITY_SHADER_PARMS; i++ ) {
		r.Float( effect.shaderParms[i], "effect shader parm" );
	}
	if ( !r.Ok() ) {
		return false;
	}
	if ( decl == 0 ) {
		// the effect's state follows, and it cannot be read without a declaration
		return r.Fail( "an effect without a declaration" );
	}
	idStr name;
	if ( !r.HashString( name, "effect name" ) ) {
		return false;
	}
	const rvDeclEffect *declEffect = declManager->FindEffect( name, false );
	const idDecl *effectDecl = declManager->FindType( DECL_EFFECT, name, false );
	if ( declEffect == NULL || effectDecl == NULL ) {
		// its state cannot be read without the declaration that shaped it
		return r.Fail( va( "effect '%s' is not in this installation", name.c_str() ) );
	}
	effect.declEffect = effectDecl;

	const int now = demoRenderView != NULL ? demoRenderView->time : retailDemoViewTime;
	const float nowSeconds = now * 0.001f;
	effect.startTime = nowSeconds;

	idList<int> layout;
	int segments[ RETAIL_MAX_EFFECT_SEGMENTS ];
	const int numSegments = bse->DemoParticleLayout( declEffect, &effect, nowSeconds, segments, RETAIL_MAX_EFFECT_SEGMENTS );
	if ( numSegments < 0 ) {
		return r.Fail( va( "effect '%s' cannot be laid out here", name.c_str() ) );
	}
	for ( int i = 0; i < numSegments; i++ ) {
		layout.Append( segments[i] );
	}
	float recordedStart = -1.0f;
	if ( !RetailSkipEffectState( r, layout, recordedStart ) ) {
		return false;
	}
	if ( !FLOAT_IS_NAN( recordedStart ) && recordedStart >= 0.0f && recordedStart <= nowSeconds ) {
		effect.startTime = recordedStart;
	}

	// Playback learns of an effect when the recording first wrote it. One that
	// had been running for a while is run through its last seconds first,
	// silently, so it is shown as it stood (as openQ4's own demos do).
	static const float DEMO_EFFECT_PREROLL_SECONDS = 12.0f;
	static const float DEMO_EFFECT_PREROLL_STEP = 0.25f;
	const bool known = index < effectsDef.Num() && effectsDef[index] != NULL;
	if ( !known && nowSeconds - effect.startTime > DEMO_EFFECT_PREROLL_STEP ) {
		renderEffect_t silent = effect;
		silent.referenceSoundHandle = 0;
		const float from = Max( effect.startTime, nowSeconds - DEMO_EFFECT_PREROLL_SECONDS );
		for ( float t = from; t < nowSeconds - 0.5f * DEMO_EFFECT_PREROLL_STEP; t += DEMO_EFFECT_PREROLL_STEP ) {
			if ( UpdateEffectDef( index, &silent, static_cast<int>( t * 1000.0f ) ) ) {
				FreeEffectDef( index );
				break;
			}
		}
	}
	UpdateEffectDef( index, &effect, now );
	if ( r_showDemo.GetBool() ) {
		common->Printf( "retail DC_EFFECTDEF: %i = %s\n", index, name.c_str() );
	}
	return true;
}

/*
================
idRenderWorldLocal::ProcessRetailDemoCommand
================
*/
bool idRenderWorldLocal::ProcessRetailDemoCommand( idDemoFile *readDemo, renderView_t *demoRenderView,
	renderView_t *portalSkyRenderView, int *demoTimeOffset ) {
	idRetailDemoReader r( readDemo );
	byte command = 0;
	if ( !r.Byte( command, "render command" ) ) {
		return false;
	}

	switch ( command ) {
	case RDC_RENDERVIEW:
	case RDC_PORTALSKY_RENDERVIEW: {
		renderView_t view;
		if ( !RetailReadRenderView( r, view ) ) {
			return false;
		}
		if ( command == RDC_PORTALSKY_RENDERVIEW ) {
			if ( portalSkyRenderView != NULL ) {
				*portalSkyRenderView = view;
			}
			return false;
		}
		*demoRenderView = view;
		retailDemoViewTime = view.time;
		if ( r_showDemo.GetBool() ) {
			common->Printf( "retail DC_RENDERVIEW: %i\n", view.time );
		}
		if ( pendingDemoTimeOffset && demoTimeOffset ) {
			*demoTimeOffset = view.time - eventLoop->Milliseconds();
		}
		pendingDemoTimeOffset = false;
		return false;
	}

	case RDC_UPDATE_ENTITYDEF:
	case RDC_UPDATE_LIGHTDEF:
	case RDC_UPDATE_EFFECTDEF: {
		// retail marks a definition for rewriting here; its record follows when drawn
		int handle;
		r.Bounded( handle, 0, RETAIL_MAX_HANDLE, "definition handle" );
		return false;
	}

	case RDC_DELETE_ENTITYDEF:
	case RDC_DELETE_LIGHTDEF:
	case RDC_STOP_EFFECTDEF:
	case RDC_DELETE_EFFECTDEF: {
		int handle;
		if ( !r.Bounded( handle, 0, RETAIL_MAX_HANDLE, "definition handle" ) ) {
			return false;
		}
		if ( command == RDC_DELETE_ENTITYDEF ) {
			if ( handle < entityDefs.Num() && entityDefs[handle] != NULL ) {
				FreeEntityDef( handle );
			}
		} else if ( command == RDC_DELETE_LIGHTDEF ) {
			if ( handle < lightDefs.Num() && lightDefs[handle] != NULL ) {
				FreeLightDef( handle );
			}
		} else if ( handle < effectsDef.Num() && effectsDef[handle] != NULL ) {
			if ( command == RDC_STOP_EFFECTDEF ) {
				StopEffectDef( handle );
			} else {
				FreeEffectDef( handle );
			}
		}
		return false;
	}

	case RDC_ENTITYDEF:
		ReadRetailRenderEntity( readDemo );
		return false;

	case RDC_LIGHTDEF:
		ReadRetailRenderLight( readDemo );
		return false;

	case RDC_EFFECTDEF:
		ReadRetailRenderEffect( readDemo, demoRenderView );
		return false;

	case RDC_LOADMAP: {
		int version, entityBytes, lightBytes, unknown, startTime;
		char mapName[ RETAIL_MAP_NAME_BYTES ];
		r.Int( version, "map header" );
		r.Int( entityBytes, "map header" );
		r.Int( lightBytes, "map header" );
		r.Int( unknown, "map header" );
		r.Bytes( mapName, sizeof( mapName ), "map name" );
		r.Int( startTime, "map start" );
		if ( !r.Ok() ) {
			return false;
		}
		mapName[ sizeof( mapName ) - 1 ] = '\0';
		if ( entityBytes != RETAIL_RENDER_ENTITY_BYTES || lightBytes != RETAIL_RENDER_LIGHT_BYTES ) {
			return r.Fail( va( "map header names %d/%d-byte records, not retail Quake 4's", entityBytes, lightBytes ) );
		}
		if ( r_showDemo.GetBool() ) {
			common->Printf( "retail DC_LOADMAP: %s\n", mapName );
		}
		if ( !InitFromMap( mapName ) ) {
			return r.Fail( va( "map '%s' could not be loaded", mapName ) );
		}
		pendingDemoTimeOffset = true;
		return false;
	}

	case RDC_CROP_RENDER: {
		int size[3];
		r.Int( size[0], "crop" );
		r.Int( size[1], "crop" );
		r.Int( size[2], "crop" );
		if ( !r.Ok() ) {
			return false;
		}
		if ( size[0] < 1 || size[0] > 32768 || size[1] < 1 || size[1] > 32768 ) {
			return r.Fail( va( "crop size %d x %d", size[0], size[1] ) );
		}
		renderSystem->CropRenderSize( size[0], size[1], size[2] != 0 );
		return false;
	}

	case RDC_UNCROP_RENDER:
		renderSystem->UnCrop();
		return false;

	case RDC_CAPTURE_RENDER: {
		idStr name;
		if ( r.HashString( name, "capture image" ) ) {
			renderSystem->CaptureRenderToImage( name );
		}
		return false;
	}

	case RDC_END_FRAME:
		return true;

	case RDC_DEFINE_MODEL:
		r.Fail( "a model definition, which retail never writes" );
		return false;

	case RDC_SET_PORTAL_STATE: {
		int portal, blockingBits;
		r.Int( portal, "portal" );
		r.Int( blockingBits, "portal state" );
		if ( !r.Ok() ) {
			return false;
		}
		if ( portal < 0 || portal > numInterAreaPortals ) {
			r.Fail( va( "portal %d is out of range", portal ) );
			return false;
		}
		SetPortalState( portal, blockingBits );
		return false;
	}

	case RDC_GUI_MODEL:
		// retail's 2D model is the legacy openQ4 layout, which session->renderdemoVersion 0 selects
		if ( !tr.demoGuiModel->ReadFromDemo( readDemo ) && readDemo->IsOpen() ) {
			readDemo->Close();
		}
		return false;

	default:
		r.Fail( va( "unknown render command %d", static_cast<int>( command ) ) );
		return false;
	}
}
