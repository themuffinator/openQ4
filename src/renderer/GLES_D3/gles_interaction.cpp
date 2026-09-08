// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	gles_d3 -- per-light bump/diffuse/specular interactions.

	The centre of the backend, and the pass that makes a Quake 4 scene
	visible: world surfaces carry no ambient stage, so everything except
	emissives and GUIs is black until this runs.

	Ports, in order of how much they matter:
	  RB_CreateSingleDrawInteractionsFiltered  tr_render.cpp:875
	  RB_SubmittInteraction                    tr_render.cpp:836
	  R_SetDrawInteraction                     tr_render.cpp:782
	  RB_DetermineLightScale                   tr_render.cpp:675
	  RB_BakeTextureMatrixIntoTexgen           draw_common.cpp
	all of which live in translation units this module excludes.

	Shadows are handled separately, in gles_stencilshadow.cpp. Every light
	here lights everything it reaches.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "gles_d3_local.h"
#include "gles_program.h"

// texture units, 1:1 with the Vulkan descriptor sets so the two shaders stay
// diffable (Vulkan/shaders/interaction.frag:19-24)
static const int GLESD3_TMU_SPECULAR_TABLE = 0;
static const int GLESD3_TMU_BUMP = 1;
static const int GLESD3_TMU_LIGHT_FALLOFF = 2;
static const int GLESD3_TMU_LIGHT_PROJECTION = 3;
static const int GLESD3_TMU_DIFFUSE = 4;
static const int GLESD3_TMU_SPECULAR = 5;

static glesProgram_t *	gles_interactionProgram = NULL;
// the GLESD3_AMBIENT variant: constant-direction light, no specular chain.
// Selected per interaction by din->ambientLight rather than by uniform, so
// the common per-light fragment path carries no branch (gles_program.h, D8).
static glesProgram_t *	gles_interactionAmbientProgram = NULL;
static float			gles_ambientDir[ 3 ] = { 1.0f, 0.0f, 0.0f };
static int				gles_interactionDraws = 0;
static bool				gles_reportedLights = false;

/*
====================
R_GLESD3_BakeTextureMatrixIntoTexgen

RB_BakeTextureMatrixIntoTexgen, which lives in the excluded draw_common.cpp.
Folds a light stage's texture matrix into the light projection planes so the
shader keeps sampling with a plain textureProj.

Shared with the blend light pass (gles_fog.cpp), which projects through the
same planes.
====================
*/
void R_GLESD3_BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float textureMatrix[16] ) {
	float genMatrix[16];
	float final[16];

	genMatrix[0] = lightProject[0][0];
	genMatrix[4] = lightProject[0][1];
	genMatrix[8] = lightProject[0][2];
	genMatrix[12] = lightProject[0][3];

	genMatrix[1] = lightProject[1][0];
	genMatrix[5] = lightProject[1][1];
	genMatrix[9] = lightProject[1][2];
	genMatrix[13] = lightProject[1][3];

	genMatrix[2] = 0;
	genMatrix[6] = 0;
	genMatrix[10] = 0;
	genMatrix[14] = 0;

	genMatrix[3] = lightProject[2][0];
	genMatrix[7] = lightProject[2][1];
	genMatrix[11] = lightProject[2][2];
	genMatrix[15] = lightProject[2][3];

	myGlMultMatrix( genMatrix, textureMatrix, final );

	lightProject[0][0] = final[0];
	lightProject[0][1] = final[4];
	lightProject[0][2] = final[8];
	lightProject[0][3] = final[12];

	lightProject[1][0] = final[1];
	lightProject[1][1] = final[5];
	lightProject[1][2] = final[9];
	lightProject[1][3] = final[13];
}

