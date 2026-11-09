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


typedef struct {
	idVec3		origin;
	idMat3		axis;
} orientation_t;

static ID_INLINE bool R_GetSubviewVertexPosition( const srfTriangles_t *tri, int index, idVec3 &position ) {
	if ( tri == NULL || index < 0 || index >= tri->numVerts ) {
		position.Zero();
		return false;
	}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->silTraceVerts != NULL ) {
		position = reinterpret_cast<const rvSilTraceVertT *>( tri->silTraceVerts )[index].xyzw.ToVec3();
		return true;
	}
#endif

	if ( tri->verts == NULL ) {
		position.Zero();
		return false;
	}

	position = tri->verts[index].xyz;
	return true;
}

static ID_INLINE void R_SyncSubviewTime( viewDef_t *parms ) {
	parms->floatTime = parms->renderView.time * 0.001f;
}

// A subview starts as a copy of its parent view, temporal metadata included.
// Inheriting temporalPrepared made R_TemporalPresentation_PrepareView skip the
// subview, so mirrors, x-ray, remote and refraction views were drawn with the
// parent's temporal AA jitter; on Vulkan their direct render then retired the
// main view's history every frame. Clear it so the subview's own projection
// setup prepares it, unjittered, under its own identity (REN-04).
static void R_ResetSubviewTemporalState( viewDef_t *parms ) {
	parms->temporalPrepared = false;
	parms->temporalJitterEnabled = false;
	parms->temporalJitterPixels.Zero();
	parms->temporalHistoryValid = false;
	parms->temporalPreviousProjectionValid = false;
	parms->temporalNeutralProjectionValid = false;
}


/*
=================
R_MirrorPoint
=================
*/
static void R_MirrorPoint( const idVec3 in, orientation_t *surface, orientation_t *camera, idVec3 &out ) {
	int		i;
	idVec3	local;
	idVec3	transformed;
	float	d;

	local = in - surface->origin;

	transformed = vec3_origin;
	for ( i = 0 ; i < 3 ; i++ ) {
		d = local * surface->axis[i];
		transformed += d * camera->axis[i];
	}

	out = transformed + camera->origin;
}

/*
=================
R_MirrorVector
=================
*/
static void R_MirrorVector( const idVec3 in, orientation_t *surface, orientation_t *camera, idVec3 &out ) {
	int		i;
	float	d;

	out = vec3_origin;
	for ( i = 0 ; i < 3 ; i++ ) {
		d = in * surface->axis[i];
		out += d * camera->axis[i];
	}
}

/*
=============
R_PlaneForSurface

Returns the plane for the first triangle in the surface
FIXME: check for degenerate triangle?
=============
*/
static void R_PlaneForSurface( const srfTriangles_t *tri, idPlane &plane ) {
	idVec3 v1, v2, v3;

	if ( tri == NULL || tri->numIndexes < 3
		|| !R_GetSubviewVertexPosition( tri, tri->indexes[0], v1 )
		|| !R_GetSubviewVertexPosition( tri, tri->indexes[1], v2 )
		|| !R_GetSubviewVertexPosition( tri, tri->indexes[2], v3 ) ) {
		plane.SetNormal( idVec3( 0.0f, 0.0f, 1.0f ) );
		plane[3] = 0.0f;
		return;
	}

	plane.FromPoints( v1, v2, v3 );
}

