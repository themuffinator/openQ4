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




#include "tr_local.h"
#include "RendererMetrics.h"
#include "ScenePackets.h"
#ifdef __ppc__
#include <vecLib/vecLib.h>
#endif
#if defined(MACOS_X) && defined(__i386__)
#include <xmmintrin.h>
#endif

//====================================================================

/*
======================
idScreenRect::Clear
======================
*/
void idScreenRect::Clear() {
	x1 = y1 = 32000;
	x2 = y2 = -32000;
	zmin = 0.0f; zmax = 1.0f;
}

/*
======================
idScreenRect::AddPoint
======================
*/
void idScreenRect::AddPoint( float x, float y ) {
	int	ix = idMath::FtoiFast( x );
	int iy = idMath::FtoiFast( y );

	if ( ix < x1 ) {
		x1 = ix;
	}
	if ( ix > x2 ) {
		x2 = ix;
	}
	if ( iy < y1 ) {
		y1 = iy;
	}
	if ( iy > y2 ) {
		y2 = iy;
	}
}

/*
======================
idScreenRect::Expand
======================
*/
void idScreenRect::Expand() {
	x1--;
	y1--;
	x2++;
	y2++;
}

/*
======================
idScreenRect::Intersect
======================
*/
void idScreenRect::Intersect( const idScreenRect &rect ) {
	if ( rect.x1 > x1 ) {
		x1 = rect.x1;
	}
	if ( rect.x2 < x2 ) {
		x2 = rect.x2;
	}
	if ( rect.y1 > y1 ) {
		y1 = rect.y1;
	}
	if ( rect.y2 < y2 ) {
		y2 = rect.y2;
	}
}

/*
======================
idScreenRect::Union
======================
*/
void idScreenRect::Union( const idScreenRect &rect ) {
	if ( rect.x1 < x1 ) {
		x1 = rect.x1;
	}
	if ( rect.x2 > x2 ) {
		x2 = rect.x2;
	}
	if ( rect.y1 < y1 ) {
		y1 = rect.y1;
	}
	if ( rect.y2 > y2 ) {
		y2 = rect.y2;
	}
}

/*
======================
idScreenRect::Equals
======================
*/
bool idScreenRect::Equals( const idScreenRect &rect ) const {
	return ( x1 == rect.x1 && x2 == rect.x2 && y1 == rect.y1 && y2 == rect.y2 );
}

/*
======================
idScreenRect::IsEmpty
======================
*/
bool idScreenRect::IsEmpty() const {
	return ( x1 > x2 || y1 > y2 );
}

/*
======================
R_ClampedScreenCoord

idScreenRect stores shorts, and nothing upstream bounds what reaches this
conversion.

idFrustum::ProjectionBounds divides by the distance along the frustum axis when
it projects a box corner (Frustum.cpp, AddLocalLineToProjectionBoundsSetCull:
"if ( cull1 == 0 && start.x > 0.0f )" then "start.y * dFar / ( start.x * dLeft
)"). A corner sitting essentially on the eye point satisfies that test and
divides by a near-zero, so the projection comes back as a huge finite y or z --
with the function still returning true, so a caller checking the return value
learns nothing.

0.5 * ( 1 - y ) * width then overflows a short at y < -26 on a 2400-pixel-wide
viewport, but only at y < -50 on a 1280-pixel one. That is why this reproduces
on a phone and not in a desktop test window, and idMath::FtoiFast is lrintf on
Android, which saturates the opposite way from x86-64 before the truncation.

The overflowed value wraps back into range, so the rect looks plausible:
IsEmpty() does not reject it, and the Intersect in R_AddModelSurfaces takes a
min and so cannot undo it either. The entity ends up drawn clipped to a bogus
small box. Nothing clears the colour buffer, so everything that box stops
covering keeps the previous frame -- a frozen screen with one small live box
moving around in it.

Clamping in float, ahead of the narrowing conversion, is what makes that
impossible. This is behaviour-preserving for every value that was not already
wrapping: callers all intersect the result into a rect no larger than the
viewport, and that Intersect takes the min either way. The one pixel of slack
matches idScreenRect::Expand.
======================
*/
static int r_screenRectClampReports = 0;

static short R_ClampedScreenCoord( float coord, int dimension, bool &clamped ) {
	const float low = -1.0f;
	const float high = ( float )( dimension + 1 );

	// ordered so a NaN fails the test and is clamped rather than converted
	if ( coord >= low && coord <= high ) {
		return ( short )idMath::FtoiFast( coord );
	}

	clamped = true;
	return ( coord < low ) ? ( short )-1 : ( short )( dimension + 1 );
}

/*
======================
R_ScreenRectFromViewFrustumBounds
======================
*/
idScreenRect R_ScreenRectFromViewFrustumBounds( const idBounds &bounds ) {
	idScreenRect screenRect;

	const int width = tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1;
	const int height = tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1;
	bool clamped = false;

	screenRect.x1 = R_ClampedScreenCoord( 0.5f * ( 1.0f - bounds[1].y ) * width, width, clamped );
	screenRect.x2 = R_ClampedScreenCoord( 0.5f * ( 1.0f - bounds[0].y ) * width, width, clamped );
	screenRect.y1 = R_ClampedScreenCoord( 0.5f * ( 1.0f + bounds[0].z ) * height, height, clamped );
	screenRect.y2 = R_ClampedScreenCoord( 0.5f * ( 1.0f + bounds[1].z ) * height, height, clamped );

	// Loud and self-limiting. Bounds this far out are a real anomaly, and the
	// whole reason this survived so long is that it was silent: the value
	// wrapped into a plausible-looking rect instead of an obviously broken one.
	// If the frozen-frame bug reproduces without these lines in the log, the
	// cause is somewhere else and this fix is not it.
	if ( clamped && r_screenRectClampReports < 8 ) {
		r_screenRectClampReports++;
		common->Printf( "R_ScreenRectFromViewFrustumBounds: bounds out of range, clamped -- "
				"y %f..%f z %f..%f, viewport %ix%i, rect %i,%i..%i,%i\n",
				bounds[0].y, bounds[1].y, bounds[0].z, bounds[1].z,
				width, height,
				screenRect.x1, screenRect.y1, screenRect.x2, screenRect.y2 );
	}

	if ( r_useDepthBoundsTest.GetInteger() ) {
		R_TransformEyeZToWin( -bounds[0].x, tr.viewDef->projectionMatrix, screenRect.zmin );
		R_TransformEyeZToWin( -bounds[1].x, tr.viewDef->projectionMatrix, screenRect.zmax );
	}

	return screenRect;
}

