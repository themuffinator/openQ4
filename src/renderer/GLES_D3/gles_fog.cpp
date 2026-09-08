// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	gles_d3 -- fog volumes and blend lights.

	Ports RB_STD_FogAllLights / RB_FogPass / RB_T_BasicFog / RB_BlendLight /
	RB_T_BlendLight (draw_common.cpp:8161, :8035, :7993, :7910, :7864), all of
	which live in the translation unit this module excludes.

	These are the last two Doom 3 core passes. Everything they draw is
	fixed-function in the original -- two modulate chains over texgen'd
	coordinates -- so the port is entirely a question of moving four texgen
	planes per surface from glTexGenfv into uniforms. The Vulkan module made
	that translation first (vk_Interactions.cpp:2373-2859) and its shaders are
	transliterated here rather than re-derived, so the two back ends can be
	diffed against each other when they disagree.


===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "gles_d3_local.h"
#include "gles_program.h"

// The fog texgen plane scratch (indexed by fogPlaneIndex_t) is extern in
// tr_local.h and owned by draw_common.cpp, which this module excludes -- so
// the module's definition lives here, exactly as the vk module's does
// (vk_Interactions.cpp:2376).
idPlane fogTexGenPlanes[4];

// texture units. Unit 0 is the density/projection image and unit 1 the
// correction/falloff image in both passes, matching the Vulkan descriptor set
// numbering; uTexture0/uTexture1 are bound to them once at link time.
static const int GLESD3_TMU_FOG = 0;
static const int GLESD3_TMU_FOG_ENTER = 1;

static glesProgram_t *		gles_fogProgram = NULL;
static glesProgram_t *		gles_blendProgram = NULL;

// tracked separately from backEnd.currentSpace: the interaction and material
// passes both advance that one, and a chain walk here has to re-issue its
// per-space uniforms from a known-clean start
static const viewEntity_t *	gles_fogSpace = NULL;

static float				gles_blendColor[ 4 ];
static const float *		gles_blendTextureMatrix = NULL;

static int	gles_fogLights = 0;
static int	gles_blendLights = 0;
static int	gles_fogDraws = 0;
static int	gles_blendDraws = 0;
static int	gles_fogSkips = 0;

/*
====================
GLESD3_FogDistanceScale

RB_FogDistanceScale (draw_common.cpp:241), which is static there. Fog alphas up
to 1.0 select the default 500-unit ramp; a larger alpha IS the fog distance in
world units.
====================
*/
static float GLESD3_FogDistanceScale( float alpha ) {
	if ( alpha <= 1.0f ) {
		return -0.5f / DEFAULT_FOG_DISTANCE;
	}
	return -0.5f / alpha;
}

/*
====================
GLESD3_FogIndexCache

The light frustum is the one piece of geometry in the frame that arrives with
an ambient cache and NO index cache: R_AddLightSurfaces builds it with
R_CreateAmbientCache (tr_light.cpp:1743), which allocates vertices only,
because the legacy path draws the cap with a client-memory index pointer.
Client-memory index pointers do not exist in ES, so the cap would silently not
draw -- the same class of defect that cost the ModernGL bring-up a round.

A frame-temp index cache is allocated here instead of being written back into
the light's frustumTris. Both alternatives are worse: a frame-temp stored on
persistent geometry dangles after the frame ends, and a persistent allocation
would have this pass taking ownership of a lifetime the light def already owns.
The cap is a dozen triangles, so re-uploading it per fog light per frame costs
nothing worth measuring.

Returns NULL when the geometry cannot be drawn; the caller counts that.
====================
*/
static vertCache_t *GLESD3_FogIndexCache( const srfTriangles_t *tri ) {
	// R_GLESD3_EnsureIndexCache does exactly this now, for every pass -- the
	// frustum cap was only the first place the missing-index-cache problem
	// surfaced, not the only one.
	return R_GLESD3_EnsureIndexCache( tri );
}

/*
====================
GLESD3_DrawFogElements

R_GLESD3_DrawElements reads tri->indexCache, which the frustum cap does not
have; this takes the cache explicitly so both callers share one draw and one
set of counters.
====================
*/
static void GLESD3_DrawFogElements( const srfTriangles_t *tri, vertCache_t *indexCache ) {
	backEnd.pc.c_drawElements++;
	backEnd.pc.c_drawIndexes += tri->numIndexes;
	backEnd.pc.c_drawVertexes += tri->numVerts;
	backEnd.pc.c_vboIndexes += tri->numIndexes;

	glDrawElements( GL_TRIANGLES,
			r_singleTriangle.GetBool() ? 3 : tri->numIndexes,
			GL_INDEX_TYPE,
			vertexCache.Position( indexCache ) );
}