/*
=========================
R_PreciseCullSurface

Check the surface for visibility on a per-triangle basis
for cases when it is going to be VERY expensive to draw (subviews)

If not culled, also returns the bounding box of the surface in 
Normalized Device Coordinates, so it can be used to crop the scissor rect.

OPTIMIZE: we could also take exact portal passing into consideration
=========================
*/
bool R_PreciseCullSurface( const drawSurf_t *drawSurf, idBounds &ndcBounds ) {
	const srfTriangles_t *tri;
	int numTriangles;
	idPlane clip, eye;
	int i, j;
	unsigned int pointAnd;
	idVec3 localView;
	idFixedWinding w;

	tri = drawSurf->geo;

	pointAnd = (unsigned int)~0;

	// get an exact bounds of the triangles for scissor cropping
	ndcBounds.Clear();

	for ( i = 0; i < tri->numVerts; i++ ) {
		int j;
		unsigned int pointFlags;
		idVec3 localPoint;

		if ( !R_GetSubviewVertexPosition( tri, i, localPoint ) ) {
			return true;
		}

		R_TransformModelToClip( localPoint, drawSurf->space->modelViewMatrix,
			tr.viewDef->projectionMatrix, eye, clip );

		pointFlags = 0;
		for ( j = 0; j < 3; j++ ) {
			if ( clip[j] >= clip[3] ) {
				pointFlags |= (1 << (j*2));
			} else if ( clip[j] <= -clip[3] ) {
				pointFlags |= ( 1 << (j*2+1));
			}
		}

		pointAnd &= pointFlags;
	}

	// trivially reject
	if ( pointAnd ) {
		return true;
	}

	// backface and frustum cull
	numTriangles = tri->numIndexes / 3;

	R_GlobalPointToLocal( drawSurf->space->modelMatrix, tr.viewDef->renderView.vieworg, localView );

	for ( i = 0; i < tri->numIndexes; i += 3 ) {
		idVec3	dir, normal;
		float	dot;
		idVec3	d1, d2;
		idVec3	v1, v2, v3;

		if ( !R_GetSubviewVertexPosition( tri, tri->indexes[i], v1 )
			|| !R_GetSubviewVertexPosition( tri, tri->indexes[i+1], v2 )
			|| !R_GetSubviewVertexPosition( tri, tri->indexes[i+2], v3 ) ) {
			continue;
		}

		// this is a hack, because R_GlobalPointToLocal doesn't work with the non-normalized
		// axis that we get from the gui view transform.  It doesn't hurt anything, because
		// we know that all gui generated surfaces are front facing
		if ( tr.guiRecursionLevel == 0 ) {
			// we don't care that it isn't normalized,
			// all we want is the sign
			d1 = v2 - v1;
			d2 = v3 - v1;
			normal = d2.Cross( d1 );

			dir = v1 - localView;

			dot = normal * dir;
			if ( dot >= 0.0f ) {
				return true;
			}
		}

		// now find the exact screen bounds of the clipped triangle
		w.SetNumPoints( 3 );
		R_LocalPointToGlobal( drawSurf->space->modelMatrix, v1, w[0].ToVec3() );
		R_LocalPointToGlobal( drawSurf->space->modelMatrix, v2, w[1].ToVec3() );
		R_LocalPointToGlobal( drawSurf->space->modelMatrix, v3, w[2].ToVec3() );
		w[0].s = w[0].t = w[1].s = w[1].t = w[2].s = w[2].t = 0.0f;

		for ( j = 0; j < 4; j++ ) {
			if ( !w.ClipInPlace( -tr.viewDef->frustum[j], 0.1f ) ) {
				break;
			}
		}
		const int windingPointCount = w.GetNumPoints();
		for ( j = 0; j < windingPointCount; j++ ) {
			idVec3	screen;

			R_GlobalToNormalizedDeviceCoordinates( w[j].ToVec3(), screen );
			ndcBounds.AddPoint( screen );
		}
	}

	// if we don't enclose any area, return
	if ( ndcBounds.IsCleared() ) {
		return true;
	}

	return false;
}