/*
======================
R_ShowColoredScreenRect
======================
*/
void R_ShowColoredScreenRect( const idScreenRect &rect, int colorIndex ) {
	if ( !rect.IsEmpty() ) {
		static idVec4 colors[] = { colorRed, colorGreen, colorBlue, colorYellow, colorMagenta, colorCyan, colorWhite, colorPurple };
		tr.viewDef->renderWorld->DebugScreenRect( colors[colorIndex & 7], rect, tr.viewDef );
	}
}

/*
====================
R_ToggleSmpFrame
====================
*/
void R_ToggleSmpFrame( void ) {
	if ( r_lockSurfaces.GetBool() ) {
		return;
	}
	R_FreeDeferredTriSurfs( frameData );
	for ( int i = 0; i < tr.worlds.Num(); i++ ) {
		if ( tr.worlds[i] != NULL ) {
			tr.worlds[i]->FreeDeferredLightDefs();
		}
	}

	// clear frame-temporary data
	frameData_t		*frame;
	frameMemoryBlock_t	*block;

	// update the highwater mark
	R_CountFrameData();

	frame = frameData;

	// reset the memory allocation to the first block
	frame->alloc = frame->memory;

	// clear all the blocks
	for ( block = frame->memory ; block ; block = block->next ) {
		block->used = 0;
	}

	R_ClearCommandChain();
}


//=====================================================

#define	MEMORY_BLOCK_SIZE	0x100000

/*
=====================
R_ShutdownFrameData
=====================
*/
void R_ShutdownFrameData( void ) {
	frameData_t *frame;
	frameMemoryBlock_t *block;

	// free any current data
	frame = frameData;
	if ( !frame ) {
		return;
	}

	R_FreeDeferredTriSurfs( frame );

	frameMemoryBlock_t *nextBlock;
	for ( block = frame->memory ; block ; block = nextBlock ) {
		nextBlock = block->next;
		Mem_Free16( block );
	}
	Mem_Free( frame );
	frameData = NULL;
}

/*
=====================
R_InitFrameData
=====================
*/
void R_InitFrameData( void ) {
	int size;
	frameData_t *frame;
	frameMemoryBlock_t *block;

	R_ShutdownFrameData();

	frameData = (frameData_t *)Mem_ClearedAlloc( sizeof( *frameData ));
	frame = frameData;
	size = MEMORY_BLOCK_SIZE;
	block = (frameMemoryBlock_t *)Mem_Alloc16( size + sizeof( *block ) );
	if ( !block ) {
		common->FatalError( "R_InitFrameData: Mem_Alloc16() failed" );
	}
	block->size = size;
	block->used = 0;
	block->next = NULL;
	frame->memory = block;
	frame->memoryHighwater = 0;

	R_ToggleSmpFrame();
}

/*
================
R_CountFrameData
================
*/
int R_CountFrameData( void ) {
	frameData_t		*frame;
	frameMemoryBlock_t	*block;
	int				count;

	count = 0;
	frame = frameData;
	for ( block = frame->memory ; block ; block=block->next ) {
		count += block->used;
		if ( block == frame->alloc ) {
			break;
		}
	}

	// note if this is a new highwater mark
	if ( count > frame->memoryHighwater ) {
		frame->memoryHighwater = count;
	}

	return count;
}

/*
=================
R_ImageToolsCounters

R_StaticAlloc/R_StaticFree moved into the shared imagetools library; the
renderer keeps its performance counters through these installed hooks.
=================
*/
static void R_ImageToolsOnStaticAlloc( size_t bytes ) {
	tr.pc.c_alloc++;
	tr.staticAllocCount += bytes;
}

static void R_ImageToolsOnStaticFree( void ) {
	tr.pc.c_free++;
}

void R_InstallImageToolsHooks( void ) {
	imageToolsAllocHooks_t allocHooks;

	allocHooks.onStaticAlloc = R_ImageToolsOnStaticAlloc;
	allocHooks.onStaticFree = R_ImageToolsOnStaticFree;
	ImageTools_SetStaticAllocHooks( allocHooks );
}

/*
=================
R_RenderGeoHooks

Live renderer behavior for the shared render-geometry library. The library's
defaults are the dmap-correct behavior; these bind the renderer's cvars,
performance counters, vertex-cache lifetime, deferred frees, and the dynamic
turbo-shadow path.
=================
*/
static bool R_GeoUseSilRemap( void ) {
	return r_useSilRemap.GetBool();
}

static bool R_GeoReportSilEdgeWarnings( void ) {
	return r_reportSilhouetteEdgeWarnings.GetBool();
}

static bool R_GeoShadowsEnabled( void ) {
	return r_shadows.GetBool();
}

static void R_GeoCountTangentIndexes( int amount ) {
	tr.pc.c_tangentIndexes += amount;
}

static void R_GeoCountShadowVolume( void ) {
	tr.pc.c_createShadowVolumes++;
}

static void R_GeoFreeTriSurfVertCache( vertCache_t *&cache ) {
	if ( cache != NULL ) {
		if ( cache->tag != TAG_TEMP ) {
			vertexCache.Free( cache );
		}
		cache = NULL;
	}
}

static void R_GeoFreeVertexCaches( srfTriangles_t *tri ) {
	if ( tri->ambientSurface == NULL ) {
		// this is a real model surface
		R_GeoFreeTriSurfVertCache( tri->ambientCache );
	} else {
		// this is a light interaction surface that references
		// a different ambient model surface
		R_GeoFreeTriSurfVertCache( tri->lightingCache );
	}
	R_GeoFreeTriSurfVertCache( tri->indexCache );
	if ( tri->shadowCache && ( tri->shadowVertexes != NULL || tri->verts != NULL ) ) {
		// if we don't have tri->shadowVertexes, these are a reference to a
		// shadowCache on the original surface, which a vertex program
		// will take care of making unique for each light
		R_GeoFreeTriSurfVertCache( tri->shadowCache );
	}
}

static bool R_GeoDeferFree( srfTriangles_t *tri ) {
	frameData_t *frame = frameData;

	if ( !frame ) {
		// command line utility, or rendering in editor preview mode
		return false;
	}
	tri->nextDeferredFree = NULL;
	if ( frame->lastDeferredFreeTriSurf ) {
		frame->lastDeferredFreeTriSurf->nextDeferredFree = tri;
	} else {
		frame->firstDeferredFreeTriSurf = tri;
	}
	frame->lastDeferredFreeTriSurf = tri;
	return true;
}

static bool R_GeoTryTurboShadow( const idRenderEntityLocal *ent, const srfTriangles_t *tri,
								 const idRenderLightLocal *light, srfCullInfo_t &cullInfo, srfTriangles_t **out ) {
	if ( !r_useTurboShadow.GetBool() ) {
		return false;
	}
	*out = R_CreateTurboShadowVolumeForSurface( ent, tri, light, cullInfo );
	return true;
}

