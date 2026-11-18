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
#include "Model_local.h"

/*

This is a simple dynamic model that just creates a stretched quad between
two points that faces the view, like a dynamic deform tube.

*/

static const char *beam_SnapshotName = "_beam_Snapshot_";

/*
===============
idRenderModelBeam::IsDynamicModel
===============
*/
dynamicModel_t idRenderModelBeam::IsDynamicModel() const {
	return DM_CONTINUOUS;	// regenerate for every view
}

/*
===============
idRenderModelBeam::IsLoaded
===============
*/
bool idRenderModelBeam::IsLoaded() {
	return true;	// don't ever need to load
}

/*
===============
idRenderModelBeam::InstantiateDynamicModel
===============
*/
idRenderModel *idRenderModelBeam::InstantiateDynamicModel( const struct renderEntity_s *renderEntity, const struct viewDef_s *viewDef, idRenderModel *cachedModel ) {
	idRenderModelStatic *staticModel;
	srfTriangles_t *tri;
	modelSurface_t surf = {};

	// Match the sprite/md5 dynamic models: keep the cached snapshot for reuse
	// unless caching is disabled. Deleting it unconditionally made the reuse
	// branch below dead code and rebuilt the whole static model every view.
	if ( cachedModel && !r_useCachedDynamicModels.GetBool() ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if ( renderEntity == NULL || viewDef == NULL ) {
		delete cachedModel;
		return NULL;
	}

	if ( cachedModel != NULL ) {

		assert( dynamic_cast<idRenderModelStatic *>( cachedModel ) != NULL );
		assert( idStr::Icmp( cachedModel->Name(), beam_SnapshotName ) == 0 );

		staticModel = static_cast<idRenderModelStatic *>( cachedModel );
		surf = *staticModel->Surface( 0 );
		tri = surf.geometry;

	} else {

		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty( beam_SnapshotName );

		tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts( tri, 4 );
		R_AllocStaticTriSurfIndexes( tri, 6 );

		tri->verts[0].Clear();
		tri->verts[0].st[0] = 0;
		tri->verts[0].st[1] = 0;

		tri->verts[1].Clear();
		tri->verts[1].st[0] = 0;
		tri->verts[1].st[1] = 1;

		tri->verts[2].Clear();
		tri->verts[2].st[0] = 1;
		tri->verts[2].st[1] = 0;

		tri->verts[3].Clear();
		tri->verts[3].st[0] = 1;
		tri->verts[3].st[1] = 1;

		tri->indexes[0] = 0;
		tri->indexes[1] = 2;
		tri->indexes[2] = 1;
		tri->indexes[3] = 2;
		tri->indexes[4] = 3;
		tri->indexes[5] = 1;

		tri->numVerts = 4;
		tri->numIndexes = 6;

		surf.geometry = tri;
		surf.id = 0;
		surf.shader = tr.defaultMaterial;
		staticModel->AddSurface( surf );
	}

	idVec3	target = *reinterpret_cast<const idVec3 *>( &renderEntity->shaderParms[SHADERPARM_BEAM_END_X] );

	// we need the view direction to project the minor axis of the tube
	// as the view changes
	idVec3	localView, localTarget;
	float	modelMatrix[16];
	R_AxisToModelMatrix( renderEntity->axis, renderEntity->origin, modelMatrix );
	R_GlobalPointToLocal( modelMatrix, viewDef->renderView.vieworg, localView ); 
	R_GlobalPointToLocal( modelMatrix, target, localTarget ); 

	idVec3	major = localTarget;
	idVec3	minor;

	idVec3	mid = 0.5f * localTarget;
	idVec3	dir = mid - localView;
	minor.Cross( major, dir );
	minor.Normalize();
	if ( renderEntity->shaderParms[SHADERPARM_BEAM_WIDTH] != 0.0f ) {
		minor *= renderEntity->shaderParms[SHADERPARM_BEAM_WIDTH] * 0.5f;
	}

	int red		= idMath::FtoiFast( renderEntity->shaderParms[SHADERPARM_RED] * 255.0f );
	int green	= idMath::FtoiFast( renderEntity->shaderParms[SHADERPARM_GREEN] * 255.0f );
	int blue	= idMath::FtoiFast( renderEntity->shaderParms[SHADERPARM_BLUE] * 255.0f );
	int alpha	= idMath::FtoiFast( renderEntity->shaderParms[SHADERPARM_ALPHA] * 255.0f );

	tri->verts[0].xyz = minor;
	tri->verts[0].color[0] = red;
	tri->verts[0].color[1] = green;
	tri->verts[0].color[2] = blue;
	tri->verts[0].color[3] = alpha;

	tri->verts[1].xyz = -minor;
	tri->verts[1].color[0] = red;
	tri->verts[1].color[1] = green;
	tri->verts[1].color[2] = blue;
	tri->verts[1].color[3] = alpha;

	tri->verts[2].xyz = localTarget + minor;
	tri->verts[2].color[0] = red;
	tri->verts[2].color[1] = green;
	tri->verts[2].color[2] = blue;
	tri->verts[2].color[3] = alpha;

	tri->verts[3].xyz = localTarget - minor;
	tri->verts[3].color[0] = red;
	tri->verts[3].color[1] = green;
	tri->verts[3].color[2] = blue;
	tri->verts[3].color[3] = alpha;

	R_BoundTriSurf( tri );

	// The beam verts are recomputed every view from the camera, but the reused
	// snapshot keeps its persistent static ambientCache, which R_CreateAmbientCache
	// short-circuits on. Free the vertex caches so the rewritten verts re-upload;
	// a no-op on a freshly allocated tri.
	R_FreeStaticTriSurfVertexCaches( tri );

	staticModel->bounds = tri->bounds;

	return staticModel;
}

/*
===============
idRenderModelBeam::Bounds
===============
*/
idBounds idRenderModelBeam::Bounds( const struct renderEntity_s *renderEntity ) const {
	idBounds	b;

	b.Zero();
	if ( !renderEntity ) {
		b.ExpandSelf( 8.0f );
	} else {
		idVec3	target = *reinterpret_cast<const idVec3 *>( &renderEntity->shaderParms[SHADERPARM_BEAM_END_X] );
		idVec3	localTarget;
		float	modelMatrix[16];
		R_AxisToModelMatrix( renderEntity->axis, renderEntity->origin, modelMatrix );
		R_GlobalPointToLocal( modelMatrix, target, localTarget ); 

		b.AddPoint( localTarget );
		if ( renderEntity->shaderParms[SHADERPARM_BEAM_WIDTH] != 0.0f ) {
			b.ExpandSelf( renderEntity->shaderParms[SHADERPARM_BEAM_WIDTH] * 0.5f );
		}
	}
	return b;
}

/*
===============================================================================

	openQ4 cone beam

A light beam drawn as a sheet that always faces the view: narrow at the apex
(the renderEntity origin) and widening to the end point, like the cone of
lit air in front of a flashlight. It is three columns wide and several rows
long, the rows closer together near the apex where the sheet widens fastest,
so the cross-section keeps its shape instead of shearing along a diagonal.

The texture is sampled only across the beam: every row reads the middle line
of the image (s = 0.5) from one edge (t = 0) to the other (t = 1). Nothing in
it runs along the beam, so no texture detail stretches into lines however long
the beam is; a smooth radial image gives a soft cross-section. Brightness
falls off along the beam in the vertex colour instead, linearly to nothing at
the full reach, where SHADERPARM_CONEBEAM_REACH says the end is (a beam cut
short by a wall ends at the brightness it had there). The sheet also fades
where it would be seen end on (looking down the beam) and where it passes
through the camera, the two places a flat sheet cannot pass for a volume.
Colour and alpha fade together, which suits additive materials.

===============================================================================
*/

static const char *coneBeam_SnapshotName = "_coneBeam_Snapshot_";
static const int CONEBEAM_ROWS = 8;
static const int CONEBEAM_COLUMNS = 3;
static const int CONEBEAM_VERTS = CONEBEAM_ROWS * CONEBEAM_COLUMNS;
static const int CONEBEAM_INDEXES = ( CONEBEAM_ROWS - 1 ) * ( CONEBEAM_COLUMNS - 1 ) * 6;

static float R_ConeBeamWidth( const struct renderEntity_s *renderEntity, const float fraction ) {
	const float startWidth = Max( 0.0f, renderEntity->shaderParms[SHADERPARM_CONEBEAM_START_WIDTH] );
	const float endWidth = Max( 0.0f, renderEntity->shaderParms[SHADERPARM_BEAM_WIDTH] );
	return startWidth + ( endWidth - startWidth ) * fraction;
}

static byte R_ConeBeamColorByte( const float value ) {
	return static_cast<byte>( idMath::ClampInt( 0, 255, idMath::FtoiFast( value * 255.0f ) ) );
}

/*
===============
idRenderModelConeBeam::IsDynamicModel
===============
*/
dynamicModel_t idRenderModelConeBeam::IsDynamicModel() const {
	return DM_CONTINUOUS;	// regenerate for every view
}

/*
===============
idRenderModelConeBeam::IsLoaded
===============
*/
bool idRenderModelConeBeam::IsLoaded() {
	return true;
}

/*
===============
idRenderModelConeBeam::InstantiateDynamicModel
===============
*/
idRenderModel *idRenderModelConeBeam::InstantiateDynamicModel( const struct renderEntity_s *renderEntity, const struct viewDef_s *viewDef, idRenderModel *cachedModel ) {
	if ( cachedModel && !r_useCachedDynamicModels.GetBool() ) {
		delete cachedModel;
		cachedModel = NULL;
	}
	if ( renderEntity == NULL || viewDef == NULL ) {
		delete cachedModel;
		return NULL;
	}

	idRenderModelStatic *staticModel;
	srfTriangles_t *tri;
	if ( cachedModel != NULL ) {
		assert( dynamic_cast<idRenderModelStatic *>( cachedModel ) != NULL );
		assert( idStr::Icmp( cachedModel->Name(), coneBeam_SnapshotName ) == 0 );
		staticModel = static_cast<idRenderModelStatic *>( cachedModel );
		tri = staticModel->Surface( 0 )->geometry;
	} else {
		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty( coneBeam_SnapshotName );

		tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts( tri, CONEBEAM_VERTS );
		R_AllocStaticTriSurfIndexes( tri, CONEBEAM_INDEXES );
		for ( int i = 0; i < CONEBEAM_VERTS; i++ ) {
			tri->verts[i].Clear();
		}
		int numIndexes = 0;
		for ( int row = 0; row < CONEBEAM_ROWS - 1; row++ ) {
			for ( int column = 0; column < CONEBEAM_COLUMNS - 1; column++ ) {
				const int a = row * CONEBEAM_COLUMNS + column;
				const int b = a + 1;
				const int c = a + CONEBEAM_COLUMNS;
				const int d = c + 1;
				tri->indexes[numIndexes++] = a;
				tri->indexes[numIndexes++] = c;
				tri->indexes[numIndexes++] = b;
				tri->indexes[numIndexes++] = c;
				tri->indexes[numIndexes++] = d;
				tri->indexes[numIndexes++] = b;
			}
		}
		tri->numVerts = CONEBEAM_VERTS;
		tri->numIndexes = CONEBEAM_INDEXES;

		modelSurface_t surf = {};
		surf.geometry = tri;
		surf.id = 0;
		surf.shader = tr.defaultMaterial;
		staticModel->AddSurface( surf );
	}

	const idVec3 target = *reinterpret_cast<const idVec3 *>( &renderEntity->shaderParms[SHADERPARM_BEAM_END_X] );
	idVec3 localView, localTarget;
	float modelMatrix[16];
	R_AxisToModelMatrix( renderEntity->axis, renderEntity->origin, modelMatrix );
	R_GlobalPointToLocal( modelMatrix, viewDef->renderView.vieworg, localView );
	R_GlobalPointToLocal( modelMatrix, target, localTarget );

	idVec3 majorDir = localTarget;
	const float length = majorDir.Normalize();
	const float reach = idMath::ClampFloat( 0.0f, 1.0f, renderEntity->shaderParms[SHADERPARM_CONEBEAM_REACH] );
	const float alpha = renderEntity->shaderParms[SHADERPARM_ALPHA];

	for ( int row = 0; row < CONEBEAM_ROWS; row++ ) {
		// rows crowd towards the apex: width and fade stay linear in distance,
		// so the vertex interpolation between rows is still exact
		const float step = static_cast<float>( row ) / static_cast<float>( CONEBEAM_ROWS - 1 );
		const float fraction = step * step;
		const idVec3 centre = localTarget * fraction;
		const float halfWidth = 0.5f * R_ConeBeamWidth( renderEntity, fraction );

		idVec3 toCentre = centre - localView;
		const float distance = toCentre.Normalize();
		// each row faces the view on its own, so a long beam near the camera
		// keeps its width along its whole length
		idVec3 minor;
		minor.Cross( majorDir, toCentre );
		const float minorLength = minor.Normalize();
		if ( length <= 0.0f || minorLength <= 1.0e-4f ) {
			minor.Zero();
		}

		// fade out where the sheet is seen end on, and where it reaches the eye
		const float endOn = idMath::Fabs( toCentre * majorDir );
		const float facingFade = idMath::ClampFloat( 0.0f, 1.0f, ( 0.985f - endOn ) / ( 0.985f - 0.85f ) );
		const float nearFade = idMath::ClampFloat( 0.0f, 1.0f, ( distance - halfWidth ) / 48.0f );

		// linear falloff with distance, to nothing at the full reach
		const float intensity = ( 1.0f - reach * fraction ) * facingFade * nearFade;

		byte color[4];
		color[0] = R_ConeBeamColorByte( renderEntity->shaderParms[SHADERPARM_RED] * intensity );
		color[1] = R_ConeBeamColorByte( renderEntity->shaderParms[SHADERPARM_GREEN] * intensity );
		color[2] = R_ConeBeamColorByte( renderEntity->shaderParms[SHADERPARM_BLUE] * intensity );
		color[3] = R_ConeBeamColorByte( alpha * intensity );

		const float s = 0.5f;
		for ( int column = 0; column < CONEBEAM_COLUMNS; column++ ) {
			idDrawVert &vert = tri->verts[row * CONEBEAM_COLUMNS + column];
			const float side = 1.0f - static_cast<float>( column );	// +1, 0, -1
			vert.xyz = centre + minor * ( halfWidth * side );
			vert.st[0] = s;
			vert.st[1] = 0.5f * static_cast<float>( column );
			vert.color[0] = color[0];
			vert.color[1] = color[1];
			vert.color[2] = color[2];
			vert.color[3] = color[3];
		}
	}

	R_BoundTriSurf( tri );
	// the verts change every view, but a reused snapshot keeps its static
	// ambient cache; free it so they re-upload (see idRenderModelBeam)
	R_FreeStaticTriSurfVertexCaches( tri );
	staticModel->bounds = tri->bounds;
	return staticModel;
}

/*
===============
idRenderModelConeBeam::Bounds
===============
*/
idBounds idRenderModelConeBeam::Bounds( const struct renderEntity_s *renderEntity ) const {
	idBounds b;
	b.Zero();
	if ( renderEntity == NULL ) {
		b.ExpandSelf( 8.0f );
		return b;
	}
	const idVec3 target = *reinterpret_cast<const idVec3 *>( &renderEntity->shaderParms[SHADERPARM_BEAM_END_X] );
	idVec3 localTarget;
	float modelMatrix[16];
	R_AxisToModelMatrix( renderEntity->axis, renderEntity->origin, modelMatrix );
	R_GlobalPointToLocal( modelMatrix, target, localTarget );
	b.AddPoint( localTarget );
	b.ExpandSelf( 0.5f * Max( R_ConeBeamWidth( renderEntity, 0.0f ), R_ConeBeamWidth( renderEntity, 1.0f ) ) );
	return b;
}
