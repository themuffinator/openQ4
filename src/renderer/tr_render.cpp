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
#include "draw_pbr.h"

/*

  back end scene + lights rendering functions

*/

static ID_INLINE GLint R_SafeStencilClearValue() {
	const int stencilBits = idMath::ClampInt( 1, 30, ( glConfig.stencilBits > 0 ) ? glConfig.stencilBits : 8 );
	return 1 << ( stencilBits - 1 );
}

static ID_INLINE bool RB_HasPackedPrimBatchMesh( const srfTriangles_t *tri ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	return tri != NULL && tri->primBatchMesh != NULL;
#else
	(void)tri;
	return false;
#endif
}


/*
=================
RB_DrawElementsImmediate

Draws with immediate mode commands, which is going to be very slow.
This should never happen if the vertex cache is operating properly.
=================
*/
void RB_DrawElementsImmediate( const srfTriangles_t *tri ) {
	const idDrawVert *drawVerts = tri->verts;
	const glIndex_t *drawIndexes = tri->indexes;

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->primBatchMesh != NULL && ( drawVerts == NULL || drawIndexes == NULL ) ) {
		if ( tri->numVerts <= 0 || tri->numIndexes <= 0 ) {
			return;
		}

		idDrawVert *tempVerts = (idDrawVert *)R_FrameAlloc( tri->numVerts * sizeof( tempVerts[0] ) );
		glIndex_t *tempIndexes = (glIndex_t *)R_FrameAlloc( tri->numIndexes * sizeof( tempIndexes[0] ) );
		renderSystem->CopyPrimBatchTriangles( tempVerts, tempIndexes, tri->primBatchMesh, tri->silTraceVerts );
		drawVerts = tempVerts;
		drawIndexes = tempIndexes;
	}
#endif

	if ( drawVerts == NULL || drawIndexes == NULL ) {
		return;
	}

	backEnd.pc.c_drawElements++;
	backEnd.pc.c_drawIndexes += tri->numIndexes;
	backEnd.pc.c_drawVertexes += tri->numVerts;

	if ( tri->ambientSurface != NULL  ) {
		if ( tri->indexes == tri->ambientSurface->indexes ) {
			backEnd.pc.c_drawRefIndexes += tri->numIndexes;
		}
		if ( tri->verts == tri->ambientSurface->verts ) {
			backEnd.pc.c_drawRefVertexes += tri->numVerts;
		}
	}

	glBegin( GL_TRIANGLES );
	for ( int i = 0 ; i < tri->numIndexes ; i++ ) {
		glTexCoord2fv( drawVerts[ drawIndexes[i] ].st.ToFloatPtr() );
		glVertex3fv( drawVerts[ drawIndexes[i] ].xyz.ToFloatPtr() );
	}
	glEnd();
}

static bool RB_EnsurePackedRenderTriangleCaches( const srfTriangles_t *tri, bool needsLighting, bool createIndexCache ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri == NULL || tri->primBatchMesh == NULL ) {
		return true;
	}

	srfTriangles_t *mutableTri = const_cast<srfTriangles_t *>( tri );
	const bool needsIndexCache = createIndexCache && r_useIndexBuffers.GetBool() && tri->numIndexes > 0;
	if ( mutableTri->ambientCache == NULL || ( needsIndexCache && mutableTri->indexCache == NULL ) ) {
		if ( !R_CreatePackedSurfaceFrameCaches( mutableTri, needsLighting, createIndexCache ) ) {
			return false;
		}
	}

	R_TouchVertexCache( mutableTri->ambientCache );
	if ( mutableTri->indexCache != NULL ) {
		R_TouchVertexCache( mutableTri->indexCache );
	}
#else
	(void)tri;
	(void)needsLighting;
	(void)createIndexCache;
#endif

	return true;
}


/*
================
RB_DrawElementsWithCounters
================
*/
void RB_DrawElementsWithCounters( const srfTriangles_t *tri ) {
	if ( RB_HasPackedPrimBatchMesh( tri ) && tr.backEndRenderer == BE_ARB2 ) {
		if ( RB_ARB2_DrawPreparedPackedMD5RStageBatches( tri ) ) {
			return;
		}
		if ( RB_ARB2_DrawPreparedPackedMD5RDirectBatches( tri ) ) {
			return;
		}
	}

	backEnd.pc.c_drawElements++;
	backEnd.pc.c_drawIndexes += tri->numIndexes;
	backEnd.pc.c_drawVertexes += tri->numVerts;

	if ( tri->ambientSurface != NULL  ) {
		if ( tri->indexes == tri->ambientSurface->indexes ) {
			backEnd.pc.c_drawRefIndexes += tri->numIndexes;
		}
		if ( tri->verts == tri->ambientSurface->verts ) {
			backEnd.pc.c_drawRefVertexes += tri->numVerts;
		}
	}

	if ( tri->indexCache && r_useIndexBuffers.GetBool() ) {
		glDrawElements( GL_TRIANGLES, 
						r_singleTriangle.GetBool() ? 3 : tri->numIndexes,
						GL_INDEX_TYPE,
						(int *)vertexCache.Position( tri->indexCache ) );
		backEnd.pc.c_vboIndexes += tri->numIndexes;
	} else {
		// The modern renderer can leave an element buffer on the legacy VAO
		// even when classic index uploads are disabled. A CPU index pointer is
		// valid only with EBO zero; otherwise GL interprets it as an offset.
		vertexCache.UnbindIndex();
		glDrawElements( GL_TRIANGLES, 
						r_singleTriangle.GetBool() ? 3 : tri->numIndexes,
						GL_INDEX_TYPE,
						tri->indexes );
	}
}