void R_InstallRenderGeoHooks( void ) {
	renderGeoHooks_t geoHooks;

	memset( &geoHooks, 0, sizeof( geoHooks ) );
	geoHooks.useSilRemap = R_GeoUseSilRemap;
	geoHooks.reportSilEdgeWarnings = R_GeoReportSilEdgeWarnings;
	geoHooks.shadowsEnabled = R_GeoShadowsEnabled;
	geoHooks.countTangentIndexes = R_GeoCountTangentIndexes;
	geoHooks.countShadowVolume = R_GeoCountShadowVolume;
	geoHooks.freeVertexCaches = R_GeoFreeVertexCaches;
	geoHooks.deferFree = R_GeoDeferFree;
	geoHooks.tryTurboShadow = R_GeoTryTurboShadow;
	// superOptimize/cleanupOptimizedShadowTris stay dmap-bound; SetHooks
	// preserves them
	RenderGeo_SetHooks( geoHooks );
}

/*
================
R_FrameAlloc

This data will be automatically freed when the
current frame's back end completes.

This should only be called by the front end.  The
back end shouldn't need to allocate memory.

If we passed smpFrame in, the back end could
alloc memory, because it will always be a
different frameData than the front end is using.

All temporary data, like dynamic tesselations
and local spaces are allocated here.

The memory will not move, but it may not be
contiguous with previous allocations even
from this frame.

The memory is NOT zero filled.
Should part of this be inlined in a macro?
================
*/
void *R_FrameAlloc( int bytes ) {
	frameData_t		*frame;
	frameMemoryBlock_t	*block;
	void			*buf;

	// A negative size (an overflowed count*sizeof in a caller) would round to a
	// still-negative value, pass the first-fit test below, rewind block->used,
	// and silently hand back overlapping live memory. Reject it, and fold the
	// oversize fatal up here so a huge positive request that wraps to negative
	// after the +16 rounding is also caught before the block math runs.
	if ( bytes < 0 || bytes > MEMORY_BLOCK_SIZE ) {
		common->FatalError( "R_FrameAlloc: invalid size %i", bytes );
	}
	bytes = (bytes+16)&~15;
	// see if it can be satisfied in the current block
	frame = frameData;
	block = frame->alloc;

	if ( block->size - block->used >= bytes ) {
		buf = block->base + block->used;
		block->used += bytes;
		return buf;
	}

	// advance to the next memory block if available
	block = block->next;
	// create a new block if we are at the end of
	// the chain
	if ( !block ) {
		int		size;

		size = MEMORY_BLOCK_SIZE;
		block = (frameMemoryBlock_t *)Mem_Alloc16( size + sizeof( *block ) );
		if ( !block ) {
			common->FatalError( "R_FrameAlloc: Mem_Alloc16() failed" );
		}
		block->size = size;
		block->used = 0;
		block->next = NULL;
		frame->alloc->next = block;
	}

	// we could fix this if we needed to...
	if ( bytes > block->size ) {
		common->FatalError( "R_FrameAlloc of %i exceeded MEMORY_BLOCK_SIZE",
			bytes );
	}

	frame->alloc = block;

	block->used = bytes;

	return block->base;
}

/*
==================
R_ClearedFrameAlloc
==================
*/
void *R_ClearedFrameAlloc( int bytes ) {
	void	*r;

	r = R_FrameAlloc( bytes );
	SIMDProcessor->Memset( r, 0, bytes );
	return r;
}


/*
==================
R_FrameFree

This does nothing at all, as the frame data is reused every frame
and can only be stack allocated.

The only reason for it's existance is so functions that can
use either static or frame memory can set function pointers
to both alloc and free.
==================
*/
void R_FrameFree( void *data ) {
}



//==========================================================================



// FIXME: these assume no skewing or scaling transforms








// transform Z in eye coordinates to window coordinates
void R_TransformEyeZToWin( float src_z, const float *projectionMatrix, float &dst_z ) {
	float clip_z, clip_w;

	// projection
	clip_z = src_z * projectionMatrix[ 2 + 2 * 4 ] + projectionMatrix[ 2 + 3 * 4 ];
	clip_w = src_z * projectionMatrix[ 3 + 2 * 4 ] + projectionMatrix[ 3 + 3 * 4 ];

	if ( clip_w <= 0.0f ) {
		dst_z = 0.0f;					// clamp to near plane
	} else {
		dst_z = clip_z / clip_w;
		dst_z = dst_z * 0.5f + 0.5f;	// convert to window coords
	}
}

/*
=================
R_RadiusCullLocalBox

A fast, conservative center-to-corner culling test
Returns true if the box is outside the given global frustum, (positive sides are out)
=================
*/
bool R_RadiusCullLocalBox( const idBounds &bounds, const float modelMatrix[16], int numPlanes, const idPlane *planes ) {
	int			i;
	float		d;
	idVec3		worldOrigin;
	float		worldRadius;
	const idPlane	*frust;

	if ( r_useCulling.GetInteger() == 0 ) {
		return false;
	}

	// transform the surface bounds into world space
	idVec3	localOrigin = ( bounds[0] + bounds[1] ) * 0.5;

	R_LocalPointToGlobal( modelMatrix, localOrigin, worldOrigin );

	worldRadius = (bounds[0] - localOrigin).Length();	// FIXME: won't be correct for scaled objects

	for ( i = 0 ; i < numPlanes ; i++ ) {
		frust = planes + i;
		d = frust->Distance( worldOrigin );
		if ( d > worldRadius ) {
			return true;	// culled
		}
	}

	return false;		// no culled
}

/*
=================
R_CornerCullLocalBox

Tests all corners against the frustum.
Can still generate a few false positives when the box is outside a corner.
Returns true if the box is outside the given global frustum, (positive sides are out)
=================
*/
bool R_CornerCullLocalBox( const idBounds &bounds, const float modelMatrix[16], int numPlanes, const idPlane *planes ) {
	int			i, j;
	idVec3		transformed[8];
	float		dists[8];
	idVec3		v;
	const idPlane *frust;

	// we can disable box culling for experimental timing purposes
	if ( r_useCulling.GetInteger() < 2 ) {
		return false;
	}

	// transform into world space
	for ( i = 0 ; i < 8 ; i++ ) {
		v[0] = bounds[i&1][0];
		v[1] = bounds[(i>>1)&1][1];
		v[2] = bounds[(i>>2)&1][2];

		R_LocalPointToGlobal( modelMatrix, v, transformed[i] );
	}

	// check against frustum planes
	for ( i = 0 ; i < numPlanes ; i++ ) {
		frust = planes + i;
		for ( j = 0 ; j < 8 ; j++ ) {
			dists[j] = frust->Distance( transformed[j] );
			if ( dists[j] < 0 ) {
				break;
			}
		}
		if ( j == 8 ) {
			// all points were behind one of the planes
			tr.pc.c_box_cull_out++;
			return true;
		}
	}

	tr.pc.c_box_cull_in++;

	return false;		// not culled
}