/*
========================
R_MirrorViewBySurface
========================
*/
static viewDef_t *R_MirrorViewBySurface( drawSurf_t *drawSurf ) {
	viewDef_t		*parms;
	orientation_t	surface, camera;
	idPlane			originalPlane, plane;

	// copy the viewport size from the original
	parms = (viewDef_t *)R_FrameAlloc( sizeof( *parms ) );
	*parms = *tr.viewDef;
	parms->renderView.viewID = 0;	// clear to allow player bodies to show up, and suppress view weapons

	parms->isSubview = true;
	parms->isMirror = true;
	R_SyncSubviewTime( parms );
	R_ResetSubviewTemporalState( parms );

	// create plane axis for the portal we are seeing
	R_PlaneForSurface( drawSurf->geo, originalPlane );
	R_LocalPlaneToGlobal( drawSurf->space->modelMatrix, originalPlane, plane );

	surface.origin = plane.Normal() * -plane[3];
	surface.axis[0] = plane.Normal();
	surface.axis[0].NormalVectors( surface.axis[1], surface.axis[2] );
	surface.axis[2] = -surface.axis[2];

	camera.origin = surface.origin;
	camera.axis[0] = -surface.axis[0];
	camera.axis[1] = surface.axis[1];
	camera.axis[2] = surface.axis[2];

	// set the mirrored origin and axis
	R_MirrorPoint( tr.viewDef->renderView.vieworg, &surface, &camera, parms->renderView.vieworg );

	R_MirrorVector( tr.viewDef->renderView.viewaxis[0], &surface, &camera, parms->renderView.viewaxis[0] );
	R_MirrorVector( tr.viewDef->renderView.viewaxis[1], &surface, &camera, parms->renderView.viewaxis[1] );
	R_MirrorVector( tr.viewDef->renderView.viewaxis[2], &surface, &camera, parms->renderView.viewaxis[2] );

	// make the view origin 16 units away from the center of the surface
	idVec3	viewOrigin = ( drawSurf->geo->bounds[0] + drawSurf->geo->bounds[1] ) * 0.5;
	viewOrigin += ( originalPlane.Normal() * 16 );

	R_LocalPointToGlobal( drawSurf->space->modelMatrix, viewOrigin, parms->initialViewAreaOrigin );

	// set the mirror clip plane
	parms->numClipPlanes = 1;
	parms->clipPlanes[0] = -camera.axis[0];

	parms->clipPlanes[0][3] = -( camera.origin * parms->clipPlanes[0].Normal() );
	
	return parms;
}

/*
========================
R_XrayViewBySurface
========================
*/
static viewDef_t *R_XrayViewBySurface( drawSurf_t *drawSurf ) {
	viewDef_t		*parms;
	orientation_t	surface, camera;
	idPlane			originalPlane, plane;

	// copy the viewport size from the original
	parms = (viewDef_t *)R_FrameAlloc( sizeof( *parms ) );
	*parms = *tr.viewDef;
	parms->renderView.viewID = 0;	// clear to allow player bodies to show up, and suppress view weapons

	parms->isSubview = true;
	parms->isXraySubview = true;
	R_SyncSubviewTime( parms );
	R_ResetSubviewTemporalState( parms );

	return parms;
}

/*
===============
R_RemoteRender
===============
*/
static void R_RemoteRender( drawSurf_t *surf, textureStage_t *stage ) {
	viewDef_t		*parms;

	// remote views can be reused in a single frame, by both eyes of a stereo
	// one: the camera does not depend on the viewer
	if ( stage->dynamicFrameCount == tr.frameCount ) {
		return;
	}

	// if the entity doesn't have a remoteRenderView, do nothing. BSE effect
	// surfaces and in-world GUI emits carry a NULL entityDef, so guard it: an
	// entity-less surface can never supply a remoteRenderView.
	if ( surf->space->entityDef == NULL || !surf->space->entityDef->parms.remoteRenderView ) {
		return;
	}

	// copy the viewport size from the original
	parms = (viewDef_t *)R_FrameAlloc( sizeof( *parms ) );
	*parms = *tr.viewDef;

	parms->isSubview = true;
	parms->isMirror = false;

	parms->renderView = *surf->space->entityDef->parms.remoteRenderView;
	parms->renderView.viewID = 0;	// clear to allow player bodies to show up, and suppress view weapons
	R_SyncSubviewTime( parms );
	R_ResetSubviewTemporalState( parms );
	parms->initialViewAreaOrigin = parms->renderView.vieworg;

	tr.CropRenderSize( stage->width, stage->height, true );

	parms->renderView.x = 0;
	parms->renderView.y = 0;
	parms->renderView.width = SCREEN_WIDTH;
	parms->renderView.height = SCREEN_HEIGHT;

	tr.RenderViewToViewport( &parms->renderView, &parms->viewport );

	parms->scissor.x1 = 0;
	parms->scissor.y1 = 0;
	parms->scissor.x2 = parms->viewport.x2 - parms->viewport.x1;
	parms->scissor.y2 = parms->viewport.y2 - parms->viewport.y1;

	parms->superView = tr.viewDef;
	parms->subviewSurface = surf;

	// generate render commands for it
	R_RenderView(parms);

	// copy this rendering to the image
	stage->dynamicFrameCount = tr.frameCount;
	tr.pc.c_remoteSubviews++;
	if (!stage->image) {
		stage->image = globalImages->scratchImage;
	}

	tr.CaptureRenderToImage( stage->image->GetName() );
	tr.UnCrop();
}

