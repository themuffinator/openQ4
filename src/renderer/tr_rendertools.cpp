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
#include "simplex.h"	// line font definition

#define MAX_DEBUG_LINES			16384

typedef struct debugLine_s {
	idVec4		rgb;
	idVec3		start;
	idVec3		end;
	bool		depthTest;
	int			lifeTime;
} debugLine_t;

debugLine_t		rb_debugLines[ MAX_DEBUG_LINES ];
int				rb_numDebugLines = 0;
int				rb_debugLineTime = 0;

#define MAX_DEBUG_TEXT			512

typedef struct debugText_s {
	idStr		text;
	idVec3		origin;
	float		scale;
	idVec4		color;
	idMat3		viewAxis;
	int			align;
	int			lifeTime;
	bool		depthTest;
} debugText_t;

debugText_t		rb_debugText[ MAX_DEBUG_TEXT ];
int				rb_numDebugText = 0;
int				rb_debugTextTime = 0;

#define MAX_DEBUG_POLYGONS		8192

typedef struct debugPolygon_s {
	idVec4		rgb;
	idWinding	winding;
	bool		depthTest;
	int			lifeTime;
} debugPolygon_t;

debugPolygon_t	rb_debugPolygons[ MAX_DEBUG_POLYGONS ];
int				rb_numDebugPolygons = 0;
int				rb_debugPolygonTime = 0;

static void RB_DrawText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align );

/*
================
RB_DrawBounds
================
*/
void RB_DrawBounds( const idBounds &bounds ) {
	if ( bounds.IsCleared() ) {
		return;
	}

	glBegin( GL_LINE_LOOP );
	glVertex3f( bounds[0][0], bounds[0][1], bounds[0][2] );
	glVertex3f( bounds[0][0], bounds[1][1], bounds[0][2] );
	glVertex3f( bounds[1][0], bounds[1][1], bounds[0][2] );
	glVertex3f( bounds[1][0], bounds[0][1], bounds[0][2] );
	glEnd();
	glBegin( GL_LINE_LOOP );
	glVertex3f( bounds[0][0], bounds[0][1], bounds[1][2] );
	glVertex3f( bounds[0][0], bounds[1][1], bounds[1][2] );
	glVertex3f( bounds[1][0], bounds[1][1], bounds[1][2] );
	glVertex3f( bounds[1][0], bounds[0][1], bounds[1][2] );
	glEnd();

	glBegin( GL_LINES );
	glVertex3f( bounds[0][0], bounds[0][1], bounds[0][2] );
	glVertex3f( bounds[0][0], bounds[0][1], bounds[1][2] );

	glVertex3f( bounds[0][0], bounds[1][1], bounds[0][2] );
	glVertex3f( bounds[0][0], bounds[1][1], bounds[1][2] );

	glVertex3f( bounds[1][0], bounds[0][1], bounds[0][2] );
	glVertex3f( bounds[1][0], bounds[0][1], bounds[1][2] );

	glVertex3f( bounds[1][0], bounds[1][1], bounds[0][2] );
	glVertex3f( bounds[1][0], bounds[1][1], bounds[1][2] );
	glEnd();
}


/*
================
RB_SimpleSurfaceSetup
================
*/
void RB_SimpleSurfaceSetup( const drawSurf_t *drawSurf ) {
	// change the matrix if needed
	if ( drawSurf->space != backEnd.currentSpace ) {
		glLoadMatrixf( drawSurf->space->modelViewMatrix );
		backEnd.currentSpace = drawSurf->space;
	}

	// change the scissor if needed
	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( drawSurf->scissorRect ) ) {
		backEnd.currentScissor = drawSurf->scissorRect;
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1, 
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}
}

/*
================
RB_SimpleWorldSetup
================
*/
void RB_SimpleWorldSetup( void ) {
	backEnd.currentSpace = &backEnd.viewDef->worldSpace;
	glLoadMatrixf( backEnd.viewDef->worldSpace.modelViewMatrix );

	backEnd.currentScissor = backEnd.viewDef->scissor;
	glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1, 
		backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
		backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
		backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
}

static bool RB_RendertoolsGetTriDebugGeometry( const srfTriangles_t *tri, const idDrawVert *&vertsOut, const glIndex_t *&indexesOut ) {
	vertsOut = NULL;
	indexesOut = NULL;

	if ( tri == NULL ) {
		return false;
	}

	vertsOut = tri->verts;
	indexesOut = tri->indexes;

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->primBatchMesh != NULL && ( vertsOut == NULL || indexesOut == NULL ) ) {
		if ( tri->numVerts <= 0 || tri->numIndexes <= 0 ) {
			return false;
		}

		idDrawVert *tempVerts = (idDrawVert *)R_FrameAlloc( tri->numVerts * sizeof( tempVerts[0] ) );
		glIndex_t *tempIndexes = (glIndex_t *)R_FrameAlloc( tri->numIndexes * sizeof( tempIndexes[0] ) );
		renderSystem->CopyPrimBatchTriangles( tempVerts, tempIndexes, tri->primBatchMesh, tri->silTraceVerts );
		vertsOut = tempVerts;
		indexesOut = tempIndexes;
	}
#endif

	return vertsOut != NULL && indexesOut != NULL;
}

static bool RB_RendertoolsPrepareInteractionVertexCache( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->geo == NULL ) {
		return false;
	}

	const srfTriangles_t *tri = surf->geo;
	srfTriangles_t *mutableTri = const_cast<srfTriangles_t *>( tri );
	srfTriangles_t *ambientTri = ( tri->ambientSurface != NULL ) ? tri->ambientSurface : mutableTri;
	const bool needsLighting = ( surf->material != NULL ) ? surf->material->ReceivesLighting() : false;

	if ( ambientTri != NULL && ambientTri->ambientCache == NULL ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		if ( ambientTri->primBatchMesh != NULL ) {
			if ( !R_CreatePackedSurfaceFrameCaches( ambientTri, needsLighting, false ) ) {
				return false;
			}
		} else
#endif
		if ( ambientTri->verts != NULL ) {
			if ( !R_CreateAmbientCache( ambientTri, needsLighting ) ) {
				return false;
			}
		}
	}

	if ( mutableTri->ambientCache == NULL && ambientTri != NULL && ambientTri->ambientCache != NULL ) {
		mutableTri->ambientCache = ambientTri->ambientCache;
		mutableTri->tempAmbientCache = ambientTri->tempAmbientCache;
	}

	if ( mutableTri->ambientCache == NULL ) {
		return false;
	}

	R_TouchVertexCache( mutableTri->ambientCache );
	return true;
}

static bool RB_RendertoolsGetInteractionDrawIndexes( const srfTriangles_t *tri, const glIndex_t *&indexesOut, int &numIndexesOut ) {
	indexesOut = NULL;
	numIndexesOut = 0;

	if ( tri == NULL || tri->indexes == NULL || tri->numIndexes <= 0 ) {
		return false;
	}

	indexesOut = tri->indexes;
	numIndexesOut = tri->numIndexes;

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->primBatchMesh == NULL || tri->ambientSurface == NULL ) {
		return true;
	}

	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( tri );
	if ( mesh == NULL || mesh->primBatches.Num() <= 0 ) {
		return true;
	}

	const int numPrimBatches = mesh->primBatches.Num();
	bool hasHeaderedLightTris = ( tri->numAllocedIndices >= tri->numIndexes + numPrimBatches );
	if ( hasHeaderedLightTris ) {
		int headerIndexCount = 0;
		for ( int primBatchIndex = 0; primBatchIndex < numPrimBatches; ++primBatchIndex ) {
			const int batchIndexCount = tri->indexes[ primBatchIndex ];
			if ( batchIndexCount < 0
				|| ( batchIndexCount % 3 ) != 0
				|| batchIndexCount > tri->numIndexes - headerIndexCount ) {
				hasHeaderedLightTris = false;
				break;
			}
			headerIndexCount += batchIndexCount;
		}
		hasHeaderedLightTris = hasHeaderedLightTris && ( headerIndexCount == tri->numIndexes );
	}

	if ( !hasHeaderedLightTris ) {
		return true;
	}

	glIndex_t *flattenedIndexes = (glIndex_t *)R_FrameAlloc( tri->numIndexes * sizeof( flattenedIndexes[0] ) );
	const glIndex_t *batchHeader = tri->indexes;
	const glIndex_t *batchIndexes = tri->indexes + numPrimBatches;
	int destVertexBase = 0;
	int flattenedIndexCount = 0;

	for ( int primBatchIndex = 0; primBatchIndex < numPrimBatches; ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
		const int batchIndexCount = batchHeader[ primBatchIndex ];
		if ( !primBatch.hasDrawGeoSpec
			|| primBatch.drawGeoSpec.vertexStart < 0
			|| primBatch.drawGeoSpec.vertexCount < 0 ) {
			return false;
		}

		for ( int batchIndex = 0; batchIndex < batchIndexCount; ++batchIndex ) {
			const int drawIndex = batchIndexes[ batchIndex ];
			if ( drawIndex < primBatch.drawGeoSpec.vertexStart
				|| drawIndex >= primBatch.drawGeoSpec.vertexStart + primBatch.drawGeoSpec.vertexCount ) {
				return false;
			}

			const int localIndex = drawIndex - primBatch.drawGeoSpec.vertexStart + destVertexBase;
			if ( localIndex < 0 || localIndex >= tri->ambientSurface->numVerts ) {
				return false;
			}

			flattenedIndexes[ flattenedIndexCount++ ] = localIndex;
		}

		batchIndexes += batchIndexCount;
		destVertexBase += primBatch.drawGeoSpec.vertexCount;
	}

	indexesOut = flattenedIndexes;
	numIndexesOut = flattenedIndexCount;
#endif

	return true;
}

static void RB_RendertoolsDrawLightCountSurface( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = ( surf != NULL ) ? surf->geo : NULL;
	if ( tri == NULL || !RB_RendertoolsPrepareInteractionVertexCache( surf ) ) {
		return;
	}

	const idDrawVert *ambientCache = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), &ambientCache->xyz );

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->primBatchMesh != NULL && tri->ambientSurface != NULL ) {
		const glIndex_t *drawIndexes = NULL;
		int drawIndexCount = 0;
		if ( !RB_RendertoolsGetInteractionDrawIndexes( tri, drawIndexes, drawIndexCount ) || drawIndexCount <= 0 ) {
			return;
		}

		backEnd.pc.c_drawElements++;
		backEnd.pc.c_drawIndexes += drawIndexCount;
		backEnd.pc.c_drawVertexes += tri->numVerts;

		vertexCache.UnbindIndex();
		glDrawElements( GL_TRIANGLES,
			r_singleTriangle.GetBool() ? 3 : drawIndexCount,
			GL_INDEX_TYPE,
			drawIndexes );
		return;
	}
#endif

	RB_DrawElementsWithCounters( tri );
}

/*
=================
RB_PolygonClear

This will cover the entire screen with normal rasterization.
Texturing is disabled, but the existing glColor, glDepthMask,
glColorMask, and the enabled state of depth buffering and
stenciling will matter.
=================
*/
void RB_PolygonClear( void ) {
	glPushMatrix();
	glPushAttrib( GL_ALL_ATTRIB_BITS  );
	glLoadIdentity();
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_SCISSOR_TEST );
	glBegin( GL_POLYGON );
	glVertex3f( -20, -20, -10 );
	glVertex3f( 20, -20, -10 );
	glVertex3f( 20, 20, -10 );
	glVertex3f( -20, 20, -10 );
	glEnd();
	glPopAttrib();
	glPopMatrix();
}