/*
=================
R_CullLocalBox

Performs quick test before expensive test
Returns true if the box is outside the given global frustum, (positive sides are out)
=================
*/
bool R_CullLocalBox( const idBounds &bounds, const float modelMatrix[16], int numPlanes, const idPlane *planes ) {
	if ( R_RadiusCullLocalBox( bounds, modelMatrix, numPlanes, planes ) ) {
		return true;
	}
	return R_CornerCullLocalBox( bounds, modelMatrix, numPlanes, planes );
}

/*
==========================
R_TransformModelToClip
==========================
*/
void R_TransformModelToClip( const idVec3 &src, const float *modelMatrix, const float *projectionMatrix, idPlane &eye, idPlane &dst ) {
	int i;

	for ( i = 0 ; i < 4 ; i++ ) {
		eye[i] = 
			src[0] * modelMatrix[ i + 0 * 4 ] +
			src[1] * modelMatrix[ i + 1 * 4 ] +
			src[2] * modelMatrix[ i + 2 * 4 ] +
			1 * modelMatrix[ i + 3 * 4 ];
	}

	for ( i = 0 ; i < 4 ; i++ ) {
		dst[i] = 
			eye[0] * projectionMatrix[ i + 0 * 4 ] +
			eye[1] * projectionMatrix[ i + 1 * 4 ] +
			eye[2] * projectionMatrix[ i + 2 * 4 ] +
			eye[3] * projectionMatrix[ i + 3 * 4 ];
	}
}

/*
==========================
R_GlobalToNormalizedDeviceCoordinates

-1 to 1 range in x, y, and z
==========================
*/
void R_GlobalToNormalizedDeviceCoordinates( const idVec3 &global, idVec3 &ndc ) {
	int		i;
	idPlane	view;
	idPlane	clip;

	// _D3XP added work on primaryView when no viewDef
	if ( !tr.viewDef ) {

		for ( i = 0 ; i < 4 ; i ++ ) {
			view[i] = 
				global[0] * tr.primaryView->worldSpace.modelViewMatrix[ i + 0 * 4 ] +
				global[1] * tr.primaryView->worldSpace.modelViewMatrix[ i + 1 * 4 ] +
				global[2] * tr.primaryView->worldSpace.modelViewMatrix[ i + 2 * 4 ] +
					tr.primaryView->worldSpace.modelViewMatrix[ i + 3 * 4 ];
		}

		for ( i = 0 ; i < 4 ; i ++ ) {
			clip[i] = 
				view[0] * tr.primaryView->projectionMatrix[ i + 0 * 4 ] +
				view[1] * tr.primaryView->projectionMatrix[ i + 1 * 4 ] +
				view[2] * tr.primaryView->projectionMatrix[ i + 2 * 4 ] +
				view[3] * tr.primaryView->projectionMatrix[ i + 3 * 4 ];
		}

	} else {

		for ( i = 0 ; i < 4 ; i ++ ) {
			view[i] = 
				global[0] * tr.viewDef->worldSpace.modelViewMatrix[ i + 0 * 4 ] +
				global[1] * tr.viewDef->worldSpace.modelViewMatrix[ i + 1 * 4 ] +
				global[2] * tr.viewDef->worldSpace.modelViewMatrix[ i + 2 * 4 ] +
				tr.viewDef->worldSpace.modelViewMatrix[ i + 3 * 4 ];
		}


		for ( i = 0 ; i < 4 ; i ++ ) {
			clip[i] = 
				view[0] * tr.viewDef->projectionMatrix[ i + 0 * 4 ] +
				view[1] * tr.viewDef->projectionMatrix[ i + 1 * 4 ] +
				view[2] * tr.viewDef->projectionMatrix[ i + 2 * 4 ] +
				view[3] * tr.viewDef->projectionMatrix[ i + 3 * 4 ];
		}

	}

	ndc[0] = clip[0] / clip[3];
	ndc[1] = clip[1] / clip[3];
	ndc[2] = ( clip[2] + clip[3] ) / ( 2 * clip[3] );
}

/*
==========================
R_TransformClipToDevice

Clip to normalized device coordinates
==========================
*/
void R_TransformClipToDevice( const idPlane &clip, const viewDef_t *view, idVec3 &normalized ) {
	normalized[0] = clip[0] / clip[3];
	normalized[1] = clip[1] / clip[3];
	normalized[2] = clip[2] / clip[3];
}


/*
==========================
myGlMultMatrix
==========================
*/
void myGlMultMatrix( const float a[16], const float b[16], float out[16] ) {
#if 0
	int		i, j;

	for ( i = 0 ; i < 4 ; i++ ) {
		for ( j = 0 ; j < 4 ; j++ ) {
			out[ i * 4 + j ] =
				a [ i * 4 + 0 ] * b [ 0 * 4 + j ]
				+ a [ i * 4 + 1 ] * b [ 1 * 4 + j ]
				+ a [ i * 4 + 2 ] * b [ 2 * 4 + j ]
				+ a [ i * 4 + 3 ] * b [ 3 * 4 + j ];
		}
	}
#else
	out[0*4+0] = a[0*4+0]*b[0*4+0] + a[0*4+1]*b[1*4+0] + a[0*4+2]*b[2*4+0] + a[0*4+3]*b[3*4+0];
	out[0*4+1] = a[0*4+0]*b[0*4+1] + a[0*4+1]*b[1*4+1] + a[0*4+2]*b[2*4+1] + a[0*4+3]*b[3*4+1];
	out[0*4+2] = a[0*4+0]*b[0*4+2] + a[0*4+1]*b[1*4+2] + a[0*4+2]*b[2*4+2] + a[0*4+3]*b[3*4+2];
	out[0*4+3] = a[0*4+0]*b[0*4+3] + a[0*4+1]*b[1*4+3] + a[0*4+2]*b[2*4+3] + a[0*4+3]*b[3*4+3];
	out[1*4+0] = a[1*4+0]*b[0*4+0] + a[1*4+1]*b[1*4+0] + a[1*4+2]*b[2*4+0] + a[1*4+3]*b[3*4+0];
	out[1*4+1] = a[1*4+0]*b[0*4+1] + a[1*4+1]*b[1*4+1] + a[1*4+2]*b[2*4+1] + a[1*4+3]*b[3*4+1];
	out[1*4+2] = a[1*4+0]*b[0*4+2] + a[1*4+1]*b[1*4+2] + a[1*4+2]*b[2*4+2] + a[1*4+3]*b[3*4+2];
	out[1*4+3] = a[1*4+0]*b[0*4+3] + a[1*4+1]*b[1*4+3] + a[1*4+2]*b[2*4+3] + a[1*4+3]*b[3*4+3];
	out[2*4+0] = a[2*4+0]*b[0*4+0] + a[2*4+1]*b[1*4+0] + a[2*4+2]*b[2*4+0] + a[2*4+3]*b[3*4+0];
	out[2*4+1] = a[2*4+0]*b[0*4+1] + a[2*4+1]*b[1*4+1] + a[2*4+2]*b[2*4+1] + a[2*4+3]*b[3*4+1];
	out[2*4+2] = a[2*4+0]*b[0*4+2] + a[2*4+1]*b[1*4+2] + a[2*4+2]*b[2*4+2] + a[2*4+3]*b[3*4+2];
	out[2*4+3] = a[2*4+0]*b[0*4+3] + a[2*4+1]*b[1*4+3] + a[2*4+2]*b[2*4+3] + a[2*4+3]*b[3*4+3];
	out[3*4+0] = a[3*4+0]*b[0*4+0] + a[3*4+1]*b[1*4+0] + a[3*4+2]*b[2*4+0] + a[3*4+3]*b[3*4+0];
	out[3*4+1] = a[3*4+0]*b[0*4+1] + a[3*4+1]*b[1*4+1] + a[3*4+2]*b[2*4+1] + a[3*4+3]*b[3*4+1];
	out[3*4+2] = a[3*4+0]*b[0*4+2] + a[3*4+1]*b[1*4+2] + a[3*4+2]*b[2*4+2] + a[3*4+3]*b[3*4+2];
	out[3*4+3] = a[3*4+0]*b[0*4+3] + a[3*4+1]*b[1*4+3] + a[3*4+2]*b[2*4+3] + a[3*4+3]*b[3*4+3];
#endif
}

