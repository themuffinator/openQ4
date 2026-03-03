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

// tr_stencilShadow.c -- creaton of stencil shadow volumes

struct shadowMapAtlasState_t {
	int		viewCount;
	int		atlasSize;
	int		tileSize;
	int		tilesPerRow;
	int		maxTiles;
	int		tilesUsed;
	idImage			*atlasImage;
	idRenderTexture	*atlasRenderTexture;
	bool			useAtlas;
};

static const char * const SHADOW_MAP_ATLAS_IMAGE_NAME = "_rbdoom_shadowMapAtlas";

static shadowMapAtlasState_t g_shadowMapAtlas = { -1, 0, 0, 0, 0, 0, NULL, NULL, false };

static bool R_InitShadowMapAtlasRenderTarget( void ) {
	idImageOpts atlasImageOpts;

	atlasImageOpts.format		= FMT_DEPTH;
	atlasImageOpts.width		= g_shadowMapAtlas.atlasSize;
	atlasImageOpts.height		= g_shadowMapAtlas.atlasSize;
	atlasImageOpts.numLevels	= 1;
	atlasImageOpts.textureType	= TT_2D;

	if ( g_shadowMapAtlas.atlasImage == NULL ||
		 g_shadowMapAtlas.atlasImage->GetOpts().width != g_shadowMapAtlas.atlasSize ||
		 g_shadowMapAtlas.atlasImage->GetOpts().height != g_shadowMapAtlas.atlasSize ||
		 g_shadowMapAtlas.atlasImage->GetOpts().format != FMT_DEPTH ||
		 !g_shadowMapAtlas.atlasImage->IsLoaded() ) {
		g_shadowMapAtlas.atlasImage = renderSystem->CreateImage(
			SHADOW_MAP_ATLAS_IMAGE_NAME, &atlasImageOpts, TF_NEAREST );
	}

	if ( g_shadowMapAtlas.atlasImage == NULL || !g_shadowMapAtlas.atlasImage->IsLoaded() ) {
		return false;
	}

	if ( g_shadowMapAtlas.atlasRenderTexture == NULL ) {
		g_shadowMapAtlas.atlasRenderTexture = renderSystem->CreateRenderTexture(
			NULL, g_shadowMapAtlas.atlasImage );
		return g_shadowMapAtlas.atlasRenderTexture != NULL;
	}

	renderSystem->ResizeRenderTexture( g_shadowMapAtlas.atlasRenderTexture,
		g_shadowMapAtlas.atlasSize, g_shadowMapAtlas.atlasSize );

	return true;
}

static void R_LogParallelShadowCascadePreview( int requestedSplits ) {
	static bool warned = false;
	if ( warned || requestedSplits <= 0 ) {
		return;
	}
	warned = true;
	common->Warning( "r_shadowMapSplits is set to %d; using cascaded parallel/sun-light shadow-map allocations.", requestedSplits );
}

static void R_ComputeShadowMapCascadeRanges( viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->shadowMapCascadeCount <= 0 ) {
		return;
	}

	if ( !vLight->parallel || vLight->shadowMapCascadeCount == 1 || tr.viewDef == NULL ) {
		const float cascadeStep = 1.0f / ( float )vLight->shadowMapCascadeCount;
		for ( int i = 0 ; i < vLight->shadowMapCascadeCount ; i++ ) {
			vLight->shadowMapCascadeNear[i] = i * cascadeStep;
			vLight->shadowMapCascadeFar[i] = ( i == vLight->shadowMapCascadeCount - 1 ) ? 1.0f : ( i + 1 ) * cascadeStep;
		}
		return;
	}

	float viewNear = tr.viewDef->viewFrustum.GetNearDistance();
	if ( viewNear < 0.01f ) {
		viewNear = 0.01f;
	}
	float viewFar = tr.viewDef->viewFrustum.GetFarDistance();
	if ( viewFar < viewNear + 1.0f ) {
		viewFar = viewNear + 1.0f;
	}
	const float viewRange = viewFar - viewNear;
	const float logRange = viewFar / viewNear;
	const float splitWeight = 0.65f;

	float previousSplit = viewNear;
	for ( int i = 0 ; i < vLight->shadowMapCascadeCount ; i++ ) {
		const float p = ( float )( i + 1 ) / ( float )vLight->shadowMapCascadeCount;
		const float linearSplit = viewNear + viewRange * p;
		const float logarithmicSplit = viewNear * idMath::Pow( logRange, p );
		const float cascadeSplit = splitWeight * logarithmicSplit + ( 1.0f - splitWeight ) * linearSplit;

		const float nearNorm = ( previousSplit - viewNear ) / viewRange;
		const float farNorm = ( cascadeSplit - viewNear ) / viewRange;

		vLight->shadowMapCascadeNear[i] = idMath::ClampFloat( 0.0f, 1.0f, nearNorm );
		vLight->shadowMapCascadeFar[i] = idMath::ClampFloat( 0.0f, 1.0f, farNorm );
		previousSplit = cascadeSplit;
	}

	vLight->shadowMapCascadeNear[0] = 0.0f;
	vLight->shadowMapCascadeFar[vLight->shadowMapCascadeCount - 1] = 1.0f;
}

void R_BuildShadowMapProjectionForCascade( const viewLight_t *vLight, const idPlane lightProject[4],
		const idVec3 &viewOrigin, int cascadeIndex, idVec4 shadowProjection[3], idVec4 *shadowClipW ) {
	if ( lightProject == NULL || shadowProjection == NULL ) {
		return;
	}

	idVec4 row0;
	idVec4 row1;
	idVec4 row2;
	idVec4 clipW;

	row0[0] = ( float )( 2.0f * lightProject[0][0] - lightProject[2][0] );
	row0[1] = ( float )( 2.0f * lightProject[0][1] - lightProject[2][1] );
	row0[2] = ( float )( 2.0f * lightProject[0][2] - lightProject[2][2] );
	row0[3] = ( float )( 2.0f * lightProject[0][3] - lightProject[2][3] );

	row1[0] = ( float )( 2.0f * lightProject[1][0] - lightProject[2][0] );
	row1[1] = ( float )( 2.0f * lightProject[1][1] - lightProject[2][1] );
	row1[2] = ( float )( 2.0f * lightProject[1][2] - lightProject[2][2] );
	row1[3] = ( float )( 2.0f * lightProject[1][3] - lightProject[2][3] );

	row2[0] = ( float )lightProject[3][0];
	row2[1] = ( float )lightProject[3][1];
	row2[2] = ( float )lightProject[3][2];
	row2[3] = ( float )lightProject[3][3];

	clipW[0] = ( float )lightProject[2][0];
	clipW[1] = ( float )lightProject[2][1];
	clipW[2] = ( float )lightProject[2][2];
	clipW[3] = ( float )lightProject[2][3];

	if ( vLight != NULL && vLight->parallel && vLight->shadowMapCascadeCount > 1 ) {
		const int clampedCascade = idMath::ClampInt( 0, vLight->shadowMapCascadeCount - 1, cascadeIndex );
		const float cascadeNear = idMath::ClampFloat( 0.0f, 1.0f, vLight->shadowMapCascadeNear[clampedCascade] );
		const float cascadeFar = idMath::ClampFloat( 0.0f, 1.0f, vLight->shadowMapCascadeFar[clampedCascade] );
		const float cascadeRange = idMath::ClampFloat( 0.02f, 1.0f, cascadeFar - cascadeNear );
		const float cascadeScale = idMath::ClampFloat( 0.2f, 1.0f, cascadeRange * 2.4f );

		row0 *= cascadeScale;
		row1 *= cascadeScale;

		const float focusS = row0[0] * viewOrigin[0] + row0[1] * viewOrigin[1] + row0[2] * viewOrigin[2] + row0[3];
		const float focusT = row1[0] * viewOrigin[0] + row1[1] * viewOrigin[1] + row1[2] * viewOrigin[2] + row1[3];
		row0[3] -= focusS;
		row1[3] -= focusT;

		if ( vLight->shadowMapTileSize > 0 ) {
			const float texelStep = 2.0f / ( float )vLight->shadowMapTileSize;
			row0[3] = idMath::Floor( row0[3] / texelStep + 0.5f ) * texelStep;
			row1[3] = idMath::Floor( row1[3] / texelStep + 0.5f ) * texelStep;
		}
	}

	shadowProjection[0] = row0;
	shadowProjection[1] = row1;
	shadowProjection[2] = row2;

	if ( shadowClipW != NULL ) {
		*shadowClipW = clipW;
	}
}

static void R_InitShadowMapAtlasState( void ) {
	g_shadowMapAtlas.viewCount = tr.viewCount;
	g_shadowMapAtlas.atlasSize = r_shadowMapAtlasSize.GetInteger();
	g_shadowMapAtlas.tileSize = r_shadowMapImageSize.GetInteger();
	g_shadowMapAtlas.tilesUsed = 0;
	g_shadowMapAtlas.tilesPerRow = 0;
	g_shadowMapAtlas.maxTiles = 0;
	g_shadowMapAtlas.useAtlas = false;

	if ( !r_useShadowMapping.GetBool() || !r_useShadowAtlas.GetBool() ||
		 g_shadowMapAtlas.atlasSize <= 0 || g_shadowMapAtlas.tileSize <= 0 || !glConfig.isInitialized ) {
		g_shadowMapAtlas.maxTiles = 0;
		return;
	}

	g_shadowMapAtlas.tilesPerRow = g_shadowMapAtlas.atlasSize / g_shadowMapAtlas.tileSize;
	if ( g_shadowMapAtlas.tilesPerRow <= 0 ) {
		g_shadowMapAtlas.maxTiles = 0;
		return;
	}

	g_shadowMapAtlas.maxTiles = g_shadowMapAtlas.tilesPerRow * g_shadowMapAtlas.tilesPerRow;
	g_shadowMapAtlas.useAtlas = R_InitShadowMapAtlasRenderTarget();
}

void R_ShutdownShadowMapAtlas( void ) {
	if ( g_shadowMapAtlas.atlasRenderTexture != NULL ) {
		renderSystem->DestroyRenderTexture( g_shadowMapAtlas.atlasRenderTexture );
		g_shadowMapAtlas.atlasRenderTexture = NULL;
	}
	g_shadowMapAtlas.atlasImage = NULL;
	g_shadowMapAtlas.tilesPerRow = 0;
	g_shadowMapAtlas.maxTiles = 0;
	g_shadowMapAtlas.tilesUsed = 0;
	g_shadowMapAtlas.atlasSize = 0;
	g_shadowMapAtlas.tileSize = 0;
	g_shadowMapAtlas.useAtlas = false;
}