/*
====================
RB_ShowDestinationAlpha
====================
*/
void RB_ShowDestinationAlpha( void ) {
	GL_State( GLS_SRCBLEND_DST_ALPHA | GLS_DSTBLEND_ZERO | GLS_DEPTHMASK | GLS_DEPTHFUNC_ALWAYS );
	glColor3f( 1, 1, 1 );
	RB_PolygonClear();
}

/*
===================
RB_ScanStencilBuffer

Debugging tool to see what values are in the stencil buffer
===================
*/
void RB_ScanStencilBuffer( void ) {
	int		counts[256];
	int		i;
	byte	*stencilReadback;

	memset( counts, 0, sizeof( counts ) );

	stencilReadback = (byte *)R_StaticAlloc( glConfig.vidWidth * glConfig.vidHeight );
	glReadPixels( 0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencilReadback );

	for ( i = 0; i < glConfig.vidWidth * glConfig.vidHeight; i++ ) {
		counts[ stencilReadback[i] ]++;
	}

	R_StaticFree( stencilReadback );

	// print some stats (not supposed to do from back end in SMP...)
	common->Printf( "stencil values:\n" );
	for ( i = 0 ; i < 255 ; i++ ) {
		if ( counts[i] ) {
			common->Printf( "%i: %i\n", i, counts[i] );
		}
	}
}


/*
===================
RB_CountStencilBuffer

Print an overdraw count based on stencil index values
===================
*/
void RB_CountStencilBuffer( void ) {
	int		count;
	int		i;
	byte	*stencilReadback;


	stencilReadback = (byte *)R_StaticAlloc( glConfig.vidWidth * glConfig.vidHeight );
	glReadPixels( 0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencilReadback );

	count = 0;
	for ( i = 0; i < glConfig.vidWidth * glConfig.vidHeight; i++ ) {
		count += stencilReadback[i];
	}

	R_StaticFree( stencilReadback );

	// print some stats (not supposed to do from back end in SMP...)
	common->Printf( "overdraw: %5.1f\n", (float)count/(glConfig.vidWidth * glConfig.vidHeight)  );
}

/*
===================
R_ColorByStencilBuffer

Sets the screen colors based on the contents of the
stencil buffer.  Stencil of 0 = black, 1 = red, 2 = green,
3 = blue, ..., 7+ = white
===================
*/
static void R_ColorByStencilBuffer( void ) {
	int		i;
	static float	colors[8][3] = {
		{0,0,0},
		{1,0,0},
		{0,1,0},
		{0,0,1},
		{0,1,1},
		{1,0,1},
		{1,1,0},
		{1,1,1},
	};

	// clear color buffer to white (>6 passes)
	glClearColor( 1, 1, 1, 1 );
	glDisable( GL_SCISSOR_TEST );
	glClear( GL_COLOR_BUFFER_BIT );

	// now draw color for each stencil value
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	for ( i = 0 ; i < 6 ; i++ ) {
		glColor3fv( colors[i] );
		glStencilFunc( GL_EQUAL, i, 255 );
		RB_PolygonClear();
	}

	glStencilFunc( GL_ALWAYS, 0, 255 );
}

//======================================================================

/*
==================
RB_ShowOverdraw
==================
*/
void RB_ShowOverdraw( void ) {
	const idMaterial *	material;
	int					i;
	drawSurf_t * *		drawSurfs;
	const drawSurf_t *	surf;
	int					numDrawSurfs;
	viewLight_t *		vLight;

	if ( r_showOverDraw.GetInteger() == 0 ) {
		return;
	}

	material = declManager->FindMaterial( "textures/common/overdrawtest", false );
	if ( material == NULL ) {
		return;
	}

	drawSurfs = backEnd.viewDef->drawSurfs;
	numDrawSurfs = backEnd.viewDef->numDrawSurfs;

	int interactions = 0;
	for ( vLight = backEnd.viewDef->viewLights; vLight; vLight = vLight->next ) {
		for ( surf = vLight->localInteractions; surf; surf = surf->nextOnLight ) {
			interactions++;
		}
		for ( surf = vLight->globalInteractions; surf; surf = surf->nextOnLight ) {
			interactions++;
		}
	}

	drawSurf_t **newDrawSurfs = (drawSurf_t **)R_FrameAlloc( numDrawSurfs + interactions * sizeof( newDrawSurfs[0] ) );

	for ( i = 0; i < numDrawSurfs; i++ ) {
		surf = drawSurfs[i];
		if ( surf->material ) {
			const_cast<drawSurf_t *>(surf)->material = material;
		}
		newDrawSurfs[i] = const_cast<drawSurf_t *>(surf);
	}

	for ( vLight = backEnd.viewDef->viewLights; vLight; vLight = vLight->next ) {
		for ( surf = vLight->localInteractions; surf; surf = surf->nextOnLight ) {
			const_cast<drawSurf_t *>(surf)->material = material;
			newDrawSurfs[i++] = const_cast<drawSurf_t *>(surf);
		}
		for ( surf = vLight->globalInteractions; surf; surf = surf->nextOnLight ) {
			const_cast<drawSurf_t *>(surf)->material = material;
			newDrawSurfs[i++] = const_cast<drawSurf_t *>(surf);
		}
		vLight->localInteractions = NULL;
		vLight->globalInteractions = NULL;
	}

	switch( r_showOverDraw.GetInteger() ) {
		case 1: // geometry overdraw
			const_cast<viewDef_t *>(backEnd.viewDef)->drawSurfs = newDrawSurfs;
			const_cast<viewDef_t *>(backEnd.viewDef)->numDrawSurfs = numDrawSurfs;
			break;
		case 2: // light interaction overdraw
			const_cast<viewDef_t *>(backEnd.viewDef)->drawSurfs = &newDrawSurfs[numDrawSurfs];
			const_cast<viewDef_t *>(backEnd.viewDef)->numDrawSurfs = interactions;
			break;
		case 3: // geometry + light interaction overdraw
			const_cast<viewDef_t *>(backEnd.viewDef)->drawSurfs = newDrawSurfs;
			const_cast<viewDef_t *>(backEnd.viewDef)->numDrawSurfs += interactions;
			break;
	}
}

static int RB_CountDebugDrawSurfChain( const drawSurf_t *surf ) {
	int count = 0;
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		count++;
	}
	return count;
}

struct viewLightDebugVisual_t {
	int		lightIndex;
	bool	affecting;
	idVec3	lightOrigin;
	idVec3	boxCenter;
	idVec3	lightRadius;
	idMat3	boxAxis;
	idVec4	drawColor;
	idStr	label;
};

static idList<viewLightDebugVisual_t> rb_viewLightDebugVisuals;

static void RB_ClearViewLightDebugVisuals( void ) {
	rb_viewLightDebugVisuals.Clear();
}

static idVec4 RB_ViewLightDebugColor( const renderLight_t *parms ) {
	idVec4 color( 1.0f, 1.0f, 1.0f, 1.0f );

	if ( parms != NULL ) {
		color[0] = parms->shaderParms[0];
		color[1] = parms->shaderParms[1];
		color[2] = parms->shaderParms[2];
	}

	float maxComponent = Max( color[0], Max( color[1], color[2] ) );
	if ( maxComponent <= 0.0f ) {
		color[0] = 1.0f;
		color[1] = 0.25f;
		color[2] = 0.25f;
		return color;
	}

	if ( maxComponent > 1.0f ) {
		color *= 1.0f / maxComponent;
	}

	maxComponent = Max( color[0], Max( color[1], color[2] ) );
	if ( maxComponent < 0.25f ) {
		color *= 0.25f / maxComponent;
	}

	color[3] = 1.0f;
	return color;
}

static void RB_AppendViewLightDebugVisual( const viewLight_t *vLight, bool affecting ) {
	viewLightDebugVisual_t visual;
	const idRenderLightLocal *light = vLight != NULL ? vLight->lightDef : NULL;
	const renderLight_t *parms = light != NULL ? &light->parms : NULL;

	visual.lightIndex = light != NULL ? light->index : -1;
	visual.affecting = affecting;
	visual.lightOrigin = light != NULL ? light->globalLightOrigin : vec3_origin;
	visual.boxCenter = parms != NULL ? parms->origin : visual.lightOrigin;
	visual.lightRadius = parms != NULL ? parms->lightRadius : vec3_origin;
	visual.boxAxis = parms != NULL ? parms->axis : mat3_identity;
	visual.drawColor = RB_ViewLightDebugColor( parms );
	visual.label = va(
		"%s[%d] rgb=(%.2f %.2f %.2f) radius=(%.1f %.1f %.1f)",
		affecting ? "affect" : "visible",
		visual.lightIndex,
		parms != NULL ? parms->shaderParms[0] : 0.0f,
		parms != NULL ? parms->shaderParms[1] : 0.0f,
		parms != NULL ? parms->shaderParms[2] : 0.0f,
		visual.lightRadius[0],
		visual.lightRadius[1],
		visual.lightRadius[2] );
	rb_viewLightDebugVisuals.Append( visual );
}

static const char *RB_ViewLightDebugTypeName( const viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->lightShader == NULL ) {
		return "<null>";
	}
	if ( vLight->lightShader->IsFogLight() ) {
		return "fog";
	}
	if ( vLight->lightShader->IsBlendLight() ) {
		return "blend";
	}
	if ( vLight->lightShader->IsAmbientLight() ) {
		return "ambient";
	}
	if ( vLight->pointLight ) {
		return vLight->parallel ? "point/parallel" : "point";
	}
	return vLight->parallel ? "projected/parallel" : "projected";
}

static unsigned int RB_ViewLightDebugHashStep( unsigned int hash, int value ) {
	hash ^= static_cast<unsigned int>( value );
	hash *= 16777619u;
	return hash;
}

static void RB_DrawViewLightDebugBox( const idBox &box ) {
	idVec3 points[8];

	box.ToPoints( points );

	glBegin( GL_LINES );
	for ( int i = 0; i < 4; i++ ) {
		glVertex3fv( points[i].ToFloatPtr() );
		glVertex3fv( points[( i + 1 ) & 3].ToFloatPtr() );

		glVertex3fv( points[4 + i].ToFloatPtr() );
		glVertex3fv( points[4 + ( ( i + 1 ) & 3 )].ToFloatPtr() );

		glVertex3fv( points[i].ToFloatPtr() );
		glVertex3fv( points[4 + i].ToFloatPtr() );
	}
	glEnd();
}

static void RB_DrawViewLightDebugCross( const idVec3 &origin, float halfSize ) {
	glBegin( GL_LINES );
	glVertex3f( origin[0] - halfSize, origin[1], origin[2] );
	glVertex3f( origin[0] + halfSize, origin[1], origin[2] );
	glVertex3f( origin[0], origin[1] - halfSize, origin[2] );
	glVertex3f( origin[0], origin[1] + halfSize, origin[2] );
	glVertex3f( origin[0], origin[1], origin[2] - halfSize );
	glVertex3f( origin[0], origin[1], origin[2] + halfSize );
	glEnd();
}