/*
================
R_TransposeGLMatrix
================
*/
void R_TransposeGLMatrix( const float in[16], float out[16] ) {
	int		i, j;

	for ( i = 0 ; i < 4 ; i++ ) {
		for ( j = 0 ; j < 4 ; j++ ) {
			out[i*4+j] = in[j*4+i];
		}
	}
}

bool tr_levelshotProjectionShiftActive = false;
float tr_levelshotProjectionShiftX = 0.0f;
float tr_levelshotProjectionShiftY = 0.0f;

/*
=================
R_SetViewMatrix

Sets up the world to view matrix for a given viewParm
=================
*/
void R_SetViewMatrix( viewDef_t *viewDef ) {
	idVec3	origin;
	viewEntity_t *world;
	float	viewerMatrix[16];
	static float	s_flipMatrix[16] = {
		// convert from our coordinate system (looking down X)
		// to OpenGL's coordinate system (looking down -Z)
		0, 0, -1, 0,
		-1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 0, 1
	};

	world = &viewDef->worldSpace;

	memset( world, 0, sizeof(*world) );

	// the model matrix is an identity
	world->modelMatrix[0*4+0] = 1;
	world->modelMatrix[1*4+1] = 1;
	world->modelMatrix[2*4+2] = 1;

	// transform by the camera placement
	origin = viewDef->renderView.vieworg;

	viewerMatrix[0] = viewDef->renderView.viewaxis[0][0];
	viewerMatrix[4] = viewDef->renderView.viewaxis[0][1];
	viewerMatrix[8] = viewDef->renderView.viewaxis[0][2];
	viewerMatrix[12] = -origin[0] * viewerMatrix[0] + -origin[1] * viewerMatrix[4] + -origin[2] * viewerMatrix[8];

	viewerMatrix[1] = viewDef->renderView.viewaxis[1][0];
	viewerMatrix[5] = viewDef->renderView.viewaxis[1][1];
	viewerMatrix[9] = viewDef->renderView.viewaxis[1][2];
	viewerMatrix[13] = -origin[0] * viewerMatrix[1] + -origin[1] * viewerMatrix[5] + -origin[2] * viewerMatrix[9];

	viewerMatrix[2] = viewDef->renderView.viewaxis[2][0];
	viewerMatrix[6] = viewDef->renderView.viewaxis[2][1];
	viewerMatrix[10] = viewDef->renderView.viewaxis[2][2];
	viewerMatrix[14] = -origin[0] * viewerMatrix[2] + -origin[1] * viewerMatrix[6] + -origin[2] * viewerMatrix[10];

	viewerMatrix[3] = 0;
	viewerMatrix[7] = 0;
	viewerMatrix[11] = 0;
	viewerMatrix[15] = 1;

	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	myGlMultMatrix( viewerMatrix, s_flipMatrix, world->modelViewMatrix );
}

/*
===============
R_SetupProjection

This uses the "infinite far z" trick
===============
*/
void R_SetupProjection( void ) {
	float	xmin, xmax, ymin, ymax;
	float	width, height;
	float	zNear;
	float	jitterx, jittery;
	static	idRandom random;

	R_TemporalPresentation_PrepareView( tr.viewDef );

	// Temporal AA owns a deterministic, backend-neutral sub-pixel sequence.
	// The legacy random path remains for multi-frame screenshot accumulation.
	if ( tr.viewDef->temporalJitterEnabled ) {
		jitterx = tr.viewDef->temporalJitterPixels.x;
		jittery = tr.viewDef->temporalJitterPixels.y;
	} else
	// random jittering is usefull when multiple
	// frames are going to be blended together
	// for motion blurred anti-aliasing
	if ( r_jitter.GetBool() ) {
		jitterx = random.RandomFloat();
		jittery = random.RandomFloat();
	} else {
		jitterx = jittery = 0;
	}

	//
	// set up projection matrix
	//
	zNear	= r_znear.GetFloat();
	if ( tr.viewDef->renderView.cramZNear ) {
		zNear *= 0.25;
	}

	ymax = zNear * tan( tr.viewDef->renderView.fov_y * idMath::PI / 360.0f );
	ymin = -ymax;

	xmax = zNear * tan( tr.viewDef->renderView.fov_x * idMath::PI / 360.0f );
	xmin = -xmax;

	width = xmax - xmin;
	height = ymax - ymin;

	if ( tr_levelshotProjectionShiftActive ) {
		const float xShift = 0.5f * width * tr_levelshotProjectionShiftX;
		const float yShift = 0.5f * height * tr_levelshotProjectionShiftY;
		xmin += xShift;
		xmax += xShift;
		ymin += yShift;
		ymax += yShift;
	}

	jitterx = jitterx * width / ( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 + 1 );
	xmin += jitterx;
	xmax += jitterx;
	jittery = jittery * height / ( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 + 1 );
	ymin += jittery;
	ymax += jittery;

	tr.viewDef->projectionMatrix[0] = 2 * zNear / width;
	tr.viewDef->projectionMatrix[4] = 0;
	tr.viewDef->projectionMatrix[8] = ( xmax + xmin ) / width;	// normally 0
	tr.viewDef->projectionMatrix[12] = 0;

	tr.viewDef->projectionMatrix[1] = 0;
	tr.viewDef->projectionMatrix[5] = 2 * zNear / height;
	tr.viewDef->projectionMatrix[9] = ( ymax + ymin ) / height;	// normally 0
	tr.viewDef->projectionMatrix[13] = 0;

	// this is the far-plane-at-infinity formulation, and
	// crunches the Z range slightly so w=0 vertexes do not
	// rasterize right at the wraparound point
	tr.viewDef->projectionMatrix[2] = 0;
	tr.viewDef->projectionMatrix[6] = 0;
	tr.viewDef->projectionMatrix[10] = -0.999f;
	tr.viewDef->projectionMatrix[14] = -2.0f * zNear;

	tr.viewDef->projectionMatrix[3] = 0;
	tr.viewDef->projectionMatrix[7] = 0;
	tr.viewDef->projectionMatrix[11] = -1;
	tr.viewDef->projectionMatrix[15] = 0;

	R_TemporalPresentation_FinalizeViewProjection( tr.viewDef );
}