void R_GetShadowMapAtlasStats( int &tilesUsed, int &maxTiles, int &tileSize,
							  int &atlasSize, int &tilesPerRow ) {
	if ( g_shadowMapAtlas.viewCount != tr.viewCount ) {
		R_InitShadowMapAtlasState();
	}

	tilesUsed = g_shadowMapAtlas.tilesUsed;
	maxTiles = g_shadowMapAtlas.maxTiles;
	tileSize = g_shadowMapAtlas.tileSize;
	atlasSize = g_shadowMapAtlas.atlasSize;
	tilesPerRow = g_shadowMapAtlas.tilesPerRow;
}

bool R_ShadowMappingAvailable( void ) {
	if ( !glConfig.isInitialized ) {
		return false;
	}

	if ( !r_useShadowMapping.GetBool() || !r_useShadowAtlas.GetBool() ) {
		if ( g_shadowMapAtlas.useAtlas || g_shadowMapAtlas.atlasRenderTexture != NULL ) {
			R_ShutdownShadowMapAtlas();
		}
		return false;
	}

	if ( g_shadowMapAtlas.viewCount != tr.viewCount ||
		 g_shadowMapAtlas.atlasImage == NULL ||
		 !g_shadowMapAtlas.atlasImage->IsLoaded() ||
		 g_shadowMapAtlas.atlasImage->GetOpts().width != g_shadowMapAtlas.atlasSize ||
		 g_shadowMapAtlas.atlasImage->GetOpts().height != g_shadowMapAtlas.atlasSize ||
		 g_shadowMapAtlas.atlasRenderTexture == NULL ||
		 !g_shadowMapAtlas.useAtlas ) {
		R_InitShadowMapAtlasState();
	}

	return g_shadowMapAtlas.useAtlas &&
		g_shadowMapAtlas.atlasImage != NULL &&
		g_shadowMapAtlas.atlasImage->IsLoaded() &&
		g_shadowMapAtlas.atlasRenderTexture != NULL;
}

void R_LogShadowMappingFallback( void ) {
	static bool warned = false;
	if ( warned ) {
		return;
	}
	warned = true;
	common->Warning( "r_useShadowMapping is enabled, but shadow-map backend is not active; falling back to legacy stencil shadows." );
}

bool R_ShouldUseShadowMapForLight( const idRenderLightLocal *light ) {
	if ( light == NULL || light->viewLight == NULL ) {
		return false;
	}

	return light->viewLight->useShadowMap;
}

void R_SelectShadowMapForViewLight( idRenderLightLocal *light, viewLight_t *vLight ) {
	if ( light == NULL || vLight == NULL ) {
		return;
	}

	vLight->useShadowMap = false;
	vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_NONE;
	vLight->shadowMapTile = -1;
	vLight->shadowMapTileX = -1;
	vLight->shadowMapTileY = -1;
	vLight->shadowMapTileSize = 0;
	vLight->shadowMapAtlasSize = 0;
	vLight->shadowMapAtlas = NULL;

	if ( light->parms.noShadows || light->lightShader == NULL || !light->lightShader->LightCastsShadows() ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_NO_SHADOWS;
		tr.pc.c_shadowMapFallbackNoShadows++;
		tr.pc.c_shadowMapFallbacks++;
		return;
	}

	if ( !r_useShadowMapping.GetBool() ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_DISABLED;
		tr.pc.c_shadowMapFallbackDisabled++;
		tr.pc.c_shadowMapFallbacks++;
		return;
	}

	tr.pc.c_shadowMapCandidates++;

	// Keep directional/sun lights in this path gated behind an explicit phase-2 switch.
	if ( light->parms.parallel && !r_useParallelShadowMaps.GetBool() ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_PARALLEL;
		tr.pc.c_shadowMapAtlasMisses++;
		tr.pc.c_shadowMapFallbacks++;
		tr.pc.c_shadowMapFallbackParallel++;
		return;
	}

	if ( light->parms.parallel ) {
		const int rawRequestedSplits = r_shadowMapSplits.GetInteger();
		const int requestedSplits = ( rawRequestedSplits > 0 ) ? rawRequestedSplits : 0;
		if ( requestedSplits > 0 ) {
			tr.pc.c_shadowMapSplitRequests++;
			if ( requestedSplits > MAX_SHADOW_MAP_CASCADES ) {
				tr.pc.c_shadowMapCascadeUnsupported++;
			}
			vLight->shadowMapCascadeCount = ( requestedSplits < MAX_SHADOW_MAP_CASCADES ) ? requestedSplits : MAX_SHADOW_MAP_CASCADES;
			R_LogParallelShadowCascadePreview( vLight->shadowMapCascadeCount );
		} else {
			vLight->shadowMapCascadeCount = 1;
		}
	} else {
		vLight->shadowMapCascadeCount = 1;
	}

	if ( !R_ShadowMappingAvailable() ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_BACKEND_UNAVAILABLE;
		tr.pc.c_shadowMapUnavailable++;
		tr.pc.c_shadowMapFallbacks++;
		tr.pc.c_shadowMapFallbackBackendUnavailable++;
		R_LogShadowMappingFallback();
		return;
	}

	if ( g_shadowMapAtlas.maxTiles <= 0 ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_ATLAS_INVALID;
		tr.pc.c_shadowMapAtlasMisses++;
		tr.pc.c_shadowMapFallbacks++;
		tr.pc.c_shadowMapFallbackAtlasInvalid++;
		return;
	}

	if ( g_shadowMapAtlas.tilesUsed >= g_shadowMapAtlas.maxTiles ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_ATLAS_FULL;
		tr.pc.c_shadowMapOverflows++;
		tr.pc.c_shadowMapAtlasMisses++;
		tr.pc.c_shadowMapFallbacks++;
		tr.pc.c_shadowMapFallbackAtlasFull++;
		return;
	}

	for ( int i = 0 ; i < vLight->shadowMapCascadeCount ; i++ ) {
		if ( g_shadowMapAtlas.tilesUsed >= g_shadowMapAtlas.maxTiles ) {
			break;
		}
		const int tile = g_shadowMapAtlas.tilesUsed++;
		vLight->shadowMapCascadeTile[i] = tile;
		vLight->shadowMapCascadeTileX[i] = tile % g_shadowMapAtlas.tilesPerRow;
		vLight->shadowMapCascadeTileY[i] = tile / g_shadowMapAtlas.tilesPerRow;
		tr.pc.c_shadowMapAllocations++;
	}

	int allocatedSplitCount = 0;
	while ( allocatedSplitCount < vLight->shadowMapCascadeCount
		&& vLight->shadowMapCascadeTile[allocatedSplitCount] >= 0 ) {
		allocatedSplitCount++;
	}
	vLight->shadowMapCascadeCount = allocatedSplitCount;

	if ( vLight->shadowMapCascadeCount <= 0 ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_ATLAS_FULL;
		tr.pc.c_shadowMapAtlasMisses++;
		tr.pc.c_shadowMapFallbacks++;
		return;
	}

	R_ComputeShadowMapCascadeRanges( vLight );

	vLight->useShadowMap = true;
	vLight->shadowMapTile = vLight->shadowMapCascadeTile[0];
	vLight->shadowMapTileX = vLight->shadowMapCascadeTileX[0];
	vLight->shadowMapTileY = vLight->shadowMapCascadeTileY[0];
	vLight->shadowMapTileSize = g_shadowMapAtlas.tileSize;
	vLight->shadowMapAtlasSize = g_shadowMapAtlas.atlasSize;
	vLight->shadowMapAtlas = g_shadowMapAtlas.atlasImage;
}