static void RB_DrawCachedViewLightDebugVisuals( void ) {
	if ( !r_showViewLightsVisuals.GetBool() ) {
		return;
	}
	if ( idMath::ClampInt( 0, 3, r_showViewLights.GetInteger() ) <= 0 ) {
		return;
	}
	if ( backEnd.viewDef == NULL || backEnd.viewDef != tr.primaryView || backEnd.viewDef->viewEntitys == NULL || rb_viewLightDebugVisuals.Num() <= 0 ) {
		return;
	}

	RB_SimpleWorldSetup();

	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	globalImages->BindNull();
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_DEPTH_TEST );
	GL_Cull( CT_TWO_SIDED );
	GL_State( GLS_POLYMODE_LINE );
	glLineWidth( 2.0f );

	for ( int i = 0; i < rb_viewLightDebugVisuals.Num(); i++ ) {
		const viewLightDebugVisual_t &visual = rb_viewLightDebugVisuals[i];
		const float maxRadius = Max( visual.lightRadius[0], Max( visual.lightRadius[1], visual.lightRadius[2] ) );
		const float markerHalfSize = idMath::ClampFloat( 8.0f, 64.0f, maxRadius * 0.15f );
		const float labelOffset = idMath::ClampFloat( 10.0f, 96.0f, maxRadius * 0.25f );
		const float labelScale = idMath::ClampFloat( 0.015f, 0.05f, maxRadius * 0.00025f );
		const idVec3 labelOrigin = visual.lightOrigin + idVec3( 0.0f, 0.0f, labelOffset );

		glColor4fv( visual.drawColor.ToFloatPtr() );
		RB_DrawViewLightDebugCross( visual.lightOrigin, markerHalfSize );

		if ( visual.lightRadius.LengthSqr() > 0.0f ) {
			RB_DrawViewLightDebugBox( idBox( visual.boxCenter, visual.lightRadius, visual.boxAxis ) );
		}

		if ( ( visual.boxCenter - visual.lightOrigin ).LengthSqr() > 1.0f ) {
			glBegin( GL_LINES );
			glVertex3fv( visual.lightOrigin.ToFloatPtr() );
			glVertex3fv( visual.boxCenter.ToFloatPtr() );
			glEnd();
		}

		RB_DrawText( visual.label.c_str(), labelOrigin, labelScale, visual.drawColor, backEnd.viewDef->renderView.viewaxis, 1 );
	}

	glLineWidth( 1.0f );
	glEnable( GL_DEPTH_TEST );
	GL_State( GLS_DEFAULT );
	GL_Cull( CT_FRONT_SIDED );
}

/*
=================
RB_ReportViewLights

Structured console report for lights affecting the current view origin.
=================
*/
static void RB_ReportViewLights( void ) {
	static int lastReportFrame = -1;
	static unsigned int lastAffectingHash = 0;
	static int lastAffectingCount = -1;
	static bool wasEnabled = false;

	const int reportMode = idMath::ClampInt( 0, 3, r_showViewLights.GetInteger() );
	if ( reportMode <= 0 ) {
		wasEnabled = false;
		RB_ClearViewLightDebugVisuals();
		return;
	}
	if ( backEnd.viewDef == NULL || backEnd.viewDef != tr.primaryView || backEnd.viewDef->viewEntitys == NULL || backEnd.viewDef->renderWorld == NULL ) {
		return;
	}

	const idVec3 &viewOrigin = backEnd.viewDef->renderView.vieworg;
	const int viewArea = backEnd.viewDef->renderWorld->PointInArea( viewOrigin );

	int visibleCount = 0;
	int affectingCount = 0;
	unsigned int affectingHash = 2166136261u;

	for ( const viewLight_t *vLight = backEnd.viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
		visibleCount++;
		if ( !vLight->viewInsideLight ) {
			continue;
		}

		affectingCount++;
		affectingHash = RB_ViewLightDebugHashStep( affectingHash, vLight->lightDef != NULL ? vLight->lightDef->index : -1 );
	}

	const bool affectingSetChanged = !wasEnabled || affectingCount != lastAffectingCount || affectingHash != lastAffectingHash;
	if ( reportMode == 1 && !affectingSetChanged ) {
		return;
	}

	const int reportInterval = Max( 1, r_showViewLightsInterval.GetInteger() );
	if ( reportMode >= 2 && wasEnabled && lastReportFrame >= 0 && tr.frameCount - lastReportFrame < reportInterval ) {
		return;
	}

	lastReportFrame = tr.frameCount;
	lastAffectingHash = affectingHash;
	lastAffectingCount = affectingCount;
	wasEnabled = true;
	RB_ClearViewLightDebugVisuals();

	common->Printf(
		"ViewLights frame=%d origin=(%.1f %.1f %.1f) area=%d affecting=%d visible=%d\n",
		tr.frameCount,
		viewOrigin[0], viewOrigin[1], viewOrigin[2],
		viewArea,
		affectingCount,
		visibleCount );

	if ( reportMode < 3 && visibleCount > affectingCount ) {
		common->Printf(
			"  note: %d additional visible lights do not contain the view origin and can still light surfaces in view; use r_showViewLights 3 to list them\n",
			visibleCount - affectingCount );
	}

	if ( visibleCount <= 0 ) {
		common->Printf( "  <no visible lights>\n" );
		return;
	}
	if ( affectingCount <= 0 && reportMode < 3 ) {
		common->Printf( "  <no lights affecting current view origin>\n" );
		return;
	}

	for ( const viewLight_t *vLight = backEnd.viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
		const bool affecting = vLight->viewInsideLight;
		if ( !affecting && reportMode < 3 ) {
			continue;
		}

		const idRenderLightLocal *light = vLight->lightDef;
		const renderLight_t *parms = light != NULL ? &light->parms : NULL;
		const char *shaderName = ( light != NULL && light->lightShader != NULL ) ? light->lightShader->GetName() : "<null>";
		const idVec3 lightOrigin = light != NULL ? light->globalLightOrigin : vec3_origin;
		const idVec3 lightRadius = parms != NULL ? parms->lightRadius : vec3_origin;
		const idVec3 lightCenter = parms != NULL ? parms->lightCenter : vec3_origin;
		const int localInteractions = RB_CountDebugDrawSurfChain( vLight->localInteractions );
		const int globalInteractions = RB_CountDebugDrawSurfChain( vLight->globalInteractions );
		const int translucentInteractions = RB_CountDebugDrawSurfChain( vLight->translucentInteractions );
		const int localShadows = RB_CountDebugDrawSurfChain( vLight->localShadows );
		const int globalShadows = RB_CountDebugDrawSurfChain( vLight->globalShadows );

		common->Printf(
			"  %s[%d] shader='%s' kind=%s origin=(%.1f %.1f %.1f) radius=(%.1f %.1f %.1f) center=(%.1f %.1f %.1f) color=(%.2f %.2f %.2f %.2f) area=%d shadows=%s seesOrigin=%d surfs(l=%d g=%d t=%d) shadowSurfs(l=%d g=%d)\n",
			affecting ? "affect" : "visible",
			light != NULL ? light->index : -1,
			shaderName,
			RB_ViewLightDebugTypeName( vLight ),
			lightOrigin[0], lightOrigin[1], lightOrigin[2],
			lightRadius[0], lightRadius[1], lightRadius[2],
			lightCenter[0], lightCenter[1], lightCenter[2],
			parms != NULL ? parms->shaderParms[0] : 0.0f,
			parms != NULL ? parms->shaderParms[1] : 0.0f,
			parms != NULL ? parms->shaderParms[2] : 0.0f,
			parms != NULL ? parms->shaderParms[3] : 0.0f,
			light != NULL ? light->areaNum : -1,
			( parms != NULL && parms->noShadows ) ? "off" : "on",
			vLight->viewSeesGlobalLightOrigin ? 1 : 0,
			localInteractions,
			globalInteractions,
			translucentInteractions,
			localShadows,
			globalShadows );

		RB_AppendViewLightDebugVisual( vLight, affecting );
	}
}

/*
===================
RB_ShowIntensity

Debugging tool to see how much dynamic range a scene is using.
The greatest of the rgb values at each pixel will be used, with
the resulting color shading from red at 0 to green at 128 to blue at 255
===================
*/
void RB_ShowIntensity( void ) {
	byte	*colorReadback;
	int		i, j, c;

	if ( !r_showIntensity.GetBool() ) {
		return;
	}

	colorReadback = (byte *)R_StaticAlloc( glConfig.vidWidth * glConfig.vidHeight * 4 );
	glReadPixels( 0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_RGBA, GL_UNSIGNED_BYTE, colorReadback );

	c = glConfig.vidWidth * glConfig.vidHeight * 4;
	for ( i = 0; i < c ; i+=4 ) {
		j = colorReadback[i];
		if ( colorReadback[i+1] > j ) {
			j = colorReadback[i+1];
		}
		if ( colorReadback[i+2] > j ) {
			j = colorReadback[i+2];
		}
		if ( j < 128 ) {
			colorReadback[i+0] = 2*(128-j);
			colorReadback[i+1] = 2*j;
			colorReadback[i+2] = 0;
		} else {
			colorReadback[i+0] = 0;
			colorReadback[i+1] = 2*(255-j);
			colorReadback[i+2] = 2*(j-128);
		}
	}

	// draw it back to the screen
	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	GL_State( GLS_DEPTHFUNC_ALWAYS );
	glPushMatrix();
	glLoadIdentity(); 
    glOrtho( 0, 1, 0, 1, -1, 1 );
	glRasterPos2f( 0, 0 );
	glPopMatrix();
	glColor3f( 1, 1, 1 );
	globalImages->BindNull();
	glMatrixMode( GL_MODELVIEW );

	glDrawPixels( glConfig.vidWidth, glConfig.vidHeight, GL_RGBA , GL_UNSIGNED_BYTE, colorReadback );

	R_StaticFree( colorReadback );
}


/*
===================
RB_ShowDepthBuffer

Draw the depth buffer as colors
===================
*/
void RB_ShowDepthBuffer( void ) {
	void	*depthReadback;

	if ( !r_showDepth.GetBool() ) {
		return;
	}

	glPushMatrix();
	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity(); 
    glOrtho( 0, 1, 0, 1, -1, 1 );
	glRasterPos2f( 0, 0 );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();

	GL_State( GLS_DEPTHFUNC_ALWAYS );
	glColor3f( 1, 1, 1 );
	globalImages->BindNull();

	depthReadback = R_StaticAlloc( glConfig.vidWidth * glConfig.vidHeight*4 );
	memset( depthReadback, 0, glConfig.vidWidth * glConfig.vidHeight*4 );

	glReadPixels( 0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_DEPTH_COMPONENT , GL_FLOAT, depthReadback );

#if 0
	for ( i = 0 ; i < glConfig.vidWidth * glConfig.vidHeight ; i++ ) {
		((byte *)depthReadback)[i*4] = 
		((byte *)depthReadback)[i*4+1] = 
		((byte *)depthReadback)[i*4+2] = 255 * ((float *)depthReadback)[i];
		((byte *)depthReadback)[i*4+3] = 1;
	}
#endif

	glDrawPixels( glConfig.vidWidth, glConfig.vidHeight, GL_RGBA , GL_UNSIGNED_BYTE, depthReadback );
	R_StaticFree( depthReadback );
}