/*
=================
R_MirrorRender
=================
*/
void R_MirrorRender( drawSurf_t *surf, textureStage_t *stage, idScreenRect scissor ) {
	viewDef_t		*parms;

	// reused within a frame, but rendered again for the other eye: the camera
	// follows the viewer
	if ( stage->dynamicFrameCount == R_VR_EyeSubviewFrameKey() ) {
		return;
	}

	// issue a new view command
	parms = R_MirrorViewBySurface( surf );
	if ( !parms ) {
		return;
	}

	tr.CropRenderSize( stage->width, stage->height, true );

	parms->renderView.x = 0;
	parms->renderView.y = 0;
	parms->renderView.width = SCREEN_WIDTH;
	parms->renderView.height = SCREEN_HEIGHT;

	tr.RenderViewToViewport( &parms->renderView, &parms->viewport );

	parms->scissor.x1 = 0;
	parms->scissor.y1 = 0;
	parms->scissor.x2 = parms->viewport.x2 - parms->viewport.x1;
	parms->scissor.y2 = parms->viewport.y2 - parms->viewport.y1;

	parms->superView = tr.viewDef;
	parms->subviewSurface = surf;

	// triangle culling order changes with mirroring
	parms->isMirror = ( ( (int)parms->isMirror ^ (int)tr.viewDef->isMirror ) != 0 );

	// generate render commands for it
	R_RenderView( parms );

	// copy this rendering to the image
	stage->dynamicFrameCount = R_VR_EyeSubviewFrameKey();
	tr.pc.c_eyeSubviews++;
	// its own image: OpenGL's post passes resize _scratch every frame, which
	// reallocated it and reset the wrap the mirror's screen texgen needs
	stage->image = globalImages->ImageFromFile( "_mirrorRender", TF_DEFAULT, TR_REPEAT, TD_DEFAULT );

	tr.CaptureRenderToImage( stage->image->GetName() );
	tr.UnCrop();
}

/*
=================
R_ReflectionRender
=================
*/
void R_ReflectionRender( drawSurf_t *surf, textureStage_t *stage, idScreenRect scissor ) {
	viewDef_t		*parms;

	if ( stage->dynamicFrameCount == R_VR_EyeSubviewFrameKey() ) {
		return;
	}

	parms = R_MirrorViewBySurface( surf );
	if ( !parms ) {
		return;
	}

	tr.CropRenderSize( stage->width, stage->height, true );

	parms->renderView.x = 0;
	parms->renderView.y = 0;
	parms->renderView.width = SCREEN_WIDTH;
	parms->renderView.height = SCREEN_HEIGHT;

	tr.RenderViewToViewport( &parms->renderView, &parms->viewport );

	parms->scissor.x1 = 0;
	parms->scissor.y1 = 0;
	parms->scissor.x2 = parms->viewport.x2 - parms->viewport.x1;
	parms->scissor.y2 = parms->viewport.y2 - parms->viewport.y1;

	parms->superView = tr.viewDef;
	parms->subviewSurface = surf;
	parms->isMirror = ( ( (int)parms->isMirror ^ (int)tr.viewDef->isMirror ) != 0 );

	R_RenderView( parms );

	stage->dynamicFrameCount = R_VR_EyeSubviewFrameKey();
	tr.pc.c_eyeSubviews++;
	stage->image = globalImages->ImageFromFile( "_reflectionRender", TF_DEFAULT, TR_REPEAT, TD_DEFAULT );

	tr.CaptureRenderToImage( stage->image->GetName() );
	tr.UnCrop();
}