// Renders depth information for mapped shadowing into the atlas tile for this view-light.
bool R_RenderShadowMapForViewLight( viewLight_t *vLight ) {
	idRenderLightLocal *lightDef;
	const drawSurf_s *drawSurf;
	int tileX, tileY, tileSize, atlasSize;
	GLint oldViewport[4], oldScissor[4];
	GLboolean oldScissorEnabled;
	GLboolean oldBlendEnabled;
	GLboolean oldAlphaEnabled;
	GLboolean oldStencilEnabled;
	GLboolean oldDepthEnabled;
	GLboolean oldCullEnabled;
	GLboolean oldPolygonOffsetEnabled;
	GLboolean oldDepthMask;
	GLboolean oldColorMask[4];
	GLfloat oldDepthRange[2];
	GLfloat oldPolygonOffsetFactor;
	GLfloat oldPolygonOffsetUnits;
	GLint oldCullMode;
	GLint oldDepthFunc;
	GLint oldMatrixMode;
	GLboolean oldVertexArrayEnabled;
	GLboolean oldTexCoordArrayEnabled;
	GLboolean oldColorArrayEnabled;
	GLboolean oldNormalArrayEnabled;
	int occluderCount = 0;

	if ( !vLight || !vLight->lightDef || !vLight->lightDef->lightShader ) {
		return false;
	}

	lightDef = vLight->lightDef;
	vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_NONE;

	if ( !vLight->useShadowMap || vLight->shadowMapTile < 0 || !vLight->shadowMapAtlas
		|| vLight->shadowMapTileSize <= 0 || vLight->shadowMapAtlasSize <= 0 ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_MISSING_TILE;
		vLight->useShadowMap = false;
		tr.pc.c_shadowMapFallbackMissingTile++;
		tr.pc.c_shadowMapFallbacks++;
		return false;
	}

	if ( g_shadowMapAtlas.atlasRenderTexture == NULL || g_shadowMapAtlas.atlasImage != vLight->shadowMapAtlas ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_ATLAS_INVALID;
		vLight->useShadowMap = false;
		tr.pc.c_shadowMapFallbackAtlasInvalid++;
		tr.pc.c_shadowMapFallbacks++;
		return false;
	}

	if ( !R_ShadowMappingAvailable() ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_BACKEND_UNAVAILABLE;
		vLight->useShadowMap = false;
		tr.pc.c_shadowMapFallbackBackendUnavailable++;
		tr.pc.c_shadowMapFallbacks++;
		return false;
	}

	const idPlane		*lightProject = lightDef->lightProject;
	const drawSurf_s	*globalShadows = vLight->globalShadows;
	const drawSurf_s	*localShadows = vLight->localShadows;
	float				shadowProj[16];
	bool				drawnAny = false;
	int					cascadeCount;
	int					validCascadeCount = 0;
	idVec4				shadowProjection[3];
	idVec4				shadowClipW;

	if ( !globalShadows && !localShadows ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_NO_GEOMETRY;
		vLight->useShadowMap = false;
		tr.pc.c_shadowMapFallbackNoGeometry++;
		tr.pc.c_shadowMapFallbacks++;
		return false;
	}

	tileSize = vLight->shadowMapTileSize;
	atlasSize = vLight->shadowMapAtlasSize;
	cascadeCount = idMath::ClampInt( 1, MAX_SHADOW_MAP_CASCADES, vLight->shadowMapCascadeCount );

	if ( tileSize <= 0 || atlasSize <= 0 ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_MISSING_TILE;
		vLight->useShadowMap = false;
		tr.pc.c_shadowMapFallbackMissingTile++;
		tr.pc.c_shadowMapFallbacks++;
		return false;
	}

	for ( int cascadeIndex = 0 ; cascadeIndex < cascadeCount ; cascadeIndex++ ) {
		const int cascadeTileX = ( cascadeIndex == 0 ) ? vLight->shadowMapTileX : vLight->shadowMapCascadeTileX[cascadeIndex];
		const int cascadeTileY = ( cascadeIndex == 0 ) ? vLight->shadowMapTileY : vLight->shadowMapCascadeTileY[cascadeIndex];
		if ( cascadeTileX < 0 || cascadeTileY < 0 ) {
			continue;
		}

		tileX = cascadeTileX * tileSize;
		tileY = cascadeTileY * tileSize;
		if ( tileX < 0 || tileY < 0 || tileX + tileSize > atlasSize || tileY + tileSize > atlasSize ) {
			continue;
		}

		validCascadeCount++;
	}

	if ( validCascadeCount <= 0 ) {
		vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_MISSING_TILE;
		vLight->useShadowMap = false;
		tr.pc.c_shadowMapFallbackMissingTile++;
		tr.pc.c_shadowMapFallbacks++;
		return false;
	}

	glGetIntegerv( GL_VIEWPORT, oldViewport );
	glGetIntegerv( GL_SCISSOR_BOX, oldScissor );
	oldScissorEnabled = glIsEnabled( GL_SCISSOR_TEST );
	oldBlendEnabled = glIsEnabled( GL_BLEND );
	oldAlphaEnabled = glIsEnabled( GL_ALPHA_TEST );
	oldStencilEnabled = glIsEnabled( GL_STENCIL_TEST );
	oldDepthEnabled = glIsEnabled( GL_DEPTH_TEST );
	oldCullEnabled = glIsEnabled( GL_CULL_FACE );
	glGetIntegerv( GL_CULL_FACE_MODE, &oldCullMode );
	glGetIntegerv( GL_DEPTH_FUNC, &oldDepthFunc );
	oldPolygonOffsetEnabled = glIsEnabled( GL_POLYGON_OFFSET_FILL );
	glGetFloatv( GL_POLYGON_OFFSET_FACTOR, &oldPolygonOffsetFactor );
	glGetFloatv( GL_POLYGON_OFFSET_UNITS, &oldPolygonOffsetUnits );
	glGetFloatv( GL_DEPTH_RANGE, oldDepthRange );
	glGetBooleanv( GL_DEPTH_WRITEMASK, &oldDepthMask );
	glGetBooleanv( GL_COLOR_WRITEMASK, oldColorMask );
	glGetIntegerv( GL_MATRIX_MODE, &oldMatrixMode );
	glGetBooleanv( GL_VERTEX_ARRAY, &oldVertexArrayEnabled );
	glGetBooleanv( GL_TEXTURE_COORD_ARRAY, &oldTexCoordArrayEnabled );
	glGetBooleanv( GL_COLOR_ARRAY, &oldColorArrayEnabled );
	glGetBooleanv( GL_NORMAL_ARRAY, &oldNormalArrayEnabled );

	const int occluderFacing = r_shadowMapOccluderFacing.GetInteger();

	g_shadowMapAtlas.atlasRenderTexture->MakeCurrent();

	if ( occluderFacing == 0 ) {
		glEnable( GL_CULL_FACE );
		glCullFace( GL_FRONT );
	} else if ( occluderFacing == 1 ) {
		glEnable( GL_CULL_FACE );
		glCullFace( GL_BACK );
	} else {
		glDisable( GL_CULL_FACE );
	}

	glDisable( GL_BLEND );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glDepthMask( GL_TRUE );
	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	glEnableClientState( GL_VERTEX_ARRAY );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );

	for ( int cascadeIndex = 0 ; cascadeIndex < cascadeCount ; cascadeIndex++ ) {
		const int cascadeTileX = ( cascadeIndex == 0 ) ? vLight->shadowMapTileX : vLight->shadowMapCascadeTileX[cascadeIndex];
		const int cascadeTileY = ( cascadeIndex == 0 ) ? vLight->shadowMapTileY : vLight->shadowMapCascadeTileY[cascadeIndex];
		if ( cascadeTileX < 0 || cascadeTileY < 0 ) {
			continue;
		}

		tileX = cascadeTileX * tileSize;
		tileY = cascadeTileY * tileSize;
		if ( tileX < 0 || tileY < 0 || tileX + tileSize > atlasSize || tileY + tileSize > atlasSize ) {
			continue;
		}

		R_BuildShadowMapProjectionForCascade( vLight, lightProject, tr.viewDef->renderView.vieworg,
			cascadeIndex, shadowProjection, &shadowClipW );

		shadowProj[0] = shadowProjection[0][0];
		shadowProj[4] = shadowProjection[0][1];
		shadowProj[8] = shadowProjection[0][2];
		shadowProj[12] = shadowProjection[0][3];

		shadowProj[1] = shadowProjection[1][0];
		shadowProj[5] = shadowProjection[1][1];
		shadowProj[9] = shadowProjection[1][2];
		shadowProj[13] = shadowProjection[1][3];

		shadowProj[2] = shadowProjection[2][0];
		shadowProj[6] = shadowProjection[2][1];
		shadowProj[10] = shadowProjection[2][2];
		shadowProj[14] = shadowProjection[2][3];

		shadowProj[3] = shadowClipW[0];
		shadowProj[7] = shadowClipW[1];
		shadowProj[11] = shadowClipW[2];
		shadowProj[15] = shadowClipW[3];

		glViewport( tileX, tileY, tileSize, tileSize );
		glScissor( tileX, tileY, tileSize, tileSize );
		glEnable( GL_SCISSOR_TEST );
		glDepthRange( 0.0f, 1.0f );
		glClearDepth( 1.0f );
		glClear( GL_DEPTH_BUFFER_BIT );

		glMatrixMode( GL_PROJECTION );
		glPushMatrix();
		glLoadMatrixf( shadowProj );
		glMatrixMode( GL_MODELVIEW );
		glPushMatrix();
		glLoadIdentity();

		for ( drawSurf = globalShadows; drawSurf; drawSurf = drawSurf->nextOnLight ) {
			occluderCount++;
			const srfTriangles_t *tri = drawSurf->geo;
			if ( !tri || !tri->ambientCache ) {
				continue;
			}
			if ( drawSurf->space != backEnd.currentSpace ) {
				backEnd.currentSpace = drawSurf->space;
				glLoadMatrixf( drawSurf->space->modelMatrix );
			}
			idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
			glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
			RB_DrawElementsWithCounters( tri );
			drawnAny = true;
		}

		for ( drawSurf = localShadows; drawSurf; drawSurf = drawSurf->nextOnLight ) {
			occluderCount++;
			const srfTriangles_t *tri = drawSurf->geo;
			if ( !tri || !tri->ambientCache ) {
				continue;
			}
			if ( drawSurf->space != backEnd.currentSpace ) {
				backEnd.currentSpace = drawSurf->space;
				glLoadMatrixf( drawSurf->space->modelMatrix );
			}
			idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
			glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
			RB_DrawElementsWithCounters( tri );
			drawnAny = true;
		}

		glPopMatrix();
		glMatrixMode( GL_PROJECTION );
		glPopMatrix();
	}

	if ( oldNormalArrayEnabled ) {
		glEnableClientState( GL_NORMAL_ARRAY );
	} else {
		glDisableClientState( GL_NORMAL_ARRAY );
	}
	if ( oldColorArrayEnabled ) {
		glEnableClientState( GL_COLOR_ARRAY );
	} else {
		glDisableClientState( GL_COLOR_ARRAY );
	}
	if ( oldTexCoordArrayEnabled ) {
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	} else {
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	}
	if ( oldVertexArrayEnabled ) {
		glEnableClientState( GL_VERTEX_ARRAY );
	} else {
		glDisableClientState( GL_VERTEX_ARRAY );
	}

	if ( oldBlendEnabled ) {
		glEnable( GL_BLEND );
	} else {
		glDisable( GL_BLEND );
	}
	if ( oldAlphaEnabled ) {
		glEnable( GL_ALPHA_TEST );
	} else {
		glDisable( GL_ALPHA_TEST );
	}
	if ( oldStencilEnabled ) {
		glEnable( GL_STENCIL_TEST );
	} else {
		glDisable( GL_STENCIL_TEST );
	}
	if ( oldDepthEnabled ) {
		glEnable( GL_DEPTH_TEST );
	} else {
		glDisable( GL_DEPTH_TEST );
	}
	if ( oldCullEnabled ) {
		glEnable( GL_CULL_FACE );
		glCullFace( oldCullMode );
	} else {
		glDisable( GL_CULL_FACE );
	}
	if ( oldPolygonOffsetEnabled ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( oldPolygonOffsetFactor, oldPolygonOffsetUnits );
	} else {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	glDepthFunc( oldDepthFunc );
	glDepthRange( oldDepthRange[0], oldDepthRange[1] );
	glDepthMask( oldDepthMask );
	glColorMask( oldColorMask[0], oldColorMask[1], oldColorMask[2], oldColorMask[3] );
	glViewport( oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3] );
	glScissor( oldScissor[0], oldScissor[1], oldScissor[2], oldScissor[3] );
	if ( oldScissorEnabled ) {
		glEnable( GL_SCISSOR_TEST );
	} else {
		glDisable( GL_SCISSOR_TEST );
	}
	glMatrixMode( oldMatrixMode );

	// restore original render target
	if ( backEnd.renderTexture ) {
		backEnd.renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
	}

	if ( !drawnAny ) {
		if ( occluderCount == 0 ) {
			vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_NO_GEOMETRY;
			tr.pc.c_shadowMapFallbackNoGeometry++;
		} else {
			vLight->shadowMapFallbackReason = SHADOW_MAP_FALLBACK_RENDER_FAILED;
			tr.pc.c_shadowMapFallbackRenderFailed++;
		}
		tr.pc.c_shadowMapFallbacks++;
		vLight->useShadowMap = false;
	}

	return drawnAny;
}