/*
=================
RB_ShowLightCount

This is a debugging tool that will draw each surface with a color
based on how many lights are effecting it
=================
*/
void RB_ShowLightCount( void ) {
	int		i;
	const drawSurf_t	*surf;
	const viewLight_t	*vLight;

	if ( !r_showLightCount.GetBool() ) {
		return;
	}

	GL_State( GLS_DEPTHFUNC_EQUAL );

	RB_SimpleWorldSetup();
	glClearStencil( 0 );
	glClear( GL_STENCIL_BUFFER_BIT );

	glEnable( GL_STENCIL_TEST );

	// optionally count everything through walls
	if ( r_showLightCount.GetInteger() >= 2 ) {
		glStencilOp( GL_KEEP, GL_INCR, GL_INCR );
	} else {
		glStencilOp( GL_KEEP, GL_KEEP, GL_INCR );
	}

	glStencilFunc( GL_ALWAYS, 1, 255 );

	globalImages->defaultImage->Bind();

	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		for ( i = 0 ; i < 2 ; i++ ) {
			for ( surf = i ? vLight->localInteractions: vLight->globalInteractions; surf; surf = (drawSurf_t *)surf->nextOnLight ) {
				RB_SimpleSurfaceSetup( surf );
				RB_RendertoolsDrawLightCountSurface( surf );
			}
		}
	}

	// display the results
	R_ColorByStencilBuffer();

	if ( r_showLightCount.GetInteger() > 2 ) {
		RB_CountStencilBuffer();
	}
}


/*
=================
RB_ShowSilhouette

Blacks out all edges, then adds color for each edge that a shadow
plane extends from, allowing you to see doubled edges
=================
*/
void RB_ShowSilhouette( void ) {
	int		i;
	const drawSurf_t	*surf;
	const viewLight_t	*vLight;

	if ( !r_showSilhouette.GetBool() ) {
		return;
	}

	//
	// clear all triangle edges to black
	//
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	globalImages->BindNull();
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_STENCIL_TEST );

	glColor3f( 0, 0, 0 );

	GL_State( GLS_POLYMODE_LINE );

	GL_Cull( CT_TWO_SIDED );
	glDisable( GL_DEPTH_TEST );

	RB_RenderDrawSurfListWithFunction( backEnd.viewDef->drawSurfs, backEnd.viewDef->numDrawSurfs, 
		RB_T_RenderTriangleSurface );


	//
	// now blend in edges that cast silhouettes
	//
	RB_SimpleWorldSetup();
	glColor3f( 0.5, 0, 0 );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );

	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		for ( i = 0 ; i < 2 ; i++ ) {
			for ( surf = i ? vLight->localShadows : vLight->globalShadows
				; surf ; surf = (drawSurf_t *)surf->nextOnLight ) {
				RB_SimpleSurfaceSetup( surf );

				const srfTriangles_t	*tri = surf->geo;

				glVertexPointer( 3, GL_FLOAT, sizeof( shadowCache_t ), vertexCache.Position( tri->shadowCache ) );
				glBegin( GL_LINES );

				for ( int j = 0 ; j < tri->numIndexes ; j+=3 ) {
					int		i1 = tri->indexes[j+0];
					int		i2 = tri->indexes[j+1];
					int		i3 = tri->indexes[j+2];

					if ( (i1 & 1) + (i2 & 1) + (i3 & 1) == 1 ) {
						if ( (i1 & 1) + (i2 & 1) == 0 ) {
							glArrayElement( i1 );
							glArrayElement( i2 );
						} else if ( (i1 & 1 ) + (i3 & 1) == 0 ) {
							glArrayElement( i1 );
							glArrayElement( i3 );
						}
					}
				}
				glEnd();

			}
		}
	}

	glEnable( GL_DEPTH_TEST );

	GL_State( GLS_DEFAULT );
	glColor3f( 1,1,1 );
	GL_Cull( CT_FRONT_SIDED );
}



/*
=================
RB_ShowShadowCount

This is a debugging tool that will draw only the shadow volumes
and count up the total fill usage
=================
*/
static void RB_ShowShadowCount( void ) {
	int		i;
	const drawSurf_t	*surf;
	const viewLight_t	*vLight;

	if ( !r_showShadowCount.GetBool() ) {
		return;
	}

	GL_State( GLS_DEFAULT );

	glClearStencil( 0 );
	glClear( GL_STENCIL_BUFFER_BIT );

	glEnable( GL_STENCIL_TEST );

	glStencilOp( GL_KEEP, GL_INCR, GL_INCR );

	glStencilFunc( GL_ALWAYS, 1, 255 );

	globalImages->defaultImage->Bind();

	// draw both sides
	GL_Cull( CT_TWO_SIDED );

	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		for ( i = 0 ; i < 2 ; i++ ) {
			for ( surf = i ? vLight->localShadows : vLight->globalShadows 
				; surf ; surf = (drawSurf_t *)surf->nextOnLight ) {
				RB_SimpleSurfaceSetup( surf );
				const srfTriangles_t	*tri = surf->geo;
				if ( !tri->shadowCache ) {
					continue;
				}

				if ( r_showShadowCount.GetInteger() == 3 ) {
					// only show turboshadows
					if ( tri->numShadowIndexesNoCaps != tri->numIndexes ) {
						continue;
					}
				}
				if ( r_showShadowCount.GetInteger() == 4 ) {
					// only show static shadows
					if ( tri->numShadowIndexesNoCaps == tri->numIndexes ) {
						continue;
					}
				}

				shadowCache_t *cache = (shadowCache_t *)vertexCache.Position( tri->shadowCache );
				glVertexPointer( 4, GL_FLOAT, sizeof( *cache ), &cache->xyz );
				RB_DrawElementsWithCounters( tri );
			}
		}
	}

	// display the results
	R_ColorByStencilBuffer();

	if ( r_showShadowCount.GetInteger() == 2 ) {
		common->Printf( "all shadows " );
	} else if ( r_showShadowCount.GetInteger() == 3 ) {
		common->Printf( "turboShadows " );
	} else if ( r_showShadowCount.GetInteger() == 4 ) {
		common->Printf( "static shadows " );
	}

	if ( r_showShadowCount.GetInteger() >= 2 ) {
		RB_CountStencilBuffer();
	}

	GL_Cull( CT_FRONT_SIDED );
}


/*
===============
RB_T_RenderTriangleSurfaceAsLines

===============
*/
void RB_T_RenderTriangleSurfaceAsLines( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = surf->geo;
	const idDrawVert *verts = NULL;
	const glIndex_t *indexes = NULL;

	if ( !RB_RendertoolsGetTriDebugGeometry( tri, verts, indexes ) ) {
		return;
	}

	const glIndex_t *lineIndexes = ( tri->silIndexes != NULL ) ? tri->silIndexes : indexes;
	if ( lineIndexes == NULL ) {
		return;
	}

	glBegin( GL_LINES );
	for ( int i = 0 ; i < tri->numIndexes ; i+= 3 ) {
		for ( int j = 0 ; j < 3 ; j++ ) {
			int k = ( j + 1 ) % 3;
			glVertex3fv( verts[ lineIndexes[i+j] ].xyz.ToFloatPtr() );
			glVertex3fv( verts[ lineIndexes[i+k] ].xyz.ToFloatPtr() );
		}
	}
	glEnd();
}


/*
=====================
RB_ShowTris

Debugging tool
=====================
*/
static void RB_ShowTris( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	modelTrace_t mt;
	idVec3 end;

	if ( !r_showTris.GetInteger() ) {
		return;
	}

	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	globalImages->BindNull();
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_STENCIL_TEST );

	glColor3f( 1, 1, 1 );


	GL_State( GLS_POLYMODE_LINE );

	switch ( r_showTris.GetInteger() ) {
	case 1:	// only draw visible ones
		glPolygonOffset( -1, -2 );
		glEnable( GL_POLYGON_OFFSET_LINE );
		break;
	default:
	case 2:	// draw all front facing
		GL_Cull( CT_FRONT_SIDED );
		glDisable( GL_DEPTH_TEST );
		break;
	case 3: // draw all
		GL_Cull( CT_TWO_SIDED );
		glDisable( GL_DEPTH_TEST );
		break;
	}

	RB_RenderDrawSurfListWithFunction( drawSurfs, numDrawSurfs, RB_T_RenderTriangleSurface );

	glEnable( GL_DEPTH_TEST );
	glDisable( GL_POLYGON_OFFSET_LINE );

	glDepthRange( 0, 1 );
	GL_State( GLS_DEFAULT );
	GL_Cull( CT_FRONT_SIDED );
}


/*
=====================
RB_ShowSurfaceInfo

Debugging tool
=====================
*/
static void RB_ShowSurfaceInfo( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	modelTrace_t mt;
	idVec3 start, end;
	
	if ( !r_showSurfaceInfo.GetBool() ) {
		return;
	}

	// start far enough away that we don't hit the player model
	start = tr.primaryView->renderView.vieworg + tr.primaryView->renderView.viewaxis[0] * 16;
	end = start + tr.primaryView->renderView.viewaxis[0] * 1000.0f;
	if ( !tr.primaryWorld->Trace( mt, start, end, 0.0f, false ) ) {
		return;
	}

	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	globalImages->BindNull();
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_STENCIL_TEST );

	glColor3f( 1, 1, 1 );

	GL_State( GLS_POLYMODE_LINE );

	glPolygonOffset( -1, -2 );
	glEnable( GL_POLYGON_OFFSET_LINE );

	idVec3	trans[3];
	float	matrix[16];

	// transform the object verts into global space
	R_AxisToModelMatrix( mt.entity->axis, mt.entity->origin, matrix );

	tr.primaryWorld->DrawText( mt.entity->hModel->Name(), mt.point + tr.primaryView->renderView.viewaxis[2] * 12,
		0.35f, colorRed, tr.primaryView->renderView.viewaxis );
	tr.primaryWorld->DrawText( mt.material->GetName(), mt.point, 
		0.35f, colorBlue, tr.primaryView->renderView.viewaxis );

	glEnable( GL_DEPTH_TEST );
	glDisable( GL_POLYGON_OFFSET_LINE );

	glDepthRange( 0, 1 );
	GL_State( GLS_DEFAULT );
	GL_Cull( CT_FRONT_SIDED );
}


/*
=====================
RB_ShowViewEntitys

Debugging tool
=====================
*/
static void RB_ShowViewEntitys( viewEntity_t *vModels ) {
	if ( !r_showViewEntitys.GetBool() ) {
		return;
	}
	if ( r_showViewEntitys.GetInteger() == 2 ) {
		common->Printf( "view entities: " );
		for ( ; vModels ; vModels = vModels->next ) {
			common->Printf( "%i ", vModels->entityDef->index );
		}
		common->Printf( "\n" );
		return;
	}

	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	globalImages->BindNull();
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_STENCIL_TEST );

	glColor3f( 1, 1, 1 );


	GL_State( GLS_POLYMODE_LINE );

	GL_Cull( CT_TWO_SIDED );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_SCISSOR_TEST );

	for ( ; vModels ; vModels = vModels->next ) {
		idBounds	b;

		glLoadMatrixf( vModels->modelViewMatrix );

		if ( !vModels->entityDef ) {
			continue;
		}

		// draw the reference bounds in yellow
		glColor3f( 1, 1, 0 );
		RB_DrawBounds( vModels->entityDef->referenceBounds );


		// draw the model bounds in white
		glColor3f( 1, 1, 1 );

		idRenderModel *model = R_EntityDefDynamicModel( vModels->entityDef );
		if ( !model ) {
			continue;	// particles won't instantiate without a current view
		}
		b = model->Bounds( &vModels->entityDef->parms );
		RB_DrawBounds( b );
	}

	glEnable( GL_DEPTH_TEST );
	glDisable( GL_POLYGON_OFFSET_LINE );

	glDepthRange( 0, 1 );
	GL_State( GLS_DEFAULT );
	GL_Cull( CT_FRONT_SIDED );
}