/*
=================
R_RefractionRender
=================
*/
static void R_RefractionRender( drawSurf_t *surf, textureStage_t *stage, idScreenRect scissor ) {
	viewDef_t		*parms;

	if ( stage->dynamicFrameCount == R_VR_EyeSubviewFrameKey() ) {
		return;
	}

	// Raven exposes refractionRenderMap as a distinct dynamic image from reflection.
	// The legacy implementation rendered through a removed special-effects path, so
	// preserve the material contract by capturing the current view into a dedicated
	// _refractionRender image without introducing that obsolete system.
	parms = (viewDef_t *)R_FrameAlloc( sizeof( *parms ) );
	*parms = *tr.viewDef;
	parms->renderView.viewID = 0;
	parms->isSubview = true;
	parms->isMirror = false;
	R_SyncSubviewTime( parms );
	R_ResetSubviewTemporalState( parms );
	parms->initialViewAreaOrigin = parms->renderView.vieworg;

	tr.CropRenderSize( stage->width, stage->height, true );

	parms->renderView.x = 0;
	parms->renderView.y = 0;
	parms->renderView.width = SCREEN_WIDTH;
	parms->renderView.height = SCREEN_HEIGHT;

	tr.RenderViewToViewport( &parms->renderView, &parms->viewport );

	parms->scissor.x1 = 0;
	parms->scissor.y1 = 0;
	parms->scissor.x2 = parms->viewport.x2 - parms->viewport.x1;
	parms->scissor.y2 = parms->viewport.y2 - parms->viewport.y1;

	parms->superView = tr.viewDef;
	parms->subviewSurface = surf;

	R_RenderView( parms );

	stage->dynamicFrameCount = R_VR_EyeSubviewFrameKey();
	tr.pc.c_eyeSubviews++;
	stage->image = globalImages->ImageFromFile( "_refractionRender", TF_DEFAULT, TR_REPEAT, TD_DEFAULT );

	tr.CaptureRenderToImage( stage->image->GetName() );
	tr.UnCrop();
}

/*
=================
R_XrayRender
=================
*/
void R_XrayRender( drawSurf_t *surf, textureStage_t *stage, idScreenRect scissor ) {
	viewDef_t		*parms;

	// reused within a frame, but rendered again for the other eye: the camera
	// follows the viewer
	if ( stage->dynamicFrameCount == R_VR_EyeSubviewFrameKey() ) {
		return;
	}

	// issue a new view command
	parms = R_XrayViewBySurface( surf );
	if ( !parms ) {
		return;
	}

	tr.CropRenderSize( stage->width, stage->height, true );

	parms->renderView.x = 0;
	parms->renderView.y = 0;
	parms->renderView.width = SCREEN_WIDTH;
	parms->renderView.height = SCREEN_HEIGHT;

	tr.RenderViewToViewport( &parms->renderView, &parms->viewport );

	parms->scissor.x1 = 0;
	parms->scissor.y1 = 0;
	parms->scissor.x2 = parms->viewport.x2 - parms->viewport.x1;
	parms->scissor.y2 = parms->viewport.y2 - parms->viewport.y1;

	parms->superView = tr.viewDef;
	parms->subviewSurface = surf;

	// triangle culling order changes with mirroring
	parms->isMirror = ( ( (int)parms->isMirror ^ (int)tr.viewDef->isMirror ) != 0 );

	// generate render commands for it
	R_RenderView( parms );

	// copy this rendering to the image
	stage->dynamicFrameCount = R_VR_EyeSubviewFrameKey();
	tr.pc.c_eyeSubviews++;
	stage->image = globalImages->scratchImage2;

	tr.CaptureRenderToImage( stage->image->GetName());
	tr.UnCrop();
}