static void R_GetViewFrustumExtents( float &zNear, float &xmin, float &xmax, float &ymin, float &ymax ) {
	zNear = r_znear.GetFloat();
	if ( tr.viewDef->renderView.cramZNear ) {
		zNear *= 0.25f;
	}

	ymax = zNear * tan( tr.viewDef->renderView.fov_y * idMath::PI / 360.0f );
	ymin = -ymax;

	xmax = zNear * tan( tr.viewDef->renderView.fov_x * idMath::PI / 360.0f );
	xmin = -xmax;

	if ( tr_levelshotProjectionShiftActive ) {
		const float width = xmax - xmin;
		const float height = ymax - ymin;
		const float xShift = 0.5f * width * tr_levelshotProjectionShiftX;
		const float yShift = 0.5f * height * tr_levelshotProjectionShiftY;
		xmin += xShift;
		xmax += xShift;
		ymin += yShift;
		ymax += yShift;
	}
}

/*
=================
R_SetupViewFrustum

Setup that culling frustum planes for the current view
FIXME: derive from modelview matrix times projection matrix
=================
*/
static void R_SetupViewFrustum( void ) {
	int		i;
	float	zNear;
	float	xmin, xmax, ymin, ymax;
	float	xs, xc;
	float	ang;

	if ( tr_levelshotProjectionShiftActive ) {
		const idVec3 &forward = tr.viewDef->renderView.viewaxis[0];
		const idVec3 &horizontal = tr.viewDef->renderView.viewaxis[1];
		const idVec3 &vertical = tr.viewDef->renderView.viewaxis[2];

		R_GetViewFrustumExtents( zNear, xmin, xmax, ymin, ymax );

		// Off-axis levelshot tiles need frustum planes that match the shifted
		// projection. Keep this branch isolated so ordinary gameplay stays on the
		// stock frustum setup. The horizontal pair carries the s_flipMatrix sign
		// flip (engine left -> GL -x); the vertical axis maps to GL +y unflipped,
		// so its planes take the opposite extents.
		tr.viewDef->frustum[0] = xmax * forward + zNear * horizontal;
		tr.viewDef->frustum[1] = -xmin * forward - zNear * horizontal;
		tr.viewDef->frustum[2] = -ymin * forward + zNear * vertical;
		tr.viewDef->frustum[3] = ymax * forward - zNear * vertical;

		// plane four is the front clipping plane
		tr.viewDef->frustum[4] = /* vec3_origin - */ forward;
	} else {
		ang = DEG2RAD( tr.viewDef->renderView.fov_x ) * 0.5f;
		idMath::SinCos( ang, xs, xc );

		tr.viewDef->frustum[0] = xs * tr.viewDef->renderView.viewaxis[0] + xc * tr.viewDef->renderView.viewaxis[1];
		tr.viewDef->frustum[1] = xs * tr.viewDef->renderView.viewaxis[0] - xc * tr.viewDef->renderView.viewaxis[1];

		ang = DEG2RAD( tr.viewDef->renderView.fov_y ) * 0.5f;
		idMath::SinCos( ang, xs, xc );

		tr.viewDef->frustum[2] = xs * tr.viewDef->renderView.viewaxis[0] + xc * tr.viewDef->renderView.viewaxis[2];
		tr.viewDef->frustum[3] = xs * tr.viewDef->renderView.viewaxis[0] - xc * tr.viewDef->renderView.viewaxis[2];

		// plane four is the front clipping plane
		tr.viewDef->frustum[4] = /* vec3_origin - */ tr.viewDef->renderView.viewaxis[0];
	}

	for ( i = 0; i < 5; i++ ) {
		// flip direction so positive side faces out (FIXME: globally unify this)
		tr.viewDef->frustum[i] = -tr.viewDef->frustum[i].Normal();
		tr.viewDef->frustum[i][3] = -( tr.viewDef->renderView.vieworg * tr.viewDef->frustum[i].Normal() );
	}

	// eventually, plane five will be the rear clipping plane for fog

	float dNear, dFar, dLeft, dUp;

	dNear = r_znear.GetFloat();
	if ( tr.viewDef->renderView.cramZNear ) {
		dNear *= 0.25f;
	}
	dFar = MAX_WORLD_SIZE;
	if ( tr_levelshotProjectionShiftActive ) {
		dLeft = dFar * Max( idMath::Fabs( xmin ), idMath::Fabs( xmax ) ) / Max( dNear, idMath::FLOAT_EPSILON );
		dUp = dFar * Max( idMath::Fabs( ymin ), idMath::Fabs( ymax ) ) / Max( dNear, idMath::FLOAT_EPSILON );
	} else {
		dLeft = dFar * tan( DEG2RAD( tr.viewDef->renderView.fov_x * 0.5f ) );
		dUp = dFar * tan( DEG2RAD( tr.viewDef->renderView.fov_y * 0.5f ) );
	}
	tr.viewDef->viewFrustum.SetOrigin( tr.viewDef->renderView.vieworg );
	tr.viewDef->viewFrustum.SetAxis( tr.viewDef->renderView.viewaxis );
	tr.viewDef->viewFrustum.SetSize( dNear, dFar, dLeft, dUp );
}

/*
===================
R_ConstrainViewFrustum
===================
*/
static void R_ConstrainViewFrustum( void ) {
	idBounds bounds;

	// constrain the view frustum to the total bounds of all visible lights and visible entities
	bounds.Clear();
	for ( viewLight_t *vLight = tr.viewDef->viewLights; vLight; vLight = vLight->next ) {
		bounds.AddBounds( vLight->lightDef->frustumTris->bounds );
	}
	for ( viewEntity_t *vEntity = tr.viewDef->viewEntitys; vEntity; vEntity = vEntity->next ) {
		bounds.AddBounds( vEntity->entityDef->referenceBounds );
	}
	tr.viewDef->viewFrustum.ConstrainToBounds( bounds );

	if ( r_useFrustumFarDistance.GetFloat() > 0.0f ) {
		tr.viewDef->viewFrustum.MoveFarDistance( r_useFrustumFarDistance.GetFloat() );
	}
}