/*

  Should we split shadow volume surfaces when they exceed max verts
  or max indexes?

  a problem is that the number of vertexes needed for the
  shadow volume will be twice the number in the original,
  and possibly up to 8/3 when near plane clipped.

  The maximum index count is 7x when not clipped and all
  triangles are completely discrete.  Near plane clipping
  can increase this to 10x.

  The maximum expansions are always with discrete triangles.
  Meshes of triangles will result in less index expansion because
  there will be less silhouette edges, although it will always be
  greater than the source if a cap is present.

  can't just project onto a plane if some surface points are
  behind the light.

  The cases when a face is edge on to a light is robustly handled
  with closed volumes, because only a single one of it's neighbors
  will pass the edge test.  It may be an issue with non-closed models.

  It is crucial that the shadow volumes be completely enclosed.
  The triangles identified as shadow sources will be projected
  directly onto the light far plane.
  The sil edges must be handled carefully.
  A partially clipped explicit sil edge will still generate a sil
  edge.
  EVERY new edge generated by clipping the triangles to the view
  will generate a sil edge.

  If a triangle has no points inside the frustum, it is completely
  culled away.  If a sil edge is either in or on the frustum, it is
  added.
  If a triangle has no points outside the frustum, it does not
  need to be clipped.

  

  USING THE STENCIL BUFFER FOR SHADOWING

  basic triangle property

  view plane inside shadow volume problem

  quad triangulation issue

  issues with silhouette optimizations

  the shapes of shadow projections are poor for sphere or box culling
  
  the gouraud shading problem


  // epsilon culling rules:

// the positive side of the frustum is inside
d = tri->verts[i].xyz * frustum[j].Normal() + frustum[j][3];
if ( d < LIGHT_CLIP_EPSILON ) {
	pointCull[i] |= ( 1 << j );
}
if ( d > -LIGHT_CLIP_EPSILON ) {
	pointCull[i] |= ( 1 << (6+j) );
}

If a low order bit is set, the point is on or outside the plane
If a high order bit is set, the point is on or inside the plane
If a low order bit is clear, the point is inside the plane (definately positive)
If a high order bit is clear, the point is outside the plane (definately negative)


*/

#define TRIANGLE_CULLED(p1,p2,p3) ( pointCull[p1] & pointCull[p2] & pointCull[p3] & 0x3f )

//#define TRIANGLE_CLIPPED(p1,p2,p3) ( ( pointCull[p1] | pointCull[p2] | pointCull[p3] ) & 0xfc0 )
#define TRIANGLE_CLIPPED(p1,p2,p3) ( ( ( pointCull[p1] & pointCull[p2] & pointCull[p3] ) & 0xfc0 ) != 0xfc0 )

// an edge that is on the plane is NOT culled
#define EDGE_CULLED(p1,p2) ( ( pointCull[p1] ^ 0xfc0 ) & ( pointCull[p2] ^ 0xfc0 ) & 0xfc0 )

#define EDGE_CLIPPED(p1,p2) ( ( pointCull[p1] & pointCull[p2] & 0xfc0 ) != 0xfc0 )

// a point that is on the plane is NOT culled
//#define	POINT_CULLED(p1) ( ( pointCull[p1] ^ 0xfc0 ) & 0xfc0 )
#define	POINT_CULLED(p1) ( ( pointCull[p1] & 0xfc0 ) != 0xfc0 )

//#define	LIGHT_CLIP_EPSILON	0.001f
#define	LIGHT_CLIP_EPSILON		0.1f

#define	MAX_CLIP_SIL_EDGES		2048
static int	numClipSilEdges;
static int	clipSilEdges[MAX_CLIP_SIL_EDGES][2];

// facing will be 0 if forward facing, 1 if backwards facing
// grabbed with alloca
static byte	*globalFacing;

// faceCastsShadow will be 1 if the face is in the projection
// and facing the apropriate direction
static byte	*faceCastsShadow;

static int	*remap;

#define	MAX_SHADOW_INDEXES		0x18000
#define	MAX_SHADOW_VERTS		0x18000
static int	numShadowIndexes;
static glIndex_t	shadowIndexes[MAX_SHADOW_INDEXES];
static int	numShadowVerts;
static idVec4	shadowVerts[MAX_SHADOW_VERTS];
static bool overflowed;

idPlane	pointLightFrustums[6][6] = {
	{
		idPlane( 1,0,0,0 ),
		idPlane( 1,1,0,0 ),
		idPlane( 1,-1,0,0 ),
		idPlane( 1,0,1,0 ),
		idPlane( 1,0,-1,0 ),
		idPlane( -1,0,0,0 ),
	},
	{
		idPlane( -1,0,0,0 ),
		idPlane( -1,1,0,0 ),
		idPlane( -1,-1,0,0 ),
		idPlane( -1,0,1,0 ),
		idPlane( -1,0,-1,0 ),
		idPlane( 1,0,0,0 ),
	},

	{
		idPlane( 0,1,0,0 ),
		idPlane( 0,1,1,0 ),
		idPlane( 0,1,-1,0 ),
		idPlane( 1,1,0,0 ),
		idPlane( -1,1,0,0 ),
		idPlane( 0,-1,0,0 ),
	},
	{
		idPlane( 0,-1,0,0 ),
		idPlane( 0,-1,1,0 ),
		idPlane( 0,-1,-1,0 ),
		idPlane( 1,-1,0,0 ),
		idPlane( -1,-1,0,0 ),
		idPlane( 0,1,0,0 ),
	},

	{
		idPlane( 0,0,1,0 ),
		idPlane( 1,0,1,0 ),
		idPlane( -1,0,1,0 ),
		idPlane( 0,1,1,0 ),
		idPlane( 0,-1,1,0 ),
		idPlane( 0,0,-1,0 ),
	},
	{
		idPlane( 0,0,-1,0 ),
		idPlane( 1,0,-1,0 ),
		idPlane( -1,0,-1,0 ),
		idPlane( 0,1,-1,0 ),
		idPlane( 0,-1,-1,0 ),
		idPlane( 0,0,1,0 ),
	},
};

int	c_caps, c_sils;

static bool	callOptimizer;			// call the preprocessor optimizer after clipping occluders

typedef struct {
	int		frontCapStart;
	int		rearCapStart;
	int		silStart;
	int		end;
} indexRef_t;
static indexRef_t	indexRef[6];
static int indexFrustumNumber;		// which shadow generating side of a light the indexRef is for

/*
===============
PointsOrdered

To make sure the triangulations of the sil edges is consistant,
we need to be able to order two points.  We don't care about how
they compare with any other points, just that when the same two
points are passed in (in either order), they will always specify
the same one as leading.

Currently we need to have separate faces in different surfaces
order the same way, so we must look at the actual coordinates.
If surfaces are ever guaranteed to not have to edge match with
other surfaces, we could just compare indexes.
===============
*/
static bool PointsOrdered( const idVec3 &a, const idVec3 &b ) {
	float	i, j;

	// vectors that wind up getting an equal hash value will
	// potentially cause a misorder, which can show as a couple
	// crack pixels in a shadow

	// scale by some odd numbers so -8, 8, 8 will not be equal
	// to 8, -8, 8

	// in the very rare case that these might be equal, all that would
	// happen is an oportunity for a tiny rasterization shadow crack
	i = a[0] + a[1]*127 + a[2]*1023;
	j = b[0] + b[1]*127 + b[2]*1023;

	return (bool)(i < j);
}

/*
====================
R_LightProjectionMatrix

====================
*/
void R_LightProjectionMatrix( const idVec3 &origin, const idPlane &rearPlane, idVec4 mat[4] ) {
	idVec4		lv;
	float		lg;

	// calculate the homogenious light vector
	lv.x = origin.x;
	lv.y = origin.y;
	lv.z = origin.z;
	lv.w = 1;

	lg = rearPlane.ToVec4() * lv;

	// outer product
	mat[0][0] = lg -rearPlane[0] * lv[0];
	mat[0][1] = -rearPlane[1] * lv[0];
	mat[0][2] = -rearPlane[2] * lv[0];
	mat[0][3] = -rearPlane[3] * lv[0];

	mat[1][0] = -rearPlane[0] * lv[1];
	mat[1][1] = lg -rearPlane[1] * lv[1];
	mat[1][2] = -rearPlane[2] * lv[1];
	mat[1][3] = -rearPlane[3] * lv[1];

	mat[2][0] = -rearPlane[0] * lv[2];
	mat[2][1] = -rearPlane[1] * lv[2];
	mat[2][2] = lg -rearPlane[2] * lv[2];
	mat[2][3] = -rearPlane[3] * lv[2];

	mat[3][0] = -rearPlane[0] * lv[3];
	mat[3][1] = -rearPlane[1] * lv[3];
	mat[3][2] = -rearPlane[2] * lv[3];
	mat[3][3] = lg -rearPlane[3] * lv[3];
}