/*
================
RB_DrawShadowElementsWithCounters

May not use all the indexes in the surface if caps are skipped
================
*/
void RB_DrawShadowElementsWithCounters( const drawSurf_t *surf, int numIndexes ) {
	const srfTriangles_t *tri = surf->geo;

	if ( RB_HasPackedPrimBatchMesh( tri ) ) {
		RB_ARB2_MD5R_DrawShadowElements( surf, numIndexes );
		return;
	}

	backEnd.pc.c_shadowElements++;
	backEnd.pc.c_shadowIndexes += numIndexes;
	backEnd.pc.c_shadowVertexes += tri->numVerts;

	if ( tri->indexCache && r_useIndexBuffers.GetBool() ) {
		glDrawElements( GL_TRIANGLES, 
						r_singleTriangle.GetBool() ? 3 : numIndexes,
						GL_INDEX_TYPE,
						(int *)vertexCache.Position( tri->indexCache ) );
		backEnd.pc.c_vboIndexes += numIndexes;
	} else {
		vertexCache.UnbindIndex();
		glDrawElements( GL_TRIANGLES, 
						r_singleTriangle.GetBool() ? 3 : numIndexes,
						GL_INDEX_TYPE,
						tri->indexes );
	}
}


/*
===============
RB_RenderTriangleSurface

Sets texcoord and vertex pointers
===============
*/
void RB_RenderTriangleSurface( const srfTriangles_t *tri ) {
	if ( !RB_EnsurePackedRenderTriangleCaches( tri, false, true ) ) {
		return;
	}

	if ( !tri->ambientCache ) {
		RB_DrawElementsImmediate( tri );
		return;
	}


	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );

	RB_DrawElementsWithCounters( tri );
}

/*
===============
RB_T_RenderTriangleSurface

===============
*/
void RB_T_RenderTriangleSurface( const drawSurf_t *surf ) {
	if ( !RB_EnsurePackedRenderTriangleCaches(
		surf->geo,
		( surf->material != NULL ) ? surf->material->ReceivesLighting() : false,
		true ) ) {
		return;
	}

	RB_RenderTriangleSurface( surf->geo );
}

static float RB_CalcFovForAspect( float fovX, float width, float height ) {
	const float clampedFovX = idMath::ClampFloat( 0.75f, 179.0f, fovX );
	const float safeWidth = Max( width, 1.0f );
	const float safeHeight = Max( height, 1.0f );
	const float x = safeWidth / idMath::Tan( DEG2RAD( clampedFovX ) * 0.5f );
	return RAD2DEG( idMath::ATan( safeHeight / Max( x, idMath::FLOAT_EPSILON ) ) ) * 2.0f;
}