/*
==========================================================================================

DRAWSURF SORTING

==========================================================================================
*/


/*
=================
R_SortDrawSurfs

Sorts the drawsurfs by sort type, then orientation, then shader.

Replaces the indirect-call CRT qsort with a stable LSD radix sort on the
IEEE-754 bit pattern of the float sort key (small lists use an insertion
sort). This runs every view at presentation rate, so the constant factor
matters; the radix passes also skip bytes that are constant across all keys,
which covers the exponent bytes of typical material sort values.
=================
*/
typedef struct {
	unsigned int	key;
	drawSurf_t *	surf;
} sortRecord_t;

static void R_SortDrawSurfs( void ) {
	const int count = tr.viewDef->numDrawSurfs;
	drawSurf_t **surfs = tr.viewDef->drawSurfs;

	if ( count < 2 ) {
		return;
	}

	// small per-view lists: insertion sort beats the radix setup cost
	if ( count <= 64 ) {
		for ( int i = 1; i < count; i++ ) {
			drawSurf_t *s = surfs[i];
			const float key = s->sort;
			int j = i - 1;
			while ( j >= 0 && surfs[j]->sort > key ) {
				surfs[j + 1] = surfs[j];
				j--;
			}
			surfs[j + 1] = s;
		}
		return;
	}

	sortRecord_t *a = (sortRecord_t *)R_FrameAlloc( count * sizeof( sortRecord_t ) );
	sortRecord_t *b = (sortRecord_t *)R_FrameAlloc( count * sizeof( sortRecord_t ) );

	// map the float keys to unsigned ints with the same ordering:
	// flip all bits of negatives, flip only the sign bit of positives
	for ( int i = 0; i < count; i++ ) {
		unsigned int bits;
		memcpy( &bits, &surfs[i]->sort, sizeof( bits ) );
		bits ^= ( bits & 0x80000000u ) ? 0xFFFFFFFFu : 0x80000000u;
		a[i].key = bits;
		a[i].surf = surfs[i];
	}

	for ( int shift = 0; shift < 32; shift += 8 ) {
		int hist[256] = { 0 };
		for ( int i = 0; i < count; i++ ) {
			hist[( a[i].key >> shift ) & 255]++;
		}
		// all keys share this byte, nothing to reorder
		if ( hist[( a[0].key >> shift ) & 255] == count ) {
			continue;
		}
		int offset = 0;
		for ( int j = 0; j < 256; j++ ) {
			const int bucketCount = hist[j];
			hist[j] = offset;
			offset += bucketCount;
		}
		for ( int i = 0; i < count; i++ ) {
			b[hist[( a[i].key >> shift ) & 255]++] = a[i];
		}
		sortRecord_t *swap = a;
		a = b;
		b = swap;
	}

	for ( int i = 0; i < count; i++ ) {
		surfs[i] = a[i].surf;
	}
}

static int R_CountViewEntityList( const viewEntity_t *viewEntity ) {
	int count = 0;
	for ( const viewEntity_t *current = viewEntity; current != NULL; current = current->next ) {
		count++;
	}
	return count;
}

static int R_CountViewLightList( const viewLight_t *viewLight ) {
	int count = 0;
	for ( const viewLight_t *current = viewLight; current != NULL; current = current->next ) {
		count++;
	}
	return count;
}



//========================================================================


//==============================================================================