/*
====================
GLESD3_FogBlendSurfaceScissor

RB_RenderDrawSurfChainWithFunction sets the scissor per surface before calling
the tri function; both chain walks here go through this.
====================
*/
static bool GLESD3_FogBlendSurfaceScissor( const drawSurf_t *surf ) {
	// false means the rect is empty and the surface can cover no pixels
	return RB_GLESD3_SetScissor( surf->scissorRect );
}

/*
====================
GLESD3_T_BasicFog

RB_T_BasicFog (draw_common.cpp:7993). On a space change the three carried
texgen rows localize -- tex0 S gains the +0.5 centre bias, tex1 T gains
+FOG_ENTER, and tex1 S (a zero-normal plane) transforms to itself -- and go out
as uniforms. They persist across the space's surfaces exactly like GL's latched
texgen planes did.

tex0 T is not carried at all: the GL path computes FOG_DISTANCE_PLANE_T and
then immediately overwrites it with the constant (0,0,0,0.5) row on every
surface, so the shader writes that constant directly.
====================
*/
static void GLESD3_T_BasicFog( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = surf->geo;

	if ( tri == NULL || tri->numIndexes <= 0 || tri->ambientCache == NULL ) {
		gles_fogSkips++;
		return;
	}

	vertCache_t *indexCache = GLESD3_FogIndexCache( tri );
	if ( indexCache == NULL ) {
		gles_fogSkips++;
		return;
	}
	if ( !R_GLESD3_BindDrawVertAttributes( tri ) ) {
		gles_fogSkips++;
		return;
	}

	if ( surf->space != gles_fogSpace ) {
		gles_fogSpace = surf->space;
		backEnd.currentSpace = surf->space;

		float mvp[ 16 ];
		myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mvp );
		glUniformMatrix4fv( gles_fogProgram->uMVP, 1, GL_FALSE, mvp );

		idPlane local;

		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_DISTANCE_PLANE_S], local );
		local[3] += 0.5f;
		glUniform4fv( gles_fogProgram->uFogDistanceS, 1, local.ToFloatPtr() );

		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_ENTER_PLANE_T], local );
		local[3] += FOG_ENTER;
		glUniform4fv( gles_fogProgram->uFogEnterT, 1, local.ToFloatPtr() );

		// constant per viewer: the zero-normal plane localizes to itself
		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_ENTER_PLANE_S], local );
		glUniform4fv( gles_fogProgram->uFogEnterS, 1, local.ToFloatPtr() );
	}

	GLESD3_DrawFogElements( tri, indexCache );
	gles_fogDraws++;
}