/*
===============
R_GetDepthHackProjectionMatrix

Copies the view projection and applies the depth-hack transforms
(modelDepthHack takes precedence; the weapon hack applies the optional
cl_gunfov override before squashing depth). Single home for this math: the
fixed-function depth-hack loads and the packed-MD5R env-param path both
delegate here so the variants cannot diverge.
===============
*/
void R_GetDepthHackProjectionMatrix( const viewDef_t *viewDef, bool weaponDepthHack, float modelDepthHack, float matrix[16] ) {
	memcpy( matrix, viewDef->projectionMatrix, sizeof( float ) * 16 );

	if ( modelDepthHack != 0.0f ) {
		matrix[14] -= modelDepthHack;
		return;
	}
	if ( !weaponDepthHack ) {
		return;
	}

	// a VR eye's off-axis projection is the eye's; a symmetric gun FOV would tear it
	const float weaponFovOverride = cl_gunfov.GetFloat();
	if ( weaponFovOverride > 0.0f && !viewDef->renderView.asymmetricFov ) {
		const float viewportWidth = static_cast<float>( Max( 1, viewDef->viewport.x2 - viewDef->viewport.x1 + 1 ) );
		const float viewportHeight = static_cast<float>( Max( 1, viewDef->viewport.y2 - viewDef->viewport.y1 + 1 ) );

		float weaponFovX = idMath::ClampFloat( 30.0f, 160.0f, weaponFovOverride );
		float weaponFovY = 0.0f;
		if ( cl_gunfov_adjust.GetBool() ) {
			weaponFovY = RB_CalcFovForAspect( weaponFovX, 4.0f, 3.0f );
			weaponFovX = RB_CalcFovForAspect( weaponFovY, viewportHeight, viewportWidth );
		} else {
			weaponFovY = RB_CalcFovForAspect( weaponFovX, viewportWidth, viewportHeight );
		}

		weaponFovX = idMath::ClampFloat( 1.0f, 179.0f, weaponFovX );
		weaponFovY = idMath::ClampFloat( 1.0f, 179.0f, weaponFovY );

		matrix[0] = 1.0f / idMath::Tan( DEG2RAD( weaponFovX ) * 0.5f );
		matrix[5] = 1.0f / idMath::Tan( DEG2RAD( weaponFovY ) * 0.5f );
	}

	matrix[14] *= 0.25f;
}

/*
===============
RB_EnterWeaponDepthHack
===============
*/
void RB_EnterWeaponDepthHack() {
	glDepthRange( 0.0f, 0.5f );

	float	matrix[16];
	R_GetDepthHackProjectionMatrix( backEnd.viewDef, true, 0.0f, matrix );

	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf( matrix );
	glMatrixMode(GL_MODELVIEW);
}

/*
===============
RB_EnterModelDepthHack
===============
*/
void RB_EnterModelDepthHack( float depth ) {
	glDepthRange( 0.0f, 1.0f );

	float	matrix[16];
	R_GetDepthHackProjectionMatrix( backEnd.viewDef, false, depth, matrix );

	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf( matrix );
	glMatrixMode(GL_MODELVIEW);
}

/*
===============
RB_LeaveDepthHack
===============
*/
void RB_LeaveDepthHack() {
	glDepthRange( 0, 1 );

	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode(GL_MODELVIEW);
}

/*
====================
RB_RenderDrawSurfListWithFunction

The triangle functions can check backEnd.currentSpace != surf->space
to see if they need to perform any new matrix setup.  The modelview
matrix will already have been loaded, and backEnd.currentSpace will
be updated after the triangle function completes.
====================
*/
void RB_RenderDrawSurfListWithFunction( drawSurf_t **drawSurfs, int numDrawSurfs, 
											  void (*triFunc_)( const drawSurf_t *) ) {
	int				i;
	const drawSurf_t		*drawSurf;

	backEnd.currentSpace = NULL;

	for (i = 0  ; i < numDrawSurfs ; i++ ) {
		drawSurf = drawSurfs[i];

		// change the matrix if needed
		if ( drawSurf->space != backEnd.currentSpace ) {
			glLoadMatrixf( drawSurf->space->modelViewMatrix );
		}

		if ( drawSurf->space->weaponDepthHack ) {
			RB_EnterWeaponDepthHack();
		}

		if ( drawSurf->space->modelDepthHack != 0.0f ) {
			RB_EnterModelDepthHack( drawSurf->space->modelDepthHack );
		}

		// change the scissor if needed
		if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( drawSurf->scissorRect ) ) {
			backEnd.currentScissor = drawSurf->scissorRect;
			glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1, 
				backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		}

		// render it
		triFunc_( drawSurf );

		if ( drawSurf->space->weaponDepthHack || drawSurf->space->modelDepthHack != 0.0f ) {
			RB_LeaveDepthHack();
		}

		backEnd.currentSpace = drawSurf->space;
	}
}

/*
====================
RB_RenderDrawSurfListWithFunctionIgnoreScissor

Special-effect capture passes render into fixed-size offscreen targets, so they must
not inherit per-surface screen scissors from the main framebuffer view.
====================
*/
void RB_RenderDrawSurfListWithFunctionIgnoreScissor( drawSurf_t **drawSurfs, int numDrawSurfs,
											  void (*triFunc_)( const drawSurf_t *) ) {
	int i;
	const drawSurf_t *drawSurf;

	backEnd.currentSpace = NULL;

	for ( i = 0; i < numDrawSurfs; i++ ) {
		drawSurf = drawSurfs[i];

		if ( drawSurf->space != backEnd.currentSpace ) {
			glLoadMatrixf( drawSurf->space->modelViewMatrix );
		}

		if ( drawSurf->space->weaponDepthHack ) {
			RB_EnterWeaponDepthHack();
		}

		if ( drawSurf->space->modelDepthHack != 0.0f ) {
			RB_EnterModelDepthHack( drawSurf->space->modelDepthHack );
		}

		triFunc_( drawSurf );

		if ( drawSurf->space->weaponDepthHack || drawSurf->space->modelDepthHack != 0.0f ) {
			RB_LeaveDepthHack();
		}

		backEnd.currentSpace = drawSurf->space;
	}
}