/*
====================
R_GLESD3_GetShaderTextureMatrix

The module's RB_GetShaderTextureMatrix stub returns identity unconditionally
(GLES/gles_Backend.cpp), which is fine where it is used but would silently
drop a light's scrolling/rotating projection here. Built locally instead.

Shared with the blend light pass (gles_fog.cpp), whose stages carry the same
kind of matrix.
====================
*/
void R_GLESD3_GetShaderTextureMatrix( const float *shaderRegisters,
		const textureStage_t *texture, float matrix[16] ) {
	matrix[0] = shaderRegisters[ texture->matrix[0][0] ];
	matrix[4] = shaderRegisters[ texture->matrix[0][1] ];
	matrix[8] = 0.0f;
	matrix[12] = shaderRegisters[ texture->matrix[0][2] ];

	// see R_SetDrawInteraction: keep scrolls from generating enormous texture
	// coordinates, while leaving centred rotations and scales room to exceed 1
	if ( matrix[12] < -40.0f || matrix[12] > 40.0f ) {
		matrix[12] -= (int)matrix[12];
	}

	matrix[1] = shaderRegisters[ texture->matrix[1][0] ];
	matrix[5] = shaderRegisters[ texture->matrix[1][1] ];
	matrix[9] = 0.0f;
	matrix[13] = shaderRegisters[ texture->matrix[1][2] ];
	if ( matrix[13] < -40.0f || matrix[13] > 40.0f ) {
		matrix[13] -= (int)matrix[13];
	}

	matrix[2] = matrix[6] = matrix[10] = matrix[14] = 0.0f;
	matrix[3] = matrix[7] = matrix[11] = 0.0f;
	matrix[15] = 1.0f;
}

/*
====================
GLESD3_SetDrawInteraction

R_SetDrawInteraction (tr_render.cpp:782).
====================
*/
static void GLESD3_SetDrawInteraction( const shaderStage_t *surfaceStage, const float *surfaceRegs,
		idImage **image, idVec4 matrix[2], float color[4] ) {
	*image = surfaceStage->texture.image;

	if ( surfaceStage->texture.hasMatrix ) {
		matrix[0][0] = surfaceRegs[ surfaceStage->texture.matrix[0][0] ];
		matrix[0][1] = surfaceRegs[ surfaceStage->texture.matrix[0][1] ];
		matrix[0][2] = 0.0f;
		matrix[0][3] = surfaceRegs[ surfaceStage->texture.matrix[0][2] ];

		matrix[1][0] = surfaceRegs[ surfaceStage->texture.matrix[1][0] ];
		matrix[1][1] = surfaceRegs[ surfaceStage->texture.matrix[1][1] ];
		matrix[1][2] = 0.0f;
		matrix[1][3] = surfaceRegs[ surfaceStage->texture.matrix[1][2] ];

		if ( matrix[0][3] < -40.0f || matrix[0][3] > 40.0f ) {
			matrix[0][3] -= (int)matrix[0][3];
		}
		if ( matrix[1][3] < -40.0f || matrix[1][3] > 40.0f ) {
			matrix[1][3] -= (int)matrix[1][3];
		}
	} else {
		matrix[0].Set( 1.0f, 0.0f, 0.0f, 0.0f );
		matrix[1].Set( 0.0f, 1.0f, 0.0f, 0.0f );
	}

	if ( color != NULL ) {
		for ( int i = 0; i < 4; i++ ) {
			// clamped so a card with greater range does not look different
			color[i] = idMath::ClampFloat( 0.0f, 1.0f, surfaceRegs[ surfaceStage->color.registers[i] ] );
		}
	}
}