/*
===================
R_ProjectPointsToFarPlane

make a projected copy of the even verts into the odd spots
that is on the far light clip plane
===================
*/
static void R_ProjectPointsToFarPlane( const idRenderEntityLocal *ent, const idRenderLightLocal *light,
									const idPlane &lightPlaneLocal,
									int firstShadowVert, int numShadowVerts ) {
	idVec3		lv;
	idVec4		mat[4];
	int			i;
	idVec4		*in;

	R_GlobalPointToLocal( ent->modelMatrix, light->globalLightOrigin, lv );
	R_LightProjectionMatrix( lv, lightPlaneLocal, mat );

#if 1
	// make a projected copy of the even verts into the odd spots
	in = &shadowVerts[firstShadowVert];
	for ( i = firstShadowVert ; i < numShadowVerts ; i+= 2, in += 2 ) {
		float	w, oow;

		in[0].w = 1;

		w = in->ToVec3() * mat[3].ToVec3() + mat[3][3];
		if ( w == 0 ) {
			in[1] = in[0];
			continue;
		}

		oow = 1.0 / w;
		in[1].x = ( in->ToVec3() * mat[0].ToVec3() + mat[0][3] ) * oow;
		in[1].y = ( in->ToVec3() * mat[1].ToVec3() + mat[1][3] ) * oow;
		in[1].z = ( in->ToVec3() * mat[2].ToVec3() + mat[2][3] ) * oow;
		in[1].w = 1;
	}

#else
	// messing with W seems to cause some depth precision problems

	// make a projected copy of the even verts into the odd spots
	in = &shadowVerts[firstShadowVert];
	for ( i = firstShadowVert ; i < numShadowVerts ; i+= 2, in += 2 ) {
		in[0].w = 1;
		in[1].x = *in * mat[0].ToVec3() + mat[0][3];
		in[1].y = *in * mat[1].ToVec3() + mat[1][3];
		in[1].z = *in * mat[2].ToVec3() + mat[2][3];
		in[1].w = *in * mat[3].ToVec3() + mat[3][3];
	}
#endif
}



#define	MAX_CLIPPED_POINTS	20
typedef struct {
	int		numVerts;
	idVec3	verts[MAX_CLIPPED_POINTS];
	int		edgeFlags[MAX_CLIPPED_POINTS];
} clipTri_t;