/*
====================
GLESD3_FogPass

RB_FogPass (draw_common.cpp:8035): the stage-0 fog colour and density, the
view-space eye-depth S plane and the scaled fogPlane rows, then the light's two
interaction chains at depth EQUAL, then the frustum cap at depth LEQUAL with
back-sided cull under the full view scissor.

The GL implementation spends a dozen lines re-asserting fixed-function state
(programs off, colour arrays off) because it shares GL state with the ARB2
paths around it. None of that has an analogue here: the fog program reads
position and nothing else, so there is no stale array to disable.
====================
*/
static void GLESD3_FogPass( const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2 ) {
	const viewLight_t *vLight = backEnd.vLight;
	drawSurf_t ds;

	// create a surface for the light frustum triangles, which are oriented
	// drawn side out; if we ran out of vertex cache memory, skip it
	const srfTriangles_t *frustumTris = vLight->frustumTris;
	if ( frustumTris == NULL || frustumTris->ambientCache == NULL ) {
		return;
	}
	memset( &ds, 0, sizeof( ds ) );
	ds.space = &backEnd.viewDef->worldSpace;
	ds.geo = frustumTris;
	ds.scissorRect = backEnd.viewDef->scissor;

	// find the current colour and density of the fog; fog shaders are assumed
	// to have a single stage
	const idMaterial *lightShader = vLight->lightShader;
	const float *regs = vLight->shaderRegisters;
	const shaderStage_t *stage = lightShader->GetStage( 0 );
	if ( stage == NULL ) {
		return;
	}

	// glColor3fv: rgb from the stage registers. Alpha pins to 1 because
	// register 3 holds the fog DISTANCE, not an opacity -- the density reaches
	// the frame through the two intrinsic images' alpha instead.
	float fogColor[ 4 ];
	fogColor[0] = regs[ stage->color.registers[0] ];
	fogColor[1] = regs[ stage->color.registers[1] ];
	fogColor[2] = regs[ stage->color.registers[2] ];
	fogColor[3] = 1.0f;

	const float a = GLESD3_FogDistanceScale( regs[ stage->color.registers[3] ] );

	// tex0 S: eye depth, off the view-space Z row of the world modelview
	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[2];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[6];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[10];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[14];

	// tex1 T: the fade plane -- always the "top" plane on unrotated lights --
	// scaled so one texel spans about 1000 units
	fogTexGenPlanes[FOG_ENTER_PLANE_T][0] = 0.001f * vLight->fogPlane[0];
	fogTexGenPlanes[FOG_ENTER_PLANE_T][1] = 0.001f * vLight->fogPlane[1];
	fogTexGenPlanes[FOG_ENTER_PLANE_T][2] = 0.001f * vLight->fogPlane[2];
	fogTexGenPlanes[FOG_ENTER_PLANE_T][3] = 0.001f * vLight->fogPlane[3];

	// tex1 S is based on the view origin, so it is a constant plane
	const float s = backEnd.viewDef->renderView.vieworg * fogTexGenPlanes[FOG_ENTER_PLANE_T].Normal()
		+ fogTexGenPlanes[FOG_ENTER_PLANE_T][3];

	fogTexGenPlanes[FOG_ENTER_PLANE_S][0] = 0.0f;
	fogTexGenPlanes[FOG_ENTER_PLANE_S][1] = 0.0f;
	fogTexGenPlanes[FOG_ENTER_PLANE_S][2] = 0.0f;
	fogTexGenPlanes[FOG_ENTER_PLANE_S][3] = FOG_ENTER + s;

	R_GLESD3_UseProgram( gles_fogProgram );
	glUniform4fv( gles_fogProgram->uColor, 1, fogColor );

	GL_SelectTexture( GLESD3_TMU_FOG );
	globalImages->fogImage->Bind();
	GL_SelectTexture( GLESD3_TMU_FOG_ENTER );
	globalImages->fogEnterImage->Bind();
	GL_SelectTexture( 0 );

	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA
			| GLS_DEPTHFUNC_EQUAL );
	GL_Cull( CT_FRONT_SIDED );

	// each chain walk starts from a clean space, exactly like
	// RB_RenderDrawSurfChainWithFunction
	gles_fogSpace = NULL;
	for ( const drawSurf_t *surf = drawSurfs; surf != NULL; surf = surf->nextOnLight ) {
		if ( !GLESD3_FogBlendSurfaceScissor( surf ) ) {
			continue;
		}
		GLESD3_T_BasicFog( surf );
	}
	gles_fogSpace = NULL;
	for ( const drawSurf_t *surf = drawSurfs2; surf != NULL; surf = surf->nextOnLight ) {
		if ( !GLESD3_FogBlendSurfaceScissor( surf ) ) {
			continue;
		}
		GLESD3_T_BasicFog( surf );
	}

	// the light frustum bounding planes aren't in the depth buffer, so the cap
	// uses depthfunc_less (GL_LEQUAL) rather than depthfunc_equal
	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA
			| GLS_DEPTHFUNC_LESS );
	GL_Cull( CT_BACK_SIDED );
	gles_fogSpace = NULL;
	if ( !GLESD3_FogBlendSurfaceScissor( &ds ) ) {
		return;
	}
	GLESD3_T_BasicFog( &ds );
	GL_Cull( CT_FRONT_SIDED );
}