/*
================
R_RenderView

A view may be either the actual camera view,
a mirror / remote location, or a 3D view on a gui surface.

Parms will typically be allocated with R_FrameAlloc
================
*/
void R_RenderView( viewDef_t *parms ) {
	viewDef_t		*oldView;

	if ( parms->renderView.width <= 0 || parms->renderView.height <= 0 ) {
		return;
	}

	tr.viewCount++;

	// save view in case we are a subview
	oldView = tr.viewDef;

	tr.viewDef = parms;

	tr.sortOffset = 0;

	const int viewBuildReportMode = r_showViewBuildTimes.GetInteger();
	const int viewBuildInterval = Max( 1, r_showViewBuildTimesInterval.GetInteger() );
	const bool reportViewBuildTimes =
		viewBuildReportMode > 0 &&
		( viewBuildReportMode >= 2 || parms->renderView.viewID >= 0 ) &&
		( tr.frameCount % viewBuildInterval ) == 0;
	int viewBuildTotalStart = 0;
	int viewBuildLastMark = 0;
	int viewBuildMatrixMsec = 0;
	int viewBuildFrustumMsec = 0;
	int viewBuildProjectionMsec = 0;
	int viewBuildVisibilityMsec = 0;
	int viewBuildConstrainMsec = 0;
	int viewBuildLightMsec = 0;
	int viewBuildModelMsec = 0;
	int viewBuildEffectMsec = 0;
	int viewBuildPruneMsec = 0;
	int viewBuildSortMsec = 0;
	int viewBuildSubviewMsec = 0;
	int viewBuildPacketMsec = 0;
	int viewBuildCommandMsec = 0;

	if ( reportViewBuildTimes ) {
		viewBuildTotalStart = Sys_Milliseconds();
		viewBuildLastMark = viewBuildTotalStart;
	}

	// set the matrix for world space to eye space
	R_SetViewMatrix( tr.viewDef );
	if ( reportViewBuildTimes ) {
		const int now = Sys_Milliseconds();
		viewBuildMatrixMsec = now - viewBuildLastMark;
		viewBuildLastMark = now;
	}

	// the four sides of the view frustum are needed
	// for culling and portal visibility
	R_SetupViewFrustum();
	if ( reportViewBuildTimes ) {
		const int now = Sys_Milliseconds();
		viewBuildFrustumMsec = now - viewBuildLastMark;
		viewBuildLastMark = now;
	}

	// we need to set the projection matrix before doing
	// portal-to-screen scissor box calculations
	R_SetupProjection();
	if ( reportViewBuildTimes ) {
		const int now = Sys_Milliseconds();
		viewBuildProjectionMsec = now - viewBuildLastMark;
		viewBuildLastMark = now;
	}

	// drawSurf->area only feeds light-grid eligibility checks (legacy indirect
	// pass and scene-packet capture), all of which reject every surface when no
	// usable grid exists; skip the per-surface PointInArea BSP descent in
	// R_AddDrawSurf whenever no light grid is available.
	parms->skipDrawSurfAreaResolve =
		parms->renderWorld == NULL
		|| !static_cast<idRenderWorldLocal *>( parms->renderWorld )->AnyLightGridAvailable();

	// identify all the visible portalAreas, and the entityDefs and
	// lightDefs that are in them and pass culling.
	const int visibilityStart = Sys_Milliseconds();
	static_cast<idRenderWorldLocal *>(parms->renderWorld)->FindViewLightsAndEntities();
	viewBuildVisibilityMsec = Sys_Milliseconds() - visibilityStart;
	R_RendererMetrics_AddVisibilityMsec( viewBuildVisibilityMsec );
	if ( reportViewBuildTimes ) {
		viewBuildLastMark = Sys_Milliseconds();
	}

	// constrain the view frustum to the view lights and entities
	R_ConstrainViewFrustum();
	if ( reportViewBuildTimes ) {
		const int now = Sys_Milliseconds();
		viewBuildConstrainMsec = now - viewBuildLastMark;
		viewBuildLastMark = now;
	}

	// make sure that interactions exist for all light / entity combinations
	// that are visible
	// add any pre-generated light shadows, and calculate the light shader values
	R_AddLightSurfaces();
	if ( reportViewBuildTimes ) {
		const int now = Sys_Milliseconds();
		viewBuildLightMsec = now - viewBuildLastMark;
		viewBuildLastMark = now;
	}

	// adds ambient surfaces and create any necessary interaction surfaces to add to the light
	// lists
	R_AddModelSurfaces();
	if ( reportViewBuildTimes ) {
		const int now = Sys_Milliseconds();
		viewBuildModelMsec = now - viewBuildLastMark;
		viewBuildLastMark = now;
	}

	// submit dynamic effect surfaces after the model pass has initialized drawSurf lists
	R_AddEffectSurfaces();
	if ( reportViewBuildTimes ) {
		const int now = Sys_Milliseconds();
		viewBuildEffectMsec = now - viewBuildLastMark;
		viewBuildLastMark = now;
	}

	// Force in the entities that want an outline wherever they are, now that every
	// pass which walks viewEntitys or builds interactions has run. Their surfaces
	// land on their own list, so R_SortDrawSurfs below never sees them either.
	R_AddThroughWorldOutlines();

	// any viewLight that didn't have visible surfaces can have it's shadows removed
	R_RemoveUnecessaryViewLights();
	if ( reportViewBuildTimes ) {
		const int now = Sys_Milliseconds();
		viewBuildPruneMsec = now - viewBuildLastMark;
		viewBuildLastMark = now;
	}

	// sort all the ambient surfaces for translucency ordering
	R_SortDrawSurfs();
	if ( reportViewBuildTimes ) {
		const int now = Sys_Milliseconds();
		viewBuildSortMsec = now - viewBuildLastMark;
		viewBuildLastMark = now;
	}

	// generate any subviews (mirrors, cameras, etc) before adding this view
	if ( R_GenerateSubViews() ) {
		if ( reportViewBuildTimes ) {
			const int now = Sys_Milliseconds();
			viewBuildSubviewMsec = now - viewBuildLastMark;
			viewBuildLastMark = now;
		}
		// if we are debugging subviews, allow the skipping of the
		// main view draw
		if ( r_subviewOnly.GetBool() ) {
			return;
		}
	} else if ( reportViewBuildTimes ) {
		const int now = Sys_Milliseconds();
		viewBuildSubviewMsec = now - viewBuildLastMark;
		viewBuildLastMark = now;
	}

	// write everything needed to the demo file
	if ( session->writeDemo ) {
		static_cast<idRenderWorldLocal *>(parms->renderWorld)->WriteVisibleDefs( tr.viewDef );
	}

	if ( R_ScenePackets_FrontEndCaptureRequired() ) {
		const int packetBuildStart = Sys_Milliseconds();
		R_ScenePackets_AddRenderView( parms );
		viewBuildPacketMsec = Sys_Milliseconds() - packetBuildStart;
		R_RendererMetrics_AddPacketBuildMsec( viewBuildPacketMsec );
		if ( reportViewBuildTimes ) {
			viewBuildLastMark = Sys_Milliseconds();
		}
	}
	// Raven's MedLabs / Alpha Labs controllers capture their source data
	// immediately before the matching view draw, then composite from within
	// that view. Keep the command adjacent so both renderer backends consume
	// the same viewDef lifetime and ordering contract.
	R_AddSpecialEffects( parms );
	R_AddDrawViewCmd( parms );
	if ( reportViewBuildTimes ) {
		const int now = Sys_Milliseconds();
		viewBuildCommandMsec = now - viewBuildLastMark;
		common->Printf(
			"viewBuild frame=%d viewID=%d subview=%d surfs=%d entities=%d lights=%d matrix=%d frustum=%d projection=%d visibility=%d constrain=%d light=%d model=%d effects=%d prune=%d sort=%d subviews=%d packet=%d command=%d total=%d\n",
			tr.frameCount,
			parms->renderView.viewID,
			parms->isSubview ? 1 : 0,
			parms->numDrawSurfs,
			R_CountViewEntityList( parms->viewEntitys ),
			R_CountViewLightList( parms->viewLights ),
			viewBuildMatrixMsec,
			viewBuildFrustumMsec,
			viewBuildProjectionMsec,
			viewBuildVisibilityMsec,
			viewBuildConstrainMsec,
			viewBuildLightMsec,
			viewBuildModelMsec,
			viewBuildEffectMsec,
			viewBuildPruneMsec,
			viewBuildSortMsec,
			viewBuildSubviewMsec,
			viewBuildPacketMsec,
			viewBuildCommandMsec,
			now - viewBuildTotalStart );
	}

	// restore view in case we are a subview
	tr.viewDef = oldView;
}


// frame-memory-coupled triangle-surface maintenance stays renderer-side;
// the allocators live in the shared render_geo library

/*
===============
R_PurgeTriSurfData
===============
*/
void R_PurgeTriSurfData( frameData_t *frame ) {
	// free deferred triangle surfaces
	R_FreeDeferredTriSurfs( frame );

	// free empty base blocks (allocators live in the render_geo library)
	RenderGeo_FreeEmptyBaseBlocks();
}

/*
==================
R_FreeDeferredTriSurfs
==================
*/
void R_FreeDeferredTriSurfs( frameData_t *frame ) {
	srfTriangles_t	*tri, *next;

	if ( !frame ) {
		return;
	}

	for ( tri = frame->firstDeferredFreeTriSurf; tri; tri = next ) {
		next = tri->nextDeferredFree;

		if ( tri->ambientSurface == NULL ) {
			continue;
		}

		if ( tri->topAmbientSurface != NULL
			&& tri->verts == tri->topAmbientSurface->verts
			&& tri->topAmbientSurface->referenceCount > 0 ) {
			--tri->topAmbientSurface->referenceCount;
			tri->topAmbientSurface = NULL;
		}
	}

	for ( tri = frame->firstDeferredFreeTriSurf; tri; tri = next ) {
		next = tri->nextDeferredFree;
		R_ReallyFreeStaticTriSurf( tri );
	}

	frame->firstDeferredFreeTriSurf = NULL;
	frame->lastDeferredFreeTriSurf = NULL;
}
