// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	gles_d3 -- stencil shadow volumes.

	Ports RB_StencilShadowPass / RB_T_Shadow (draw_common.cpp:7783, :7611),
	which live in a translation unit this module excludes.

	The near-plane question this stage was flagged on turned out to be
	settled before it started: the ARB2 shadow vertex program and the d3es
	GLES port both extrude with the homogeneous-coordinate trick and neither
	uses GL_DEPTH_CLAMP -- a grep across the whole d3es renderer finds none,
	and it casts correct shadows on real Android hardware. So the absence of
	depth clamp on ES costs nothing here.


===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "gles_d3_local.h"
#include "gles_program.h"

static glesProgram_t *		gles_shadowProgram = NULL;
static const viewEntity_t *	gles_shadowOriginSpace = NULL;
static int					gles_shadowDraws = 0;

/*
====================
GLESD3_DrawShadowElements

RB_DrawShadowElementsWithCounters. numIndexes may be fewer than the surface
carries: the caps are skipped when the view cannot see through them.
====================
*/
static void GLESD3_DrawShadowElements( const srfTriangles_t *tri, int numIndexes ) {
	if ( numIndexes <= 0 ) {
		return;
	}
	// The server may disable optional index caching. Upload the CPU stream
	// through the same bounded fallback used by ordinary GLES draws.
	vertCache_t *indexCache = R_GLESD3_EnsureIndexCache( tri );
	if ( indexCache == NULL ) {
		return;
	}

	backEnd.pc.c_shadowElements++;
	backEnd.pc.c_shadowIndexes += numIndexes;
	backEnd.pc.c_shadowVertexes += tri->numVerts;

	glDrawElements( GL_TRIANGLES, numIndexes, GL_INDEX_TYPE,
			vertexCache.Position( indexCache ) );
	gles_shadowDraws++;
}

/*
====================
GLESD3_BindShadowVertexes

Shadow geometry is shadowCache_t -- a bare idVec4 -- not idDrawVert, so the
position attribute is FOUR components with a 16-byte stride, and the w
component carries the extrusion flag the vertex shader keys on. Binding it as
three components would silently drop every extruded vertex onto its near cap
and produce no volume at all.
====================
*/
static bool GLESD3_BindShadowVertexes( const srfTriangles_t *tri ) {
	if ( tri->shadowCache == NULL ) {
		return false;
	}
	const void *base = vertexCache.Position( tri->shadowCache );

	R_GLESD3_EnableAttributesForShadow();
	glVertexAttribPointer( GLESD3_ATTR_POSITION, 4, GL_FLOAT, GL_FALSE,
			sizeof( shadowCache_t ),
			RB_DrawVertAttributePointer( base, offsetof( shadowCache_t, xyz ) ) );
	return true;
}