/*
====================
GLESD3_DrawInteraction

One primitive interaction: bump x diffuse x specular for one light.
====================
*/
static void GLESD3_DrawInteraction( const drawInteraction_t *din ) {
	const drawSurf_t *surf = din->surf;
	const srfTriangles_t *tri = surf->geo;
	glesProgram_t *program = din->ambientLight
			? gles_interactionAmbientProgram : gles_interactionProgram;

	// tri->indexCache may legitimately be NULL here: R_CreateAmbientCache
	// (tr_light.cpp:242) allocates VERTICES ONLY, so a light's generated
	// interaction tris routinely arrive without one. The legacy path does not
	// care -- it draws them with a client-memory index pointer, which ES
	// removed -- and dropping them silently is what made models lose their
	// lighting while world geometry kept it. R_GLESD3_DrawElements now
	// allocates a frame temp when the cache is missing.
	if ( program == NULL || tri == NULL || tri->ambientCache == NULL ) {
		return;
	}

	if ( !R_GLESD3_BindDrawVertAttributes( tri ) ) {
		return;
	}

	float mvp[ 16 ];
	myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mvp );

	R_GLESD3_UseProgram( program );
	glUniformMatrix4fv( program->uMVP, 1, GL_FALSE, mvp );

	glUniform4fv( program->uLocalLightOrigin, 1, din->localLightOrigin.ToFloatPtr() );
	glUniform4fv( program->uLocalViewOrigin, 1, din->localViewOrigin.ToFloatPtr() );
	glUniform4fv( program->uLightProjectionS, 1, din->lightProjection[0].ToFloatPtr() );
	glUniform4fv( program->uLightProjectionT, 1, din->lightProjection[1].ToFloatPtr() );
	glUniform4fv( program->uLightProjectionQ, 1, din->lightProjection[2].ToFloatPtr() );
	glUniform4fv( program->uLightFalloffS, 1, din->lightProjection[3].ToFloatPtr() );
	glUniform4fv( program->uBumpMatrixS, 1, din->bumpMatrix[0].ToFloatPtr() );
	glUniform4fv( program->uBumpMatrixT, 1, din->bumpMatrix[1].ToFloatPtr() );
	glUniform4fv( program->uDiffuseMatrixS, 1, din->diffuseMatrix[0].ToFloatPtr() );
	glUniform4fv( program->uDiffuseMatrixT, 1, din->diffuseMatrix[1].ToFloatPtr() );
	glUniform4fv( program->uSpecularMatrixS, 1, din->specularMatrix[0].ToFloatPtr() );
	glUniform4fv( program->uSpecularMatrixT, 1, din->specularMatrix[1].ToFloatPtr() );
	glUniform4fv( program->uDiffuseColor, 1, din->diffuseColor.ToFloatPtr() );
	glUniform4fv( program->uSpecularColor, 1, din->specularColor.ToFloatPtr() );

	// only the ambient variant declares uAmbientDir; -1 no-ops on the base
	glUniform3fv( program->uAmbientDir, 1, gles_ambientDir );

	// the (rgbModulate, rgbAdd, alphaModulate, alphaAdd) SVC packing, shared
	// with the material program. The interaction shader consumes only the RGB
	// half -- lighting has no alpha term -- but the layout is kept identical so
	// a colour fault can be diffed between the two paths rather than re-derived.
	float vertexColor[ 4 ];
	switch ( din->vertexColor ) {
		case SVC_MODULATE:			vertexColor[0] = 1.0f;  vertexColor[1] = 0.0f; break;
		case SVC_INVERSE_MODULATE:	vertexColor[0] = -1.0f; vertexColor[1] = 1.0f; break;
		case SVC_IGNORE:
		default:					vertexColor[0] = 0.0f;  vertexColor[1] = 1.0f; break;
	}
	// alpha modulates in every mode except SVC_IGNORE; inverse-modulate inverts
	// RGB only (draw_common.cpp:7338-7352)
	vertexColor[2] = ( din->vertexColor == SVC_IGNORE ) ? 0.0f : 1.0f;
	vertexColor[3] = ( din->vertexColor == SVC_IGNORE ) ? 1.0f : 0.0f;
	glUniform4fv( program->uVertexColor, 1, vertexColor );

	GL_SelectTexture( GLESD3_TMU_SPECULAR_TABLE );
	globalImages->specularTableImage->Bind();
	GL_SelectTexture( GLESD3_TMU_BUMP );
	din->bumpImage->Bind();
	GL_SelectTexture( GLESD3_TMU_LIGHT_FALLOFF );
	din->lightFalloffImage->Bind();
	GL_SelectTexture( GLESD3_TMU_LIGHT_PROJECTION );
	din->lightImage->Bind();
	GL_SelectTexture( GLESD3_TMU_DIFFUSE );
	din->diffuseImage->Bind();
	GL_SelectTexture( GLESD3_TMU_SPECULAR );
	din->specularImage->Bind();
	GL_SelectTexture( 0 );

	R_GLESD3_DrawElements( tri );
	gles_interactionDraws++;
}