/*
==================
R_GenerateSurfaceSubview
==================
*/
bool	R_GenerateSurfaceSubview( drawSurf_t *drawSurf ) {
	idBounds		ndcBounds;
	viewDef_t		*parms;
	const idMaterial		*shader;

	// for testing the performance hit
	if ( r_skipSubviews.GetBool() ) {
		return false;
	}

	if ( R_PreciseCullSurface( drawSurf, ndcBounds ) ) {
		return false;
	}

	shader = drawSurf->material;

	// never recurse through a subview surface that we are
	// already seeing through
	for ( parms = tr.viewDef ; parms ; parms = parms->superView ) {
		if ( parms->subviewSurface
			&& parms->subviewSurface->geo == drawSurf->geo
			&& parms->subviewSurface->space->entityDef == drawSurf->space->entityDef ) {
			break;
		}
	}
	if ( parms ) {
		return false;
	}

	// crop the scissor bounds based on the precise cull
	idScreenRect	scissor;

	idScreenRect	*v = &tr.viewDef->viewport;
	scissor.x1 = v->x1 + (int)( (v->x2 - v->x1 + 1 ) * 0.5f * ( ndcBounds[0][0] + 1.0f ));
	scissor.y1 = v->y1 + (int)( (v->y2 - v->y1 + 1 ) * 0.5f * ( ndcBounds[0][1] + 1.0f ));
	scissor.x2 = v->x1 + (int)( (v->x2 - v->x1 + 1 ) * 0.5f * ( ndcBounds[1][0] + 1.0f ));
	scissor.y2 = v->y1 + (int)( (v->y2 - v->y1 + 1 ) * 0.5f * ( ndcBounds[1][1] + 1.0f ));

	// nudge a bit for safety
	scissor.Expand();

	scissor.Intersect( tr.viewDef->scissor );

	if ( scissor.IsEmpty() ) {
		// cropped out
		return false;
	}

	// see what kind of subview we are making
	if ( shader->GetSort() != SS_SUBVIEW ) {
		const int stageCount = shader->GetNumStages();
		for ( int i = 0 ; i < stageCount ; i++ ) {
			const shaderStage_t	*stage = shader->GetStage( i );
			switch ( stage->texture.dynamic ) {
			case DI_REMOTE_RENDER:
				R_RemoteRender( drawSurf, const_cast<textureStage_t *>(&stage->texture) );
				break;
			case DI_MIRROR_RENDER:
				R_MirrorRender( drawSurf, const_cast<textureStage_t *>(&stage->texture), scissor );
				break;
			case DI_REFLECTION_RENDER:
				R_ReflectionRender( drawSurf, const_cast<textureStage_t *>(&stage->texture), scissor );
				break;
			case DI_REFRACTION_RENDER:
				R_RefractionRender( drawSurf, const_cast<textureStage_t *>(&stage->texture), scissor );
				break;
			case DI_XRAY_RENDER:
				R_XrayRender( drawSurf, const_cast<textureStage_t *>(&stage->texture), scissor );
				break;
			}
		}
		return true;
	}

	// issue a new view command
	parms = R_MirrorViewBySurface( drawSurf );
	if ( !parms ) {
		return false;
	}

	parms->scissor = scissor;
	parms->superView = tr.viewDef;
	parms->subviewSurface = drawSurf;

	// triangle culling order changes with mirroring
	parms->isMirror = ( ( (int)parms->isMirror ^ (int)tr.viewDef->isMirror ) != 0 );

	// generate render commands for it
	R_RenderView( parms );

	return true;
}

/*
================
R_GenerateSubViews

If we need to render another view to complete the current view,
generate it first.

It is important to do this after all drawSurfs for the current
view have been generated, because it may create a subview which
would change tr.viewCount.
================
*/
bool R_GenerateSubViews( void ) {
	drawSurf_t		*drawSurf;
	int				i;
	bool			subviews;
	const idMaterial		*shader;

	// for testing the performance hit
	if ( r_skipSubviews.GetBool() ) {
		return false;
	}

	subviews = false;

	// scan the surfaces until we either find a subview, or determine
	// there are no more subview surfaces.
	for ( i = 0 ; i < tr.viewDef->numDrawSurfs ; i++ ) {
		drawSurf = tr.viewDef->drawSurfs[i];
		shader = drawSurf->material;

		if ( !shader || !shader->HasSubview() ) {
			continue;
		}

		if ( R_GenerateSurfaceSubview( drawSurf ) ) {
			subviews = true;
		}
	}

	return subviews;
}