/*
=====================
RB_ShowTexturePolarity

Shade triangle red if they have a positive texture area
green if they have a negative texture area, or blue if degenerate area
=====================
*/
static void RB_ShowTexturePolarity( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int		i, j;
	drawSurf_t	*drawSurf;
	const srfTriangles_t	*tri;

	if ( !r_showTexturePolarity.GetBool() ) {
		return;
	}
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	globalImages->BindNull();
	glDisable( GL_STENCIL_TEST );

	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

	glColor3f( 1, 1, 1 );

	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		drawSurf = drawSurfs[i];
		tri = drawSurf->geo;
		const idDrawVert *verts = NULL;
		const glIndex_t *indexes = NULL;
		if ( !RB_RendertoolsGetTriDebugGeometry( tri, verts, indexes ) ) {
			continue;
		}

		RB_SimpleSurfaceSetup( drawSurf );

		glBegin( GL_TRIANGLES );
		for ( j = 0 ; j < tri->numIndexes ; j+=3 ) {
			const idDrawVert	*a, *b, *c;
			float		d0[5], d1[5];
			float		area;

			a = verts + indexes[j];
			b = verts + indexes[j+1];
			c = verts + indexes[j+2];

			// VectorSubtract( b->xyz, a->xyz, d0 );
			d0[3] = b->st[0] - a->st[0];
			d0[4] = b->st[1] - a->st[1];
			// VectorSubtract( c->xyz, a->xyz, d1 );
			d1[3] = c->st[0] - a->st[0];
			d1[4] = c->st[1] - a->st[1];

			area = d0[3] * d1[4] - d0[4] * d1[3];

			if ( idMath::Fabs( area ) < 0.0001 ) {
				glColor4f( 0, 0, 1, 0.5 );
			} else  if ( area < 0 ) {
				glColor4f( 1, 0, 0, 0.5 );
			} else {
				glColor4f( 0, 1, 0, 0.5 );
			}
			glVertex3fv( a->xyz.ToFloatPtr() );
			glVertex3fv( b->xyz.ToFloatPtr() );
			glVertex3fv( c->xyz.ToFloatPtr() );
		}
		glEnd();
	}

	GL_State( GLS_DEFAULT );
}


/*
=====================
RB_ShowUnsmoothedTangents

Shade materials that are using unsmoothed tangents
=====================
*/
static void RB_ShowUnsmoothedTangents( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int		i, j;
	drawSurf_t	*drawSurf;
	const srfTriangles_t	*tri;

	if ( !r_showUnsmoothedTangents.GetBool() ) {
		return;
	}
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	globalImages->BindNull();
	glDisable( GL_STENCIL_TEST );

	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

	glColor4f( 0, 1, 0, 0.5 );

	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		drawSurf = drawSurfs[i];

		if ( !drawSurf->material->UseUnsmoothedTangents() ) {
			continue;
		}

		RB_SimpleSurfaceSetup( drawSurf );

		tri = drawSurf->geo;
		const idDrawVert *verts = NULL;
		const glIndex_t *indexes = NULL;
		if ( !RB_RendertoolsGetTriDebugGeometry( tri, verts, indexes ) ) {
			continue;
		}
		glBegin( GL_TRIANGLES );
		for ( j = 0 ; j < tri->numIndexes ; j+=3 ) {
			const idDrawVert	*a, *b, *c;

			a = verts + indexes[j];
			b = verts + indexes[j+1];
			c = verts + indexes[j+2];

			glVertex3fv( a->xyz.ToFloatPtr() );
			glVertex3fv( b->xyz.ToFloatPtr() );
			glVertex3fv( c->xyz.ToFloatPtr() );
		}
		glEnd();
	}

	GL_State( GLS_DEFAULT );
}


/*
=====================
RB_ShowTangentSpace

Shade a triangle by the RGB colors of its tangent space
1 = tangents[0]
2 = tangents[1]
3 = normal
=====================
*/
static void RB_ShowTangentSpace( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int		i, j;
	drawSurf_t	*drawSurf;
	const srfTriangles_t	*tri;

	if ( !r_showTangentSpace.GetInteger() ) {
		return;
	}
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	globalImages->BindNull();
	glDisable( GL_STENCIL_TEST );

	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		drawSurf = drawSurfs[i];

		RB_SimpleSurfaceSetup( drawSurf );

		tri = drawSurf->geo;
		const idDrawVert *verts = NULL;
		const glIndex_t *indexes = NULL;
		if ( !RB_RendertoolsGetTriDebugGeometry( tri, verts, indexes ) ) {
			continue;
		}
		glBegin( GL_TRIANGLES );
		for ( j = 0 ; j < tri->numIndexes ; j++ ) {
			const idDrawVert *v;

			v = &verts[indexes[j]];

			if ( r_showTangentSpace.GetInteger() == 1 ) {
				glColor4f( 0.5 + 0.5 * v->tangents[0][0],  0.5 + 0.5 * v->tangents[0][1],  
					0.5 + 0.5 * v->tangents[0][2], 0.5 );
			} else if ( r_showTangentSpace.GetInteger() == 2 ) {
				glColor4f( 0.5 + 0.5 * v->tangents[1][0],  0.5 + 0.5 * v->tangents[1][1],  
					0.5 + 0.5 * v->tangents[1][2], 0.5 );
			} else {
				glColor4f( 0.5 + 0.5 * v->normal[0],  0.5 + 0.5 * v->normal[1],  
					0.5 + 0.5 * v->normal[2], 0.5 );
			}
			glVertex3fv( v->xyz.ToFloatPtr() );
		}
		glEnd();
	}

	GL_State( GLS_DEFAULT );
}

/*
=====================
RB_ShowVertexColor

Draw each triangle with the solid vertex colors
=====================
*/
static void RB_ShowVertexColor( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int		i, j;
	drawSurf_t	*drawSurf;
	const srfTriangles_t	*tri;

	if ( !r_showVertexColor.GetBool() ) {
		return;
	}
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	globalImages->BindNull();
	glDisable( GL_STENCIL_TEST );

	GL_State( GLS_DEPTHFUNC_LESS );

	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		drawSurf = drawSurfs[i];

		RB_SimpleSurfaceSetup( drawSurf );

		tri = drawSurf->geo;
		const idDrawVert *verts = NULL;
		const glIndex_t *indexes = NULL;
		if ( !RB_RendertoolsGetTriDebugGeometry( tri, verts, indexes ) ) {
			continue;
		}
		glBegin( GL_TRIANGLES );
		for ( j = 0 ; j < tri->numIndexes ; j++ ) {
			const idDrawVert *v;

			v = &verts[indexes[j]];
			glColor4ubv( v->color );
			glVertex3fv( v->xyz.ToFloatPtr() );
		}
		glEnd();
	}

	GL_State( GLS_DEFAULT );
}


/*
=====================
RB_ShowNormals

Debugging tool
=====================
*/
static void RB_ShowNormals( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int			i, j;
	drawSurf_t	*drawSurf;
	idVec3		end;
	const srfTriangles_t	*tri;
	float		size;
	bool		showNumbers;
	idVec3		pos;

	if ( r_showNormals.GetFloat() == 0.0f ) {
		return;
	}

	GL_State( GLS_POLYMODE_LINE );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );

	globalImages->BindNull();
	glDisable( GL_STENCIL_TEST );
	if ( !r_debugLineDepthTest.GetBool() ) {
		glDisable( GL_DEPTH_TEST );
	} else {
		glEnable( GL_DEPTH_TEST );
	}

	size = r_showNormals.GetFloat();
	if ( size < 0.0f ) {
		size = -size;
		showNumbers = true;
	} else {
		showNumbers = false;
	}

	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		drawSurf = drawSurfs[i];

		RB_SimpleSurfaceSetup( drawSurf );

		tri = drawSurf->geo;
		const idDrawVert *verts = NULL;
		const glIndex_t *indexes = NULL;
		if ( !RB_RendertoolsGetTriDebugGeometry( tri, verts, indexes ) ) {
			continue;
		}

		glBegin( GL_LINES );
		for ( j = 0 ; j < tri->numVerts ; j++ ) {
			glColor3f( 0, 0, 1 );
			glVertex3fv( verts[j].xyz.ToFloatPtr() );
			VectorMA( verts[j].xyz, size, verts[j].normal, end );
			glVertex3fv( end.ToFloatPtr() );

			glColor3f( 1, 0, 0 );
			glVertex3fv( verts[j].xyz.ToFloatPtr() );
			VectorMA( verts[j].xyz, size, verts[j].tangents[0], end );
			glVertex3fv( end.ToFloatPtr() );

			glColor3f( 0, 1, 0 );
			glVertex3fv( verts[j].xyz.ToFloatPtr() );
			VectorMA( verts[j].xyz, size, verts[j].tangents[1], end );
			glVertex3fv( end.ToFloatPtr() );
		}
		glEnd();
	}

	if ( showNumbers ) {
		RB_SimpleWorldSetup();
		for ( i = 0 ; i < numDrawSurfs ; i++ ) {
			drawSurf = drawSurfs[i];
			tri = drawSurf->geo;
			const idDrawVert *verts = NULL;
			const glIndex_t *indexes = NULL;
			if ( !RB_RendertoolsGetTriDebugGeometry( tri, verts, indexes ) ) {
				continue;
			}
			
			for ( j = 0 ; j < tri->numVerts ; j++ ) {
				R_LocalPointToGlobal( drawSurf->space->modelMatrix, verts[j].xyz + verts[j].tangents[0] + verts[j].normal * 0.2f, pos );
				RB_DrawText( va( "%d", j ), pos, 0.01f, colorWhite, backEnd.viewDef->renderView.viewaxis, 1 );
			}

			for ( j = 0 ; j < tri->numIndexes; j += 3 ) {
				R_LocalPointToGlobal( drawSurf->space->modelMatrix, ( verts[ indexes[ j + 0 ] ].xyz + verts[ indexes[ j + 1 ] ].xyz + verts[ indexes[ j + 2 ] ].xyz ) * ( 1.0f / 3.0f ) + verts[ indexes[ j + 0 ] ].normal * 0.2f, pos );
				RB_DrawText( va( "%d", j / 3 ), pos, 0.01f, colorCyan, backEnd.viewDef->renderView.viewaxis, 1 );
			}
		}
	}

	glEnable( GL_STENCIL_TEST );
}


/*
=====================
RB_ShowNormals

Debugging tool
=====================
*/
static void RB_AltShowNormals( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int			i, j, k;
	drawSurf_t	*drawSurf;
	idVec3		end;
	const srfTriangles_t	*tri;

	if ( r_showNormals.GetFloat() == 0.0f ) {
		return;
	}

	GL_State( GLS_DEFAULT );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );

	globalImages->BindNull();
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_DEPTH_TEST );

	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		drawSurf = drawSurfs[i];

		RB_SimpleSurfaceSetup( drawSurf );

		tri = drawSurf->geo;
		const idDrawVert *verts = NULL;
		const glIndex_t *indexes = NULL;
		if ( !RB_RendertoolsGetTriDebugGeometry( tri, verts, indexes ) || tri->facePlanes == NULL ) {
			continue;
		}
		glBegin( GL_LINES );
		for ( j = 0 ; j < tri->numIndexes ; j += 3 ) {
			const idDrawVert *v[3];
			idVec3		mid;

			v[0] = &verts[indexes[j+0]];
			v[1] = &verts[indexes[j+1]];
			v[2] = &verts[indexes[j+2]];

			// make the midpoint slightly above the triangle
			mid = ( v[0]->xyz + v[1]->xyz + v[2]->xyz ) * ( 1.0f / 3.0f );
			mid += 0.1f * tri->facePlanes[ j / 3 ].Normal();

			for ( k = 0 ; k < 3 ; k++ ) {
				idVec3	pos;

				pos = ( mid + v[k]->xyz * 3.0f ) * 0.25f;

				glColor3f( 0, 0, 1 );
				glVertex3fv( pos.ToFloatPtr() );
				VectorMA( pos, r_showNormals.GetFloat(), v[k]->normal, end );
				glVertex3fv( end.ToFloatPtr() );

				glColor3f( 1, 0, 0 );
				glVertex3fv( pos.ToFloatPtr() );
				VectorMA( pos, r_showNormals.GetFloat(), v[k]->tangents[0], end );
				glVertex3fv( end.ToFloatPtr() );

				glColor3f( 0, 1, 0 );
				glVertex3fv( pos.ToFloatPtr() );
				VectorMA( pos, r_showNormals.GetFloat(), v[k]->tangents[1], end );
				glVertex3fv( end.ToFloatPtr() );

				glColor3f( 1, 1, 1 );
				glVertex3fv( pos.ToFloatPtr() );
				glVertex3fv( v[k]->xyz.ToFloatPtr() );
			}
		}
		glEnd();
	}

	glEnable( GL_DEPTH_TEST );
	glEnable( GL_STENCIL_TEST );
}