/*
====================
GLESD3_SubmitInteraction

RB_SubmittInteraction (tr_render.cpp:836): substitute defaults for the
missing halves, then drop the draw entirely if it could not contribute.
====================
*/
static void GLESD3_SubmitInteraction( drawInteraction_t *din ) {
	if ( !din->bumpImage ) {
		return;
	}
	if ( !din->diffuseImage || r_skipDiffuse.GetBool() ) {
		din->diffuseImage = globalImages->blackImage;
	}
	// an ambient light has no specular term at all
	if ( !din->specularImage || r_skipSpecular.GetBool() || din->ambientLight ) {
		din->specularImage = globalImages->blackImage;
	}
	if ( r_skipBump.GetBool() ) {
		din->bumpImage = globalImages->flatNormalMap;
	}

	const bool diffuseContributes =
			( din->diffuseColor[0] > 0.0f || din->diffuseColor[1] > 0.0f || din->diffuseColor[2] > 0.0f )
			&& din->diffuseImage != globalImages->blackImage;
	const bool specularContributes =
			( din->specularColor[0] > 0.0f || din->specularColor[1] > 0.0f || din->specularColor[2] > 0.0f )
			&& din->specularImage != globalImages->blackImage;

	if ( diffuseContributes || specularContributes ) {
		GLESD3_DrawInteraction( din );
	}
}