/*
====================
GLESD3_T_BlendLight

RB_T_BlendLight (draw_common.cpp:7864). On a space change the four lightProject
planes localize, with the stage texture matrix folded into S and T -- the
replacement for GL's texture matrix, which ES does not have -- and go out as
uniforms.

The GL version also accepts geometry that carries only a shadowCache, since it
shares that helper with the shadow draw. That fallback is not reproduced: the
blend light chains are the light's INTERACTION chains, which are ambient-cached
by construction, and the counter below would show it if that ever stopped
holding.
====================
*/
static void GLESD3_T_BlendLight( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = surf->geo;

	if ( tri == NULL || tri->numIndexes <= 0 || tri->ambientCache == NULL ) {
		gles_fogSkips++;
		return;
	}

	vertCache_t *indexCache = GLESD3_FogIndexCache( tri );
	if ( indexCache == NULL ) {
		gles_fogSkips++;
		return;
	}
	if ( !R_GLESD3_BindDrawVertAttributes( tri ) ) {
		gles_fogSkips++;
		return;
	}

	if ( surf->space != gles_fogSpace ) {
		gles_fogSpace = surf->space;
		backEnd.currentSpace = surf->space;

		float mvp[ 16 ];
		myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mvp );
		glUniformMatrix4fv( gles_blendProgram->uMVP, 1, GL_FALSE, mvp );

		idPlane lightProject[ 4 ];
		for ( int i = 0; i < 4; i++ ) {
			R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[i],
					lightProject[i] );
		}
		if ( gles_blendTextureMatrix != NULL ) {
			R_GLESD3_BakeTextureMatrixIntoTexgen( lightProject, gles_blendTextureMatrix );
		}

		glUniform4fv( gles_blendProgram->uLightProjectionS, 1, lightProject[0].ToFloatPtr() );
		glUniform4fv( gles_blendProgram->uLightProjectionT, 1, lightProject[1].ToFloatPtr() );
		glUniform4fv( gles_blendProgram->uLightProjectionQ, 1, lightProject[2].ToFloatPtr() );
		glUniform4fv( gles_blendProgram->uLightFalloffS, 1, lightProject[3].ToFloatPtr() );
	}

	GLESD3_DrawFogElements( tri, indexCache );
	gles_blendDraws++;
}

/*
====================
GLESD3_BlendLight

RB_BlendLight (draw_common.cpp:7910): every condition-gated stage of the light
material projects its texture through lightProject[0..2] with the falloff
through lightProject[3], modulated by the stage's RGBA colour -- alpha
included, unlike a normal light -- and blended by the stage's own blend keyword
at depth EQUAL with depth writes off.

GL's empty-chain early-out tests the FIRST list only, which means a light whose
global chain is empty draws nothing even if its local chain is not. That looks
like a bug and is preserved anyway: it is observable behaviour shared with
ARB2 and with vk (vk_Interactions.cpp:2668), and changing it here would make
this back end differ from both references it is measured against.
====================
*/
static void GLESD3_BlendLight( const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2 ) {
	const viewLight_t *vLight = backEnd.vLight;
	float textureMatrix[ 16 ];

	if ( drawSurfs == NULL || r_skipBlendLights.GetBool() ) {
		return;
	}
	if ( vLight->falloffImage == NULL ) {
		return;
	}

	const idMaterial *lightShader = vLight->lightShader;
	const float *regs = vLight->shaderRegisters;

	R_GLESD3_UseProgram( gles_blendProgram );

	// texture 1 gets the falloff, once for every stage: T pins to 0.5 in the
	// shader, which is GL's glTexCoord2f( 0, 0.5 ) on the second unit
	GL_SelectTexture( GLESD3_TMU_FOG_ENTER );
	vLight->falloffImage->Bind();

	const int lightStageCount = lightShader->GetNumStages();
	for ( int i = 0; i < lightStageCount; i++ ) {
		const shaderStage_t *stage = lightShader->GetStage( i );

		if ( !regs[ stage->conditionRegister ] ) {
			continue;
		}
		if ( stage->texture.image == NULL ) {
			continue;
		}

		GL_State( GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL );
		GL_Cull( CT_FRONT_SIDED );

		GL_SelectTexture( GLESD3_TMU_FOG );
		stage->texture.image->Bind();

		if ( stage->texture.hasMatrix ) {
			R_GLESD3_GetShaderTextureMatrix( regs, &stage->texture, textureMatrix );
			gles_blendTextureMatrix = textureMatrix;
		} else {
			gles_blendTextureMatrix = NULL;
		}

		// the modulate values come from the light including alpha, unlike
		// normal lights
		gles_blendColor[0] = regs[ stage->color.registers[0] ];
		gles_blendColor[1] = regs[ stage->color.registers[1] ];
		gles_blendColor[2] = regs[ stage->color.registers[2] ];
		gles_blendColor[3] = regs[ stage->color.registers[3] ];
		glUniform4fv( gles_blendProgram->uColor, 1, gles_blendColor );

		gles_fogSpace = NULL;
		for ( const drawSurf_t *surf = drawSurfs; surf != NULL; surf = surf->nextOnLight ) {
			if ( !GLESD3_FogBlendSurfaceScissor( surf ) ) {
			continue;
		}
			GLESD3_T_BlendLight( surf );
		}
		gles_fogSpace = NULL;
		for ( const drawSurf_t *surf = drawSurfs2; surf != NULL; surf = surf->nextOnLight ) {
			if ( !GLESD3_FogBlendSurfaceScissor( surf ) ) {
			continue;
		}
			GLESD3_T_BlendLight( surf );
		}
	}

	gles_blendTextureMatrix = NULL;
}