/*
=============
R_ChopWinding

Clips a triangle from one buffer to another, setting edge flags
The returned buffer may be the same as inNum if no clipping is done
If entirely clipped away, clipTris[returned].numVerts == 0

I have some worries about edge flag cases when polygons are clipped
multiple times near the epsilon.
=============
*/
static int R_ChopWinding( clipTri_t clipTris[2], int inNum, const idPlane &plane ) {
	clipTri_t	*in, *out;
	float	dists[MAX_CLIPPED_POINTS];
	int		sides[MAX_CLIPPED_POINTS];
	int		counts[3];
	float	dot;
	int		i, j;
	idVec3	*p1, *p2;
	idVec3	mid;

	in = &clipTris[inNum];
	out = &clipTris[inNum^1];
	counts[0] = counts[1] = counts[2] = 0;

	// determine sides for each point
	for ( i = 0 ; i < in->numVerts ; i++ ) {
		dot = plane.Distance( in->verts[i] );
		dists[i] = dot;
		if ( dot < -LIGHT_CLIP_EPSILON ) {
			sides[i] = SIDE_BACK;
		} else if ( dot > LIGHT_CLIP_EPSILON ) {
			sides[i] = SIDE_FRONT;
		} else {
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}

	// if none in front, it is completely clipped away
	if ( !counts[SIDE_FRONT] ) {
		in->numVerts = 0;
		return inNum;
	}
	if ( !counts[SIDE_BACK] ) {
		return inNum;		// inout stays the same
	}

	// avoid wrapping checks by duplicating first value to end
	sides[i] = sides[0];
	dists[i] = dists[0];
	in->verts[in->numVerts] = in->verts[0];
	in->edgeFlags[in->numVerts] = in->edgeFlags[0];

	out->numVerts = 0;
	for ( i = 0 ; i < in->numVerts ; i++ ) {
		p1 = &in->verts[i];

		if ( sides[i] != SIDE_BACK ) {
			out->verts[out->numVerts] = *p1;
			if ( sides[i] == SIDE_ON && sides[i+1] == SIDE_BACK ) {
				out->edgeFlags[out->numVerts] = 1;
			} else {
				out->edgeFlags[out->numVerts] = in->edgeFlags[i];
			}
			out->numVerts++;
		}

		if ( (sides[i] == SIDE_FRONT && sides[i+1] == SIDE_BACK)
			|| (sides[i] == SIDE_BACK && sides[i+1] == SIDE_FRONT) ) {
			// generate a split point
			p2 = &in->verts[i+1];
			
			dot = dists[i] / (dists[i]-dists[i+1]);
			for ( j=0 ; j<3 ; j++ ) {
				mid[j] = (*p1)[j] + dot*((*p2)[j]-(*p1)[j]);
			}
				
			out->verts[out->numVerts] = mid;

			// set the edge flag
			if ( sides[i+1] != SIDE_FRONT ) {
				out->edgeFlags[out->numVerts] = 1;
			} else {
				out->edgeFlags[out->numVerts] = in->edgeFlags[i];
			}

			out->numVerts++;
		}
	}

	return inNum ^ 1;
}

/*
===================
R_ClipTriangleToLight

Returns false if nothing is left after clipping
===================
*/
static bool	R_ClipTriangleToLight( const idVec3 &a, const idVec3 &b, const idVec3 &c, int planeBits,
							  const idPlane frustum[6] ) {
	int			i;
	int			base;
	clipTri_t	pingPong[2], *ct;
	int			p;

	pingPong[0].numVerts = 3;
	pingPong[0].edgeFlags[0] = 0;
	pingPong[0].edgeFlags[1] = 0;
	pingPong[0].edgeFlags[2] = 0;
	pingPong[0].verts[0] = a;
	pingPong[0].verts[1] = b;
	pingPong[0].verts[2] = c;

	p = 0;
	for ( i = 0 ; i < 6 ; i++ ) {
		if ( planeBits & ( 1 << i ) ) {
			p = R_ChopWinding( pingPong, p, frustum[i] );
			if ( pingPong[p].numVerts < 1 ) {
				return false;
			}
		}
	}
	ct = &pingPong[p];

	// copy the clipped points out to shadowVerts
	if ( numShadowVerts + ct->numVerts * 2 > MAX_SHADOW_VERTS ) {
		overflowed = true;
		return false;
	}

	base = numShadowVerts;
	for ( i = 0 ; i < ct->numVerts ; i++ ) {
		shadowVerts[ base + i*2 ].ToVec3() = ct->verts[i];
	}
	numShadowVerts += ct->numVerts * 2;

	if ( numShadowIndexes + 3 * ( ct->numVerts - 2 ) > MAX_SHADOW_INDEXES ) {
		overflowed = true;
		return false;
	}

	for ( i = 2 ; i < ct->numVerts ; i++ ) {
		shadowIndexes[numShadowIndexes++] = base + i * 2;
		shadowIndexes[numShadowIndexes++] = base + ( i - 1 ) * 2;
		shadowIndexes[numShadowIndexes++] = base;
	}

	// any edges that were created by the clipping process will
	// have a silhouette quad created for it, because it is one
	// of the exterior bounds of the shadow volume
	for ( i = 0 ; i < ct->numVerts ; i++ ) {
		if ( ct->edgeFlags[i] ) {
			if ( numClipSilEdges == MAX_CLIP_SIL_EDGES ) {
				break;
			}
			clipSilEdges[ numClipSilEdges ][0] = base + i * 2;
			if ( i == ct->numVerts - 1 ) {
				clipSilEdges[ numClipSilEdges ][1] = base;
			} else {
				clipSilEdges[ numClipSilEdges ][1] = base + ( i + 1 ) * 2;
			}
			numClipSilEdges++;
		}
	}

	return true;
}

/*
===================
R_ClipLineToLight

If neither point is clearly behind the clipping
plane, the edge will be passed unmodified.  A sil edge that
is on a border plane must be drawn.

If one point is clearly clipped by the plane and the
other point is on the plane, it will be completely removed.
===================
*/
static bool R_ClipLineToLight(	const idVec3 &a, const idVec3 &b, const idPlane frustum[4], 
						   idVec3 &p1, idVec3 &p2 ) {
	float	*clip;
	int		j;
	float	d1, d2;
	float	f;

	p1 = a;
	p2 = b;

	// clip it
	for ( j = 0 ; j < 6 ; j++ ) {
		d1 = frustum[j].Distance( p1 );
		d2 = frustum[j].Distance( p2 );

		// if both on or in front, not clipped to this plane
		if ( d1 > -LIGHT_CLIP_EPSILON && d2 > -LIGHT_CLIP_EPSILON ) {
			continue;
		}

		// if one is behind and the other isn't clearly in front, the edge is clipped off
		if ( d1 <= -LIGHT_CLIP_EPSILON && d2 < LIGHT_CLIP_EPSILON ) {
			return false;
		}
		if ( d2 <= -LIGHT_CLIP_EPSILON && d1 < LIGHT_CLIP_EPSILON ) {
			return false;
		}

		// clip it, keeping the negative side
		if ( d1 < 0 ) {
			clip = p1.ToFloatPtr();
		} else {
			clip = p2.ToFloatPtr();
		}

#if 0
		if ( idMath::Fabs(d1 - d2) < 0.001 ) {
			d2 = d1 - 0.1;
		}
#endif

		f = d1 / ( d1 - d2 );
		clip[0] = p1[0] + f * ( p2[0] - p1[0] );
		clip[1] = p1[1] + f * ( p2[1] - p1[1] );
		clip[2] = p1[2] + f * ( p2[2] - p1[2] );
	}

	return true;	// retain a fragment
}


/*
==================
R_AddClipSilEdges

Add sil edges for each triangle clipped to the side of
the frustum.

Only done for simple projected lights, not point lights.
==================
*/
static void R_AddClipSilEdges( void ) {
	int		v1, v2;
	int		v1_back, v2_back;
	int		i;

	// don't allow it to overflow
	if ( numShadowIndexes + numClipSilEdges * 6 > MAX_SHADOW_INDEXES ) {
		overflowed = true;
		return;
	}

	for ( i = 0 ; i < numClipSilEdges ; i++ ) {
		v1 = clipSilEdges[i][0];
		v2 = clipSilEdges[i][1];
		v1_back = v1 + 1;
		v2_back = v2 + 1;
		if ( PointsOrdered( shadowVerts[ v1 ].ToVec3(), shadowVerts[ v2 ].ToVec3() ) ) {
			shadowIndexes[numShadowIndexes++] = v1;
			shadowIndexes[numShadowIndexes++] = v2;
			shadowIndexes[numShadowIndexes++] = v1_back;
			shadowIndexes[numShadowIndexes++] = v2;
			shadowIndexes[numShadowIndexes++] = v2_back;
			shadowIndexes[numShadowIndexes++] = v1_back;
		} else {
			shadowIndexes[numShadowIndexes++] = v1;
			shadowIndexes[numShadowIndexes++] = v2;
			shadowIndexes[numShadowIndexes++] = v2_back;
			shadowIndexes[numShadowIndexes++] = v1;
			shadowIndexes[numShadowIndexes++] = v2_back;
			shadowIndexes[numShadowIndexes++] = v1_back;
		}
	}
}

/*
=================
R_AddSilEdges

Add quads from the front points to the projected points
for each silhouette edge in the light
=================
*/
static void R_AddSilEdges( const srfTriangles_t *tri, unsigned short *pointCull, const idPlane frustum[6] ) {
	int		v1, v2;
	int		i;
	silEdge_t	*sil;
	int		numPlanes;

	numPlanes = tri->numIndexes / 3;

	// add sil edges for any true silhouette boundaries on the surface
	for ( i = 0 ; i < tri->numSilEdges ; i++ ) {
		sil = tri->silEdges + i;
		if ( sil->p1 < 0 || sil->p1 > numPlanes || sil->p2 < 0 || sil->p2 > numPlanes ) {
			common->Error( "Bad sil planes" );
		}

		// an edge will be a silhouette edge if the face on one side
		// casts a shadow, but the face on the other side doesn't.
		// "casts a shadow" means that it has some surface in the projection,
		// not just that it has the correct facing direction
		// This will cause edges that are exactly on the frustum plane
		// to be considered sil edges if the face inside casts a shadow.
		if ( !( faceCastsShadow[ sil->p1 ] ^ faceCastsShadow[ sil->p2 ] ) ) {
			continue;
		}

		// if the edge is completely off the negative side of
		// a frustum plane, don't add it at all.  This can still
		// happen even if the face is visible and casting a shadow
		// if it is partially clipped
		if ( EDGE_CULLED( sil->v1, sil->v2 ) ) {
			continue;
		}

		// see if the edge needs to be clipped
		if ( EDGE_CLIPPED( sil->v1, sil->v2 ) ) {
			if ( numShadowVerts + 4 > MAX_SHADOW_VERTS ) {
				overflowed = true;
				return;
			}
			v1 = numShadowVerts;
			v2 = v1 + 2;
			if ( !R_ClipLineToLight( tri->verts[ sil->v1 ].xyz, tri->verts[ sil->v2 ].xyz, 
				frustum, shadowVerts[v1].ToVec3(), shadowVerts[v2].ToVec3() ) ) {
				continue;	// clipped away
			}

			numShadowVerts += 4;
		} else {
			// use the entire edge
			v1 = remap[ sil->v1 ];
			v2 = remap[ sil->v2 ];
			if ( v1 < 0 || v2 < 0 ) {
				common->Error( "R_AddSilEdges: bad remap[]" );
			}
		}

		// don't overflow
		if ( numShadowIndexes + 6 > MAX_SHADOW_INDEXES ) {
			overflowed = true;
			return;
		}

		// we need to choose the correct way of triangulating the silhouette quad
		// consistantly between any two points, no matter which order they are specified.
		// If this wasn't done, slight rasterization cracks would show in the shadow
		// volume when two sil edges were exactly coincident
		if ( faceCastsShadow[ sil->p2 ] ) {
			if ( PointsOrdered( shadowVerts[ v1 ].ToVec3(), shadowVerts[ v2 ].ToVec3() ) ) {
				shadowIndexes[numShadowIndexes++] = v1;
				shadowIndexes[numShadowIndexes++] = v1+1;
				shadowIndexes[numShadowIndexes++] = v2;
				shadowIndexes[numShadowIndexes++] = v2;
				shadowIndexes[numShadowIndexes++] = v1+1;
				shadowIndexes[numShadowIndexes++] = v2+1;
			} else {
				shadowIndexes[numShadowIndexes++] = v1;
				shadowIndexes[numShadowIndexes++] = v2+1;
				shadowIndexes[numShadowIndexes++] = v2;
				shadowIndexes[numShadowIndexes++] = v1;
				shadowIndexes[numShadowIndexes++] = v1+1;
				shadowIndexes[numShadowIndexes++] = v2+1;
			}
		} else { 
			if ( PointsOrdered( shadowVerts[ v1 ].ToVec3(), shadowVerts[ v2 ].ToVec3() ) ) {
				shadowIndexes[numShadowIndexes++] = v1;
				shadowIndexes[numShadowIndexes++] = v2;
				shadowIndexes[numShadowIndexes++] = v1+1;
				shadowIndexes[numShadowIndexes++] = v2;
				shadowIndexes[numShadowIndexes++] = v2+1;
				shadowIndexes[numShadowIndexes++] = v1+1;
			} else {
				shadowIndexes[numShadowIndexes++] = v1;
				shadowIndexes[numShadowIndexes++] = v2;
				shadowIndexes[numShadowIndexes++] = v2+1;
				shadowIndexes[numShadowIndexes++] = v1;
				shadowIndexes[numShadowIndexes++] = v2+1;
				shadowIndexes[numShadowIndexes++] = v1+1;
			}
		}
	}
}

/*
================
R_CalcPointCull

Also inits the remap[] array to all -1
================
*/
static void R_CalcPointCull( const srfTriangles_t *tri, const idPlane frustum[6], unsigned short *pointCull ) {
	int i;
	int frontBits;
	float *planeSide;
	byte *side1, *side2;

	SIMDProcessor->Memset( remap, -1, tri->numVerts * sizeof( remap[0] ) );

	for ( frontBits = 0, i = 0; i < 6; i++ ) {
		// get front bits for the whole surface
		if ( tri->bounds.PlaneDistance( frustum[i] ) >= LIGHT_CLIP_EPSILON ) {
			frontBits |= 1<<(i+6);
		}
	}

	// initialize point cull
	for ( i = 0; i < tri->numVerts; i++ ) {
		pointCull[i] = frontBits;
	}

	// if the surface is not completely inside the light frustum
	if ( frontBits == ( ( ( 1 << 6 ) - 1 ) ) << 6 ) {
		return;
	}

	planeSide = (float *) _alloca16( tri->numVerts * sizeof( float ) );
	side1 = (byte *) _alloca16( tri->numVerts * sizeof( byte ) );
	side2 = (byte *) _alloca16( tri->numVerts * sizeof( byte ) );
	SIMDProcessor->Memset( side1, 0, tri->numVerts * sizeof( byte ) );
	SIMDProcessor->Memset( side2, 0, tri->numVerts * sizeof( byte ) );

	for ( i = 0; i < 6; i++ ) {

		if ( frontBits & (1<<(i+6)) ) {
			continue;
		}

		SIMDProcessor->Dot( planeSide, frustum[i], tri->verts, tri->numVerts );
		SIMDProcessor->CmpLT( side1, i, planeSide, LIGHT_CLIP_EPSILON, tri->numVerts );
		SIMDProcessor->CmpGT( side2, i, planeSide, -LIGHT_CLIP_EPSILON, tri->numVerts );
	}
	for ( i = 0; i < tri->numVerts; i++ ) {
		pointCull[i] |= side1[i] | (side2[i] << 6);
	}
}

/*
=================
R_CreateShadowVolumeInFrustum

Adds new verts and indexes to the shadow volume.

If the frustum completely defines the projected light,
makeClippedPlanes should be true, which will cause sil quads to
be added along all clipped edges.

If the frustum is just part of a point light, clipped planes don't
need to be added.
=================
*/
static void R_CreateShadowVolumeInFrustum( const idRenderEntityLocal *ent, 
										  const srfTriangles_t *tri,
										  const idRenderLightLocal *light,									
										  const idVec3 lightOrigin,
										  const idPlane frustum[6],
										  const idPlane &farPlane,
										  bool makeClippedPlanes ) {
	int		i;
	int		numTris;
	unsigned short		*pointCull;
	int		numCapIndexes;
	int		firstShadowIndex;
	int		firstShadowVert;
	int		cullBits;

	pointCull = (unsigned short *)_alloca16( tri->numVerts * sizeof( pointCull[0] ) );

	// test the vertexes for inside the light frustum, which will allow
	// us to completely cull away some triangles from consideration.
	R_CalcPointCull( tri, frustum, pointCull );

	// this may not be the first frustum added to the volume
	firstShadowIndex = numShadowIndexes;
	firstShadowVert = numShadowVerts;

	// decide which triangles front shadow volumes, clipping as needed
	numClipSilEdges = 0;
	numTris = tri->numIndexes / 3;
	for ( i = 0 ; i < numTris ; i++ ) {
		int		i1, i2, i3;

		faceCastsShadow[i] = 0;	// until shown otherwise

		// if it isn't facing the right way, don't add it
		// to the shadow volume
		if ( globalFacing[i] ) {
			continue;
		}

		i1 = tri->silIndexes[ i*3 + 0 ];
		i2 = tri->silIndexes[ i*3 + 1 ];
		i3 = tri->silIndexes[ i*3 + 2 ];

		// if all the verts are off one side of the frustum,
		// don't add any of them
		if ( TRIANGLE_CULLED( i1, i2, i3 ) ) {
			continue;
		}

		// make sure the verts that are not on the negative sides
		// of the frustum are copied over.
		// we need to get the original verts even from clipped triangles
		// so the edges reference correctly, because an edge may be unclipped
		// even when a triangle is clipped.
		if ( numShadowVerts + 6 > MAX_SHADOW_VERTS ) {
			overflowed = true;
			return;
		}

		if ( !POINT_CULLED(i1) && remap[i1] == -1 ) {
			remap[i1] = numShadowVerts;
			shadowVerts[ numShadowVerts ].ToVec3() = tri->verts[i1].xyz;
			numShadowVerts+=2;
		}
		if ( !POINT_CULLED(i2) && remap[i2] == -1 ) {
			remap[i2] = numShadowVerts;
			shadowVerts[ numShadowVerts ].ToVec3() = tri->verts[i2].xyz;
			numShadowVerts+=2;
		}
		if ( !POINT_CULLED(i3) && remap[i3] == -1 ) {
			remap[i3] = numShadowVerts;
			shadowVerts[ numShadowVerts ].ToVec3() = tri->verts[i3].xyz;
			numShadowVerts+=2;
		}

		// clip the triangle if any points are on the negative sides
		if ( TRIANGLE_CLIPPED( i1, i2, i3 ) ) {
			cullBits = ( ( pointCull[ i1 ] ^ 0xfc0 ) | ( pointCull[ i2 ] ^ 0xfc0 ) | ( pointCull[ i3 ] ^ 0xfc0 ) ) >> 6;
			// this will also define clip edges that will become
			// silhouette planes
			if ( R_ClipTriangleToLight( tri->verts[i1].xyz, tri->verts[i2].xyz, 
				tri->verts[i3].xyz, cullBits, frustum ) ) {
				faceCastsShadow[i] = 1;
			}
		} else {
			// instead of overflowing or drawing a streamer shadow, don't draw a shadow at all
			if ( numShadowIndexes + 3 > MAX_SHADOW_INDEXES ) {
				overflowed = true;
				return;
			}
			if ( remap[i1] == -1 || remap[i2] == -1 || remap[i3] == -1 ) {
				common->Error( "R_CreateShadowVolumeInFrustum: bad remap[]" );
			}
			shadowIndexes[numShadowIndexes++] = remap[i3];
			shadowIndexes[numShadowIndexes++] = remap[i2];
			shadowIndexes[numShadowIndexes++] = remap[i1];
			faceCastsShadow[i] = 1;
		}
	}

	// add indexes for the back caps, which will just be reversals of the
	// front caps using the back vertexes
	numCapIndexes = numShadowIndexes - firstShadowIndex;

	// if no faces have been defined for the shadow volume,
	// there won't be anything at all
	if ( numCapIndexes == 0 ) {
		return;
	}

	//--------------- off-line processing ------------------

	// if we are running from dmap, perform the (very) expensive shadow optimizations
	// to remove internal sil edges and optimize the caps
	if ( callOptimizer ) {
		optimizedShadow_t opt;
		
		// project all of the vertexes to the shadow plane, generating
		// an equal number of back vertexes
//		R_ProjectPointsToFarPlane( ent, light, farPlane, firstShadowVert, numShadowVerts );

		opt = SuperOptimizeOccluders( shadowVerts, shadowIndexes + firstShadowIndex, numCapIndexes, farPlane, lightOrigin );

		// pull off the non-optimized data
		numShadowIndexes = firstShadowIndex;
		numShadowVerts = firstShadowVert;

		// add the optimized data
		if ( numShadowIndexes + opt.totalIndexes > MAX_SHADOW_INDEXES 
			|| numShadowVerts + opt.numVerts > MAX_SHADOW_VERTS ) {
			overflowed = true;
			common->Printf( "WARNING: overflowed MAX_SHADOW tables, shadow discarded\n" );
			Mem_Free( opt.verts );
			Mem_Free( opt.indexes );
			return;
		}

		for ( i = 0 ; i < opt.numVerts ; i++ ) {
			shadowVerts[numShadowVerts+i][0] = opt.verts[i][0];
			shadowVerts[numShadowVerts+i][1] = opt.verts[i][1];
			shadowVerts[numShadowVerts+i][2] = opt.verts[i][2];
			shadowVerts[numShadowVerts+i][3] = 1;
		}
		for ( i = 0 ; i < opt.totalIndexes ; i++ ) {
			int	index = opt.indexes[i];
			if ( index < 0 || index > opt.numVerts ) {
				common->Error( "optimized shadow index out of range" );
			}
			shadowIndexes[numShadowIndexes+i] = index + numShadowVerts;
		}

		numShadowVerts += opt.numVerts;
		numShadowIndexes += opt.totalIndexes;

		// note the index distribution so we can sort all the caps after all the sils
		indexRef[indexFrustumNumber].frontCapStart = firstShadowIndex;
		indexRef[indexFrustumNumber].rearCapStart = firstShadowIndex+opt.numFrontCapIndexes;
		indexRef[indexFrustumNumber].silStart = firstShadowIndex+opt.numFrontCapIndexes+opt.numRearCapIndexes;
		indexRef[indexFrustumNumber].end = numShadowIndexes;
		indexFrustumNumber++;

		Mem_Free( opt.verts );
		Mem_Free( opt.indexes );
		return;
	}

	//--------------- real-time processing ------------------

	// the dangling edge "face" is never considered to cast a shadow,
	// so any face with dangling edges that casts a shadow will have
	// it's dangling sil edge trigger a sil plane
	faceCastsShadow[numTris] = 0;

	// instead of overflowing or drawing a streamer shadow, don't draw a shadow at all
	// if we ran out of space
	if ( numShadowIndexes + numCapIndexes > MAX_SHADOW_INDEXES ) {
		overflowed = true;
		return;
	}
	for ( i = 0 ; i < numCapIndexes ; i += 3 ) {
		shadowIndexes[ numShadowIndexes + i + 0 ] = shadowIndexes[ firstShadowIndex + i + 2 ] + 1;
		shadowIndexes[ numShadowIndexes + i + 1 ] = shadowIndexes[ firstShadowIndex + i + 1 ] + 1;
		shadowIndexes[ numShadowIndexes + i + 2 ] = shadowIndexes[ firstShadowIndex + i + 0 ] + 1;
	}
	numShadowIndexes += numCapIndexes;

c_caps += numCapIndexes * 2;

int preSilIndexes = numShadowIndexes;

	// if any triangles were clipped, we will have a list of edges
	// on the frustum which must now become sil edges
	if ( makeClippedPlanes ) {
		R_AddClipSilEdges();
	}

	// any edges that are a transition between a shadowing and
	// non-shadowing triangle will cast a silhouette edge
	R_AddSilEdges( tri, pointCull, frustum );

c_sils += numShadowIndexes - preSilIndexes;

	// project all of the vertexes to the shadow plane, generating
	// an equal number of back vertexes
	R_ProjectPointsToFarPlane( ent, light, farPlane, firstShadowVert, numShadowVerts );

	// note the index distribution so we can sort all the caps after all the sils
	indexRef[indexFrustumNumber].frontCapStart = firstShadowIndex;
	indexRef[indexFrustumNumber].rearCapStart = firstShadowIndex+numCapIndexes;
	indexRef[indexFrustumNumber].silStart = preSilIndexes;
	indexRef[indexFrustumNumber].end = numShadowIndexes;
	indexFrustumNumber++;
}

/*
===================
R_MakeShadowFrustums

Called at definition derivation time
===================
*/
void R_MakeShadowFrustums( idRenderLightLocal *light ) {
	int		i, j;

	if ( light->parms.pointLight ) {
#if 0
		idVec3	adjustedRadius;

		// increase the light radius to cover any origin offsets.
		// this will cause some shadows to extend out of the exact light
		// volume, but is simpler than adjusting all the frustums
		adjustedRadius[0] = light->parms.lightRadius[0] + idMath::Fabs( light->parms.lightCenter[0] );
		adjustedRadius[1] = light->parms.lightRadius[1] + idMath::Fabs( light->parms.lightCenter[1] );
		adjustedRadius[2] = light->parms.lightRadius[2] + idMath::Fabs( light->parms.lightCenter[2] );

		light->numShadowFrustums = 0;
		// a point light has to project against six planes
		for ( i = 0 ; i < 6 ; i++ ) {
			shadowFrustum_t	*frust = &light->shadowFrustums[ light->numShadowFrustums ];

			frust->numPlanes = 6;
			frust->makeClippedPlanes = false;
			for ( j = 0 ; j < 6 ; j++ ) {
				idPlane &plane = frust->planes[j];
				plane[0] = pointLightFrustums[i][j][0] / adjustedRadius[0];
				plane[1] = pointLightFrustums[i][j][1] / adjustedRadius[1];
				plane[2] = pointLightFrustums[i][j][2] / adjustedRadius[2];
				plane.Normalize();
				plane[3] = -( plane.Normal() * light->globalLightOrigin );
				if ( j == 5 ) {
					plane[3] += adjustedRadius[i>>1];
				}
			}

			light->numShadowFrustums++;
		}
#else
		// exact projection,taking into account asymetric frustums when 
		// globalLightOrigin isn't centered

		static int	faceCorners[6][4] = {
			{ 7, 5, 1, 3 },		// positive X side
			{ 4, 6, 2, 0 },		// negative X side
			{ 6, 7, 3, 2 },		// positive Y side
			{ 5, 4, 0, 1 },		// negative Y side
			{ 6, 4, 5, 7 },		// positive Z side
			{ 3, 1, 0, 2 }		// negative Z side
		};
		static int	faceEdgeAdjacent[6][4] = {
			{ 4, 4, 2, 2 },		// positive X side
			{ 7, 7, 1, 1 },		// negative X side
			{ 5, 5, 0, 0 },		// positive Y side
			{ 6, 6, 3, 3 },		// negative Y side
			{ 0, 0, 3, 3 },		// positive Z side
			{ 5, 5, 6, 6 }		// negative Z side
		};

		bool	centerOutside = false;

		// if the light center of projection is outside the light bounds,
		// we will need to build the planes a little differently
		if ( fabs( light->parms.lightCenter[0] ) > light->parms.lightRadius[0]
			|| fabs( light->parms.lightCenter[1] ) > light->parms.lightRadius[1]
			|| fabs( light->parms.lightCenter[2] ) > light->parms.lightRadius[2] ) {
			centerOutside = true;
		}

		// make the corners
		idVec3	corners[8];

		for ( i = 0 ; i < 8 ; i++ ) {
			idVec3	temp;
			for ( j = 0 ; j < 3 ; j++ ) {
				if ( i & ( 1 << j ) ) {
					temp[j] = light->parms.lightRadius[j];
				} else {
					temp[j] = -light->parms.lightRadius[j];
				}
			}

			// transform to global space
			corners[i] = light->parms.origin + light->parms.axis * temp;
		}

		light->numShadowFrustums = 0;
		for ( int side = 0 ; side < 6 ; side++ ) {
			shadowFrustum_t	*frust = &light->shadowFrustums[ light->numShadowFrustums ];
			idVec3 &p1 = corners[faceCorners[side][0]];
			idVec3 &p2 = corners[faceCorners[side][1]];
			idVec3 &p3 = corners[faceCorners[side][2]];
			idPlane backPlane;

			// plane will have positive side inward
			backPlane.FromPoints( p1, p2, p3 );

			// if center of projection is on the wrong side, skip
			float d = backPlane.Distance( light->globalLightOrigin );
			if ( d < 0 ) {
				continue;
			}

			frust->numPlanes = 6;
			frust->planes[5] = backPlane;
			frust->planes[4] = backPlane;	// we don't really need the extra plane

			// make planes with positive side facing inwards in light local coordinates
			for ( int edge = 0 ; edge < 4 ; edge++ ) {
				idVec3 &p1 = corners[faceCorners[side][edge]];
				idVec3 &p2 = corners[faceCorners[side][(edge+1)&3]];

				// create a plane that goes through the center of projection
				frust->planes[edge].FromPoints( p2, p1, light->globalLightOrigin );

				// see if we should use an adjacent plane instead
				if ( centerOutside ) {
					idVec3 &p3 = corners[faceEdgeAdjacent[side][edge]];
					idPlane sidePlane;

					sidePlane.FromPoints( p2, p1, p3 );
					d = sidePlane.Distance( light->globalLightOrigin );
					if ( d < 0 ) {
						// use this plane instead of the edged plane
						frust->planes[edge] = sidePlane;
					}
					// we can't guarantee a neighbor, so add sill planes at edge
					light->shadowFrustums[ light->numShadowFrustums ].makeClippedPlanes = true;
				}
			}
			light->numShadowFrustums++;
		}

#endif
		return;
	}
	
	// projected light

	light->numShadowFrustums = 1;
	shadowFrustum_t	*frust = &light->shadowFrustums[ 0 ];

	// flip and transform the frustum planes so the positive side faces
	// inward in local coordinates

	// it is important to clip against even the near clip plane, because
	// many projected lights that are faking area lights will have their
	// origin behind solid surfaces.
	for ( i = 0 ; i < 6 ; i++ ) {
		idPlane &plane = frust->planes[i];

		plane.SetNormal( -light->frustum[i].Normal() );
		plane.SetDist( -light->frustum[i].Dist() );
	}
	
	frust->numPlanes = 6;

	frust->makeClippedPlanes = true;
	// projected lights don't have shared frustums, so any clipped edges
	// right on the planes must have a sil plane created for them
}

/*
=================
R_CreateShadowVolume

The returned surface will have a valid bounds and radius for culling.

Triangles are clipped to the light frustum before projecting.

A single triangle can clip to as many as 7 vertexes, so
the worst case expansion is 2*(numindexes/3)*7 verts when counting both
the front and back caps, although it will usually only be a modest
increase in vertexes for closed modesl

The worst case index count is much larger, when the 7 vertex clipped triangle
needs 15 indexes for the front, 15 for the back, and 42 (a quad on seven sides)
for the sides, for a total of 72 indexes from the original 3.  Ouch.

NULL may be returned if the surface doesn't create a shadow volume at all,
as with a single face that the light is behind.

If an edge is within an epsilon of the border of the volume, it must be treated
as if it is clipped for triangles, generating a new sil edge, and act
as if it was culled for edges, because the sil edge will have been
generated by the triangle irregardless of if it actually was a sil edge.
=================
*/
srfTriangles_t *R_CreateShadowVolume( const idRenderEntityLocal *ent,
									 const srfTriangles_t *tri, const idRenderLightLocal *light,
									 shadowGen_t optimize, srfCullInfo_t &cullInfo ) {
	int		i, j;
	idVec3	lightOrigin;
	srfTriangles_t	*newTri;
	int		capPlaneBits;

	if ( !r_shadows.GetBool() ) {
		return NULL;
	}

	if ( tri->numSilEdges == 0 || tri->numIndexes == 0 || tri->numVerts == 0 ) {
		return NULL;
	}

	if ( tri->numIndexes < 0 ) {
		common->Error( "R_CreateShadowVolume: tri->numIndexes = %i", tri->numIndexes );
	}

	if ( tri->numVerts < 0 ) {
		common->Error( "R_CreateShadowVolume: tri->numVerts = %i", tri->numVerts );
	}

	tr.pc.c_createShadowVolumes++;

	// use the fast infinite projection in dynamic situations, which
	// trades somewhat more overdraw and no cap optimizations for
	// a very simple generation process
	if ( optimize == SG_DYNAMIC && r_useTurboShadow.GetBool() ) {
		if ( tr.backEndRendererHasVertexPrograms && r_useShadowVertexProgram.GetBool() ) {
			return R_CreateVertexProgramTurboShadowVolume( ent, tri, light, cullInfo );
		} else {
			return R_CreateTurboShadowVolume( ent, tri, light, cullInfo );
		}
	}

	R_CalcInteractionFacing( ent, tri, light, cullInfo );

	int numFaces = tri->numIndexes / 3;
	int allFront = 1;
	for ( i = 0; i < numFaces && allFront; i++ ) {
		allFront &= cullInfo.facing[i];
	}
	if ( allFront ) {
		// if no faces are the right direction, don't make a shadow at all
		return NULL;
	}

	// clear the shadow volume
	numShadowIndexes = 0;
	numShadowVerts = 0;
	overflowed = false;
	indexFrustumNumber = 0;
	capPlaneBits = 0;
	callOptimizer = (optimize == SG_OFFLINE);

	// the facing information will be the same for all six projections
	// from a point light, as well as for any directed lights
	globalFacing = cullInfo.facing;
	faceCastsShadow = (byte *)_alloca16( tri->numIndexes / 3 + 1 );	// + 1 for fake dangling edge face
	remap = (int *)_alloca16( tri->numVerts * sizeof( remap[0] ) );

	R_GlobalPointToLocal( ent->modelMatrix, light->globalLightOrigin, lightOrigin );

	// run through all the shadow frustums, which is one for a projected light,
	// and usually six for a point light, but point lights with centers outside
	// the box may have less
	for ( int frustumNum = 0 ; frustumNum < light->numShadowFrustums ; frustumNum++ ) {
		const shadowFrustum_t	*frust = &light->shadowFrustums[frustumNum];
		ALIGN16( idPlane frustum[6] );

		// transform the planes into entity space
		// we could share and reverse some of the planes between frustums for a minor
		// speed increase

		// the cull test is redundant for a single shadow frustum projected light, because
		// the surface has already been checked against the main light frustums

		for ( j = 0 ; j < frust->numPlanes ; j++ ) {
			R_GlobalPlaneToLocal( ent->modelMatrix, frust->planes[j], frustum[j] );

			// try to cull the entire surface against this frustum
			float d = tri->bounds.PlaneDistance( frustum[j] );
			if ( d < -LIGHT_CLIP_EPSILON ) {
				break;
			}
		}
		if ( j != frust->numPlanes ) {
			continue;
		}
		// we need to check all the triangles
		int		oldFrustumNumber = indexFrustumNumber;

		R_CreateShadowVolumeInFrustum( ent, tri, light, lightOrigin, frustum, frustum[5], frust->makeClippedPlanes );

		// if we couldn't make a complete shadow volume, it is better to
		// not draw one at all, avoiding streamer problems
		if ( overflowed ) {
			return NULL;
		}

		if ( indexFrustumNumber != oldFrustumNumber ) {
			// note that we have caps projected against this frustum,
			// which may allow us to skip drawing the caps if all projected
			// planes face away from the viewer and the viewer is outside the light volume
			capPlaneBits |= 1<<frustumNum;
		}
	}

	// if no faces have been defined for the shadow volume,
	// there won't be anything at all
	if ( numShadowIndexes == 0 ) {
		return NULL;
	}

	// this should have been prevented by the overflowed flag, so if it ever happens,
	// it is a code error
	if ( numShadowVerts > MAX_SHADOW_VERTS || numShadowIndexes > MAX_SHADOW_INDEXES ) {
		common->FatalError( "Shadow volume exceeded allocation" );
	}

	// allocate a new surface for the shadow volume
	newTri = R_AllocStaticTriSurf();

	// we might consider setting this, but it would only help for
	// large lights that are partially off screen
	newTri->bounds.Clear();

	// copy off the verts and indexes
	newTri->numVerts = numShadowVerts;
	newTri->numIndexes = numShadowIndexes;

	// the shadow verts will go into a main memory buffer as well as a vertex
	// cache buffer, so they can be copied back if they are purged
	R_AllocStaticTriSurfShadowVerts( newTri, newTri->numVerts );
	SIMDProcessor->Memcpy( newTri->shadowVertexes, shadowVerts, newTri->numVerts * sizeof( newTri->shadowVertexes[0] ) );

	R_AllocStaticTriSurfIndexes( newTri, newTri->numIndexes );

	if ( 1 /* sortCapIndexes */ ) {
		newTri->shadowCapPlaneBits = capPlaneBits;

		// copy the sil indexes first
		newTri->numShadowIndexesNoCaps = 0;
		for ( i = 0 ; i < indexFrustumNumber ; i++ ) {
			int	c = indexRef[i].end - indexRef[i].silStart;
			SIMDProcessor->Memcpy( newTri->indexes+newTri->numShadowIndexesNoCaps, 
									shadowIndexes+indexRef[i].silStart, c * sizeof( newTri->indexes[0] ) );
			newTri->numShadowIndexesNoCaps += c;
		}
		// copy rear cap indexes next
		newTri->numShadowIndexesNoFrontCaps = newTri->numShadowIndexesNoCaps;
		for ( i = 0 ; i < indexFrustumNumber ; i++ ) {
			int	c = indexRef[i].silStart - indexRef[i].rearCapStart;
			SIMDProcessor->Memcpy( newTri->indexes+newTri->numShadowIndexesNoFrontCaps, 
									shadowIndexes+indexRef[i].rearCapStart, c * sizeof( newTri->indexes[0] ) );
			newTri->numShadowIndexesNoFrontCaps += c;
		}
		// copy front cap indexes last
		newTri->numIndexes = newTri->numShadowIndexesNoFrontCaps;
		for ( i = 0 ; i < indexFrustumNumber ; i++ ) {
			int	c = indexRef[i].rearCapStart - indexRef[i].frontCapStart;
			SIMDProcessor->Memcpy( newTri->indexes+newTri->numIndexes, 
									shadowIndexes+indexRef[i].frontCapStart, c * sizeof( newTri->indexes[0] ) );
			newTri->numIndexes += c;
		}

	} else {
		newTri->shadowCapPlaneBits = 63;	// we don't have optimized index lists
		SIMDProcessor->Memcpy( newTri->indexes, shadowIndexes, newTri->numIndexes * sizeof( newTri->indexes[0] ) );
	}

	if ( optimize == SG_OFFLINE ) {
		CleanupOptimizedShadowTris( newTri );
	}

	return newTri;
}