/*
====================
RB_GLESD3_T_Shadow
====================
*/
static void RB_GLESD3_T_Shadow( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = surf->geo;

	if ( tri == NULL || tri->shadowCache == NULL
			|| ( tri->indexCache == NULL && tri->indexes == NULL ) ) {
		return;
	}

	// the light origin is per-space, and the shader wants it local with w = 0
	if ( surf->space != gles_shadowOriginSpace ) {
		idVec4 localLight;
		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.vLight->globalLightOrigin,
				localLight.ToVec3() );
		localLight.w = 0.0f;
		glUniform4fv( gles_shadowProgram->uLightOrigin, 1, localLight.ToFloatPtr() );
		gles_shadowOriginSpace = surf->space;
	}

	if ( surf->space != backEnd.currentSpace ) {
		backEnd.currentSpace = surf->space;
	}
	float mvp[ 16 ];
	myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mvp );
	glUniformMatrix4fv( gles_shadowProgram->uMVP, 1, GL_FALSE, mvp );

	// an empty rect covers no pixels: skip rather than issue a rejected call
	if ( !RB_GLESD3_SetScissor( surf->scissorRect ) ) {
		return;
	}

	if ( !GLESD3_BindShadowVertexes( tri ) ) {
		return;
	}

	// the sil planes are always drawn; the caps often are not
	int numIndexes;
	bool external = false;

	if ( !r_useExternalShadows.GetInteger() ) {
		numIndexes = tri->numIndexes;
	} else if ( r_useExternalShadows.GetInteger() == 2 ) {
		numIndexes = tri->numShadowIndexesNoCaps;
	} else if ( !( surf->dsFlags & DSF_VIEW_INSIDE_SHADOW ) ) {
		// outside the shadow projection: no caps are ever needed
		numIndexes = tri->numShadowIndexesNoCaps;
		external = true;
	} else if ( !backEnd.vLight->viewInsideLight
			&& !( tri->shadowCapPlaneBits & SHADOW_CAP_INFINITE ) ) {
		if ( backEnd.vLight->viewSeesShadowPlaneBits & tri->shadowCapPlaneBits ) {
			numIndexes = tri->numShadowIndexesNoFrontCaps;
		} else {
			numIndexes = tri->numShadowIndexesNoCaps;
		}
		external = true;
	} else {
		numIndexes = tri->numIndexes;
	}

	// a surface that could not use the external optimization has had its
	// no-caps counts forced back to the full count; treat it as internal so
	// the robust depth-fail path below runs
	if ( numIndexes == tri->numIndexes ) {
		external = false;
	}

	// Two-sided in one draw. ES 3.0 has glStencilOpSeparate in core and, with
	// GL_INCR_WRAP / GL_DECR_WRAP (also core), the interleaved single-pass
	// deltas are order-equivalent to the legacy cull-flipped two-pass
	// sequence, so the resulting stencil buffer is identical.
	//
	// In non-mirror views CT_FRONT_SIDED culls GL_FRONT (idTech4 winding), so
	// the ops of the legacy CT_FRONT_SIDED draws belong to the GL_BACK face;
	// a mirror view flips them exactly as GL_Cull does.
	const GLenum frontSidedFace = backEnd.viewDef->isMirror ? GL_FRONT : GL_BACK;
	const GLenum backSidedFace = backEnd.viewDef->isMirror ? GL_BACK : GL_FRONT;

	GL_Cull( CT_TWO_SIDED );

	if ( !external ) {
		// Depth-fail ("Carmack's reverse"; the patent expired in 2019, after
		// the preload workaround was written). One capped draw counting the
		// volume faces that FAIL the depth test.
		//
		// This is fragment-for-fragment what the old preload + depth-pass pair
		// computed: preload wrote front -1 / back +1 on every fragment, the
		// depth-pass draw wrote front +1 / back -1 where depth passed, and with
		// the wrap ops the mod-2^N sums cancel to exactly "front -1 / back +1
		// where depth fails". Same stencil buffer, half the stencil fill --
		// which on a mobile tiler is the dominant cost of every volume the
		// view sits inside.
		glStencilOpSeparate( frontSidedFace, GL_KEEP, tr.stencilDecr, GL_KEEP );
		glStencilOpSeparate( backSidedFace, GL_KEEP, tr.stencilIncr, GL_KEEP );
		GLESD3_DrawShadowElements( tri, numIndexes );
		return;
	}

	// traditional depth-pass stencil shadows for external volumes, which are
	// never clipped by the near plane and need no caps
	glStencilOpSeparate( frontSidedFace, GL_KEEP, GL_KEEP, tr.stencilIncr );
	glStencilOpSeparate( backSidedFace, GL_KEEP, GL_KEEP, tr.stencilDecr );
	GLESD3_DrawShadowElements( tri, numIndexes );
}

/*
====================
RB_GLESD3_StencilShadowPass

Stencil test is already enabled and the buffer already holds
R_SafeStencilClearValue() (128) wherever a surface can receive shadows -- the
view clear in RB_GLESD3_BeginDrawingView does that.

On return the stencil function is left as GL_GEQUAL 128, which is what the
interaction pass draws through.
====================
*/
void RB_GLESD3_StencilShadowPass( const drawSurf_t *drawSurfs ) {
	if ( !r_shadows.GetBool() || drawSurfs == NULL ) {
		return;
	}

	gles_shadowProgram = R_GLESD3_Program( GLESD3_PROGRAM_STENCIL_SHADOW );
	if ( gles_shadowProgram == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_GLESD3_StencilShadowPass ----------\n" );

	R_GLESD3_UseProgram( gles_shadowProgram );

	if ( r_showShadows.GetInteger() ) {
		// GLS_POLYMODE_LINE has no ES equivalent, so mode 1 renders as mode 2
		GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_LESS );
	} else {
		// stencil only: no colour, no depth
		GL_State( GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS );
	}

	if ( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() ) {
		glPolygonOffset( r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat() );
		glEnable( GL_POLYGON_OFFSET_FILL );
	}

	glStencilFunc( GL_ALWAYS, 1, 255 );

	// tracked separately from backEnd.currentSpace, which the interaction pass
	// also advances
	gles_shadowOriginSpace = NULL;

	for ( const drawSurf_t *surf = drawSurfs; surf != NULL; surf = surf->nextOnLight ) {
		RB_GLESD3_T_Shadow( surf );
	}

	GL_Cull( CT_FRONT_SIDED );

	if ( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}

	// shadowed pixels ended up below 128; the interaction pass draws where the
	// stencil is still at or above it
	glStencilFunc( GL_GEQUAL, 128, 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );

	// the shadow program bound position as vec4 from shadowCache_t; the next
	// pass rebinds from idDrawVert, so drop the enables rather than leave a
	// four-component pointer aimed at a 32-byte-stride buffer
	R_GLESD3_InvalidateAttributeState();
}

/*
====================
RB_GLESD3_ShadowStats
====================
*/
int RB_GLESD3_ShadowDrawCount( void ) {
	return gles_shadowDraws;
}

void RB_GLESD3_ResetShadowDrawCount( void ) {
	gles_shadowDraws = 0;
}

#endif /* OPENQ4_RENDERER_GLES_MODULE */