/*
======================
RB_RenderDrawSurfChainWithFunction
======================
*/
void RB_RenderDrawSurfChainWithFunction( const drawSurf_t *drawSurfs, 
										void (*triFunc_)( const drawSurf_t *) ) {
	const drawSurf_t		*drawSurf;

	backEnd.currentSpace = NULL;

	for ( drawSurf = drawSurfs ; drawSurf ; drawSurf = drawSurf->nextOnLight ) {
		// change the matrix if needed
		if ( drawSurf->space != backEnd.currentSpace ) {
			glLoadMatrixf( drawSurf->space->modelViewMatrix );
		}

		if ( drawSurf->space->weaponDepthHack ) {
			RB_EnterWeaponDepthHack();
		}

		if ( drawSurf->space->modelDepthHack ) {
			RB_EnterModelDepthHack( drawSurf->space->modelDepthHack );
		}

		// change the scissor if needed
		if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( drawSurf->scissorRect ) ) {
			backEnd.currentScissor = drawSurf->scissorRect;
			glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1, 
				backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		}

		// render it
		triFunc_( drawSurf );

		if ( drawSurf->space->weaponDepthHack || drawSurf->space->modelDepthHack != 0.0f ) {
			RB_LeaveDepthHack();
		}

		backEnd.currentSpace = drawSurf->space;
	}
}

/*
======================
RB_GetShaderTextureMatrix
======================
*/
void RB_GetShaderTextureMatrix( const float *shaderRegisters,
							   const textureStage_t *texture, float matrix[16] ) {
	matrix[0] = shaderRegisters[ texture->matrix[0][0] ];
	matrix[4] = shaderRegisters[ texture->matrix[0][1] ];
	matrix[8] = 0;
	matrix[12] = shaderRegisters[ texture->matrix[0][2] ];

	// we attempt to keep scrolls from generating incredibly large texture values, but
	// center rotations and center scales can still generate offsets that need to be > 1
	if ( matrix[12] < -40 || matrix[12] > 40 ) {
		matrix[12] -= (int)matrix[12];
	}

	matrix[1] = shaderRegisters[ texture->matrix[1][0] ];
	matrix[5] = shaderRegisters[ texture->matrix[1][1] ];
	matrix[9] = 0;
	matrix[13] = shaderRegisters[ texture->matrix[1][2] ];
	if ( matrix[13] < -40 || matrix[13] > 40 ) {
		matrix[13] -= (int)matrix[13];
	}

	matrix[2] = 0;
	matrix[6] = 0;
	matrix[10] = 1;
	matrix[14] = 0;

	matrix[3] = 0;
	matrix[7] = 0;
	matrix[11] = 0;
	matrix[15] = 1;
}

/*
======================
RB_LoadShaderTextureMatrix
======================
*/
void RB_LoadShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture ) {
	float	matrix[16];

	RB_GetShaderTextureMatrix( shaderRegisters, texture, matrix );
	glMatrixMode( GL_TEXTURE );
	glLoadMatrixf( matrix );
	glMatrixMode( GL_MODELVIEW );
}

// Cross-frame dedupe for the shared cinematic scratch image. RoQ decodes at
// ~30fps while the backend can bind the same decoded frame many times per
// second; when the identical frame is already resident we only rebind.
// frameSerial 0 = producer opted out (always upload). storageGeneration
// invalidates across vid_restart / reloadImages (AllocImage bumps it).
static const idCinematic *	rb_lastCinematic = NULL;
static int					rb_lastCinematicSerial = 0;
static uint64_t				rb_lastCinematicStorageGeneration = 0;