/*
====================
GLESD3_CreateSingleDrawInteractions

RB_CreateSingleDrawInteractionsFiltered (tr_render.cpp:875): decompose a
material's lighting stages against one light into primitive interactions.
====================
*/
static void GLESD3_CreateSingleDrawInteractions( const drawSurf_t *surf ) {
	const idMaterial *surfaceShader = surf->material;
	const float *surfaceRegs = surf->shaderRegisters;
	const viewLight_t *vLight = backEnd.vLight;
	const idMaterial *lightShader = vLight->lightShader;
	const float *lightRegs = vLight->shaderRegisters;
	drawInteraction_t inter;

	if ( r_skipInteractions.GetBool() || surf->geo == NULL || surf->geo->ambientCache == NULL ) {
		return;
	}

	if ( surf->space != backEnd.currentSpace ) {
		backEnd.currentSpace = surf->space;
	}

	// an empty rect covers no pixels: skip rather than issue a rejected call
	if ( !RB_GLESD3_SetScissor( surf->scissorRect ) ) {
		return;
	}

	memset( &inter, 0, sizeof( inter ) );
	inter.surf = surf;
	inter.lightFalloffImage = vLight->falloffImage;

	R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, inter.localLightOrigin.ToVec3() );
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, inter.localViewOrigin.ToVec3() );
	inter.localLightOrigin[3] = 0.0f;
	inter.localViewOrigin[3] = 1.0f;
	inter.ambientLight = lightShader->IsAmbientLight();

	// the base projections may be modified by a texture matrix on light stages
	idPlane lightProject[4];
	for ( int i = 0; i < 4; i++ ) {
		R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[i], lightProject[i] );
	}

	const int lightStageCount = lightShader->GetNumStages();
	const int surfaceStageCount = surfaceShader->GetNumStages();

	for ( int lightStageNum = 0; lightStageNum < lightStageCount; lightStageNum++ ) {
		const shaderStage_t *lightStage = lightShader->GetStage( lightStageNum );
		if ( !lightRegs[ lightStage->conditionRegister ] ) {
			continue;
		}

		inter.lightImage = lightStage->texture.image;

		memcpy( inter.lightProjection, lightProject, sizeof( inter.lightProjection ) );
		if ( lightStage->texture.hasMatrix ) {
			float lightTextureMatrix[16];
			R_GLESD3_GetShaderTextureMatrix( lightRegs, &lightStage->texture, lightTextureMatrix );
			R_GLESD3_BakeTextureMatrixIntoTexgen(
					reinterpret_cast<idPlane *>( inter.lightProjection ), lightTextureMatrix );
		}

		inter.bumpImage = NULL;
		inter.specularImage = NULL;
		inter.diffuseImage = NULL;
		inter.diffuseColor.Zero();
		inter.specularColor.Zero();
		inter.flatDiffuseParams.Zero();

		// backEnd.lightScale keeps lightColor under tr.backEndRendererMaxLight
		float lightColor[4];
		lightColor[0] = backEnd.lightScale * lightRegs[ lightStage->color.registers[0] ];
		lightColor[1] = backEnd.lightScale * lightRegs[ lightStage->color.registers[1] ];
		lightColor[2] = backEnd.lightScale * lightRegs[ lightStage->color.registers[2] ];
		lightColor[3] = lightRegs[ lightStage->color.registers[3] ];

		for ( int surfaceStageNum = 0; surfaceStageNum < surfaceStageCount; surfaceStageNum++ ) {
			const shaderStage_t *surfaceStage = surfaceShader->GetStage( surfaceStageNum );

			switch ( surfaceStage->lighting ) {
				case SL_AMBIENT:
					// ambient stages are the material pass's business
					break;

				case SL_BUMP:
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					// a new bump map starts a new interaction
					GLESD3_SubmitInteraction( &inter );
					inter.diffuseImage = NULL;
					inter.specularImage = NULL;
					GLESD3_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.bumpImage,
							inter.bumpMatrix, NULL );
					break;

				case SL_DIFFUSE:
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					if ( inter.diffuseImage ) {
						GLESD3_SubmitInteraction( &inter );
					}
					GLESD3_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.diffuseImage,
							inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr() );
					RB_ApplyFlatDiffuseStage( surf, &inter.diffuseImage,
							inter.diffuseColor.ToFloatPtr(), inter.flatDiffuseParams );
					inter.diffuseColor[0] *= lightColor[0];
					inter.diffuseColor[1] *= lightColor[1];
					inter.diffuseColor[2] *= lightColor[2];
					inter.diffuseColor[3] *= lightColor[3];
					inter.vertexColor = surfaceStage->vertexColor;
					break;

				case SL_SPECULAR:
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					if ( inter.specularImage ) {
						GLESD3_SubmitInteraction( &inter );
					}
					GLESD3_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.specularImage,
							inter.specularMatrix, inter.specularColor.ToFloatPtr() );
					inter.specularColor[0] *= lightColor[0];
					inter.specularColor[1] *= lightColor[1];
					inter.specularColor[2] *= lightColor[2];
					inter.specularColor[3] *= lightColor[3];
					inter.vertexColor = surfaceStage->vertexColor;
					break;
			}
		}

		GLESD3_SubmitInteraction( &inter );
	}
}

/*
====================
GLESD3_DrawInteractionChain

One chain of surfaces for the current light. The interaction state -- program,
blend, depth func -- is re-established here rather than once per view, because
the stencil shadow pass in between owns the program and the GL state.
====================
*/
static void GLESD3_DrawInteractionChain( const drawSurf_t *chain, int depthFunc = GLS_DEPTHFUNC_EQUAL ) {
	if ( chain == NULL ) {
		return;
	}

	// additive, depth-tested against the prepass, no depth writes
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | depthFunc );
	GL_Cull( CT_FRONT_SIDED );
	R_GLESD3_UseProgram( gles_interactionProgram );

	for ( const drawSurf_t *surf = chain; surf != NULL; surf = surf->nextOnLight ) {
		GLESD3_CreateSingleDrawInteractions( surf );
	}
}