/*
=====================
RB_ShowTextureVectors

Draw texture vectors in the center of each triangle
=====================
*/
static void RB_ShowTextureVectors( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int			i, j;
	drawSurf_t	*drawSurf;
	const srfTriangles_t	*tri;

	if ( r_showTextureVectors.GetFloat() == 0.0f ) {
		return;
	}

	GL_State( GLS_DEPTHFUNC_LESS );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );

	globalImages->BindNull();

	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		drawSurf = drawSurfs[i];

		tri = drawSurf->geo;

		const idDrawVert *verts = NULL;
		const glIndex_t *indexes = NULL;
		if ( !RB_RendertoolsGetTriDebugGeometry( tri, verts, indexes ) ) {
			continue;
		}
		if ( !tri->facePlanes ) {
			continue;
		}
		RB_SimpleSurfaceSetup( drawSurf );

		// draw non-shared edges in yellow
		glBegin( GL_LINES );

		for ( j = 0 ; j < tri->numIndexes ; j+= 3 ) {
			const idDrawVert *a, *b, *c;
			float	area, inva;
			idVec3	temp;
			float		d0[5], d1[5];
			idVec3		mid;
			idVec3		tangents[2];

			a = &verts[indexes[j+0]];
			b = &verts[indexes[j+1]];
			c = &verts[indexes[j+2]];

			// make the midpoint slightly above the triangle
			mid = ( a->xyz + b->xyz + c->xyz ) * ( 1.0f / 3.0f );
			mid += 0.1f * tri->facePlanes[ j / 3 ].Normal();

			// calculate the texture vectors
			VectorSubtract( b->xyz, a->xyz, d0 );
			d0[3] = b->st[0] - a->st[0];
			d0[4] = b->st[1] - a->st[1];
			VectorSubtract( c->xyz, a->xyz, d1 );
			d1[3] = c->st[0] - a->st[0];
			d1[4] = c->st[1] - a->st[1];

			area = d0[3] * d1[4] - d0[4] * d1[3];
			if ( area == 0 ) {
				continue;
			}
			inva = 1.0 / area;

			temp[0] = (d0[0] * d1[4] - d0[4] * d1[0]) * inva;
			temp[1] = (d0[1] * d1[4] - d0[4] * d1[1]) * inva;
			temp[2] = (d0[2] * d1[4] - d0[4] * d1[2]) * inva;
			temp.Normalize();
			tangents[0] = temp;
        
			temp[0] = (d0[3] * d1[0] - d0[0] * d1[3]) * inva;
			temp[1] = (d0[3] * d1[1] - d0[1] * d1[3]) * inva;
			temp[2] = (d0[3] * d1[2] - d0[2] * d1[3]) * inva;
			temp.Normalize();
			tangents[1] = temp;

			// draw the tangents
			tangents[0] = mid + tangents[0] * r_showTextureVectors.GetFloat();
			tangents[1] = mid + tangents[1] * r_showTextureVectors.GetFloat();

			glColor3f( 1, 0, 0 );
			glVertex3fv( mid.ToFloatPtr() );
			glVertex3fv( tangents[0].ToFloatPtr() );

			glColor3f( 0, 1, 0 );
			glVertex3fv( mid.ToFloatPtr() );
			glVertex3fv( tangents[1].ToFloatPtr() );
		}

		glEnd();
	}
}

/*
=====================
RB_ShowDominantTris

Draw lines from each vertex to the dominant triangle center
=====================
*/
static void RB_ShowDominantTris( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int			i, j;
	drawSurf_t	*drawSurf;
	const srfTriangles_t	*tri;

	if ( !r_showDominantTri.GetBool() ) {
		return;
	}

	GL_State( GLS_DEPTHFUNC_LESS );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );

	glPolygonOffset( -1, -2 );
	glEnable( GL_POLYGON_OFFSET_LINE );

	globalImages->BindNull();

	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		drawSurf = drawSurfs[i];

		tri = drawSurf->geo;

		const idDrawVert *verts = NULL;
		const glIndex_t *indexes = NULL;
		if ( !RB_RendertoolsGetTriDebugGeometry( tri, verts, indexes ) ) {
			continue;
		}
		if ( !tri->dominantTris ) {
			continue;
		}
		RB_SimpleSurfaceSetup( drawSurf );

		glColor3f( 1, 1, 0 );
		glBegin( GL_LINES );

		for ( j = 0 ; j < tri->numVerts ; j++ ) {
			const idDrawVert *a, *b, *c;
			idVec3		mid;

			// find the midpoint of the dominant tri

			a = &verts[j];
			b = &verts[tri->dominantTris[j].v2];
			c = &verts[tri->dominantTris[j].v3];

			mid = ( a->xyz + b->xyz + c->xyz ) * ( 1.0f / 3.0f );

			glVertex3fv( mid.ToFloatPtr() );
			glVertex3fv( a->xyz.ToFloatPtr() );
		}

		glEnd();
	}
	glDisable( GL_POLYGON_OFFSET_LINE );
}

/*
=====================
RB_ShowEdges

Debugging tool
=====================
*/
static void RB_ShowEdges( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int			i, j, k, m, n, o;
	drawSurf_t	*drawSurf;
	const srfTriangles_t	*tri;
	const silEdge_t			*edge;
	int			danglePlane;

	if ( !r_showEdges.GetBool() ) {
		return;
	}

	GL_State( GLS_DEFAULT );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );

	globalImages->BindNull();
	glDisable( GL_DEPTH_TEST );

	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		drawSurf = drawSurfs[i];

		tri = drawSurf->geo;

		const idDrawVert *verts = NULL;
		const glIndex_t *indexes = NULL;
		if ( !RB_RendertoolsGetTriDebugGeometry( tri, verts, indexes ) ) {
			continue;
		}

		RB_SimpleSurfaceSetup( drawSurf );

		// draw non-shared edges in yellow
		glColor3f( 1, 1, 0 );
		glBegin( GL_LINES );

		for ( j = 0 ; j < tri->numIndexes ; j+= 3 ) {
			for ( k = 0 ; k < 3 ; k++ ) {
				int		l, i1, i2;
				l = ( k == 2 ) ? 0 : k + 1;
				i1 = indexes[j+k];
				i2 = indexes[j+l];

				// if these are used backwards, the edge is shared
				for ( m = 0 ; m < tri->numIndexes ; m += 3 ) {
					for ( n = 0 ; n < 3 ; n++ ) {
						o = ( n == 2 ) ? 0 : n + 1;
						if ( indexes[m+n] == i2 && indexes[m+o] == i1 ) {
							break;
						}
					}
					if ( n != 3 ) {
						break;
					}
				}

				// if we didn't find a backwards listing, draw it in yellow
				if ( m == tri->numIndexes ) {
					glVertex3fv( verts[ i1 ].xyz.ToFloatPtr() );
					glVertex3fv( verts[ i2 ].xyz.ToFloatPtr() );
				}

			}
		}

		glEnd();

		// draw dangling sil edges in red
		if ( !tri->silEdges ) {
			continue;
		}

		// the plane number after all real planes
		// is the dangling edge
		danglePlane = tri->numIndexes / 3;

		glColor3f( 1, 0, 0 );

		glBegin( GL_LINES );
		for ( j = 0 ; j < tri->numSilEdges ; j++ ) {
			edge = tri->silEdges + j;

			if ( edge->p1 != danglePlane && edge->p2 != danglePlane ) {
				continue;
			}

			glVertex3fv( verts[ edge->v1 ].xyz.ToFloatPtr() );
			glVertex3fv( verts[ edge->v2 ].xyz.ToFloatPtr() );
		}
		glEnd();
	}

	glEnable( GL_DEPTH_TEST );
}

/*
==============
RB_ShowLights

Visualize all light volumes used in the current scene
r_showLights 1	: just print volumes numbers, highlighting ones covering the view
r_showLights 2	: also draw planes of each volume
r_showLights 3	: also draw edges of each volume
==============
*/
void RB_ShowLights( void ) {
	const idRenderLightLocal	*light;
	int					count;
	srfTriangles_t		*tri;
	viewLight_t			*vLight;

	if ( !r_showLights.GetInteger() ) {
		return;
	}

	// all volumes are expressed in world coordinates
	RB_SimpleWorldSetup();

	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	globalImages->BindNull();
	glDisable( GL_STENCIL_TEST );


	GL_Cull( CT_TWO_SIDED );
	glDisable( GL_DEPTH_TEST );


	common->Printf( "volumes: " );	// FIXME: not in back end!

	count = 0;
	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		light = vLight->lightDef;
		count++;

		tri = light->frustumTris;

		// depth buffered planes
		if ( r_showLights.GetInteger() >= 2 ) {
			GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHMASK );
			glColor4f( 0, 0, 1, 0.25 );
			glEnable( GL_DEPTH_TEST );
			RB_RenderTriangleSurface( tri );
		}

		// non-hidden lines
		if ( r_showLights.GetInteger() >= 3 ) {
			GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK  );
			glDisable( GL_DEPTH_TEST );
			glColor3f( 1, 1, 1 );
			RB_RenderTriangleSurface( tri );
		}

		int index;

		index = backEnd.viewDef->renderWorld->lightDefs.FindIndex( vLight->lightDef );
		if ( vLight->viewInsideLight ) {
			// view is in this volume
			common->Printf( "[%i] ", index );
		} else {
			common->Printf( "%i ", index );
		}
	}

	glEnable( GL_DEPTH_TEST );
	glDisable( GL_POLYGON_OFFSET_LINE );

	glDepthRange( 0, 1 );
	GL_State( GLS_DEFAULT );
	GL_Cull( CT_FRONT_SIDED );

	common->Printf( " = %i total\n", count );
}

/*
=====================
RB_ShowPortals

Debugging tool, won't work correctly with SMP or when mirrors are present
=====================
*/
void RB_ShowPortals( void ) {
	if ( !r_showPortals.GetBool() ) {
		return;
	}

	// all portals are expressed in world coordinates
	RB_SimpleWorldSetup();

	globalImages->BindNull();
	glDisable( GL_DEPTH_TEST );

	GL_State( GLS_DEFAULT );

	((idRenderWorldLocal *)backEnd.viewDef->renderWorld)->ShowPortals();

	glEnable( GL_DEPTH_TEST );
}