/*
======================
RB_BindVariableStageImage

Handles generating a cinematic frame if needed
======================
*/
void RB_BindVariableStageImage( const textureStage_t *texture, const float *shaderRegisters ) {
	if ( texture->cinematic ) {
		cinData_t	cin;

		if ( r_skipDynamicTextures.GetBool() ) {
			globalImages->defaultImage->Bind();
			return;
		}

		// offset time by shaderParm[7] (FIXME: make the time offset a parameter of the shader?)
		// We make no attempt to optimize for multiple identical cinematics being in view, or
		// for cinematics going at a lower framerate than the renderer.
		cin = texture->cinematic->ImageForTime( (int)(1000 * ( backEnd.viewDef->floatTime + backEnd.viewDef->renderView.shaderParms[11] ) ) );

		if ( cin.image ) {
			idImage *cinImage = globalImages->cinematicImage;
			const bool sameFrameResident =
				cin.frameSerial != 0
				&& texture->cinematic == rb_lastCinematic
				&& cin.frameSerial == rb_lastCinematicSerial
				&& cinImage->IsLoaded()
				&& cinImage->GetStorageGeneration() == rb_lastCinematicStorageGeneration
				&& cinImage->GetOpts().textureType == TT_2D
				&& cin.imageHeight != cin.imageWidth * 6;
			if ( sameFrameResident ) {
				// identical pixels already resident: rebind exactly the way
				// UploadScratch's SubImageUpload would have (direct access with
				// tmu-tracker update, no Bind() texture-type enable dance)
				R_BindTextureForDirectAccess( GL_TEXTURE_2D, cinImage->GetDeviceHandle() );
			} else {
				globalImages->cinematicImage->UploadScratch( cin.image, cin.imageWidth, cin.imageHeight );
				rb_lastCinematic = texture->cinematic;
				rb_lastCinematicSerial = cin.frameSerial;
				rb_lastCinematicStorageGeneration = cinImage->GetStorageGeneration();
			}
		} else {
			globalImages->blackImage->Bind();
		}
	} else {
		//FIXME: see why image is invalid
		if (texture->image) {
			texture->image->Bind();
		}
	}
}

/*
======================
RB_BindStageTexture
======================
*/
void RB_BindStageTexture( const float *shaderRegisters, const textureStage_t *texture, const drawSurf_t *surf ) {
	// image
	RB_BindVariableStageImage( texture, shaderRegisters );

	// texgens
	if ( texture->texgen == TG_DIFFUSE_CUBE ) {
		glTexCoordPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( vertexCache.Position( surf->geo->ambientCache ), offsetof( idDrawVert, normal ) ) );
	}
	if ( texture->texgen == TG_SKYBOX_CUBE || texture->texgen == TG_WOBBLESKY_CUBE ) {
		glTexCoordPointer( 3, GL_FLOAT, 0, vertexCache.Position( surf->dynamicTexCoords ) );
	}
	if ( texture->texgen == TG_REFLECT_CUBE ) {
		glEnable( GL_TEXTURE_GEN_S );
		glEnable( GL_TEXTURE_GEN_T );
		glEnable( GL_TEXTURE_GEN_R );
		glTexGenf( GL_S, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
		glTexGenf( GL_T, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
		glTexGenf( GL_R, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
		glEnableClientState( GL_NORMAL_ARRAY );
		glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( vertexCache.Position( surf->geo->ambientCache ), offsetof( idDrawVert, normal ) ) );

		glMatrixMode( GL_TEXTURE );
		float	mat[16];

		R_TransposeGLMatrix( backEnd.viewDef->worldSpace.modelViewMatrix, mat );

		glLoadMatrixf( mat );
		glMatrixMode( GL_MODELVIEW );
	}

	// matrix
	if ( texture->hasMatrix ) {
		RB_LoadShaderTextureMatrix( shaderRegisters, texture );
	}
}

/*
======================
RB_FinishStageTexture
======================
*/
void RB_FinishStageTexture( const textureStage_t *texture, const drawSurf_t *surf ) {
	if ( texture->texgen == TG_DIFFUSE_CUBE || texture->texgen == TG_SKYBOX_CUBE 
		|| texture->texgen == TG_WOBBLESKY_CUBE ) {
		glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), 
			RB_DrawVertAttributePointer( vertexCache.Position( surf->geo->ambientCache ), offsetof( idDrawVert, st ) ) );
	}

	if ( texture->texgen == TG_REFLECT_CUBE ) {
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_R );
		glTexGenf( GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
		glTexGenf( GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
		glTexGenf( GL_R, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
		glDisableClientState( GL_NORMAL_ARRAY );

		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
	}

	if ( texture->hasMatrix ) {
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
	}
}



//=============================================================================================


/*
=================
RB_DetermineLightScale

Sets:
backEnd.lightScale
backEnd.overBright

Find out how much we are going to need to overscale the lighting, so we
can down modulate the pre-lighting passes.

We only look at light calculations, but an argument could be made that
we should also look at surface evaluations, which would let surfaces
overbright past 1.0
=================
*/
void RB_DetermineLightScale( void ) {
	viewLight_t			*vLight;
	const idMaterial	*shader;
	float				max;
	int					i, j, numStages;
	const shaderStage_t	*stage;

	if ( tr.backEndRenderer == BE_ARB2 && glConfig.disableARB2Interactions ) {
		backEnd.pc.maxLightValue = 1.0f;
		backEnd.lightScale = 1.0f;
		backEnd.overBright = 1.0f;
		return;
	}

	// the light scale will be based on the largest color component of any surface
	// that will be drawn.
	// should we consider separating rgb scales?

	// if there are no lights, this will remain at 1.0, so GUI-only
	// rendering will not lose any bits of precision
	max = 1.0;

	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		// lights with no surfaces or shaderparms may still be present
		// for debug display
		if ( !vLight->localInteractions && !vLight->globalInteractions
			&& !vLight->translucentInteractions ) {
			continue;
		}

		shader = vLight->lightShader;
		numStages = shader->GetNumStages();
		for ( i = 0 ; i < numStages ; i++ ) {
			stage = shader->GetStage( i );
			for ( j = 0 ; j < 3 ; j++ ) {
				float	v = r_lightScale.GetFloat() * vLight->shaderRegisters[ stage->color.registers[j] ];
				if ( v > max ) {
					max = v;
				}
			}
		}
	}

	backEnd.pc.maxLightValue = max;
	if ( max <= tr.backEndRendererMaxLight ) {
		backEnd.lightScale = r_lightScale.GetFloat();
		backEnd.overBright = 1.0;
	} else {
		backEnd.lightScale = r_lightScale.GetFloat() * tr.backEndRendererMaxLight / max;
		backEnd.overBright = max / tr.backEndRendererMaxLight;
	}
}


