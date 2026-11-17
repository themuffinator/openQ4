/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include "RenderGeometry.h"
#include "../imagetools/ImageTools.h"
#include "InteractionCullBits.h"

#if defined( OPENQ4_CULLBITS_ONE_PASS )
// The one-pass kernel reproduces idSIMD_SSE2's Dot and CmpLT; with any other
// processor active (com_forceGenericSIMD) keep that processor's own passes.
static idCVar r_cullBitsOnePass( "r_cullBitsOnePass", "1", CVAR_RENDERER | CVAR_BOOL,
	"compute light-interaction cull bits in one pass over the vertices (identical bits); 0 = one pass per light plane, for comparison" );

static bool R_SIMDIsSSE2( void ) {
	return r_cullBitsOnePass.GetBool() && SIMDProcessor != NULL && idStr::Cmp( SIMDProcessor->GetName(), "SSE2" ) == 0;
}
#endif

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
// local copy of the renderer's inline helper; both sides read the packed
// sil-trace vertex array straight off the surface
static ID_INLINE const rvSilTraceVertT *R_GetInteractionSilTraceVerts( const srfTriangles_t *tri ) {
	return ( tri != NULL && tri->silTraceVerts != NULL )
		? reinterpret_cast<const rvSilTraceVertT *>( tri->silTraceVerts )
		: NULL;
}
#endif

/*
================
R_CalcInteractionFacing

Determines which triangles of the surface are facing towards the light origin.

The facing array should be allocated with one extra index than
the number of surface triangles, which will be used to handle dangling
edge silhouettes.
================
*/
void R_CalcInteractionFacing( const idRenderEntityLocal *ent, const srfTriangles_t *tri, const idRenderLightLocal *light, srfCullInfo_t &cullInfo ) {
	idVec3 localLightOrigin;

	if ( cullInfo.facing != NULL ) {
		return;
	}

	R_GlobalPointToLocal( ent->modelMatrix, light->globalLightOrigin, localLightOrigin );

	int numFaces = tri->numIndexes / 3;

	if ( !tri->facePlanes || !tri->facePlanesCalculated ) {
		R_DeriveFacePlanes( const_cast<srfTriangles_t *>(tri) );
	}

	cullInfo.facing = (byte *) R_StaticAlloc( ( numFaces + 1 ) * sizeof( cullInfo.facing[0] ) );

	// calculate back face culling
	float *planeSide = (float *) _alloca16( numFaces * sizeof( float ) );

	// exact geometric cull against face
	SIMDProcessor->Dot( planeSide, localLightOrigin, tri->facePlanes, numFaces );
	SIMDProcessor->CmpGE( cullInfo.facing, planeSide, 0.0f, numFaces );

	cullInfo.facing[ numFaces ] = 1;	// for dangling edges to reference
}

/*
=====================
R_CalcInteractionCullBits

We want to cull a little on the sloppy side, because the pre-clipping
of geometry to the lights in dmap will give many cases that are right
at the border we throw things out on the border, because if any one
vertex is clearly inside, the entire triangle will be accepted.
=====================
*/
void R_CalcInteractionCullBits( const idRenderEntityLocal *ent, const srfTriangles_t *tri, const idRenderLightLocal *light, srfCullInfo_t &cullInfo ) {
	int i, frontBits;

	if ( cullInfo.cullBits != NULL ) {
		return;
	}

	frontBits = 0;

	// cull the triangle surface bounding box
	for ( i = 0; i < 6; i++ ) {

		R_GlobalPlaneToLocal( ent->modelMatrix, -light->frustum[i], cullInfo.localClipPlanes[i] );

		// get front bits for the whole surface
		if ( tri->bounds.PlaneDistance( cullInfo.localClipPlanes[i] ) >= LIGHT_CLIP_EPSILON ) {
			frontBits |= 1<<i;
		}
	}

	// if the surface is completely inside the light frustum
	if ( frontBits == ( ( 1 << 6 ) - 1 ) ) {
		cullInfo.cullBits = LIGHT_CULL_ALL_FRONT;
		return;
	}

	cullInfo.cullBits = (byte *) R_StaticAlloc( tri->numVerts * sizeof( cullInfo.cullBits[0] ) );

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	const rvSilTraceVertT *silTraceVerts = R_GetInteractionSilTraceVerts( tri );
#endif

#if defined( OPENQ4_CULLBITS_ONE_PASS )
	// One pass over the vertices for every crossing plane, bit-identical to the
	// SSE2 Dot + CmpLT passes below (InteractionCullBits.h).
	if (
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		silTraceVerts == NULL &&
#endif
		R_SIMDIsSSE2() ) {
		R_CullBitsOnePass( cullInfo.cullBits, cullInfo.localClipPlanes, frontBits, tri->verts, tri->numVerts, LIGHT_CLIP_EPSILON );
		return;
	}
#endif

	SIMDProcessor->Memset( cullInfo.cullBits, 0, tri->numVerts * sizeof( cullInfo.cullBits[0] ) );

	float *planeSide = (float *) _alloca16( tri->numVerts * sizeof( float ) );

	for ( i = 0; i < 6; i++ ) {
		// if completely infront of this clipping plane
		if ( frontBits & ( 1 << i ) ) {
			continue;
		}
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		if ( silTraceVerts != NULL ) {
			for ( int vertNum = 0; vertNum < tri->numVerts; ++vertNum ) {
				planeSide[vertNum] = cullInfo.localClipPlanes[i].Distance( silTraceVerts[vertNum].xyzw.ToVec3() );
			}
		} else
#endif
		SIMDProcessor->Dot( planeSide, cullInfo.localClipPlanes[i], tri->verts, tri->numVerts );
		SIMDProcessor->CmpLT( cullInfo.cullBits, i, planeSide, LIGHT_CLIP_EPSILON, tri->numVerts );
	}
}

/*
================
R_FreeInteractionCullInfo
================
*/
void R_FreeInteractionCullInfo( srfCullInfo_t &cullInfo ) {
	if ( cullInfo.facing != NULL ) {
		R_StaticFree( cullInfo.facing );
		cullInfo.facing = NULL;
	}
	if ( cullInfo.cullBits != NULL ) {
		if ( cullInfo.cullBits != LIGHT_CULL_ALL_FRONT ) {
			R_StaticFree( cullInfo.cullBits );
		}
		cullInfo.cullBits = NULL;
	}
}