/*
====================
RB_GLESD3_DetermineLightScale

RB_DetermineLightScale (tr_render.cpp:675). tr.backEndRendererMaxLight is 999
for this backend, as it is for ARB2 and Vulkan, so lightScale normally stays
r_lightScale and overBright 1.0.
====================
*/
void RB_GLESD3_DetermineLightScale( void ) {
	float max = 1.0f;

	for ( const viewLight_t *vLight = backEnd.viewDef->viewLights; vLight; vLight = vLight->next ) {
		if ( !vLight->localInteractions && !vLight->globalInteractions
				&& !vLight->translucentInteractions ) {
			continue;
		}
		const idMaterial *shader = vLight->lightShader;
		const int numStages = shader->GetNumStages();
		for ( int i = 0; i < numStages; i++ ) {
			const shaderStage_t *stage = shader->GetStage( i );
			for ( int j = 0; j < 3; j++ ) {
				const float v = r_lightScale.GetFloat() * vLight->shaderRegisters[ stage->color.registers[j] ];
				if ( v > max ) {
					max = v;
				}
			}
		}
	}

	backEnd.pc.maxLightValue = max;
	if ( max <= tr.backEndRendererMaxLight ) {
		backEnd.lightScale = r_lightScale.GetFloat();
		backEnd.overBright = 1.0f;
	} else {
		backEnd.lightScale = r_lightScale.GetFloat() * tr.backEndRendererMaxLight / max;
		backEnd.overBright = max / tr.backEndRendererMaxLight;
	}
}