/*
=================
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare
to actually render the visible surfaces for this view
=================
*/
void RB_BeginDrawingView (void) {
	// set the modelview matrix for the viewer
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode(GL_MODELVIEW);

	// set the window clipping
	glViewport( RB_TileOffsetX() + backEnd.viewDef->viewport.x1, 
		RB_TileOffsetY() + backEnd.viewDef->viewport.y1, 
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );

	// the scissor may be smaller than the viewport for subviews
	glScissor( RB_TileOffsetX() + backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1, 
		RB_TileOffsetY() + backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1, 
		backEnd.viewDef->scissor.x2 + 1 - backEnd.viewDef->scissor.x1,
		backEnd.viewDef->scissor.y2 + 1 - backEnd.viewDef->scissor.y1 );
	backEnd.currentScissor = backEnd.viewDef->scissor;

	// ensures that depth writes are enabled for the depth clear
	GL_State( GLS_DEFAULT );

	// we don't have to clear the depth / stencil buffer for 2D rendering
	if ( backEnd.viewDef->viewEntitys ) {
		glStencilMask( 0xff );
		// some cards may have 7 bit stencil buffers, so don't assume this
		// should be 128
		glClearStencil( R_SafeStencilClearValue() );
		glClear( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		glEnable( GL_DEPTH_TEST );
	} else {
		glDisable( GL_DEPTH_TEST );
		glDisable( GL_STENCIL_TEST );
	}

	backEnd.glState.faceCulling = -1;		// force face culling to set next time
	GL_Cull( CT_FRONT_SIDED );

}

/*
==================
R_SetDrawInteractions
==================
*/
void R_SetDrawInteraction( const shaderStage_t *surfaceStage, const float *surfaceRegs,
						  idImage **image, idVec4 matrix[2], float color[4] ) {
	*image = surfaceStage->texture.image;
	if ( surfaceStage->texture.hasMatrix ) {
		matrix[0][0] = surfaceRegs[surfaceStage->texture.matrix[0][0]];
		matrix[0][1] = surfaceRegs[surfaceStage->texture.matrix[0][1]];
		matrix[0][2] = 0;
		matrix[0][3] = surfaceRegs[surfaceStage->texture.matrix[0][2]];

		matrix[1][0] = surfaceRegs[surfaceStage->texture.matrix[1][0]];
		matrix[1][1] = surfaceRegs[surfaceStage->texture.matrix[1][1]];
		matrix[1][2] = 0;
		matrix[1][3] = surfaceRegs[surfaceStage->texture.matrix[1][2]];

		// we attempt to keep scrolls from generating incredibly large texture values, but
		// center rotations and center scales can still generate offsets that need to be > 1
		if ( matrix[0][3] < -40 || matrix[0][3] > 40 ) {
			matrix[0][3] -= (int)matrix[0][3];
		}
		if ( matrix[1][3] < -40 || matrix[1][3] > 40 ) {
			matrix[1][3] -= (int)matrix[1][3];
		}
	} else {
		matrix[0][0] = 1;
		matrix[0][1] = 0;
		matrix[0][2] = 0;
		matrix[0][3] = 0;

		matrix[1][0] = 0;
		matrix[1][1] = 1;
		matrix[1][2] = 0;
		matrix[1][3] = 0;
	}

	if ( color ) {
		for ( int i = 0 ; i < 4 ; i++ ) {
			color[i] = surfaceRegs[surfaceStage->color.registers[i]];
			// clamp here, so card with greater range don't look different.
			// we could perform overbrighting like we do for lights, but
			// it doesn't currently look worth it.
			if ( color[i] < 0 ) {
				color[i] = 0;
			} else if ( color[i] > 1.0 ) {
				color[i] = 1.0;
			}
		}
	}
}

/*
=================
RB_SubmittInteraction
=================
*/
static void RB_SubmittInteraction( drawInteraction_t *din, void (*DrawInteraction)(const drawInteraction_t *) ) {
	if ( !din->bumpImage ) {
		return;
	}

	if ( !din->diffuseImage || r_skipDiffuse.GetBool() ) {
		din->diffuseImage = globalImages->blackImage;
	}
	if ( !din->specularImage || r_skipSpecular.GetBool() || din->ambientLight ) {
		din->specularImage = globalImages->blackImage;
	}
	if ( !din->bumpImage || r_skipBump.GetBool() ) {
		din->bumpImage = globalImages->flatNormalMap;
	}

	// if we wouldn't draw anything, don't call the Draw function
	if ( 
		( ( din->diffuseColor[0] > 0 || 
		din->diffuseColor[1] > 0 || 
		din->diffuseColor[2] > 0 ) && din->diffuseImage != globalImages->blackImage )
		|| ( ( din->specularColor[0] > 0 || 
		din->specularColor[1] > 0 || 
		din->specularColor[2] > 0 ) && din->specularImage != globalImages->blackImage ) ) {
		DrawInteraction( din );
	}
}

/*
=============
RB_CreateSingleDrawInteractions

This can be used by different draw_* backends to decompose a complex light / surface
interaction into primitive interactions
=============
*/
void RB_CreateSingleDrawInteractions( const drawSurf_t *surf, void (*DrawInteraction)(const drawInteraction_t *) ) {
	RB_CreateSingleDrawInteractionsFiltered( surf, DrawInteraction, NULL );
}

void RB_CreateSingleDrawInteractionsFiltered( const drawSurf_t *surf, void (*DrawInteraction)(const drawInteraction_t *), drawInteractionStageFilter_t StageFilter ) {
	const idMaterial	*surfaceShader = surf->material;
	const float			*surfaceRegs = surf->shaderRegisters;
	const viewLight_t	*vLight = backEnd.vLight;
	const idMaterial	*lightShader = vLight->lightShader;
	const float			*lightRegs = vLight->shaderRegisters;
	drawInteraction_t	inter;

	if ( r_skipInteractions.GetBool() || !surf->geo || ( !surf->geo->ambientCache && !RB_HasPackedPrimBatchMesh( surf->geo ) ) ) {
		return;
	}

	// Native PBR (draw_pbr.cpp) owns an admitted surface's interactions: a
	// classic pass skips it and a native pass skips everything else.
	if ( RB_GLPBR_DecompositionSkips( surf ) ) {
		return;
	}

	if ( tr.logFile ) {
		RB_LogComment( "---------- RB_CreateSingleDrawInteractions %s on %s ----------\n", lightShader->GetName(), surfaceShader->GetName() );
	}

	// change the matrix and light projection vectors if needed
	if ( surf->space != backEnd.currentSpace ) {
		backEnd.currentSpace = surf->space;
		glLoadMatrixf( surf->space->modelViewMatrix );
	}

	// change the scissor if needed
	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
		backEnd.currentScissor = surf->scissorRect;
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1, 
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}

	// hack depth range if needed
	if ( surf->space->weaponDepthHack ) {
		RB_EnterWeaponDepthHack();
	}

	if ( surf->space->modelDepthHack ) {
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
	}

	inter.surf = surf;
	inter.lightFalloffImage = vLight->falloffImage;

	R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, inter.localLightOrigin.ToVec3() );
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, inter.localViewOrigin.ToVec3() );
	inter.localLightOrigin[3] = 0;
	inter.localViewOrigin[3] = 1;
	inter.ambientLight = lightShader->IsAmbientLight();

	// the base projections may be modified by texture matrix on light stages
	idPlane lightProject[4];
	for ( int i = 0 ; i < 4 ; i++ ) {
		R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i] );
	}

	const int lightStageCount = lightShader->GetNumStages();
	const int surfaceStageCount = surfaceShader->GetNumStages();
	for ( int lightStageNum = 0 ; lightStageNum < lightStageCount ; lightStageNum++ ) {
		const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

		// ignore stages that fail the condition
		if ( !lightRegs[ lightStage->conditionRegister ] ) {
			continue;
		}

		inter.lightImage = lightStage->texture.image;

		memcpy( inter.lightProjection, lightProject, sizeof( inter.lightProjection ) );
		// now multiply the texgen by the light texture matrix
		if ( lightStage->texture.hasMatrix ) {
			RB_GetShaderTextureMatrix( lightRegs, &lightStage->texture, backEnd.lightTextureMatrix );
			RB_BakeTextureMatrixIntoTexgen( reinterpret_cast<class idPlane *>(inter.lightProjection), backEnd.lightTextureMatrix );
		}

		inter.bumpImage = NULL;
		inter.specularImage = NULL;
		inter.diffuseImage = NULL;
		inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
		inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;
		inter.flatDiffuseParams.Zero();

		float lightColor[4];

		// backEnd.lightScale is calculated so that lightColor[] will never exceed
		// tr.backEndRendererMaxLight
		lightColor[0] = backEnd.lightScale * lightRegs[ lightStage->color.registers[0] ];
		lightColor[1] = backEnd.lightScale * lightRegs[ lightStage->color.registers[1] ];
		lightColor[2] = backEnd.lightScale * lightRegs[ lightStage->color.registers[2] ];
		lightColor[3] = lightRegs[ lightStage->color.registers[3] ];

		// go through the individual stages
		for ( int surfaceStageNum = 0 ; surfaceStageNum < surfaceStageCount ; surfaceStageNum++ ) {
			const shaderStage_t	*surfaceStage = surfaceShader->GetStage( surfaceStageNum );
			if ( StageFilter != NULL && !StageFilter( surfaceStage, surfaceRegs ) ) {
				continue;
			}

			switch( surfaceStage->lighting ) {
				case SL_AMBIENT: {
					// ignore ambient stages while drawing interactions
					break;
				}
				case SL_BUMP: {
					// ignore stage that fails the condition
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					// draw any previous interaction
					RB_SubmittInteraction( &inter, DrawInteraction );
					inter.diffuseImage = NULL;
					inter.specularImage = NULL;
					R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.bumpImage, inter.bumpMatrix, NULL );
					break;
				}
				case SL_DIFFUSE: {
					// ignore stage that fails the condition
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					if ( inter.diffuseImage ) {
						RB_SubmittInteraction( &inter, DrawInteraction );
					}
					R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.diffuseImage,
											inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr() );
					RB_ApplyFlatDiffuseStage( surf, &inter.diffuseImage,
						inter.diffuseColor.ToFloatPtr(), inter.flatDiffuseParams );
					inter.diffuseColor[0] *= lightColor[0];
					inter.diffuseColor[1] *= lightColor[1];
					inter.diffuseColor[2] *= lightColor[2];
					inter.diffuseColor[3] *= lightColor[3];
					inter.vertexColor = surfaceStage->vertexColor;
					break;
				}
				case SL_SPECULAR: {
					// ignore stage that fails the condition
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					if ( inter.specularImage ) {
						RB_SubmittInteraction( &inter, DrawInteraction );
					}
					R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.specularImage,
											inter.specularMatrix, inter.specularColor.ToFloatPtr() );
					inter.specularColor[0] *= lightColor[0];
					inter.specularColor[1] *= lightColor[1];
					inter.specularColor[2] *= lightColor[2];
					inter.specularColor[3] *= lightColor[3];
					inter.vertexColor = surfaceStage->vertexColor;
					break;
				}
			}
		}

		// draw the final interaction
		RB_SubmittInteraction( &inter, DrawInteraction );
	}

	// unhack depth range if needed
	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		RB_LeaveDepthHack();
	}
}