/*
=====================
RB_ShowLightGrid
=====================
*/
static void RB_ShowLightGrid( void ) {
	const int showMode = r_showLightGrid.GetInteger();
	if ( showMode <= 0 ) {
		return;
	}

	idRenderWorldLocal *world = backEnd.viewDef ? backEnd.viewDef->renderWorld : NULL;
	if ( world == NULL || world->portalAreas == NULL ) {
		return;
	}

	RB_SimpleWorldSetup();
	globalImages->BindNull();
	GL_State( GLS_DEFAULT );
	GL_Cull( CT_TWO_SIDED );

	glPointSize( 5.0f );
	glBegin( GL_POINTS );
	for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
		if ( showMode == 1 && areaIndex != backEnd.viewDef->areaNum ) {
			continue;
		}

		const LightGrid &lightGrid = world->portalAreas[ areaIndex ].lightGrid;
		if ( lightGrid.lightGridPoints.Num() <= 0 ) {
			continue;
		}

		const int gridStepY = Max( lightGrid.lightGridBounds[0], 1 );
		const int gridStepZ = Max( lightGrid.lightGridBounds[0] * lightGrid.lightGridBounds[1], 1 );

		for ( int probeIndex = 0; probeIndex < lightGrid.lightGridPoints.Num(); probeIndex++ ) {
			const lightGridPoint_t &gridPoint = lightGrid.lightGridPoints[ probeIndex ];
			if ( gridPoint.valid == 0 && showMode < 3 ) {
				continue;
			}

			idVec3 color( 0.35f, 0.0f, 0.0f );
			if ( gridPoint.valid != 0 ) {
				int gridCoord[3];
				gridCoord[0] = probeIndex % gridStepY;
				gridCoord[1] = ( probeIndex / gridStepY ) % Max( lightGrid.lightGridBounds[1], 1 );
				gridCoord[2] = probeIndex / gridStepZ;
				color = lightGrid.GetGridCoordDebugColor( gridCoord );
				if ( gridPoint.valid == LIGHTGRID_POINT_RELOCATED || gridPoint.valid == LIGHTGRID_POINT_RELOCATED_NEAR_SOLID ) {
					color = idVec3( 1.0f, 0.45f, 0.0f );
				}
				if ( gridPoint.valid == LIGHTGRID_POINT_NEAR_SOLID || gridPoint.valid == LIGHTGRID_POINT_RELOCATED_NEAR_SOLID ) {
					color = idVec3( 1.0f, 1.0f, 0.0f );
				}
			}

			glColor3fv( color.ToFloatPtr() );
			glVertex3fv( gridPoint.origin.ToFloatPtr() );
		}
	}
	glEnd();
	glPointSize( 1.0f );
}

/*
================
RB_ClearDebugText
================
*/
void RB_ClearDebugText( int time ) {
	int			i;
	int			num;
	debugText_t	*text;

	rb_debugTextTime = time;

	if ( !time ) {
		// free up our strings
		text = rb_debugText;
		for ( i = 0 ; i < MAX_DEBUG_TEXT; i++, text++ ) {
			text->text.Clear();
		}
		rb_numDebugText = 0;
		return;
	}

	// copy any text that still needs to be drawn
	num	= 0;
	text = rb_debugText;
	for ( i = 0 ; i < rb_numDebugText; i++, text++ ) {
		if ( text->lifeTime > time ) {
			if ( num != i ) {
				rb_debugText[ num ] = *text;
			}
			num++;
		}
	}
	rb_numDebugText = num;
}

/*
================
RB_AddDebugText
================
*/
void RB_AddDebugText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align, const int lifetime, const bool depthTest ) {
	debugText_t *debugText;

	if ( rb_numDebugText < MAX_DEBUG_TEXT ) {
		debugText = &rb_debugText[ rb_numDebugText++ ];
		debugText->text			= text;
		debugText->origin		= origin;
		debugText->scale		= scale;
		debugText->color		= color;
		debugText->viewAxis		= viewAxis;
		debugText->align		= align;
		debugText->lifeTime		= rb_debugTextTime + lifetime;
		debugText->depthTest	= depthTest;
	}
}

/*
================
RB_DrawTextLength

  returns the length of the given text
================
*/
float RB_DrawTextLength( const char *text, float scale, int len ) {
	int i, num, index, charIndex;
	float spacing, textLen = 0.0f;

	if ( text && *text ) {
		if ( !len ) {
			len = strlen(text);
		}
		for ( i = 0; i < len; i++ ) {
			charIndex = text[i] - 32;
			if ( charIndex < 0 || charIndex > NUM_SIMPLEX_CHARS ) {
				continue;
			}
			num = simplex[charIndex][0] * 2;
			spacing = simplex[charIndex][1];
			index = 2;

			while( index - 2 < num ) {   
				if ( simplex[charIndex][index] < 0) {  
					index++;
					continue; 
				} 
				index += 2;
				if ( simplex[charIndex][index] < 0) {  
					index++;
					continue; 
				} 
			}   
			textLen += spacing * scale;  
		}
	}
	return textLen;
}

/*
================
RB_DrawText

  oriented on the viewaxis
  align can be 0-left, 1-center (default), 2-right
================
*/
static void RB_DrawText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align ) {
	int i, j, len, num, index, charIndex, line;
	float textLen, spacing;
	idVec3 org, p1, p2;

	if ( text && *text ) {
		glBegin( GL_LINES );
		glColor3fv( color.ToFloatPtr() );

		if ( text[0] == '\n' ) {
			line = 1;
		} else {
			line = 0;
		}

		len = strlen( text );
		for ( i = 0; i < len; i++ ) {

			if ( i == 0 || text[i] == '\n' ) {
				org = origin - viewAxis[2] * ( line * 36.0f * scale );
				if ( align != 0 ) {
					for ( j = 1; i+j <= len; j++ ) {
						if ( i+j == len || text[i+j] == '\n' ) {
							textLen = RB_DrawTextLength( text+i, scale, j );
							break;
						}
					}
					if ( align == 2 ) {
						// right
						org += viewAxis[1] * textLen;
					} else {
						// center
						org += viewAxis[1] * ( textLen * 0.5f );
					}
				}
				line++;
			}

			charIndex = text[i] - 32;
			if ( charIndex < 0 || charIndex > NUM_SIMPLEX_CHARS ) {
				continue;
			}
			num = simplex[charIndex][0] * 2;
			spacing = simplex[charIndex][1];
			index = 2;

			while( index - 2 < num ) {
				if ( simplex[charIndex][index] < 0) {  
					index++;
					continue; 
				}
				p1 = org + scale * simplex[charIndex][index] * -viewAxis[1] + scale * simplex[charIndex][index+1] * viewAxis[2];
				index += 2;
				if ( simplex[charIndex][index] < 0) {
					index++;
					continue;
				}
				p2 = org + scale * simplex[charIndex][index] * -viewAxis[1] + scale * simplex[charIndex][index+1] * viewAxis[2];

				glVertex3fv( p1.ToFloatPtr() );
				glVertex3fv( p2.ToFloatPtr() );
			}
			org -= viewAxis[1] * ( spacing * scale );
		}

		glEnd();
	}
}

/*
================
RB_ShowDebugText
================
*/
void RB_ShowDebugText( void ) {
	int			i;
	int			width;
	debugText_t	*text;

	if ( !rb_numDebugText ) {
		return;
	}

	// all lines are expressed in world coordinates
	RB_SimpleWorldSetup();

	globalImages->BindNull();

	width = r_debugLineWidth.GetInteger();
	if ( width < 1 ) {
		width = 1;
	} else if ( width > 10 ) {
		width = 10;
	}

	// draw lines
	GL_State( GLS_POLYMODE_LINE );
	glLineWidth( width );

	if ( !r_debugLineDepthTest.GetBool() ) {
		glDisable( GL_DEPTH_TEST );
	}

	text = rb_debugText;
	for ( i = 0 ; i < rb_numDebugText; i++, text++ ) {
		if ( !text->depthTest ) {
			RB_DrawText( text->text, text->origin, text->scale, text->color, text->viewAxis, text->align );
		}
	}

	if ( !r_debugLineDepthTest.GetBool() ) {
		glEnable( GL_DEPTH_TEST );
	}

	text = rb_debugText;
	for ( i = 0 ; i < rb_numDebugText; i++, text++ ) {
		if ( text->depthTest ) {
			RB_DrawText( text->text, text->origin, text->scale, text->color, text->viewAxis, text->align );
		}
	}

	glLineWidth( 1 );
	GL_State( GLS_DEFAULT );
}

/*
================
RB_ClearDebugLines
================
*/
void RB_ClearDebugLines( int time ) {
	int			i;
	int			num;
	debugLine_t	*line;

	rb_debugLineTime = time;

	if ( !time ) {
		rb_numDebugLines = 0;
		return;
	}

	// copy any lines that still need to be drawn
	num	= 0;
	line = rb_debugLines;
	for ( i = 0 ; i < rb_numDebugLines; i++, line++ ) {
		if ( line->lifeTime > time ) {
			if ( num != i ) {
				rb_debugLines[ num ] = *line;
			}
			num++;
		}
	}
	rb_numDebugLines = num;
}

/*
================
RB_AddDebugLine
================
*/
void RB_AddDebugLine( const idVec4 &color, const idVec3 &start, const idVec3 &end, const int lifeTime, const bool depthTest ) {
	debugLine_t *line;

	if ( rb_numDebugLines < MAX_DEBUG_LINES ) {
		line = &rb_debugLines[ rb_numDebugLines++ ];
		line->rgb		= color;
		line->start		= start;
		line->end		= end;
		line->depthTest = depthTest;
		line->lifeTime	= rb_debugLineTime + lifeTime;
	}
}

/*
================
RB_ShowDebugLines
================
*/
void RB_ShowDebugLines( void ) {
	int			i;
	int			width;
	debugLine_t	*line;

	if ( !rb_numDebugLines ) {
		return;
	}

	// all lines are expressed in world coordinates
	RB_SimpleWorldSetup();

	globalImages->BindNull();

	width = r_debugLineWidth.GetInteger();
	if ( width < 1 ) {
		width = 1;
	} else if ( width > 10 ) {
		width = 10;
	}

	// draw lines
	GL_State( GLS_POLYMODE_LINE );//| GLS_DEPTHMASK ); //| GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	glLineWidth( width );

	if ( !r_debugLineDepthTest.GetBool() ) {
		glDisable( GL_DEPTH_TEST );
	}

	glBegin( GL_LINES );

	line = rb_debugLines;
	for ( i = 0 ; i < rb_numDebugLines; i++, line++ ) {
		if ( !line->depthTest ) {
			glColor3fv( line->rgb.ToFloatPtr() );
			glVertex3fv( line->start.ToFloatPtr() );
			glVertex3fv( line->end.ToFloatPtr() );
		}
	}
	glEnd();

	if ( !r_debugLineDepthTest.GetBool() ) {
		glEnable( GL_DEPTH_TEST );
	}

	glBegin( GL_LINES );

	line = rb_debugLines;
	for ( i = 0 ; i < rb_numDebugLines; i++, line++ ) {
		if ( line->depthTest ) {
			glColor4fv( line->rgb.ToFloatPtr() );
			glVertex3fv( line->start.ToFloatPtr() );
			glVertex3fv( line->end.ToFloatPtr() );
		}
	}

	glEnd();

	glLineWidth( 1 );
	GL_State( GLS_DEFAULT );
}