/*
====================
RB_GLESD3_FogAllLights

RB_STD_FogAllLights (draw_common.cpp:8161). Fog and blend lights draw between
the two material walks, over the lights' interaction chains only --
translucentInteractions are never fogged.

The stencil test is disabled for the whole pass and re-enabled on the way out,
as the GL version does. That is not tidiness: the stencil buffer still carries
each light's shadow volumes from the interaction pass, and a fog volume gated
on a leftover GEQUAL test would be cut to the shape of the last light's
shadows. The D3XP-disabled anti-double-fog stencil guard (GL :8188-8206, inside
an #if 0) is not ported.
====================
*/
void RB_GLESD3_FogAllLights( void ) {
	const viewDef_t *viewDef = backEnd.viewDef;

	if ( viewDef == NULL || viewDef->viewLights == NULL ) {
		return;
	}
	if ( r_skipFogLights.GetBool() || r_showOverDraw.GetInteger() != 0
			|| viewDef->isXraySubview /* don't fog in xray mode */ ) {
		return;
	}

	// keep the common no-fog view zero-cost: no state is touched unless the
	// view actually holds a fog or blend light
	viewLight_t *vLight;
	for ( vLight = viewDef->viewLights; vLight; vLight = vLight->next ) {
		if ( vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight() ) {
			break;
		}
	}
	if ( vLight == NULL ) {
		return;
	}

	gles_fogProgram = R_GLESD3_Program( GLESD3_PROGRAM_FOG );
	gles_blendProgram = R_GLESD3_Program( GLESD3_PROGRAM_BLEND_LIGHT );
	if ( gles_fogProgram == NULL && gles_blendProgram == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_GLESD3_FogAllLights ----------\n" );

	glDisable( GL_STENCIL_TEST );

	for ( vLight = viewDef->viewLights; vLight; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		if ( vLight->lightShader->IsFogLight() ) {
			if ( gles_fogProgram == NULL ) {
				continue;
			}
			gles_fogLights++;
			GLESD3_FogPass( vLight->globalInteractions, vLight->localInteractions );
		} else if ( vLight->lightShader->IsBlendLight() ) {
			if ( gles_blendProgram == NULL ) {
				continue;
			}
			gles_blendLights++;
			GLESD3_BlendLight( vLight->globalInteractions, vLight->localInteractions );
		}
	}

	glEnable( GL_STENCIL_TEST );
	backEnd.vLight = NULL;
	gles_fogSpace = NULL;

	// leave no fog sampler bound over the material walk that follows
	GL_SelectTexture( GLESD3_TMU_FOG_ENTER );
	globalImages->BindNull();
	GL_SelectTexture( GLESD3_TMU_FOG );
	globalImages->BindNull();

	GL_Cull( CT_FRONT_SIDED );

	// once per run rather than per frame, on the same terms as the interaction
	// report: at frame rate this line buries every other measurement
	static bool reported = false;
	if ( r_glesD3Report.GetInteger() > 0 && !reported
			&& ( gles_fogDraws > 0 || gles_blendDraws > 0 || gles_fogSkips > 0 ) ) {
		reported = true;
		common->Printf( "gles_d3 fog/blend: fogLights=%i blendLights=%i fogDraws=%i blendDraws=%i skipped=%i\n",
				gles_fogLights, gles_blendLights, gles_fogDraws, gles_blendDraws, gles_fogSkips );
	}
}

/*
====================
RB_GLESD3_FogBlendCounts

Exposed so the per-view report can say whether this pass ran at all on a view
that looks wrong, without waiting for the one-shot summary above.
====================
*/
void RB_GLESD3_FogBlendCounts( int &fogLights, int &blendLights, int &draws, int &skipped ) {
	fogLights = gles_fogLights;
	blendLights = gles_blendLights;
	draws = gles_fogDraws + gles_blendDraws;
	skipped = gles_fogSkips;
}

#endif /* OPENQ4_RENDERER_GLES_MODULE */