/*
=============
RB_DrawView
=============
*/
void RB_DrawView( const void *data ) {
	const drawSurfsCommand_t	*cmd;

	cmd = (const drawSurfsCommand_t *)data;

	backEnd.viewDef = cmd->viewDef;
	
	// we will need to do a new copyTexSubImage of the screen
	// when a SS_POST_PROCESS material is used
	backEnd.currentRenderCopied = false;
	backEnd.currentDepthCopied = false;

	// if there aren't any drawsurfs, do nothing
	if ( !backEnd.viewDef->numDrawSurfs ) {
		return;
	}

	// skip render bypasses everything that has models, assuming
	// them to be 3D views, but leaves 2D rendering visible
	if ( r_skipRender.GetBool() && backEnd.viewDef->viewEntitys ) {
		return;
	}

	// skip render context sets the wgl context to NULL,
	// which should factor out the API cost, under the assumption
	// that all gl calls just return if the context isn't valid
	if ( r_skipRenderContext.GetBool() && backEnd.viewDef->viewEntitys ) {
		GLimp_DeactivateContext();
	}

	backEnd.pc.c_surfaces += backEnd.viewDef->numDrawSurfs;

	RB_ShowOverdraw();

	// render the scene, jumping to the hardware specific interaction renderers
	RB_STD_DrawView();

	// restore the context for 2D drawing if we were stubbing it out
	if ( r_skipRenderContext.GetBool() && backEnd.viewDef->viewEntitys ) {
		GLimp_ActivateContext();
		RB_SetDefaultGLState();
	}
}