/*
================
RB_ClearDebugPolygons
================
*/
void RB_ClearDebugPolygons( int time ) {
	int				i;
	int				num;
	debugPolygon_t	*poly;

	rb_debugPolygonTime = time;

	if ( !time ) {
		rb_numDebugPolygons = 0;
		return;
	}

	// copy any polygons that still need to be drawn
	num	= 0;

	poly = rb_debugPolygons;
	for ( i = 0 ; i < rb_numDebugPolygons; i++, poly++ ) {
		if ( poly->lifeTime > time ) {
			if ( num != i ) {
				rb_debugPolygons[ num ] = *poly;
			}
			num++;
		}
	}
	rb_numDebugPolygons = num;
}

/*
================
RB_AddDebugPolygon
================
*/
void RB_AddDebugPolygon( const idVec4 &color, const idWinding &winding, const int lifeTime, const bool depthTest ) {
	debugPolygon_t *poly;

	if ( rb_numDebugPolygons < MAX_DEBUG_POLYGONS ) {
		poly = &rb_debugPolygons[ rb_numDebugPolygons++ ];
		poly->rgb		= color;
		poly->winding	= winding;
		poly->depthTest = depthTest;
		poly->lifeTime	= rb_debugPolygonTime + lifeTime;
	}
}

/*
================
RB_ShowDebugPolygons
================
*/
void RB_ShowDebugPolygons( void ) {
	int				i, j;
	debugPolygon_t	*poly;

	if ( !rb_numDebugPolygons ) {
		return;
	}

	// all lines are expressed in world coordinates
	RB_SimpleWorldSetup();

	globalImages->BindNull();

	glDisable( GL_TEXTURE_2D );
	glDisable( GL_STENCIL_TEST );

	glEnable( GL_DEPTH_TEST );

	if ( r_debugPolygonFilled.GetBool() ) {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHMASK );
		glPolygonOffset( -1, -2 );
		glEnable( GL_POLYGON_OFFSET_FILL );
	} else {
		GL_State( GLS_POLYMODE_LINE );
		glPolygonOffset( -1, -2 );
		glEnable( GL_POLYGON_OFFSET_LINE );
	}

	poly = rb_debugPolygons;
	for ( i = 0 ; i < rb_numDebugPolygons; i++, poly++ ) {
//		if ( !poly->depthTest ) {

			glColor4fv( poly->rgb.ToFloatPtr() );

			glBegin( GL_POLYGON );

			for ( j = 0; j < poly->winding.GetNumPoints(); j++) {
				glVertex3fv( poly->winding[j].ToFloatPtr() );
			}

			glEnd();
//		}
	}

	GL_State( GLS_DEFAULT );

	if ( r_debugPolygonFilled.GetBool() ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	} else {
		glDisable( GL_POLYGON_OFFSET_LINE );
	}

	glDepthRange( 0, 1 );
	GL_State( GLS_DEFAULT );
}

/*
================
RB_TestGamma
================
*/
#define	G_WIDTH		512
#define	G_HEIGHT	512
#define	BAR_HEIGHT	64

void RB_TestGamma( void ) {
	byte	image[G_HEIGHT][G_WIDTH][4];
	int		i, j;
	int		c, comp;
	int		v, dither;
	int		mask, y;

	if ( r_testGamma.GetInteger() <= 0 ) {
		return;
	}

	v = r_testGamma.GetInteger();
	if ( v <= 1 || v >= 196 ) {
		v = 128;
	}

	memset( image, 0, sizeof( image ) );

	for ( mask = 0 ; mask < 8 ; mask++ ) {
		y = mask * BAR_HEIGHT;
		for ( c = 0 ; c < 4 ; c++ ) {
			v = c * 64 + 32;
			// solid color
			for ( i = 0 ; i < BAR_HEIGHT/2 ; i++ ) {
				for ( j = 0 ; j < G_WIDTH/4 ; j++ ) {
					for ( comp = 0 ; comp < 3 ; comp++ ) {
						if ( mask & ( 1 << comp ) ) {
							image[y+i][c*G_WIDTH/4+j][comp] = v;
						}
					}
				}
				// dithered color
				for ( j = 0 ; j < G_WIDTH/4 ; j++ ) {
					if ( ( i ^ j ) & 1 ) {
						dither = c * 64;
					} else {
						dither = c * 64 + 63;
					}
					for ( comp = 0 ; comp < 3 ; comp++ ) {
						if ( mask & ( 1 << comp ) ) {
							image[y+BAR_HEIGHT/2+i][c*G_WIDTH/4+j][comp] = dither;
						}
					}
				}
			}
		}
	}

	// draw geometrically increasing steps in the bottom row
	y = 0 * BAR_HEIGHT;
	float	scale = 1;
	for ( c = 0 ; c < 4 ; c++ ) {
		v = (int)(64 * scale);
		if ( v < 0 ) {
			v = 0;
		} else if ( v > 255 ) {
			v = 255;
		}
		scale = scale * 1.5;
		for ( i = 0 ; i < BAR_HEIGHT ; i++ ) {
			for ( j = 0 ; j < G_WIDTH/4 ; j++ ) {
				image[y+i][c*G_WIDTH/4+j][0] = v;
				image[y+i][c*G_WIDTH/4+j][1] = v;
				image[y+i][c*G_WIDTH/4+j][2] = v;
			}
		}
	}


	glLoadIdentity();

	glMatrixMode( GL_PROJECTION );
	GL_State( GLS_DEPTHFUNC_ALWAYS );
	glColor3f( 1, 1, 1 );
	glPushMatrix();
	glLoadIdentity(); 
	glDisable( GL_TEXTURE_2D );
    glOrtho( 0, 1, 0, 1, -1, 1 );
	glRasterPos2f( 0.01f, 0.01f );
	glDrawPixels( G_WIDTH, G_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, image );
	glPopMatrix();
	glEnable( GL_TEXTURE_2D );
	glMatrixMode( GL_MODELVIEW );
}


/*
==================
RB_TestGammaBias
==================
*/
static void RB_TestGammaBias( void ) {
	byte	image[G_HEIGHT][G_WIDTH][4];

	if ( r_testGammaBias.GetInteger() <= 0 ) {
		return;
	}

	int y = 0;
	for ( int bias = -40 ; bias < 40 ; bias+=10, y += BAR_HEIGHT ) {
		float	scale = 1;
		for ( int c = 0 ; c < 4 ; c++ ) {
			int v = (int)(64 * scale + bias);
			scale = scale * 1.5;
			if ( v < 0 ) {
				v = 0;
			} else if ( v > 255 ) {
				v = 255;
			}
			for ( int i = 0 ; i < BAR_HEIGHT ; i++ ) {
				for ( int j = 0 ; j < G_WIDTH/4 ; j++ ) {
					image[y+i][c*G_WIDTH/4+j][0] = v;
					image[y+i][c*G_WIDTH/4+j][1] = v;
					image[y+i][c*G_WIDTH/4+j][2] = v;
				}
			}
		}
	}


	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	GL_State( GLS_DEPTHFUNC_ALWAYS );
	glColor3f( 1, 1, 1 );
	glPushMatrix();
	glLoadIdentity(); 
	glDisable( GL_TEXTURE_2D );
    glOrtho( 0, 1, 0, 1, -1, 1 );
	glRasterPos2f( 0.01f, 0.01f );
	glDrawPixels( G_WIDTH, G_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, image );
	glPopMatrix();
	glEnable( GL_TEXTURE_2D );
	glMatrixMode( GL_MODELVIEW );
}

/*
================
RB_TestImage

Display a single image over most of the screen
================
*/
void RB_TestImage( void ) {
	idImage	*image;
	int		max;
	float	w, h;

	image = tr.testImage;
	if ( !image ) {
		return;
	}

	if ( tr.testVideo ) {
		cinData_t	cin;

		cin = tr.testVideo->ImageForTime( (int)(1000 * ( backEnd.viewDef->floatTime - tr.testVideoStartTime ) ) );
		if ( cin.image ) {
			image->UploadScratch( cin.image, cin.imageWidth, cin.imageHeight );
		} else {
			tr.testImage = NULL;
			return;
		}
		w = 0.25;
		h = 0.25;
	} else {
		max = image->GetOpts().width > image->GetOpts().height ? image->GetOpts().width : image->GetOpts().height;

		w = 0.25 * image->GetOpts().width / max;
		h = 0.25 * image->GetOpts().height / max;

		w *= (float)glConfig.vidHeight / glConfig.vidWidth;
	}

	glLoadIdentity();

	glMatrixMode( GL_PROJECTION );
	GL_State( GLS_DEPTHFUNC_ALWAYS | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	glColor3f( 1, 1, 1 );
	glPushMatrix();
	glLoadIdentity(); 
    glOrtho( 0, 1, 0, 1, -1, 1 );

	tr.testImage->Bind();
	glBegin( GL_QUADS );
	
	glTexCoord2f( 0, 1 );
	glVertex2f( 0.5 - w, 0 );

	glTexCoord2f( 0, 0 );
	glVertex2f( 0.5 - w, h*2 );

	glTexCoord2f( 1, 0 );
	glVertex2f( 0.5 + w, h*2 );

	glTexCoord2f( 1, 1 );
	glVertex2f( 0.5 + w, 0 );

	glEnd();

	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
}

/*
=================
RB_RenderDebugTools
=================
*/
void RB_RenderDebugTools( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	// don't do anything if this was a 2D rendering
	if ( !backEnd.viewDef->viewEntitys ) {
		return;
	}

	RB_LogComment( "---------- RB_RenderDebugTools ----------\n" );

	GL_State( GLS_DEFAULT );
	backEnd.currentScissor = backEnd.viewDef->scissor;
	glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1, 
		backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
		backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
		backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );


	RB_ShowLightCount();
	RB_ShowShadowCount();
	RB_ShowTexturePolarity( drawSurfs, numDrawSurfs );
	RB_ShowTangentSpace( drawSurfs, numDrawSurfs );
	RB_ShowVertexColor( drawSurfs, numDrawSurfs );
	RB_ShowTris( drawSurfs, numDrawSurfs );
	RB_ShowUnsmoothedTangents( drawSurfs, numDrawSurfs );
	RB_ShowSurfaceInfo( drawSurfs, numDrawSurfs );
	RB_ShowEdges( drawSurfs, numDrawSurfs );
	RB_ShowNormals( drawSurfs, numDrawSurfs );
	RB_ShowViewEntitys( backEnd.viewDef->viewEntitys );
	RB_ShowLights();
	RB_ReportViewLights();
	RB_DrawCachedViewLightDebugVisuals();
	RB_ShowLightGrid();
	RB_ShowTextureVectors( drawSurfs, numDrawSurfs );
	RB_ShowDominantTris( drawSurfs, numDrawSurfs );
	if ( r_testGamma.GetInteger() > 0 ) {	// test here so stack check isn't so damn slow on debug builds
		RB_TestGamma();
	}
	if ( r_testGammaBias.GetInteger() > 0 ) {
		RB_TestGammaBias();
	}
	RB_TestImage();
	RB_ShowPortals();
	RB_ShowSilhouette();
	RB_ShowDepthBuffer();
	RB_ShowIntensity();
	RB_ShowDebugLines();
	RB_ShowDebugText();
	RB_ShowDebugPolygons();
	RB_ShowTrace( drawSurfs, numDrawSurfs );
}

/*
=================
RB_ShutdownDebugTools
=================
*/
void RB_ShutdownDebugTools( void ) {
	for ( int i = 0; i < MAX_DEBUG_POLYGONS; i++ ) {
		rb_debugPolygons[i].winding.Clear();
	}
	RB_ClearViewLightDebugVisuals();
}