/*
====================
RB_GLESD3_DrawInteractions
====================
*/
void RB_GLESD3_DrawInteractions( void ) {
	const viewDef_t *viewDef = backEnd.viewDef;

	if ( viewDef == NULL || viewDef->viewLights == NULL || r_skipInteractions.GetBool() ) {
		return;
	}

	gles_interactionProgram = R_GLESD3_Program( GLESD3_PROGRAM_INTERACTION );
	gles_interactionAmbientProgram = R_GLESD3_Program( GLESD3_PROGRAM_INTERACTION,
			GLESD3_VARIANT_AMBIENT );
	if ( gles_interactionProgram == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_GLESD3_DrawInteractions ----------\n" );

	gles_interactionDraws = 0;

	// Both GL paths sample the ambient normal-map cube and decode rgb*2-1 for
	// an ambient light. R_AmbientNormalImage stores x in the alpha channel
	// (red holds 255), so the decoded tangent-space constant is (1, q(y), q(z))
	// with the cube's 8-bit quantization -- reproduced exactly, as Vulkan does
	// (vk_Interactions.cpp:1884).
	gles_ambientDir[0] = 1.0f;
	gles_ambientDir[1] = ( (float)(byte)( 255 * tr.ambientLightVector[1] ) / 255.0f ) * 2.0f - 1.0f;
	gles_ambientDir[2] = ( (float)(byte)( 255 * tr.ambientLightVector[2] ) / 255.0f ) * 2.0f - 1.0f;

	RB_GLESD3_DetermineLightScale();

	GL_Cull( CT_FRONT_SIDED );
	RB_GLESD3_ResetShadowDrawCount();

	backEnd.currentSpace = NULL;

	for ( viewLight_t *vLight = viewDef->viewLights; vLight; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		// fog and blend lights are D6
		if ( vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight() ) {
			continue;
		}
		if ( !vLight->localInteractions && !vLight->globalInteractions
				&& !vLight->translucentInteractions ) {
			continue;
		}

		// r_glesD3Report 2: one line per light, once per run. Whole-frame
		// brightness comparisons cannot say WHICH light is missing, and the
		// per-region measurement that found this defect (lighting short by 37%
		// on one model, exact on another) needs a per-light breakdown to go
		// any further.
		const int drawsBeforeLight = gles_interactionDraws;

		const bool hasShadows = ( vLight->globalShadows != NULL || vLight->localShadows != NULL );

		if ( hasShadows ) {
			// Every light starts from a clean stencil. Without this the volumes
			// of successive lights accumulate and the scene goes progressively
			// black -- measured: the whole right-hand side of hangar1 lost its
			// lighting. Scoped to the light's scissor so the clear costs only
			// the pixels the light can reach.
			// The worst of the empty-rect cases: a rejected glScissor here left
			// the PREVIOUS box live and then scoped this clear to it, so the
			// stencil outside that box kept the last light's volume counts and
			// every later interaction there was stencil-rejected. An empty
			// light rect means the light reaches no pixels, so there is nothing
			// to clear either.
			if ( RB_GLESD3_SetScissor( vLight->scissorRect ) ) {
				glClear( GL_STENCIL_BUFFER_BIT );
			}
		} else {
			// no shadows for this light: nothing to read or write, and the
			// GEQUAL test left by a previous light must not carry over
			glStencilFunc( GL_ALWAYS, 128, 255 );
		}

		// The chains are crossed deliberately, as in every idTech 4 back end:
		// GLOBAL shadows are tested against LOCAL surfaces and vice versa.
		// A light's own geometry casts onto the world and the world casts onto
		// it, without either volume self-shadowing the surfaces it was built
		// from. Pairing them straight (global with global) both over-shadows
		// and self-shadows.
		RB_GLESD3_StencilShadowPass( vLight->globalShadows );
		GLESD3_DrawInteractionChain( vLight->localInteractions );

		RB_GLESD3_StencilShadowPass( vLight->localShadows );
		GLESD3_DrawInteractionChain( vLight->globalInteractions );

		if ( r_skipTranslucent.GetBool() ) {
			continue;
		}

		// Translucent receivers take the same stencil test as opaque ones when
		// the light actually casts, and depth LESS rather than EQUAL because
		// they were never in the depth prepass.
		const bool shadowTranslucent =
				r_stencilTranslucentShadows.GetBool() && r_shadows.GetBool() && hasShadows;
		glStencilFunc( shadowTranslucent ? GL_GEQUAL : GL_ALWAYS, 128, 255 );
		GLESD3_DrawInteractionChain( vLight->translucentInteractions, GLS_DEPTHFUNC_LESS );

		if ( r_glesD3Report.GetInteger() >= 2 && !gles_reportedLights ) {
			int nGlobal = 0, nLocal = 0, nTrans = 0;
			for ( const drawSurf_t *s = vLight->globalInteractions; s; s = s->nextOnLight ) { nGlobal++; }
			for ( const drawSurf_t *s = vLight->localInteractions; s; s = s->nextOnLight ) { nLocal++; }
			for ( const drawSurf_t *s = vLight->translucentInteractions; s; s = s->nextOnLight ) { nTrans++; }
			common->Printf( "glesd3 light '%s'%s stages=%i surfs g/l/t=%i/%i/%i draws=%i\n",
					vLight->lightShader->GetName(),
					vLight->lightShader->IsAmbientLight() ? " AMBIENT" : "",
					vLight->lightShader->GetNumStages(),
					nGlobal, nLocal, nTrans,
					gles_interactionDraws - drawsBeforeLight );
		}
	}

	gles_reportedLights = ( r_glesD3Report.GetInteger() >= 2 );

	// leave no light's stencil test armed over the passes that follow
	glStencilFunc( GL_ALWAYS, 128, 255 );

	backEnd.vLight = NULL;

	// leave no interaction sampler bound over the material pass
	for ( int unit = GLESD3_TMU_SPECULAR; unit >= 0; unit-- ) {
		GL_SelectTexture( unit );
		globalImages->BindNull();
	}
	GL_SelectTexture( 0 );

	// once per run, not per frame: at frame rate this line buries every other
	// measurement in the log
	static bool reported = false;
	if ( r_glesD3Report.GetInteger() > 0 && !reported ) {
		reported = true;
		common->Printf( "gles_d3 interactions: draws=%i shadowDraws=%i lightScale=%.3f overBright=%.3f stencilWrap=%i\n",
				gles_interactionDraws, RB_GLESD3_ShadowDrawCount(),
				backEnd.lightScale, backEnd.overBright,
				tr.stencilIncr == GL_INCR_WRAP_EXT ? 1 : 0 );
	}
}

#endif /* OPENQ4_RENDERER_GLES_MODULE */
