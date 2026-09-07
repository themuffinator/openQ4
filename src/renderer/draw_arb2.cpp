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




#include <cstring>

#include "tr_local.h"
#include "CelShading.h"
#include "Model_local.h"
#include "ShadowMapClassification.h"
#include "ShadowMapProjected.h"
#include "ShadowMapArb2Parity.h"
#include "ModernShadowPlanner.h"
#include "ModernClusteredLighting.h"
#include "ClassicInteractionDomain.h"
#include "RendererMetrics.h"

#include "cg_explicit.h"
#include <ctype.h>

#ifndef GL_TEXTURE_CUBE_MAP_SEAMLESS
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#endif

static ID_INLINE GLint RB_ShadowMapSafeStencilClearValue() {
	const int stencilBits = idMath::ClampInt( 1, 30, ( glConfig.stencilBits > 0 ) ? glConfig.stencilBits : 8 );
	return 1 << ( stencilBits - 1 );
}

CGcontext cg_context;

static const int ARB2_MD5R_VARIANTS_PER_FAMILY = 3;

typedef enum {
	ARB2_MD5R_MVP_ROW_0 = 75,
	ARB2_MD5R_MVP_ROW_1,
	ARB2_MD5R_MVP_ROW_2,
	ARB2_MD5R_MVP_ROW_3,
	ARB2_MD5R_LOCAL_LIGHT_ORIGIN,
	ARB2_MD5R_LOCAL_VIEW_ORIGIN,
	ARB2_MD5R_PROJECTION_ROW_0,
	ARB2_MD5R_PROJECTION_ROW_1,
	ARB2_MD5R_PROJECTION_ROW_2,
	ARB2_MD5R_PROJECTION_ROW_3,
	ARB2_MD5R_TEXTURE_MATRIX_ROW_0,
	ARB2_MD5R_TEXTURE_MATRIX_ROW_1,
	ARB2_MD5R_MODEL_ROW_0,
	ARB2_MD5R_MODEL_ROW_1,
	ARB2_MD5R_MODEL_ROW_2,
	ARB2_MD5R_STAGE_RESERVED,
	// Retail md5r*.vp programs read programmable stage vertex-color controls
	// from c[91]/c[92] and leave c[90] as the preserved reserved slot.
	ARB2_MD5R_STAGE_VERTEX_COLOR_MODULATE,
	ARB2_MD5R_STAGE_VERTEX_COLOR_ADD,
	ARB2_MD5R_FOG_DISTANCE_PLANE,
	ARB2_MD5R_FOG_DISTANCE_BIAS,
	ARB2_MD5R_FOG_ENTER_PLANE_T,
	ARB2_MD5R_FOG_ENTER_PLANE_S
} arb2ProgramParameter_t;

static_assert(
	ARB2_MD5R_STAGE_RESERVED == 90 &&
	ARB2_MD5R_STAGE_VERTEX_COLOR_MODULATE == 91 &&
	ARB2_MD5R_STAGE_VERTEX_COLOR_ADD == 92 &&
	ARB2_MD5R_FOG_DISTANCE_PLANE == 93,
	"Packed MD5R stage/fog registers must stay aligned with retail Quake 4 ARB programs" );

static const int ARB2_MD5R_MAX_PALETTE_TRANSFORMS = ARB2_MD5R_MVP_ROW_0 / 3;

typedef enum {
	ICM_PACKED,
	ICM_VECTOR
} interactionColorMode_t;

static interactionColorMode_t g_interactionVertexProgramAutoColorMode = ICM_PACKED;
static interactionColorMode_t g_interactionVertexProgramColorMode = ICM_PACKED;
static int g_interactionVertexProgramOverride = 0;
static bool g_interactionShaderRescueWarned = false;
static const drawSurf_t *g_packedInteractionSurf = NULL;
static int g_packedInteractionVertexFormatIndex = -1;
static const drawSurf_t *g_packedDirectDrawSurf = NULL;
static int g_packedDirectDrawVertexFormatIndex = -1;
static const drawSurf_t *g_packedStageSurf = NULL;
static int g_packedStageVertexFormatIndex = -1;
static bool g_firstARB2InteractionHandoffBreadcrumb = false;
static bool g_arb2InteractionDriverBypassWarned = false;
static bool g_arb2InteractionBypassStateBreadcrumb = false;
static bool g_appleGL21AutomaticInteractionPath = false;

static GLuint RB_CurrentInteractionProgramIdent( GLenum target );
static const char *RB_CurrentInteractionProgramFamilyName( void );
static bool RB_UseSimpleInteractionShader( void );
static void RB_WarnInteractionShaderRescueMode( void );

static const float RB_ARB2_MD5RIdentityTextureMatrixRows[2][4] = {
	{ 1.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 1.0f, 0.0f, 0.0f }
};
static const float RB_ARB2_MD5RVertexColorIgnore[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
static const float RB_ARB2_MD5RVertexColorModulate[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
static const float RB_ARB2_MD5RVertexColorInverseModulate[4] = { -1.0f, 1.0f, 0.0f, 0.0f };
static const int ARB2_MD5R_INTERACTION_PARAM_BASE = ARB2_MD5R_MVP_ROW_0;

// Value cache for ARB program env parameters set by the interaction loop.
// Light projection planes, light/view origins, and stage matrices are
// per-(light, entity) or per-stage constants that the classic loop re-sends
// for every draw; comparing 16 bytes is far cheaper than the driver call.
// Only the classic interaction registers (< 32) are cached; the packed MD5R
// registers live at 75+ and always pass through. The cache is only valid
// within one RB_ARB2_CreateDrawInteractions batch and is invalidated at
// entry, because other passes (shadows, fog, MD5R loads) write the same
// registers without going through these helpers.
static const int ARB2_ENV_PARAM_CACHE_SIZE = 32;
typedef struct {
	float			vertexParms[ARB2_ENV_PARAM_CACHE_SIZE][4];
	float			fragmentParms[8][4];
	unsigned int	vertexValid;	// bitmask
	unsigned int	fragmentValid;	// bitmask
} arb2EnvParamCache_t;
static arb2EnvParamCache_t g_arb2EnvParamCache;

ID_INLINE static void RB_ARB2_InvalidateEnvParamCache( void ) {
	g_arb2EnvParamCache.vertexValid = 0;
	g_arb2EnvParamCache.fragmentValid = 0;
}

ID_INLINE static void RB_ARB2_SetVertexEnvParm( int index, const float *v ) {
	if ( index >= 0 && index < ARB2_ENV_PARAM_CACHE_SIZE && r_useRedundantStateFiltering.GetBool() ) {
		const unsigned int bit = 1u << index;
		float *cached = g_arb2EnvParamCache.vertexParms[index];
		if ( ( g_arb2EnvParamCache.vertexValid & bit )
			&& cached[0] == v[0] && cached[1] == v[1] && cached[2] == v[2] && cached[3] == v[3] ) {
			return;
		}
		cached[0] = v[0];
		cached[1] = v[1];
		cached[2] = v[2];
		cached[3] = v[3];
		g_arb2EnvParamCache.vertexValid |= bit;
	}
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, index, v );
}

ID_INLINE static void RB_ARB2_SetFragmentEnvParm( int index, const float *v ) {
	if ( index >= 0 && index < 8 && r_useRedundantStateFiltering.GetBool() ) {
		const unsigned int bit = 1u << index;
		float *cached = g_arb2EnvParamCache.fragmentParms[index];
		if ( ( g_arb2EnvParamCache.fragmentValid & bit )
			&& cached[0] == v[0] && cached[1] == v[1] && cached[2] == v[2] && cached[3] == v[3] ) {
			return;
		}
		cached[0] = v[0];
		cached[1] = v[1];
		cached[2] = v[2];
		cached[3] = v[3];
		g_arb2EnvParamCache.fragmentValid |= bit;
	}
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, index, v );
}

static program_t RB_ARB2_GetMD5RVertexProgram( program_t familyBase, int vertexFormatIndex ) {
	if ( vertexFormatIndex < 0 || vertexFormatIndex >= ARB2_MD5R_VARIANTS_PER_FAMILY ) {
		return PROG_INVALID;
	}

	return static_cast<program_t>( familyBase + vertexFormatIndex );
}

static void RB_ARB2_LoadVertexProgramMatrixRows( int firstRegister, const float *matrix, int numRows ) {
	float parm[4];

	for ( int row = 0; row < numRows; ++row ) {
		parm[0] = matrix[row];
		parm[1] = matrix[row + 4];
		parm[2] = matrix[row + 8];
		parm[3] = matrix[row + 12];
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, firstRegister + row, parm );
	}
}

static void RB_ARB2_LoadMD5RTextureMatrix( const float *shaderRegisters, const textureStage_t *texture ) {
	float matrix[16];
	RB_GetShaderTextureMatrix( shaderRegisters, texture, matrix );
	RB_ARB2_LoadVertexProgramMatrixRows( ARB2_MD5R_TEXTURE_MATRIX_ROW_0, matrix, 2 );
}

static int RB_ARB2_GetMD5RVertexFormatIndex( const rvMD5RVertexBufferDesc &vertexBuffer ) {
	const bool hasBlendIndices =
		vertexBuffer.loadVertexFormat.hasBlendIndex
		&& vertexBuffer.blendIndices.Num() == vertexBuffer.numVertices;
	const bool hasBlendWeights =
		vertexBuffer.loadVertexFormat.hasBlendWeight
		&& vertexBuffer.blendWeights.Num() == vertexBuffer.numVertices;

	if ( hasBlendWeights && !hasBlendIndices ) {
		return -1;
	}

	if ( hasBlendWeights ) {
		return 2;
	}

	if ( hasBlendIndices ) {
		return 1;
	}

	return 0;
}

static void RB_ARB2_LoadMD5RPaletteTransformRows( int firstRegister, const float *transform ) {
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, firstRegister + 0, transform + 0 );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, firstRegister + 1, transform + 4 );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, firstRegister + 2, transform + 8 );
}

static int RB_ARB2_GetMD5RPositionSize( const rvMD5RVertexBufferDesc &vertexBuffer ) {
	int positionSize = 3;

	if ( vertexBuffer.loadVertexFormat.hasPosition ) {
		positionSize = vertexBuffer.loadVertexFormat.positionSwizzled ? 3 : vertexBuffer.loadVertexFormat.positionDim;
		if ( positionSize < 2 ) {
			positionSize = 2;
		} else if ( positionSize > 4 ) {
			positionSize = 4;
		}
	}

	return positionSize;
}

static program_t RB_ARB2_GetPackedMD5RInteractionVertexProgram( const drawSurf_t *surf, int *vertexFormatIndex ) {
	if ( vertexFormatIndex != NULL ) {
		*vertexFormatIndex = -1;
	}

	const rvMD5RVertexBufferDesc *drawVertexBuffer =
		( surf != NULL && surf->geo != NULL ) ? R_MD5R_GetDrawVertexBufferForTri( surf->geo ) : NULL;
	if ( drawVertexBuffer == NULL ) {
		return PROG_INVALID;
	}

	const int formatIndex = RB_ARB2_GetMD5RVertexFormatIndex( *drawVertexBuffer );
	if ( vertexFormatIndex != NULL ) {
		*vertexFormatIndex = formatIndex;
	}

	return RB_ARB2_GetMD5RVertexProgram( ARB2_MD5R_INTERACTION_VPROG_BASE, formatIndex );
}

static bool RB_ARB2_BindPackedMD5RInteractionVertexData( const rvMD5RVertexBufferDesc &vertexBuffer, int vertexFormatIndex ) {
	if ( vertexBuffer.numVertices <= 0
		|| vertexBuffer.positions.Num() != vertexBuffer.numVertices
		|| vertexBuffer.normals.Num() != vertexBuffer.numVertices
		|| vertexBuffer.tangents.Num() != vertexBuffer.numVertices
		|| vertexBuffer.texCoords[0].Num() != vertexBuffer.numVertices
		|| vertexBuffer.diffuseColors.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( vertexFormatIndex <= 1 && vertexBuffer.binormals.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( vertexFormatIndex >= 1 && vertexBuffer.blendIndices.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( vertexFormatIndex >= 2 && vertexBuffer.blendWeights.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	idVertexCache::BindArrayBuffer( 0 );
	glVertexPointer(
		RB_ARB2_GetMD5RPositionSize( vertexBuffer ),
		GL_FLOAT,
		sizeof( idVec4 ),
		vertexBuffer.positions.Ptr()->ToFloatPtr() );
	glColorPointer(
		4,
		GL_UNSIGNED_BYTE,
		sizeof( dword ),
		reinterpret_cast<const void *>( vertexBuffer.diffuseColors.Ptr() ) );
	glVertexAttribPointerARB( 2, 3, GL_FLOAT, GL_FALSE, sizeof( idVec3 ), vertexBuffer.normals.Ptr()->ToFloatPtr() );
	glVertexAttribPointerARB( 6, 3, GL_FLOAT, GL_FALSE, sizeof( idVec3 ), vertexBuffer.tangents.Ptr()->ToFloatPtr() );
	glVertexAttribPointerARB( 8, 2, GL_FLOAT, GL_FALSE, sizeof( idVec4 ), vertexBuffer.texCoords[0].Ptr()->ToFloatPtr() );
	glEnableVertexAttribArrayARB( 2 );
	glEnableVertexAttribArrayARB( 6 );
	glEnableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );

	if ( vertexFormatIndex <= 1 ) {
		glVertexAttribPointerARB( 7, 3, GL_FLOAT, GL_FALSE, sizeof( idVec3 ), vertexBuffer.binormals.Ptr()->ToFloatPtr() );
		glEnableVertexAttribArrayARB( 7 );
	} else {
		glDisableVertexAttribArrayARB( 7 );
	}

	if ( vertexFormatIndex >= 1 ) {
		glVertexAttribPointerARB(
			1,
			4,
			GL_UNSIGNED_BYTE,
			GL_TRUE,
			sizeof( dword ),
			reinterpret_cast<const void *>( vertexBuffer.blendIndices.Ptr() ) );
		glEnableVertexAttribArrayARB( 1 );
	} else {
		glDisableVertexAttribArrayARB( 1 );
	}

	if ( vertexFormatIndex >= 2 ) {
		glVertexAttribPointerARB(
			5,
			4,
			GL_FLOAT,
			GL_FALSE,
			sizeof( idVec4 ),
			vertexBuffer.blendWeights.Ptr()->ToFloatPtr() );
		glEnableVertexAttribArrayARB( 5 );
	} else {
		glDisableVertexAttribArrayARB( 5 );
	}

	return true;
}

static void RB_ARB2_UnbindPackedMD5RInteractionVertexData( void ) {
	glDisableVertexAttribArrayARB( 1 );
	glDisableVertexAttribArrayARB( 2 );
	glDisableVertexAttribArrayARB( 5 );
	glDisableVertexAttribArrayARB( 6 );
	glDisableVertexAttribArrayARB( 7 );
}

static bool RB_ARB2_BindPackedMD5RDrawVertexData( const rvMD5RVertexBufferDesc &vertexBuffer, int vertexFormatIndex ) {
	if ( vertexBuffer.numVertices <= 0 || vertexBuffer.positions.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	idVertexCache::BindArrayBuffer( 0 );
	glVertexPointer(
		RB_ARB2_GetMD5RPositionSize( vertexBuffer ),
		GL_FLOAT,
		sizeof( idVec4 ),
		vertexBuffer.positions.Ptr()->ToFloatPtr() );

	if ( vertexFormatIndex >= 1 ) {
		glEnableVertexAttribArrayARB( 1 );
		glVertexAttribPointerARB(
			1,
			4,
			GL_UNSIGNED_BYTE,
			GL_TRUE,
			sizeof( dword ),
			reinterpret_cast<const void *>( vertexBuffer.blendIndices.Ptr() ) );
	}

	if ( vertexFormatIndex >= 2 ) {
		glEnableVertexAttribArrayARB( 5 );
		glVertexAttribPointerARB(
			5,
			4,
			GL_FLOAT,
			GL_FALSE,
			sizeof( idVec4 ),
			vertexBuffer.blendWeights.Ptr()->ToFloatPtr() );
	}

	return true;
}

static void RB_ARB2_UnbindPackedMD5RDrawVertexData( int vertexFormatIndex ) {
	if ( vertexFormatIndex >= 2 ) {
		glDisableVertexAttribArrayARB( 5 );
	}
	if ( vertexFormatIndex >= 1 ) {
		glDisableVertexAttribArrayARB( 1 );
	}
}

static bool RB_ARB2_BindPackedMD5RStageVertexData(
	const rvMD5RVertexBufferDesc &vertexBuffer,
	int vertexFormatIndex,
	bool needsTexCoord0,
	bool needsNormals,
	bool needsTangents ) {
	if ( vertexBuffer.numVertices <= 0 || vertexBuffer.positions.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( vertexFormatIndex < 0 ) {
		return false;
	}

	if ( vertexFormatIndex >= 1 && vertexBuffer.blendIndices.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( vertexFormatIndex >= 2 && vertexBuffer.blendWeights.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( needsTexCoord0 && vertexBuffer.texCoords[0].Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( needsNormals && vertexBuffer.normals.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( needsTangents ) {
		if ( vertexBuffer.tangents.Num() != vertexBuffer.numVertices ) {
			return false;
		}
		if ( vertexFormatIndex <= 1 && vertexBuffer.binormals.Num() != vertexBuffer.numVertices ) {
			return false;
		}
	}

	idVertexCache::BindArrayBuffer( 0 );
	glVertexPointer(
		RB_ARB2_GetMD5RPositionSize( vertexBuffer ),
		GL_FLOAT,
		sizeof( idVec4 ),
		vertexBuffer.positions.Ptr()->ToFloatPtr() );

	if ( needsTexCoord0 ) {
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, GL_FALSE, sizeof( idVec4 ), vertexBuffer.texCoords[0].Ptr()->ToFloatPtr() );
		glEnableVertexAttribArrayARB( 8 );
	} else {
		glDisableVertexAttribArrayARB( 8 );
	}

	if ( needsNormals ) {
		glVertexAttribPointerARB( 2, 3, GL_FLOAT, GL_FALSE, sizeof( idVec3 ), vertexBuffer.normals.Ptr()->ToFloatPtr() );
		glEnableVertexAttribArrayARB( 2 );
	} else {
		glDisableVertexAttribArrayARB( 2 );
	}

	if ( needsTangents ) {
		glVertexAttribPointerARB( 6, 3, GL_FLOAT, GL_FALSE, sizeof( idVec3 ), vertexBuffer.tangents.Ptr()->ToFloatPtr() );
		glEnableVertexAttribArrayARB( 6 );
		if ( vertexFormatIndex <= 1 ) {
			glVertexAttribPointerARB( 7, 3, GL_FLOAT, GL_FALSE, sizeof( idVec3 ), vertexBuffer.binormals.Ptr()->ToFloatPtr() );
			glEnableVertexAttribArrayARB( 7 );
		} else {
			glDisableVertexAttribArrayARB( 7 );
		}
	} else {
		glDisableVertexAttribArrayARB( 6 );
		glDisableVertexAttribArrayARB( 7 );
	}

	if ( vertexFormatIndex >= 1 ) {
		glVertexAttribPointerARB(
			1,
			4,
			GL_UNSIGNED_BYTE,
			GL_TRUE,
			sizeof( dword ),
			reinterpret_cast<const void *>( vertexBuffer.blendIndices.Ptr() ) );
		glEnableVertexAttribArrayARB( 1 );
	} else {
		glDisableVertexAttribArrayARB( 1 );
	}

	if ( vertexFormatIndex >= 2 ) {
		glVertexAttribPointerARB(
			5,
			4,
			GL_FLOAT,
			GL_FALSE,
			sizeof( idVec4 ),
			vertexBuffer.blendWeights.Ptr()->ToFloatPtr() );
		glEnableVertexAttribArrayARB( 5 );
	} else {
		glDisableVertexAttribArrayARB( 5 );
	}

	return true;
}

static void RB_ARB2_UnbindPackedMD5RStageVertexData( void ) {
	glDisableVertexAttribArrayARB( 1 );
	glDisableVertexAttribArrayARB( 2 );
	glDisableVertexAttribArrayARB( 5 );
	glDisableVertexAttribArrayARB( 6 );
	glDisableVertexAttribArrayARB( 7 );
	glDisableVertexAttribArrayARB( 8 );
}

static bool RB_ARB2_BindPackedMD5RLegacyProgramVertexData( const rvMD5RVertexBufferDesc &vertexBuffer, int vertexFormatIndex ) {
	if ( vertexBuffer.numVertices <= 0
		|| vertexBuffer.positions.Num() != vertexBuffer.numVertices
		|| vertexBuffer.texCoords[0].Num() != vertexBuffer.numVertices
		|| vertexBuffer.normals.Num() != vertexBuffer.numVertices
		|| vertexBuffer.tangents.Num() != vertexBuffer.numVertices
		|| vertexBuffer.binormals.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( vertexFormatIndex < 0 ) {
		return false;
	}

	if ( vertexFormatIndex >= 1 && vertexBuffer.blendIndices.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( vertexFormatIndex >= 2 && vertexBuffer.blendWeights.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	idVertexCache::BindArrayBuffer( 0 );
	glVertexPointer(
		RB_ARB2_GetMD5RPositionSize( vertexBuffer ),
		GL_FLOAT,
		sizeof( idVec4 ),
		vertexBuffer.positions.Ptr()->ToFloatPtr() );
	glTexCoordPointer(
		2,
		GL_FLOAT,
		sizeof( idVec4 ),
		vertexBuffer.texCoords[0].Ptr()->ToFloatPtr() );
	glNormalPointer(
		GL_FLOAT,
		sizeof( idVec3 ),
		vertexBuffer.normals.Ptr()->ToFloatPtr() );
	glVertexAttribPointerARB( 9, 3, GL_FLOAT, GL_FALSE, sizeof( idVec3 ), vertexBuffer.tangents.Ptr()->ToFloatPtr() );
	glVertexAttribPointerARB( 10, 3, GL_FLOAT, GL_FALSE, sizeof( idVec3 ), vertexBuffer.binormals.Ptr()->ToFloatPtr() );
	glEnableClientState( GL_NORMAL_ARRAY );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );

	if ( vertexFormatIndex >= 1 ) {
		glVertexAttribPointerARB(
			1,
			4,
			GL_UNSIGNED_BYTE,
			GL_TRUE,
			sizeof( dword ),
			reinterpret_cast<const void *>( vertexBuffer.blendIndices.Ptr() ) );
		glEnableVertexAttribArrayARB( 1 );
	} else {
		glDisableVertexAttribArrayARB( 1 );
	}

	if ( vertexFormatIndex >= 2 ) {
		glVertexAttribPointerARB(
			5,
			4,
			GL_FLOAT,
			GL_FALSE,
			sizeof( idVec4 ),
			vertexBuffer.blendWeights.Ptr()->ToFloatPtr() );
		glEnableVertexAttribArrayARB( 5 );
	} else {
		glDisableVertexAttribArrayARB( 5 );
	}

	return true;
}

static bool RB_ARB2_CanDrawPackedMD5RStageBatches( const drawSurf_t *surf, int vertexFormatIndex ) {
	const srfTriangles_t *tri = ( surf != NULL ) ? surf->geo : NULL;
	const rvMD5RMesh *mesh = ( tri != NULL ) ? R_MD5R_GetMeshForTri( tri ) : NULL;
	const rvMD5RVertexBufferDesc *drawVertexBuffer = ( tri != NULL ) ? R_MD5R_GetDrawVertexBufferForTri( tri ) : NULL;
	const rvMD5RIndexBufferDesc *drawIndexBuffer = ( tri != NULL ) ? R_MD5R_GetDrawIndexBufferForTri( tri ) : NULL;
	if ( tri == NULL
		|| mesh == NULL
		|| drawVertexBuffer == NULL
		|| drawIndexBuffer == NULL
		|| tri->numIndexes != mesh->numDrawIndices
		|| mesh->primBatches.Num() <= 0
		|| drawIndexBuffer->numIndices <= 0
		|| drawIndexBuffer->indices.Num() != drawIndexBuffer->numIndices ) {
		return false;
	}

	int transformBase = 0;
	for ( int primBatchIndex = 0; primBatchIndex < mesh->primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
		const int primBatchIndexCount = primBatch.drawGeoSpec.primitiveCount * 3;
		const int primBatchTransformCount = Max( primBatch.numTransforms, 1 );
		if ( !primBatch.hasDrawGeoSpec
			|| primBatch.drawGeoSpec.vertexStart < 0
			|| primBatch.drawGeoSpec.vertexCount < 0
			|| primBatch.drawGeoSpec.vertexStart + primBatch.drawGeoSpec.vertexCount > drawVertexBuffer->numVertices
			|| primBatch.drawGeoSpec.indexStart < 0
			|| primBatch.drawGeoSpec.indexStart + primBatchIndexCount > drawIndexBuffer->numIndices ) {
			return false;
		}

		if ( vertexFormatIndex > 0 ) {
			if ( tri->skinToModelTransforms == NULL
				|| tri->numSkinToModelTransforms <= 0
				|| primBatchTransformCount > ARB2_MD5R_MAX_PALETTE_TRANSFORMS
				|| transformBase + primBatchTransformCount > tri->numSkinToModelTransforms ) {
				return false;
			}
		}

		transformBase += primBatchTransformCount;
	}

	return true;
}

bool RB_ARB2_PreparePackedMD5RProgramStageDraw( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = ( surf != NULL ) ? surf->geo : NULL;
	const rvMD5RVertexBufferDesc *drawVertexBuffer = ( tri != NULL ) ? R_MD5R_GetDrawVertexBufferForTri( tri ) : NULL;
	const int vertexFormatIndex = ( drawVertexBuffer != NULL ) ? RB_ARB2_GetMD5RVertexFormatIndex( *drawVertexBuffer ) : -1;

	g_packedStageSurf = NULL;
	g_packedStageVertexFormatIndex = -1;

	if ( tri == NULL
		|| drawVertexBuffer == NULL
		|| vertexFormatIndex < 0
		|| !RB_ARB2_CanDrawPackedMD5RStageBatches( surf, vertexFormatIndex )
		|| !RB_ARB2_BindPackedMD5RLegacyProgramVertexData( *drawVertexBuffer, vertexFormatIndex ) ) {
		return false;
	}

	g_packedStageSurf = surf;
	g_packedStageVertexFormatIndex = vertexFormatIndex;
	return true;
}

bool RB_ARB2_PreparePackedMD5RDirectDraw( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = ( surf != NULL ) ? surf->geo : NULL;
	const rvMD5RVertexBufferDesc *drawVertexBuffer = ( tri != NULL ) ? R_MD5R_GetDrawVertexBufferForTri( tri ) : NULL;
	const int vertexFormatIndex = ( drawVertexBuffer != NULL ) ? RB_ARB2_GetMD5RVertexFormatIndex( *drawVertexBuffer ) : -1;

	g_packedDirectDrawSurf = NULL;
	g_packedDirectDrawVertexFormatIndex = -1;

	if ( tri == NULL
		|| drawVertexBuffer == NULL
		|| vertexFormatIndex < 0
		|| !RB_ARB2_CanDrawPackedMD5RStageBatches( surf, vertexFormatIndex )
		|| !RB_ARB2_BindPackedMD5RDrawVertexData( *drawVertexBuffer, vertexFormatIndex ) ) {
		return false;
	}

	g_packedDirectDrawSurf = surf;
	g_packedDirectDrawVertexFormatIndex = vertexFormatIndex;
	return true;
}

void RB_ARB2_ClearPreparedPackedMD5RDirectDraw( void ) {
	if ( g_packedDirectDrawSurf != NULL ) {
		RB_ARB2_UnbindPackedMD5RDrawVertexData( g_packedDirectDrawVertexFormatIndex );
	}
	g_packedDirectDrawSurf = NULL;
	g_packedDirectDrawVertexFormatIndex = -1;
	idVertexCache::BindArrayBuffer( 0 );
	vertexCache.UnbindIndex();
}

void RB_ARB2_ClearPreparedPackedMD5RDraw( void ) {
	if ( g_packedStageSurf != NULL ) {
		RB_ARB2_UnbindPackedMD5RStageVertexData();
	}
	g_packedStageSurf = NULL;
	g_packedStageVertexFormatIndex = -1;
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableClientState( GL_NORMAL_ARRAY );
	idVertexCache::BindArrayBuffer( 0 );
	vertexCache.UnbindIndex();
}

bool RB_ARB2_DrawPreparedPackedMD5RStageBatches( const srfTriangles_t *tri ) {
	if ( g_packedStageSurf == NULL || g_packedStageSurf->geo != tri ) {
		return false;
	}

	const drawSurf_t *surf = g_packedStageSurf;
	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( tri );
	const rvMD5RIndexBufferDesc *drawIndexBuffer = R_MD5R_GetDrawIndexBufferForTri( tri );
	if ( mesh == NULL || drawIndexBuffer == NULL || !RB_ARB2_CanDrawPackedMD5RStageBatches( surf, g_packedStageVertexFormatIndex ) ) {
		return false;
	}

	vertexCache.UnbindIndex();

	int transformBase = 0;
	for ( int primBatchIndex = 0; primBatchIndex < mesh->primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
		const int primBatchIndexCount = primBatch.drawGeoSpec.primitiveCount * 3;
		const int primBatchTransformCount = Max( primBatch.numTransforms, 1 );

		if ( g_packedStageVertexFormatIndex > 0 ) {
			for ( int transformIndex = 0; transformIndex < primBatchTransformCount; ++transformIndex ) {
				RB_ARB2_LoadMD5RPaletteTransformRows(
					transformIndex * 3,
					tri->skinToModelTransforms + ( ( transformBase + transformIndex ) * 16 ) );
			}
		}

		backEnd.pc.c_drawElements++;
		backEnd.pc.c_drawIndexes += primBatchIndexCount;
		backEnd.pc.c_drawVertexes += primBatch.drawGeoSpec.vertexCount;

		glDrawElements(
			GL_TRIANGLES,
			r_singleTriangle.GetBool() ? 3 : primBatchIndexCount,
			GL_INDEX_TYPE,
			drawIndexBuffer->indices.Ptr() + primBatch.drawGeoSpec.indexStart );

		transformBase += primBatchTransformCount;
	}

	return true;
}

bool RB_ARB2_DrawPreparedPackedMD5RDirectBatches( const srfTriangles_t *tri ) {
	if ( g_packedDirectDrawSurf == NULL || g_packedDirectDrawSurf->geo != tri ) {
		return false;
	}

	const drawSurf_t *surf = g_packedDirectDrawSurf;
	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( tri );
	const rvMD5RIndexBufferDesc *drawIndexBuffer = R_MD5R_GetDrawIndexBufferForTri( tri );
	if ( mesh == NULL || drawIndexBuffer == NULL || !RB_ARB2_CanDrawPackedMD5RStageBatches( surf, g_packedDirectDrawVertexFormatIndex ) ) {
		RB_ARB2_ClearPreparedPackedMD5RDirectDraw();
		return false;
	}

	vertexCache.UnbindIndex();

	int transformBase = 0;
	for ( int primBatchIndex = 0; primBatchIndex < mesh->primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
		const int primBatchIndexCount = primBatch.drawGeoSpec.primitiveCount * 3;
		const int primBatchTransformCount = Max( primBatch.numTransforms, 1 );

		if ( g_packedDirectDrawVertexFormatIndex > 0 ) {
			for ( int transformIndex = 0; transformIndex < primBatchTransformCount; ++transformIndex ) {
				RB_ARB2_LoadMD5RPaletteTransformRows(
					transformIndex * 3,
					tri->skinToModelTransforms + ( ( transformBase + transformIndex ) * 16 ) );
			}
		}

		backEnd.pc.c_drawElements++;
		backEnd.pc.c_drawIndexes += primBatchIndexCount;
		backEnd.pc.c_drawVertexes += primBatch.drawGeoSpec.vertexCount;

		glDrawElements(
			GL_TRIANGLES,
			r_singleTriangle.GetBool() ? 3 : primBatchIndexCount,
			GL_INDEX_TYPE,
			drawIndexBuffer->indices.Ptr() + primBatch.drawGeoSpec.indexStart );

		transformBase += primBatchTransformCount;
	}

	return true;
}

static bool RB_ARB2_DrawPackedMD5RInteractionBatches( const drawInteraction_t *din, int vertexFormatIndex ) {
	const drawSurf_t *surf = ( din != NULL ) ? din->surf : NULL;
	const srfTriangles_t *tri = ( surf != NULL ) ? surf->geo : NULL;
	const rvMD5RMesh *mesh = ( tri != NULL ) ? R_MD5R_GetMeshForTri( tri ) : NULL;
	const rvMD5RVertexBufferDesc *drawVertexBuffer = ( tri != NULL ) ? R_MD5R_GetDrawVertexBufferForTri( tri ) : NULL;
	if ( tri == NULL
		|| mesh == NULL
		|| drawVertexBuffer == NULL
		|| tri->indexes == NULL
		|| tri->numIndexes <= 0
		|| mesh->primBatches.Num() <= 0 ) {
		return false;
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

	vertexCache.UnbindIndex();

	if ( hasHeaderedLightTris ) {
		const glIndex_t *batchHeader = tri->indexes;
		const glIndex_t *batchIndices = tri->indexes + numPrimBatches;
		int transformBase = 0;

		for ( int primBatchIndex = 0; primBatchIndex < numPrimBatches; ++primBatchIndex ) {
			const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
			const int primBatchTransformCount = Max( primBatch.numTransforms, 1 );
			const int batchIndexCount = batchHeader[ primBatchIndex ];

			if ( !primBatch.hasDrawGeoSpec
				|| primBatch.drawGeoSpec.vertexStart < 0
				|| primBatch.drawGeoSpec.vertexCount < 0
				|| primBatch.drawGeoSpec.vertexStart + primBatch.drawGeoSpec.vertexCount > drawVertexBuffer->numVertices ) {
				return false;
			}

			if ( vertexFormatIndex > 0 ) {
				if ( tri->skinToModelTransforms == NULL
					|| tri->numSkinToModelTransforms <= 0
					|| primBatchTransformCount > ARB2_MD5R_MAX_PALETTE_TRANSFORMS
					|| transformBase + primBatchTransformCount > tri->numSkinToModelTransforms ) {
					return false;
				}
			}

			if ( batchIndexCount > 0 ) {
				if ( vertexFormatIndex > 0 ) {
					for ( int transformIndex = 0; transformIndex < primBatchTransformCount; ++transformIndex ) {
						RB_ARB2_LoadMD5RPaletteTransformRows(
							transformIndex * 3,
							tri->skinToModelTransforms + ( ( transformBase + transformIndex ) * 16 ) );
					}
				}

				backEnd.pc.c_drawElements++;
				backEnd.pc.c_drawIndexes += batchIndexCount;
				backEnd.pc.c_drawVertexes += primBatch.drawGeoSpec.vertexCount;

				glDrawElements(
					GL_TRIANGLES,
					r_singleTriangle.GetBool() ? 3 : batchIndexCount,
					GL_INDEX_TYPE,
					batchIndices );
			}

			batchIndices += batchIndexCount;
			transformBase += primBatchTransformCount;
		}

		return true;
	}

	if ( ( tri->numIndexes % 3 ) != 0 ) {
		return false;
	}

	glIndex_t *batchIndexes = (glIndex_t *)_alloca16( tri->numIndexes * sizeof( batchIndexes[0] ) );
	const glIndex_t *lightIndexes = tri->indexes;
	int destVertexBase = 0;
	int transformBase = 0;

	for ( int primBatchIndex = 0; primBatchIndex < numPrimBatches; ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
		const int primBatchTransformCount = Max( primBatch.numTransforms, 1 );
		const int batchVertexStart = destVertexBase;
		const int batchVertexEnd = destVertexBase + primBatch.drawGeoSpec.vertexCount;
		int batchIndexCount = 0;

		if ( !primBatch.hasDrawGeoSpec
			|| primBatch.drawGeoSpec.vertexStart < 0
			|| primBatch.drawGeoSpec.vertexCount < 0
			|| primBatch.drawGeoSpec.vertexStart + primBatch.drawGeoSpec.vertexCount > drawVertexBuffer->numVertices ) {
			return false;
		}

		if ( vertexFormatIndex > 0 ) {
			if ( tri->skinToModelTransforms == NULL
				|| tri->numSkinToModelTransforms <= 0
				|| primBatchTransformCount > ARB2_MD5R_MAX_PALETTE_TRANSFORMS
				|| transformBase + primBatchTransformCount > tri->numSkinToModelTransforms ) {
				return false;
			}
		}

		for ( int indexBase = 0; indexBase < tri->numIndexes; indexBase += 3 ) {
			const int localIndex0 = lightIndexes[indexBase + 0];
			const int localIndex1 = lightIndexes[indexBase + 1];
			const int localIndex2 = lightIndexes[indexBase + 2];
			const bool index0InBatch = ( localIndex0 >= batchVertexStart && localIndex0 < batchVertexEnd );
			const bool index1InBatch = ( localIndex1 >= batchVertexStart && localIndex1 < batchVertexEnd );
			const bool index2InBatch = ( localIndex2 >= batchVertexStart && localIndex2 < batchVertexEnd );

			if ( !index0InBatch && !index1InBatch && !index2InBatch ) {
				continue;
			}

			if ( !index0InBatch || !index1InBatch || !index2InBatch ) {
				return false;
			}

			batchIndexes[batchIndexCount + 0] = localIndex0 - batchVertexStart + primBatch.drawGeoSpec.vertexStart;
			batchIndexes[batchIndexCount + 1] = localIndex1 - batchVertexStart + primBatch.drawGeoSpec.vertexStart;
			batchIndexes[batchIndexCount + 2] = localIndex2 - batchVertexStart + primBatch.drawGeoSpec.vertexStart;
			batchIndexCount += 3;
		}

		if ( batchIndexCount > 0 ) {
			if ( vertexFormatIndex > 0 ) {
				for ( int transformIndex = 0; transformIndex < primBatchTransformCount; ++transformIndex ) {
					RB_ARB2_LoadMD5RPaletteTransformRows(
						transformIndex * 3,
						tri->skinToModelTransforms + ( ( transformBase + transformIndex ) * 16 ) );
				}
			}

			backEnd.pc.c_drawElements++;
			backEnd.pc.c_drawIndexes += batchIndexCount;
			backEnd.pc.c_drawVertexes += primBatch.drawGeoSpec.vertexCount;

			glDrawElements(
				GL_TRIANGLES,
				r_singleTriangle.GetBool() ? 3 : batchIndexCount,
				GL_INDEX_TYPE,
				batchIndexes );
		}

		destVertexBase += primBatch.drawGeoSpec.vertexCount;
		transformBase += primBatchTransformCount;
	}

	return true;
}

static bool RB_ARB2_DrawPackedMD5RDepthBatches( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = ( surf != NULL ) ? surf->geo : NULL;
	const rvMD5RVertexBufferDesc *drawVertexBuffer = ( tri != NULL ) ? R_MD5R_GetDrawVertexBufferForTri( tri ) : NULL;
	if ( tri == NULL || drawVertexBuffer == NULL ) {
		return false;
	}

	const int vertexFormatIndex = RB_ARB2_GetMD5RVertexFormatIndex( *drawVertexBuffer );
	if ( vertexFormatIndex < 0 ) {
		return false;
	}

	const program_t depthProgram = RB_ARB2_GetMD5RVertexProgram( ARB2_MD5R_DEPTH_VPROG_BASE, vertexFormatIndex );
	if ( depthProgram == PROG_INVALID ) {
		return false;
	}

	if ( !RB_ARB2_CanDrawPackedMD5RStageBatches( surf, vertexFormatIndex ) ) {
		return false;
	}

	if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, depthProgram, "packed depth vertex program", false ) ) {
		return false;
	}

	glEnable( GL_VERTEX_PROGRAM_ARB );
	RB_ARB2_LoadMD5RMVPMatrix( surf );

	if ( !RB_ARB2_PreparePackedMD5RDirectDraw( surf ) ) {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
		glDisable( GL_VERTEX_PROGRAM_ARB );
		return false;
	}

	RB_DrawElementsWithCounters( tri );
	RB_ARB2_ClearPreparedPackedMD5RDirectDraw();
	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	return true;
}

static bool RB_ARB2_DrawPackedMD5RFogBatches( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = ( surf != NULL ) ? surf->geo : NULL;
	const rvMD5RVertexBufferDesc *drawVertexBuffer = ( tri != NULL ) ? R_MD5R_GetDrawVertexBufferForTri( tri ) : NULL;
	if ( tri == NULL || drawVertexBuffer == NULL ) {
		return false;
	}

	const int vertexFormatIndex = RB_ARB2_GetMD5RVertexFormatIndex( *drawVertexBuffer );
	if ( vertexFormatIndex < 0 ) {
		return false;
	}

	const program_t fogProgram = RB_ARB2_GetMD5RVertexProgram( ARB2_MD5R_BASIC_FOG_VPROG_BASE, vertexFormatIndex );
	if ( fogProgram == PROG_INVALID ) {
		return false;
	}

	if ( !RB_ARB2_CanDrawPackedMD5RStageBatches( surf, vertexFormatIndex ) ) {
		return false;
	}

	if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, fogProgram, "packed fog vertex program", false ) ) {
		return false;
	}

	idPlane local;
	glEnable( GL_VERTEX_PROGRAM_ARB );
	RB_ARB2_LoadMD5RMVPMatrix( surf );

	R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_DISTANCE_PLANE_S], local );
	local[3] += 0.5f;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_FOG_DISTANCE_PLANE, local.ToFloatPtr() );

	local[0] = 0.0f;
	local[1] = 0.0f;
	local[2] = 0.0f;
	local[3] = 0.5f;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_FOG_DISTANCE_BIAS, local.ToFloatPtr() );

	R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_ENTER_PLANE_T], local );
	local[3] += FOG_ENTER;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_FOG_ENTER_PLANE_T, local.ToFloatPtr() );

	R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_ENTER_PLANE_S], local );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_FOG_ENTER_PLANE_S, local.ToFloatPtr() );

	if ( !RB_ARB2_PreparePackedMD5RDirectDraw( surf ) ) {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
		glDisable( GL_VERTEX_PROGRAM_ARB );
		return false;
	}

	RB_DrawElementsWithCounters( tri );
	RB_ARB2_ClearPreparedPackedMD5RDirectDraw();
	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	return true;
}

static void RB_ARB2_LoadMD5RStageColor( const shaderStage_t *pStage, const drawSurf_t *surf, bool fillingDepth ) {
	static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	static const float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	if ( fillingDepth ) {
		float alphaOnly[4] = { 0.0f, 0.0f, 0.0f, surf->shaderRegisters[pStage->color.registers[3]] };
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_STAGE_VERTEX_COLOR_MODULATE, zero );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_STAGE_VERTEX_COLOR_ADD, alphaOnly );
		return;
	}

	if ( pStage->vertexColor != SVC_IGNORE ) {
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_STAGE_VERTEX_COLOR_MODULATE, one );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_STAGE_VERTEX_COLOR_ADD, zero );
		return;
	}

	float stageColor[4] = {
		surf->shaderRegisters[pStage->color.registers[0]],
		surf->shaderRegisters[pStage->color.registers[1]],
		surf->shaderRegisters[pStage->color.registers[2]],
		surf->shaderRegisters[pStage->color.registers[3]]
	};
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_STAGE_VERTEX_COLOR_MODULATE, zero );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_STAGE_VERTEX_COLOR_ADD, stageColor );
}

void RB_ARB2_LoadMD5RLocalViewOrigin( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL || backEnd.viewDef == NULL ) {
		return;
	}

	idVec4 localViewOrigin;
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3() );
	localViewOrigin.w = 1.0f;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_LOCAL_VIEW_ORIGIN, localViewOrigin.ToFloatPtr() );
}

// Packed MD5R programs take their projection from env params rather than
// GL_PROJECTION, so the depth-hack squash/offset applied by
// RB_EnterWeaponDepthHack/RB_EnterModelDepthHack (and mirrored by the modern
// path in R_ModernGLSubmitCommand_BuildModelViewProjection) must be applied
// here too or packed draws desync from classic-path siblings of the same entity.
static void RB_ARB2_GetMD5RProjectionMatrix( const viewEntity_t *space, float projectionMatrix[16] ) {
	R_GetDepthHackProjectionMatrix(
		backEnd.viewDef,
		space != NULL && space->weaponDepthHack,
		space != NULL ? space->modelDepthHack : 0.0f,
		projectionMatrix );
}

void RB_ARB2_LoadMD5RMVPMatrix( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL || backEnd.viewDef == NULL ) {
		return;
	}

	float projectionMatrix[16];
	RB_ARB2_GetMD5RProjectionMatrix( surf->space, projectionMatrix );

	float modelViewProjection[16];
	myGlMultMatrix( surf->space->modelViewMatrix, projectionMatrix, modelViewProjection );
	RB_ARB2_LoadVertexProgramMatrixRows( ARB2_MD5R_MVP_ROW_0, modelViewProjection, 4 );
}

void RB_ARB2_LoadMD5RProjectionMatrix( void ) {
	if ( backEnd.viewDef == NULL ) {
		return;
	}

	// the only caller (RB_STD_T_RenderShaderPasses) syncs backEnd.currentSpace
	// to the surface being drawn before loading matrices, so the depth-hack
	// state here matches the MVP loaded alongside these rows
	float projectionMatrix[16];
	RB_ARB2_GetMD5RProjectionMatrix( backEnd.currentSpace, projectionMatrix );
	RB_ARB2_LoadVertexProgramMatrixRows( ARB2_MD5R_PROJECTION_ROW_0, projectionMatrix, 4 );
}

void RB_ARB2_LoadMD5RModelViewMatrix( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL ) {
		return;
	}

	RB_ARB2_LoadVertexProgramMatrixRows( ARB2_MD5R_MODEL_ROW_0, surf->space->modelViewMatrix, 3 );
}

static void RB_ARB2_LoadMD5RModelMatrix( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL ) {
		return;
	}

	RB_ARB2_LoadVertexProgramMatrixRows( ARB2_MD5R_MODEL_ROW_0, surf->space->modelMatrix, 3 );
}

void RB_ARB2_PrepareStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf, bool fillingDepth ) {
	const srfTriangles_t *tri = ( surf != NULL ) ? surf->geo : NULL;
	const rvMD5RVertexBufferDesc *drawVertexBuffer = ( tri != NULL ) ? R_MD5R_GetDrawVertexBufferForTri( tri ) : NULL;
	const int vertexFormatIndex = ( drawVertexBuffer != NULL ) ? RB_ARB2_GetMD5RVertexFormatIndex( *drawVertexBuffer ) : -1;
	program_t stageVertexProgram = PROG_INVALID;
	const shaderStage_t *bumpStage = NULL;
	bool needsTexCoord0 = false;
	bool needsNormals = false;
	bool needsTangents = false;
	bool needsTextureMatrix = true;
	bool needsLocalViewOrigin = false;
	bool needsModelMatrix = false;
	bool usesTexGenTransform = false;

	g_packedStageSurf = NULL;
	g_packedStageVertexFormatIndex = -1;

	if ( tri == NULL || drawVertexBuffer == NULL || vertexFormatIndex < 0 ) {
		return;
	}

	switch ( pStage->texture.texgen ) {
	case TG_DIFFUSE_CUBE:
		stageVertexProgram = RB_ARB2_GetMD5RVertexProgram( ARB2_MD5R_DIFFUSE_CUBE_VPROG_BASE, vertexFormatIndex );
		needsNormals = true;
		needsTextureMatrix = false;
		break;

	case TG_REFLECT_CUBE:
		bumpStage = surf->material->GetBumpStage();
		if ( bumpStage != NULL ) {
			stageVertexProgram = RB_ARB2_GetMD5RVertexProgram( ARB2_MD5R_BUMPY_REFLECT_CUBE_VPROG_BASE, vertexFormatIndex );
			needsTexCoord0 = true;
			needsNormals = true;
			needsTangents = true;
			needsModelMatrix = true;
		} else {
			stageVertexProgram = RB_ARB2_GetMD5RVertexProgram( ARB2_MD5R_REFLECT_CUBE_VPROG_BASE, vertexFormatIndex );
			needsNormals = true;
		}
		needsLocalViewOrigin = true;
		needsTextureMatrix = false;
		break;

	case TG_SKYBOX_CUBE:
	case TG_WOBBLESKY_CUBE:
		if ( surf->texGenTransformAndViewOrg == NULL ) {
			return;
		}
		stageVertexProgram = RB_ARB2_GetMD5RVertexProgram( ARB2_MD5R_SKYBOX_VPROG_BASE, vertexFormatIndex );
		needsTextureMatrix = false;
		usesTexGenTransform = true;
		break;

	case TG_SCREEN:
		// Retail keeps packed TG_SCREEN on a no-op path here; SCREEN2 and
		// GLASSWARP continue through the generic MD5R stage program below.
		return;

	default:
		stageVertexProgram = RB_ARB2_GetMD5RVertexProgram( ARB2_MD5R_STAGE_VPROG_BASE, vertexFormatIndex );
		needsTexCoord0 = true;
		break;
	}

	if ( stageVertexProgram == PROG_INVALID
		|| !RB_ARB2_CanDrawPackedMD5RStageBatches( surf, vertexFormatIndex )
		|| !RB_ARB2_BindPackedMD5RStageVertexData( *drawVertexBuffer, vertexFormatIndex, needsTexCoord0, needsNormals, needsTangents ) ) {
		return;
	}

	if ( pStage->privatePolygonOffset ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset );
	}

	switch ( pStage->texture.texgen ) {
	case TG_DIFFUSE_CUBE:
		if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, stageVertexProgram, "packed diffuse-cube stage vertex program", false ) ) {
			RB_ARB2_UnbindPackedMD5RStageVertexData();
			return;
		}
		glEnable( GL_VERTEX_PROGRAM_ARB );
		break;

	case TG_REFLECT_CUBE:
		if ( bumpStage != NULL ) {
			GL_SelectTexture( 1 );
			bumpStage->texture.image->Bind();
			GL_SelectTexture( 0 );

			if ( R_BindARBProgram( GL_FRAGMENT_PROGRAM_ARB, FPROG_BUMPY_ENVIRONMENT, "packed reflect-cube stage fragment program", false ) ) {
				glEnable( GL_FRAGMENT_PROGRAM_ARB );
			}

			if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, stageVertexProgram, "packed bumpy reflect-cube stage vertex program", false ) ) {
				glDisable( GL_FRAGMENT_PROGRAM_ARB );
				RB_ARB2_UnbindPackedMD5RStageVertexData();
				return;
			}
			glEnable( GL_VERTEX_PROGRAM_ARB );
		} else {
			if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, stageVertexProgram, "packed reflect-cube stage vertex program", false ) ) {
				RB_ARB2_UnbindPackedMD5RStageVertexData();
				return;
			}
			glEnable( GL_VERTEX_PROGRAM_ARB );
		}
		break;

	case TG_SKYBOX_CUBE:
	case TG_WOBBLESKY_CUBE:
		if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, stageVertexProgram, "packed skybox stage vertex program", false ) ) {
			RB_ARB2_UnbindPackedMD5RStageVertexData();
			return;
		}
		glEnable( GL_VERTEX_PROGRAM_ARB );
		break;

	default:
		if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, stageVertexProgram, "packed stage texturing vertex program", false ) ) {
			RB_ARB2_UnbindPackedMD5RStageVertexData();
			return;
		}
		glEnable( GL_VERTEX_PROGRAM_ARB );
		break;
	}

	RB_ARB2_LoadMD5RStageColor( pStage, surf, fillingDepth );
	RB_ARB2_LoadMD5RMVPMatrix( surf );

	if ( needsLocalViewOrigin ) {
		RB_ARB2_LoadMD5RLocalViewOrigin( surf );
	}

	if ( needsModelMatrix ) {
		RB_ARB2_LoadMD5RModelMatrix( surf );
	}

	if ( usesTexGenTransform ) {
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_LOCAL_VIEW_ORIGIN, surf->texGenTransformAndViewOrg + 12 );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_PROJECTION_ROW_0, surf->texGenTransformAndViewOrg + 0 );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_PROJECTION_ROW_1, surf->texGenTransformAndViewOrg + 4 );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_PROJECTION_ROW_2, surf->texGenTransformAndViewOrg + 8 );
	}

	if ( pStage->texture.texgen == TG_DIFFUSE_CUBE || pStage->texture.texgen == TG_REFLECT_CUBE
		|| pStage->texture.texgen == TG_SKYBOX_CUBE || pStage->texture.texgen == TG_WOBBLESKY_CUBE ) {
		g_packedStageSurf = surf;
		g_packedStageVertexFormatIndex = vertexFormatIndex;
		return;
	}

	if ( needsTextureMatrix && pStage->texture.hasMatrix ) {
		RB_ARB2_LoadMD5RTextureMatrix( surf->shaderRegisters, &pStage->texture );
	} else if ( needsTextureMatrix ) {
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_TEXTURE_MATRIX_ROW_0, RB_ARB2_MD5RIdentityTextureMatrixRows[0] );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_TEXTURE_MATRIX_ROW_1, RB_ARB2_MD5RIdentityTextureMatrixRows[1] );
	}

	g_packedStageSurf = surf;
	g_packedStageVertexFormatIndex = vertexFormatIndex;
}

void RB_ARB2_MD5R_DrawDepthElements( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->geo == NULL ) {
		return;
	}

	if ( RB_ARB2_DrawPackedMD5RDepthBatches( surf ) ) {
		return;
	}

	if ( surf->geo->ambientCache == NULL ) {
		return;
	}

	if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_DEPTH_VPROG_BASE, "packed depth vertex program", false ) ) {
		RB_DrawElementsWithCounters( surf->geo );
		return;
	}

	glEnable( GL_VERTEX_PROGRAM_ARB );
	RB_ARB2_LoadMD5RMVPMatrix( surf );
	RB_DrawElementsWithCounters( surf->geo );
	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
}

static bool RB_ARB2_PackedShadowHeaderedIndexesValid( const srfTriangles_t *tri, int numPrimBatches ) {
	if ( tri == NULL || tri->indexes == NULL || numPrimBatches <= 0 ) {
		return false;
	}

	if ( tri->numAllocedIndices < tri->numIndexes + ( 2 * numPrimBatches ) ) {
		return false;
	}

	int headerIndexCount = 0;
	for ( int primBatchIndex = 0; primBatchIndex < numPrimBatches; ++primBatchIndex ) {
		const int noCapsIndexCount = tri->indexes[ primBatchIndex * 2 + 0 ];
		const int totalIndexCount = tri->indexes[ primBatchIndex * 2 + 1 ];
		if ( noCapsIndexCount < 0
			|| totalIndexCount < 0
			|| ( noCapsIndexCount % 3 ) != 0
			|| ( totalIndexCount % 3 ) != 0
			|| noCapsIndexCount > totalIndexCount
			|| headerIndexCount + totalIndexCount > tri->numIndexes ) {
			return false;
		}

		headerIndexCount += totalIndexCount;
	}

	return ( headerIndexCount == tri->numIndexes );
}

static bool RB_ARB2_DrawPackedMD5RShadowBatches( const drawSurf_t *surf, int numIndexes ) {
	const srfTriangles_t *tri = ( surf != NULL ) ? surf->geo : NULL;
	const rvMD5RMesh *mesh = ( tri != NULL ) ? R_MD5R_GetMeshForTri( tri ) : NULL;
	const rvMD5RVertexBufferDesc *shadowVertexBuffer = ( tri != NULL ) ? R_MD5R_GetShadowVertexBufferForTri( tri ) : NULL;
	if ( tri == NULL || mesh == NULL || shadowVertexBuffer == NULL || mesh->primBatches.Num() <= 0 ) {
		return false;
	}

	const int vertexFormatIndex = RB_ARB2_GetMD5RVertexFormatIndex( *shadowVertexBuffer );
	if ( vertexFormatIndex < 0 || !RB_ARB2_BindPackedMD5RDrawVertexData( *shadowVertexBuffer, vertexFormatIndex ) ) {
		return false;
	}

	const bool vertexProgramWasEnabled = ( glIsEnabled( GL_VERTEX_PROGRAM_ARB ) == GL_TRUE );
	if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_SHADOW_VOLUME_VPROG_BASE, "packed shadow vertex program", false ) ) {
		RB_ARB2_UnbindPackedMD5RDrawVertexData( vertexFormatIndex );
		return false;
	}

	idVec4 localLight;
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.vLight->globalLightOrigin, localLight.ToVec3() );
	localLight.w = 0.0f;

	glEnable( GL_VERTEX_PROGRAM_ARB );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_LOCAL_LIGHT_ORIGIN, localLight.ToFloatPtr() );
	RB_ARB2_LoadMD5RMVPMatrix( surf );

	const bool drawCaps = ( numIndexes == tri->numIndexes );
	const int numPrimBatches = mesh->primBatches.Num();
	const bool hasHeaderedShadowIndexes = RB_ARB2_PackedShadowHeaderedIndexesValid( tri, numPrimBatches );
	const rvMD5RIndexBufferDesc *shadowIndexBuffer = hasHeaderedShadowIndexes ? NULL : R_MD5R_GetShadowIndexBufferForTri( tri );
	if ( !hasHeaderedShadowIndexes && ( shadowIndexBuffer == NULL
		|| shadowIndexBuffer->numIndices <= 0
		|| shadowIndexBuffer->indices.Num() != shadowIndexBuffer->numIndices ) ) {
		RB_ARB2_UnbindPackedMD5RDrawVertexData( vertexFormatIndex );
		idVertexCache::BindArrayBuffer( 0 );
		vertexCache.UnbindIndex();
		if ( vertexProgramWasEnabled ) {
			glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
		} else {
			glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
			glDisable( GL_VERTEX_PROGRAM_ARB );
		}
		return false;
	}

	vertexCache.UnbindIndex();

	const glIndex_t *batchHeader = hasHeaderedShadowIndexes ? tri->indexes : NULL;
	const glIndex_t *batchIndices = hasHeaderedShadowIndexes ? ( tri->indexes + ( 2 * numPrimBatches ) ) : NULL;
	int transformBase = 0;
	bool drewAnything = false;

	for ( int primBatchIndex = 0; primBatchIndex < numPrimBatches; ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
		const int primBatchTransformCount = Max( primBatch.numTransforms, 1 );
		const int shadowVertexCount = primBatch.shadowVolGeoSpec.vertexCount;
		int shadowIndexCount = 0;
		const glIndex_t *indexSource = NULL;

		if ( shadowVertexCount <= 0
			|| !primBatch.hasShadowGeoSpec
			|| primBatch.shadowVolGeoSpec.vertexStart < 0
			|| primBatch.shadowVolGeoSpec.vertexStart + shadowVertexCount > shadowVertexBuffer->numVertices ) {
			break;
		}

		if ( vertexFormatIndex > 0 ) {
			if ( tri->skinToModelTransforms == NULL
				|| tri->numSkinToModelTransforms <= 0
				|| primBatchTransformCount > ARB2_MD5R_MAX_PALETTE_TRANSFORMS
				|| transformBase + primBatchTransformCount > tri->numSkinToModelTransforms ) {
				break;
			}

			for ( int transformIndex = 0; transformIndex < primBatchTransformCount; ++transformIndex ) {
				RB_ARB2_LoadMD5RPaletteTransformRows(
					transformIndex * 3,
					tri->skinToModelTransforms + ( ( transformBase + transformIndex ) * 16 ) );
			}
		}

		if ( hasHeaderedShadowIndexes ) {
			const int noCapsIndexCount = batchHeader[ primBatchIndex * 2 + 0 ];
			const int totalIndexCount = batchHeader[ primBatchIndex * 2 + 1 ];
			shadowIndexCount = drawCaps ? totalIndexCount : noCapsIndexCount;
			indexSource = batchIndices;
			batchIndices += totalIndexCount;
		} else {
			const int totalIndexCount = primBatch.shadowVolGeoSpec.primitiveCount * 3;
			const int noCapsIndexCount = primBatch.numShadowPrimitivesNoCaps * 3;
			if ( primBatch.shadowVolGeoSpec.indexStart < 0
				|| totalIndexCount < 0
				|| noCapsIndexCount < 0
				|| noCapsIndexCount > totalIndexCount
				|| primBatch.shadowVolGeoSpec.indexStart + totalIndexCount > shadowIndexBuffer->numIndices ) {
				break;
			}

			shadowIndexCount = drawCaps ? totalIndexCount : noCapsIndexCount;
			indexSource = shadowIndexBuffer->indices.Ptr() + primBatch.shadowVolGeoSpec.indexStart;
		}

		if ( shadowIndexCount > 0 && indexSource != NULL ) {
			backEnd.pc.c_shadowElements++;
			backEnd.pc.c_shadowIndexes += shadowIndexCount;
			backEnd.pc.c_shadowVertexes += shadowVertexCount;

			glDrawElements(
				GL_TRIANGLES,
				r_singleTriangle.GetBool() ? 3 : shadowIndexCount,
				GL_INDEX_TYPE,
				indexSource );
			drewAnything = true;
		}

		transformBase += primBatchTransformCount;
	}

	RB_ARB2_UnbindPackedMD5RDrawVertexData( vertexFormatIndex );
	idVertexCache::BindArrayBuffer( 0 );
	vertexCache.UnbindIndex();

	if ( vertexProgramWasEnabled ) {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
	} else {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
		glDisable( GL_VERTEX_PROGRAM_ARB );
	}

	return drewAnything;
}

void RB_ARB2_MD5R_DrawShadowElements( const drawSurf_t *surf, int numIndexes ) {
	const srfTriangles_t *tri = ( surf != NULL ) ? surf->geo : NULL;
	if ( tri == NULL ) {
		return;
	}

	if ( tri->primBatchMesh != NULL && RB_ARB2_DrawPackedMD5RShadowBatches( surf, numIndexes ) ) {
		return;
	}

	if ( tri->shadowCache == NULL ) {
		return;
	}

	const bool vertexProgramWasEnabled = glIsEnabled( GL_VERTEX_PROGRAM_ARB ) == GL_TRUE;
	if ( R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_SHADOW_VOLUME_VPROG_BASE, "packed shadow vertex program", false ) ) {
		idVec4 localLight;
		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.vLight->globalLightOrigin, localLight.ToVec3() );
		localLight.w = 0.0f;

		glEnable( GL_VERTEX_PROGRAM_ARB );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_LOCAL_LIGHT_ORIGIN, localLight.ToFloatPtr() );
		RB_ARB2_LoadMD5RMVPMatrix( surf );
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
		if ( r_useIndexBuffers.GetBool() ) {
			vertexCache.UnbindIndex();
		}
		glDrawElements( GL_TRIANGLES,
			r_singleTriangle.GetBool() ? 3 : numIndexes,
			GL_INDEX_TYPE,
			tri->indexes );
	}

	if ( vertexProgramWasEnabled ) {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
	} else {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
		glDisable( GL_VERTEX_PROGRAM_ARB );
	}
}

void RB_ARB2_MD5R_DrawBasicFog( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->geo == NULL ) {
		return;
	}

	if ( RB_ARB2_DrawPackedMD5RFogBatches( surf ) ) {
		return;
	}

	if ( surf->geo->ambientCache == NULL ) {
		return;
	}

	if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_BASIC_FOG_VPROG_BASE, "packed fog vertex program", false ) ) {
		RB_DrawElementsWithCounters( surf->geo );
		return;
	}

	idPlane local;
	glEnable( GL_VERTEX_PROGRAM_ARB );
	RB_ARB2_LoadMD5RMVPMatrix( surf );

	R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_DISTANCE_PLANE_S], local );
	local[3] += 0.5f;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_FOG_DISTANCE_PLANE, local.ToFloatPtr() );

	local[0] = 0.0f;
	local[1] = 0.0f;
	local[2] = 0.0f;
	local[3] = 0.5f;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_FOG_DISTANCE_BIAS, local.ToFloatPtr() );

	R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_ENTER_PLANE_T], local );
	local[3] += FOG_ENTER;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_FOG_ENTER_PLANE_T, local.ToFloatPtr() );

	R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_ENTER_PLANE_S], local );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_FOG_ENTER_PLANE_S, local.ToFloatPtr() );

	RB_DrawElementsWithCounters( surf->geo );
	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
}

void RB_ARB2_DisableStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf ) {
	if ( pStage->privatePolygonOffset && !surf->material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}

	if ( g_packedStageSurf == surf ) {
		RB_ARB2_ClearPreparedPackedMD5RDraw();
	}

	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
	idVertexCache::BindArrayBuffer( 0 );
	vertexCache.UnbindIndex();

	if ( pStage->texture.texgen == TG_REFLECT_CUBE && surf->material->GetBumpStage() != NULL ) {
		GL_SelectTexture( 1 );
		globalImages->BindNull();
		GL_SelectTexture( 0 );
	}
}

static const char *RB_InteractionColorModeName( interactionColorMode_t mode ) {
	switch ( mode ) {
	case ICM_PACKED:
		return "packed env16.xy";
	case ICM_VECTOR:
		return "vector env16/env17";
	default:
		return "unknown";
	}
}

static const char *RB_InteractionColorOverrideName( int modeOverride ) {
	switch ( modeOverride ) {
	case 0:
		return "auto";
	case 1:
		return "packed";
	case 2:
		return "vector";
	default:
		return "invalid";
	}
}

static void RB_StripARBProgramCommentsAndWhitespace( const char *source, idStr &normalizedSource ) {
	normalizedSource.Empty();
	if ( source == NULL ) {
		return;
	}

	bool inComment = false;
	for ( const char *p = source; *p != '\0'; ++p ) {
		const char c = *p;
		if ( c == '\r' || c == '\n' ) {
			inComment = false;
			continue;
		}

		if ( inComment ) {
			continue;
		}

		if ( c == '#' ) {
			inComment = true;
			continue;
		}

		if ( c == ' ' || c == '\t' ) {
			continue;
		}

		normalizedSource.Append( (char)tolower( (unsigned char)c ) );
	}
}

static bool RB_DetectInteractionColorMode( const char *programSource, interactionColorMode_t &modeOut ) {
	idStr normalizedProgram;
	RB_StripARBProgramCommentsAndWhitespace( programSource, normalizedProgram );
	const char *normalized = normalizedProgram.c_str();

	if ( strstr( normalized, "madresult.color,vertex.color,program.env[16].x,program.env[16].y;" ) != NULL ) {
		modeOut = ICM_PACKED;
		return true;
	}

	if ( strstr( normalized, "madresult.color,vertex.color,program.env[16],program.env[17];" ) != NULL ) {
		modeOut = ICM_VECTOR;
		return true;
	}

	const bool packedHints = strstr( normalized, "program.env[16].x" ) != NULL &&
		strstr( normalized, "program.env[16].y" ) != NULL;
	const bool usesEnv17 = strstr( normalized, "program.env[17]" ) != NULL;

	if ( packedHints && !usesEnv17 ) {
		modeOut = ICM_PACKED;
		return true;
	}

	if ( usesEnv17 ) {
		modeOut = ICM_VECTOR;
		return true;
	}

	return false;
}

static void RB_UpdateInteractionColorMode( bool forcePrint ) {
	const int modeOverride = idMath::ClampInt( 0, 2, r_interactionColorMode.GetInteger() );
	interactionColorMode_t requestedMode = g_interactionVertexProgramAutoColorMode;
	interactionColorMode_t effectiveMode = g_interactionVertexProgramAutoColorMode;

	if ( modeOverride == 1 ) {
		requestedMode = ICM_PACKED;
	} else if ( modeOverride == 2 ) {
		requestedMode = ICM_VECTOR;
	}

	if ( modeOverride != 0 && requestedMode != g_interactionVertexProgramAutoColorMode ) {
		effectiveMode = g_interactionVertexProgramAutoColorMode;
		if ( forcePrint ||
			modeOverride != g_interactionVertexProgramOverride ||
			effectiveMode != g_interactionVertexProgramColorMode ) {
			common->Warning( "r_interactionColorMode=%d (%s) is incompatible with interaction.vfp (%s); forcing compatible mode",
				modeOverride,
				RB_InteractionColorModeName( requestedMode ),
				RB_InteractionColorModeName( g_interactionVertexProgramAutoColorMode ) );
		}
	} else {
		effectiveMode = requestedMode;
	}

	const bool changed = effectiveMode != g_interactionVertexProgramColorMode ||
		modeOverride != g_interactionVertexProgramOverride;

	g_interactionVertexProgramColorMode = effectiveMode;
	g_interactionVertexProgramOverride = modeOverride;

	if ( forcePrint || changed ) {
		common->Printf( ": interaction color mode = %s (auto=%s, override=%s)\n",
			RB_InteractionColorModeName( g_interactionVertexProgramColorMode ),
			RB_InteractionColorModeName( g_interactionVertexProgramAutoColorMode ),
			RB_InteractionColorOverrideName( g_interactionVertexProgramOverride ) );
	}
}

/*
=========================================================================================

GENERAL INTERACTION RENDERING

=========================================================================================
*/

/*
====================
GL_SelectTextureNoClient
====================
*/
void GL_SelectTextureNoClient( int unit ) {
	if ( backEnd.glState.currenttmu == unit && r_useRedundantStateFiltering.GetBool() ) {
		return;
	}
	backEnd.glState.currenttmu = unit;
	glActiveTextureARB( GL_TEXTURE0_ARB + unit );
	RB_LogComment( "glActiveTextureARB( %i )\n", unit );
}

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			localLightOrigin;
	GLint			localViewOrigin;
	GLint			lightProjectionS;
	GLint			lightProjectionT;
	GLint			lightProjectionQ;
	GLint			lightFalloffS;
	GLint			bumpMatrixS;
	GLint			bumpMatrixT;
	GLint			diffuseMatrixS;
	GLint			diffuseMatrixT;
	GLint			specularMatrixS;
	GLint			specularMatrixT;
	GLint			shadowRow[4];
	GLint			vertexColorParams;
	GLint			diffuseColor;
	GLint			specularColor;
	GLint			flatDiffuseParams;
	GLint			materialEnhanced;
	GLint			materialNormalScale;
	GLint			materialSpecularBoost;
	GLint			materialFresnel;
	GLint			shadowTexelSize;
	GLint			shadowBias;
	GLint			shadowNormalBias;
	GLint			shadowTexelDepthBias;
	GLint			shadowNormalOffsetWorld;
	GLint			shadowReceiverPlaneBias;
	GLint			shadowFilterRadius;
	GLint			shadowFilterTaps;
	GLint			shadowFilterMode;
	GLint			shadowPCSSLightRadius;
	GLint			shadowPCSSMaxRadius;
	GLint			shadowAtlasRect;
	GLint			shadowSplitDepths;
	GLint			shadowCascadeBiasScale;
	GLint			shadowCascadeCount;
	GLint			shadowCascadeBlend;
	GLint			shadowDebugMode;
	GLint			shadowReceiverDebugReason;
	GLint			translucentShadowEnabled;
	GLint			translucentShadowDensity;
	GLint			translucentShadowFilterRadius;
	GLint			translucentShadowMinVariance;
	GLint			translucentShadowBleedReduction;

	GLint			bumpMap;
	GLint			lightFalloffMap;
	GLint			lightProjectionMap;
	GLint			diffuseMap;
	GLint			specularMap;
	GLint			shadowMap;
	GLint			translucentShadowMap[3];
	GLint			celParams;
	bool			depthCompareMode;
} shadowMapProgram_t;

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			alphaTexCoordS;
	GLint			alphaTexCoordT;
	GLint			shadowDepthRow;
	GLint			casterDepthOffset;
	GLint			alphaTestEnabled;
	GLint			alphaRef;
	GLint			alphaTestMode;
	GLint			alphaScale;
	GLint			alphaHashEnabled;
	GLint			alphaHashStable;
	GLint			modelMatrixRow0;
	GLint			modelMatrixRow1;
	GLint			modelMatrixRow2;
	GLint			alphaMap;
} shadowMapCasterProgram_t;

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			modelMatrixRow0;
	GLint			modelMatrixRow1;
	GLint			modelMatrixRow2;
	GLint			shadowDepthRow;
	GLint			alphaTexCoordS;
	GLint			alphaTexCoordT;
	GLint			coverageTexCoordS;
	GLint			coverageTexCoordT;
	GLint			stageColor;
	GLint			opacitySourceMode;
	GLint			vertexColorMode;
	GLint			vertexAlphaParams;
	GLint			coverageStageColor;
	GLint			coverageSourceMode;
	GLint			coverageVertexColorMode;
	GLint			coverageVertexAlphaParams;
	GLint			coverageAlphaTestRef;
	GLint			coverageAlphaTestMode;
	GLint			coverageAlphaTestEnabled;
	GLint			translucentMinAlpha;
	GLint			alphaMap;
	GLint			coverageMap;
} translucentShadowCasterProgram_t;

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			localLightOrigin;
	GLint			localViewOrigin;
	GLint			lightProjectionS;
	GLint			lightProjectionT;
	GLint			lightProjectionQ;
	GLint			lightFalloffS;
	GLint			bumpMatrixS;
	GLint			bumpMatrixT;
	GLint			diffuseMatrixS;
	GLint			diffuseMatrixT;
	GLint			specularMatrixS;
	GLint			specularMatrixT;
	GLint			modelMatrixRow0;
	GLint			modelMatrixRow1;
	GLint			modelMatrixRow2;
	GLint			globalLightOrigin;
	GLint			pointShadowFar;
	GLint			vertexColorParams;
	GLint			diffuseColor;
	GLint			specularColor;
	GLint			flatDiffuseParams;
	GLint			materialEnhanced;
	GLint			materialNormalScale;
	GLint			materialSpecularBoost;
	GLint			materialFresnel;
	GLint			shadowBias;
	GLint			shadowNormalBias;
	GLint			pointShadowTexelDepthBias;
	GLint			pointShadowNormalOffsetWorld;
	GLint			shadowFilterRadius;
	GLint			shadowFilterTaps;
	GLint			shadowFilterMode;
	GLint			shadowDebugMode;
	GLint			shadowReceiverDebugReason;
	GLint			pointShadowTexelScale;
	GLint			pointShadowDepthMode;
	GLint			translucentShadowEnabled;
	GLint			translucentShadowDensity;
	GLint			translucentShadowFilterRadius;
	GLint			translucentShadowMinVariance;
	GLint			translucentShadowBleedReduction;

	GLint			bumpMap;
	GLint			lightFalloffMap;
	GLint			lightProjectionMap;
	GLint			diffuseMap;
	GLint			specularMap;
	GLint			pointShadowMap;
	GLint			translucentShadowMap[3];
	GLint			celParams;
	bool			depthCompareMode;
} pointShadowMapProgram_t;

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			localLightOrigin;
	GLint			localViewOrigin;
	GLint			lightProjectionS;
	GLint			lightProjectionT;
	GLint			lightProjectionQ;
	GLint			lightFalloffS;
	GLint			bumpMatrixS;
	GLint			bumpMatrixT;
	GLint			diffuseMatrixS;
	GLint			diffuseMatrixT;
	GLint			specularMatrixS;
	GLint			specularMatrixT;
	GLint			vertexColorParams;
	GLint			diffuseColor;
	GLint			specularColor;
	GLint			flatDiffuseParams;
	GLint			materialNormalScale;
	GLint			materialSpecularBoost;
	GLint			materialFresnel;
	GLint			stockInteraction;
	GLint			ambientLight;
	GLint			ambientNormalMap;

	GLint			bumpMap;
	GLint			lightFalloffMap;
	GLint			lightProjectionMap;
	GLint			diffuseMap;
	GLint			specularMap;
	GLint			celParams;
} materialInteractionProgram_t;

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			modelMatrixRow0;
	GLint			modelMatrixRow1;
	GLint			modelMatrixRow2;
	GLint			globalLightOrigin;
	GLint			pointShadowFar;
	GLint			alphaTexCoordS;
	GLint			alphaTexCoordT;
	GLint			alphaRef;
	GLint			alphaTestMode;
	GLint			alphaScale;
	GLint			alphaTestEnabled;
	GLint			alphaHashEnabled;
	GLint			alphaHashStable;
	GLint			pointShadowDepthMode;
	GLint			casterDepthOffset;
	GLint			alphaMap;
	bool			depthCompareMode;
} pointShadowCasterProgram_t;

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			modelMatrixRow0;
	GLint			modelMatrixRow1;
	GLint			modelMatrixRow2;
	GLint			globalLightOrigin;
	GLint			pointShadowFar;
	GLint			alphaTexCoordS;
	GLint			alphaTexCoordT;
	GLint			coverageTexCoordS;
	GLint			coverageTexCoordT;
	GLint			stageColor;
	GLint			opacitySourceMode;
	GLint			vertexColorMode;
	GLint			vertexAlphaParams;
	GLint			coverageStageColor;
	GLint			coverageSourceMode;
	GLint			coverageVertexColorMode;
	GLint			coverageVertexAlphaParams;
	GLint			coverageAlphaTestRef;
	GLint			coverageAlphaTestMode;
	GLint			coverageAlphaTestEnabled;
	GLint			translucentMinAlpha;
	GLint			alphaMap;
	GLint			coverageMap;
} pointTranslucentShadowCasterProgram_t;

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			screenSize;
	GLint			mode;
	GLint			color;
	GLint			pointLight;
	GLint			atlasDiv;
	GLint			cascadeCount;
	GLint			passMapped;
	GLint			glyphCode;
	GLint			pointShadowDepthMode;

	GLint			shadowAtlasMap;
	GLint			pointShadowMap;
} shadowDebugOverlayProgram_t;

typedef enum {
	TRANSLUCENT_SHADOW_STAGE_NONE = 0,
	TRANSLUCENT_SHADOW_STAGE_TEXTURE_ALPHA,
	TRANSLUCENT_SHADOW_STAGE_TEXTURE_ADDITIVE,
	TRANSLUCENT_SHADOW_STAGE_CUBEMAP_ADDITIVE
} translucentShadowStageMode_t;

static const int SHADOWMAP_MAX_CASCADES = SHADOWMAP_PROJECTED_MAX_CASCADES;

typedef shadowMapProjectedLightState_t projectedShadowMapState_t;

static shadowMapProgram_t	g_shadowMapProgram = { 0, 0, 0, -1, false };
static shadowMapCasterProgram_t	g_shadowMapCasterProgram = { 0, 0, 0, -1, false };
static translucentShadowCasterProgram_t	g_translucentShadowCasterProgram = { 0, 0, 0, -1, false };
static materialInteractionProgram_t	g_materialInteractionProgram = { 0, 0, 0, -1, false };
static pointShadowMapProgram_t	g_pointShadowMapProgram = { 0, 0, 0, -1, false };
static pointShadowCasterProgram_t	g_pointShadowCasterProgram = { 0, 0, 0, -1, false };
static pointTranslucentShadowCasterProgram_t	g_pointTranslucentShadowCasterProgram = { 0, 0, 0, -1, false };
static shadowDebugOverlayProgram_t	g_shadowDebugOverlayProgram = { 0, 0, 0, -1, false };
static idImage *			g_shadowMapDepthImage = NULL;
static idRenderTexture *	g_shadowMapRenderTexture = NULL;
static idImage *			g_shadowMapScratchDepthImage = NULL;
static idRenderTexture *	g_shadowMapScratchRenderTexture = NULL;

// One persistent depth atlas holds every cached projected light's tiles
// simultaneously (uncacheable lights still render through the per-light
// scratch texture). Cached lights render into cell blocks; the receiver
// composes the cell origin into the per-light cascade rects at upload time,
// so the per-light state convention (and planner parity) is untouched.
static idImage *			g_shadowMapAtlasDepthImage = NULL;
static idRenderTexture *	g_shadowMapAtlasRenderTexture = NULL;
static int					g_shadowMapAtlasCellSize = 0;	// grid re-derives (and entries invalidate) when this changes
static int					g_shadowMapActiveSlotOriginX = 0;	// pixel origin of the active render target's slot (0 for scratch)
static int					g_shadowMapActiveSlotOriginY = 0;
static idImage *			g_translucentShadowMomentImages[3] = { NULL, NULL, NULL };
static idRenderTexture *	g_translucentShadowMapRenderTexture = NULL;
static idImage *			g_pointShadowMapColorImage = NULL;
static idImage *			g_pointShadowMapDepthImage = NULL;
static idRenderTexture *	g_pointShadowMapRenderTexture = NULL;
static idImage *			g_pointShadowMapScratchColorImage = NULL;
static idImage *			g_pointShadowMapScratchDepthImage = NULL;
static idRenderTexture *	g_pointShadowMapScratchRenderTexture = NULL;
static idImage *			g_pointTranslucentShadowMomentImages[3] = { NULL, NULL, NULL };
static idRenderTexture *	g_pointTranslucentShadowMapRenderTexture = NULL;
static projectedShadowMapState_t	g_projectedShadowMapState = { 0 };
static bool				g_projectedTranslucentShadowPassReady = false;
static bool				g_pointTranslucentShadowPassReady = false;
static bool				g_pointShadowMapDepthCompareAvailable = true;
static int				g_pointShadowMapDepthCompareAvailableGeneration = -1;

typedef enum {
	SHADOWMAP_SUPPORT_OK = 0,
	SHADOWMAP_SUPPORT_DISABLED,
	SHADOWMAP_SUPPORT_SHADOWS_DISABLED,
	SHADOWMAP_SUPPORT_NULL_LIGHT,
	SHADOWMAP_SUPPORT_NO_SHADOWS_FLAG,
	SHADOWMAP_SUPPORT_NO_DYNAMIC_SHADOWS_FLAG,
	SHADOWMAP_SUPPORT_AMBIENT_LIGHT,
	SHADOWMAP_SUPPORT_LIGHT_SHADER_NO_SHADOWS,
	SHADOWMAP_SUPPORT_POINT_DISABLED,
	SHADOWMAP_SUPPORT_TEXTURE_LIMIT,
	SHADOWMAP_SUPPORT_NO_INTERACTIONS,
	SHADOWMAP_SUPPORT_CUBEMAP_UNAVAILABLE,
	SHADOWMAP_SUPPORT_RESOURCE_FAILURE,
	SHADOWMAP_SUPPORT_COUNT
} shadowMapLightSupportReason_t;

typedef enum {
	SHADOWMAP_PASS_LOCAL = 0,
	SHADOWMAP_PASS_GLOBAL
} shadowMapPassKind_t;

typedef enum {
	SHADOWMAP_RECEIVER_FALLBACK_NONE = -1,
	SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE = 0,
	SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL,
	SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY,
	SHADOWMAP_RECEIVER_FALLBACK_COUNT
} shadowMapReceiverFallbackReason_t;

typedef enum {
	SHADOWMAP_TIMING_MAP_RENDER = 0,
	SHADOWMAP_TIMING_CACHE_REUSE,
	SHADOWMAP_TIMING_MASK_PASS,
	SHADOWMAP_TIMING_RECEIVER_FALLBACK,
	SHADOWMAP_TIMING_STENCIL_FALLBACK,
	SHADOWMAP_TIMING_COUNT
} shadowMapTimingPhase_t;

typedef struct {
	bool				csmRequested;
	bool				projectedCsmRequested;
	int					requestedCascadeCount;
	int					totalLights;
	int					supportedLights;
	int					pointLights;
	int					projectedLights;
	int					parallelLights;
	int					globalLights;
	int					cascadeLights;
	int					cascadeCount;
	int					projectedCsmEligibleLights;
	int					projectedCsmCascadeLights;
	int					projectedCsmSuppressedLights;
	int					nonProjectedCsmLights;
	int					mappedLocalPasses;
	int					mappedGlobalPasses;
	int					unshadowedLocalPasses;
	int					unshadowedGlobalPasses;
	int					fallbackLocalPasses;
	int					fallbackGlobalPasses;
	int					receiverFallbackLocalPasses;
	int					receiverFallbackGlobalPasses;
	int					receiverFallbackSurfaces[SHADOWMAP_RECEIVER_FALLBACK_COUNT];
	int					receiverWrappedCustomGLSLLocalPasses;
	int					receiverWrappedCustomGLSLGlobalPasses;
	int					receiverWrappedCustomGLSLSurfaces;
	int					renderFailLocalPasses;
	int					renderFailGlobalPasses;
	int					maskFailLocalPasses;
	int					maskFailGlobalPasses;
	int					scheduledSkipPasses;
	int					shadowMapUpdates;
	int					composePasses;		// static tiles blitted + dynamic casters drawn on top
	int					subviewUpdates;		// updates spent in mirror/remote subviews this frame (main-view report)
	int					subviewReusePasses;	// subview passes served from cached maps under r_shadowMapSubviewPolicy 1
	int					subviewFallbackPasses;	// subview passes sent to stencil by the subview policy
	int					translucentReceiverPasses;	// translucent interaction chains drawn with shadow sampling (C3)
	int					admissionDeniedPasses;	// passes denied a fresh update by importance-ordered budget admission
	int					cacheHitPasses;
	int					cacheMissPasses;
	int					cacheReusePasses;
	int					cacheBudgetReusePasses;
	int					cacheEvictions;
	int					cacheExpired;
	int					staticHysteresisPasses;
	int					pointDepthComparePasses;
	int					projectedAtlasTilesRendered;
	int					projectedAtlasTilesAllocated;
	int					projectedCacheSlotsUsed;
	int					projectedCacheSlotsTotal;
	int					pointCacheSlotsUsed;
	int					pointCacheSlotsTotal;
	int					pointFacesRendered;
	int					pointHighPrecisionPasses;
	int					casterCount;
	int					alphaCasterCount;
	int					translucentCasterCount;
	int					staticCasterCount;
	int					dynamicCasterCount;
	int					rejectedCasterCount;
	int					expandedCasterCount;
	int					lodCasterTestCount;
	int					lodCasterRejectedCount;
	int					lodAlphaCasterRejectedCount;
	int					lodTranslucentCasterRejectedCount;
	int					casterRejectReasons[SHADOWMAP_CASTER_REJECT_COUNT];
	float				cpuRenderMilliseconds;
	float				cpuMaskMilliseconds;
	float				gpuSyncMilliseconds;
	float				gpuTimerMilliseconds;
	float				cpuPhaseMilliseconds[SHADOWMAP_TIMING_COUNT];
	float				gpuSyncPhaseMilliseconds[SHADOWMAP_TIMING_COUNT];
	float				gpuTimerPhaseMilliseconds[SHADOWMAP_TIMING_COUNT];
	float				minWorldTexelSize;
	float				maxWorldTexelSize;
	int					gpuTimerSamples;
	int					gpuTimerPending;
	int					gpuTimerPhaseSamples[SHADOWMAP_TIMING_COUNT];
	int					gpuTimerPhasePending[SHADOWMAP_TIMING_COUNT];
	int					unsupportedLights[SHADOWMAP_SUPPORT_COUNT];
} shadowMapStats_t;

static shadowMapStats_t	g_shadowMapStats;
static int				g_shadowMapLastReportFrame = -0x3fffffff;
static bool				g_shadowMapReportThisFrame = false;

static bool RB_ShadowMapVerboseReport( void ) {
	return r_shadowMapReport.GetInteger() >= 3 && g_shadowMapReportThisFrame;
}

typedef struct {
	bool				valid;
	bool				pointLight;
	bool				globalPass;
	bool				mapped;
	int					lightDefIndex;
	int					tileCount;
	int					atlasDiv;
	int					cascadeCount;
	int					casterCount;
	int					alphaCasterCount;
	int					translucentCasterCount;
	int					rejectedCasterCount;
	int					expandedCasterCount;
	int					lodCasterTestCount;
	int					lodCasterRejectedCount;
	int					lodAlphaCasterRejectedCount;
	int					lodTranslucentCasterRejectedCount;
	int					shadowSurfCount;
	int					interactionCount;
	float				cpuMilliseconds;
	float				gpuMilliseconds;
	float				worldTexelSize;
	// normalized UV rect of the captured light's slot inside the bound
	// texture (identity for scratch/point; a cell block inside the atlas)
	float				slotU0;
	float				slotV0;
	float				slotU1;
	float				slotV1;
} shadowMapDebugOverlayState_t;

static shadowMapDebugOverlayState_t	g_shadowMapDebugOverlayState;

typedef enum {
	SHADOWMAP_PASS_RESULT_MAPPED = 0,
	SHADOWMAP_PASS_RESULT_CACHE_REUSE,
	SHADOWMAP_PASS_RESULT_NO_SHADOW_SURFS,
	SHADOWMAP_PASS_RESULT_STENCIL_ONLY,
	SHADOWMAP_PASS_RESULT_RECEIVER_FALLBACK,
	SHADOWMAP_PASS_RESULT_RENDER_FAIL,
	SHADOWMAP_PASS_RESULT_MASK_FAIL,
	SHADOWMAP_PASS_RESULT_SCHEDULED_SKIP
} shadowMapPassResult_t;

static const int SHADOWMAP_CACHE_MAX_SLOTS = 16;

// point cubes carry six faces of color+depth per slot, so they get their own
// resolution knob instead of inheriting the projected atlas size
static int RB_ShadowMapPointSizeValue( void ) {
	return idMath::ClampInt( 128, 2048, r_shadowMapPointSize.GetInteger() );
}
static const int SHADOWMAP_LIGHT_HISTORY_SLOTS = 256;

typedef struct {
	bool						valid;
	int							lightIndex;
	int							passKind;
	int							lightClass;
	int							signature;
	int							tileSize;
	int							atlasDiv;
	int							cascadeCount;
	int							lastUsedFrame;
	int							lastUpdatedFrame;
	std::uint64_t				atlasStorageGeneration;
	projectedShadowMapState_t	state;
	// tile placement inside the shared projected atlas: cell coordinates in
	// the atlas grid and the block span (1 for single lights, atlasDiv for
	// CSM lights). Entries no longer own textures; eviction just frees cells.
	int							atlasCellX;
	int							atlasCellY;
	int							atlasCellSpan;
} projectedShadowMapCacheEntry_t;

typedef struct {
	bool						valid;
	int							lightIndex;
	int							passKind;
	int							lightClass;
	int							signature;
	int							size;
	bool						highPrecision;
	bool						depthCompare;
	// Receiver projection identity used when this cube was rendered. Also
	// used to recognise compatible allocation history when prioritising work.
	float						lightOrigin[3];
	float						farDistance;
	int							lastUsedFrame;
	int							lastUpdatedFrame;
	idImage *					colorImage;
	idImage *					depthImage;
	idRenderTexture *			renderTexture;
	std::uint64_t				colorStorageGeneration;
	std::uint64_t				depthStorageGeneration;
} pointShadowMapCacheEntry_t;

// Shared-domain ownership prepares every mapped pass before the first write to
// the main framebuffer.  The legacy scratch images are intentionally singular,
// so they cannot retain two non-cacheable (dynamic or perforated) maps until
// the later receiver commit.  These transaction-local slots provide the same
// scratch semantics with a distinct physical image per live pass.  Slots are
// reused across views and only allocate images which a view actually needs.
typedef struct {
	idImage *					depthImage;
	idRenderTexture *			renderTexture;
} sharedInteractionProjectedScratch_t;

typedef struct {
	idImage *					colorImage;
	idImage *					depthImage;
	idRenderTexture *			renderTexture;
} sharedInteractionPointScratch_t;

typedef struct {
	bool						valid;
	int							lightIndex;
	int							lastDynamicFrame;
	int							lastTouchedFrame;
} shadowMapLightHistory_t;

typedef struct {
	bool						active;
	GLuint						query;
	int							lightIndex;
	int							passKind;
	int							lightClass;
	int							phase;
	int							issuedFrame;
} shadowMapGpuTimerQuery_t;

static projectedShadowMapCacheEntry_t	g_projectedShadowMapCache[SHADOWMAP_CACHE_MAX_SLOTS];
static pointShadowMapCacheEntry_t		g_pointShadowMapCache[SHADOWMAP_CACHE_MAX_SLOTS];
static sharedInteractionProjectedScratch_t
	g_sharedInteractionProjectedScratch[SHADOWMAP_CACHE_MAX_SLOTS];
static sharedInteractionPointScratch_t
	g_sharedInteractionPointScratch[SHADOWMAP_CACHE_MAX_SLOTS];
static shadowMapLightHistory_t			g_shadowMapLightHistory[SHADOWMAP_LIGHT_HISTORY_SLOTS];
static const idRenderWorldLocal *		g_shadowMapCacheRenderWorld = NULL;
static unsigned int					g_shadowMapCacheMapFileCRC = 0;
static int							g_shadowMapCacheMapNameHash = 0;
static projectedShadowMapCacheEntry_t *	g_activeProjectedShadowMapCache = NULL;
static pointShadowMapCacheEntry_t *		g_activePointShadowMapCache = NULL;
static const int						SHADOWMAP_GPU_TIMER_QUERY_SLOTS = 64;
static shadowMapGpuTimerQuery_t			g_shadowMapGpuTimerQuerySlots[SHADOWMAP_GPU_TIMER_QUERY_SLOTS];
static int								g_shadowMapGpuTimerQueryCursor = 0;

// Outcome of the current light's GLOBAL shadow pass: when it mapped, the
// receiver-side state (projected state, bound depth textures) still reflects
// that pass, so translucent receivers can sample the same map (C3 parity
// with r_stencilTranslucentShadows). Reset per light in the interaction loop.
typedef enum {
	SHADOWMAP_GLOBAL_MAPPED_NONE = 0,
	SHADOWMAP_GLOBAL_MAPPED_PROJECTED,
	SHADOWMAP_GLOBAL_MAPPED_POINT
} shadowMapGlobalMapped_t;
static shadowMapGlobalMapped_t			g_shadowMapGlobalPassMapped = SHADOWMAP_GLOBAL_MAPPED_NONE;
static bool						g_shadowMapGlobalPassHybrid = false;
// A translucent-only GLOBAL ownership prepares the resource in the opaque
// light walk but cannot claim a mapped result until its ordered receiver draw
// actually succeeds. Keep that result pending so diagnostics match the final
// mapped-or-stencil presentation rather than the resource preparation alone.
static bool						g_shadowMapDeferredGlobalReportPending = false;
static shadowMapPassResult_t			g_shadowMapDeferredGlobalPassResult = SHADOWMAP_PASS_RESULT_MAPPED;

// Importance-ordered update admissions (M4): when r_shadowMapMaxUpdatesPerView
// constrains the frame, the budget goes to the most important stale lights
// instead of whichever came first in the light loop. Built once per view.
static const int						SHADOWMAP_MAX_ADMITTED_LIGHTS = 128;
static int								g_shadowMapAdmittedLightIndexes[SHADOWMAP_MAX_ADMITTED_LIGHTS];
static int								g_shadowMapAdmittedLightCount = 0;
static bool								g_shadowMapAdmissionsActive = false;

typedef enum {
	SHADOWMAP_SCHEDULE_UPDATE = 0,
	SHADOWMAP_SCHEDULE_REUSE,
	SHADOWMAP_SCHEDULE_FALLBACK
} shadowMapScheduleAction_t;

typedef struct {
	shadowMapScheduleAction_t		action;
	bool							cacheable;
	bool							cacheHit;
	bool							budgetReuse;
	int								signature;
	projectedShadowMapCacheEntry_t *	projectedEntry;
	pointShadowMapCacheEntry_t *		pointEntry;
} shadowMapSchedule_t;

void RB_ARB2_CreateDrawInteractions( const drawSurf_t *surf );
static int RB_CountShadowMapReceiverFallbackSurfaces( const drawSurf_t *surf, int *reasons );
static int RB_CountShadowMapReceiverWrappedCustomGLSLSurfaces( const drawSurf_t *surf );

static const char *RB_ShadowMapSupportReasonName( shadowMapLightSupportReason_t reason ) {
	switch ( reason ) {
	case SHADOWMAP_SUPPORT_OK:
		return "ok";
	case SHADOWMAP_SUPPORT_DISABLED:
		return "disabled";
	case SHADOWMAP_SUPPORT_SHADOWS_DISABLED:
		return "shadows-disabled";
	case SHADOWMAP_SUPPORT_NULL_LIGHT:
		return "null-light";
	case SHADOWMAP_SUPPORT_NO_SHADOWS_FLAG:
		return "noShadows-flag";
	case SHADOWMAP_SUPPORT_NO_DYNAMIC_SHADOWS_FLAG:
		return "noDynamicShadows-flag";
	case SHADOWMAP_SUPPORT_AMBIENT_LIGHT:
		return "ambient-light";
	case SHADOWMAP_SUPPORT_LIGHT_SHADER_NO_SHADOWS:
		return "lightShader-noShadows";
	case SHADOWMAP_SUPPORT_POINT_DISABLED:
		return "point-disabled";
	case SHADOWMAP_SUPPORT_TEXTURE_LIMIT:
		return "texture-limit";
	case SHADOWMAP_SUPPORT_NO_INTERACTIONS:
		return "no-interactions";
	case SHADOWMAP_SUPPORT_CUBEMAP_UNAVAILABLE:
		return "cubemap-unavailable";
	case SHADOWMAP_SUPPORT_RESOURCE_FAILURE:
		return "resource-failure";
	default:
		return "unknown";
	}
}

static const char *RB_ShadowMapPassName( shadowMapPassKind_t passKind ) {
	return ( passKind == SHADOWMAP_PASS_LOCAL ) ? "local" : "global";
}

static const char *RB_ShadowMapPassResultName( shadowMapPassResult_t result ) {
	switch ( result ) {
	case SHADOWMAP_PASS_RESULT_MAPPED:
		return "mapped";
	case SHADOWMAP_PASS_RESULT_CACHE_REUSE:
		return "cache-reuse";
	case SHADOWMAP_PASS_RESULT_NO_SHADOW_SURFS:
		return "no-shadow-surfs";
	case SHADOWMAP_PASS_RESULT_STENCIL_ONLY:
		return "stencil-only";
	case SHADOWMAP_PASS_RESULT_RECEIVER_FALLBACK:
		return "receiver-fallback";
	case SHADOWMAP_PASS_RESULT_RENDER_FAIL:
		return "render-fail";
	case SHADOWMAP_PASS_RESULT_MASK_FAIL:
		return "mask-fail";
	case SHADOWMAP_PASS_RESULT_SCHEDULED_SKIP:
		return "scheduled-skip";
	default:
		return "unknown";
	}
}

static const char *RB_ShadowMapReceiverFallbackReasonName( const shadowMapReceiverFallbackReason_t reason ) {
	switch ( reason ) {
	case SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE:
		return "invalid";
	case SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL:
		return "customGLSL";
	case SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY:
		return "generatedSkinned";
	default:
		return "none";
	}
}

static const char *RB_ShadowMapTimingPhaseName( const shadowMapTimingPhase_t phase ) {
	switch ( phase ) {
	case SHADOWMAP_TIMING_MAP_RENDER:
		return "map-render";
	case SHADOWMAP_TIMING_CACHE_REUSE:
		return "cache-reuse";
	case SHADOWMAP_TIMING_MASK_PASS:
		return "mask-pass";
	case SHADOWMAP_TIMING_RECEIVER_FALLBACK:
		return "receiver-fallback";
	case SHADOWMAP_TIMING_STENCIL_FALLBACK:
		return "stencil-fallback";
	default:
		return "unknown";
	}
}

static shadowMapLightClass_t RB_ShadowMapLightClass( const viewLight_t *vLight ) {
	return R_ClassifyShadowMapLight( vLight ).lightClass;
}

static const char *RB_ShadowMapLightClassNameByClass( const int lightClass ) {
	return R_ShadowMapLightClassName( static_cast<shadowMapLightClass_t>( lightClass ) );
}

typedef struct shadowMapModernPackedJoin_s {
	int descriptorCount;
	int lightDescriptorIndex;
	int shadowDescriptorIndex;
	rendererModernLightType_t lightType;
	int flags;
	int shadowPolicy;
	int shadowFallbackReason;
	const char *debugName;
} shadowMapModernPackedJoin_t;

static const char *RB_ModernPackedLightTypeName( const rendererModernLightType_t type ) {
	switch ( type ) {
	case RENDERER_MODERN_LIGHT_POINT:
		return "point";
	case RENDERER_MODERN_LIGHT_PROJECTED:
		return "projected";
	case RENDERER_MODERN_LIGHT_FOG:
		return "fog";
	case RENDERER_MODERN_LIGHT_AMBIENT:
		return "ambient";
	case RENDERER_MODERN_LIGHT_BLEND:
		return "blend";
	case RENDERER_MODERN_LIGHT_SPECIAL:
		return "special";
	default:
		return "unknown";
	}
}

static const rendererModernShadowDescriptor_t *RB_ModernClusteredShadowDescriptorByIndex( const int descriptorIndex ) {
	if ( descriptorIndex < 0 ) {
		return NULL;
	}

	const int descriptorCount = R_ModernClusteredLighting_NumShadowDescriptors();
	for ( int i = 0; i < descriptorCount; i++ ) {
		const rendererModernShadowDescriptor_t *descriptor = R_ModernClusteredLighting_ShadowDescriptor( i );
		if ( descriptor != NULL && descriptor->descriptorIndex == descriptorIndex ) {
			return descriptor;
		}
	}
	return NULL;
}

static shadowMapModernPackedJoin_t RB_ShadowMapModernPackedJoinForLight( const int lightDefIndex, const int preferredShadowDescriptorIndex ) {
	shadowMapModernPackedJoin_t joined;
	joined.descriptorCount = 0;
	joined.lightDescriptorIndex = -1;
	joined.shadowDescriptorIndex = -1;
	joined.lightType = RENDERER_MODERN_LIGHT_SPECIAL;
	joined.flags = 0;
	joined.shadowPolicy = MODERN_SHADOW_POLICY_NONE;
	joined.shadowFallbackReason = MODERN_SHADOW_FALLBACK_NONE;
	joined.debugName = "<none>";

	if ( lightDefIndex < 0 ) {
		return joined;
	}

	const int lightDescriptorCount = R_ModernClusteredLighting_NumLightDescriptors();
	for ( int i = 0; i < lightDescriptorCount; i++ ) {
		const rendererModernLightDescriptor_t *descriptor = R_ModernClusteredLighting_LightDescriptor( i );
		if ( descriptor == NULL || descriptor->lightDefIndex != lightDefIndex ) {
			continue;
		}

		joined.descriptorCount++;
		bool chooseDescriptor = joined.lightDescriptorIndex < 0;
		if ( !chooseDescriptor && preferredShadowDescriptorIndex >= 0 ) {
			chooseDescriptor = joined.shadowDescriptorIndex != preferredShadowDescriptorIndex
				&& descriptor->shadowDescriptorIndex == preferredShadowDescriptorIndex;
		}
		if ( !chooseDescriptor && joined.shadowPolicy != MODERN_SHADOW_POLICY_MAPPED ) {
			chooseDescriptor = descriptor->shadowPolicy == MODERN_SHADOW_POLICY_MAPPED;
		}
		if ( !chooseDescriptor ) {
			continue;
		}

		joined.lightDescriptorIndex = descriptor->descriptorIndex;
		joined.shadowDescriptorIndex = descriptor->shadowDescriptorIndex;
		joined.lightType = descriptor->type;
		joined.flags = descriptor->flags;
		joined.shadowPolicy = descriptor->shadowPolicy;
		joined.shadowFallbackReason = descriptor->shadowFallbackReason;
		joined.debugName = ( descriptor->debugName[0] != '\0' ) ? descriptor->debugName : "<unnamed>";
	}

	return joined;
}

static const char *RB_ShadowMapCasterRejectReasonName( const shadowMapCasterRejectReason_t reason ) {
	switch ( reason ) {
	case SHADOWMAP_CASTER_REJECT_NO_SHADOW:
		return "noShadow";
	case SHADOWMAP_CASTER_REJECT_VIEW_ONLY:
		return "viewOnly";
	case SHADOWMAP_CASTER_REJECT_DEPTH_HACK:
		return "depthHack";
	case SHADOWMAP_CASTER_REJECT_NO_GEOMETRY:
		return "noGeometry";
	case SHADOWMAP_CASTER_REJECT_POINT_LIGHT_EMITTER:
		return "pointEmitter";
	case SHADOWMAP_CASTER_REJECT_TRANSLUCENT_DISABLED:
		return "translucentDisabled";
	case SHADOWMAP_CASTER_REJECT_TRANSLUCENT_UNSUPPORTED:
		return "translucentUnsupported";
	case SHADOWMAP_CASTER_REJECT_SURFACE_NO_SHADOW:
		return "surfaceNoShadow";
	case SHADOWMAP_CASTER_REJECT_DEDICATED_COLLISION:
		return "dedicatedCollision";
	case SHADOWMAP_CASTER_REJECT_GUI:
		return "gui";
	case SHADOWMAP_CASTER_REJECT_SUBVIEW:
		return "subview";
	case SHADOWMAP_CASTER_REJECT_AREA_DISCONNECTED:
		return "areaDisconnected";
	case SHADOWMAP_CASTER_REJECT_SPECTRUM_MISMATCH:
		return "spectrumMismatch";
	case SHADOWMAP_CASTER_REJECT_LOD:
		return "shadowLOD";
	default:
		return "unknown";
	}
}

/*
=============
RB_ShadowMapDebugMode
=============
*/
static shadowMapDebugMode_t RB_ShadowMapDebugMode( void ) {
	return static_cast<shadowMapDebugMode_t>( idMath::ClampInt( SHADOWMAP_DEBUGMODE_OFF, SHADOWMAP_DEBUGMODE_COUNT - 1, r_shadowMapDebugMode.GetInteger() ) );
}

static bool RB_ShadowMapHashedAlphaEnabled( void ) {
	return r_shadowMapHashedAlpha.GetBool();
}

static bool RB_ShadowMapDepthCompareEnabled( void ) {
	const shadowMapDebugMode_t debugMode = RB_ShadowMapDebugMode();
	if ( debugMode == SHADOWMAP_DEBUGMODE_ATLAS
		|| debugMode == SHADOWMAP_DEBUGMODE_PROJECTED_DEPTH
		|| debugMode == SHADOWMAP_DEBUGMODE_COMPARE_DELTA ) {
		return false;
	}
	// PCSS-lite needs raw depth reads for its blocker search, which comparison
	// samplers cannot provide; selecting filter mode 2 implies the manual path.
	if ( idMath::ClampInt( 0, 2, r_shadowMapFilterMode.GetInteger() ) == 2 ) {
		return false;
	}
	return r_shadowMapDepthCompare.GetBool();
}

static bool RB_PointShadowMapHighPrecisionEnabled( void ) {
	return r_shadowMapPointHighPrecision.GetBool();
}

static bool RB_PointShadowMapDepthCompareRequested( void ) {
	if ( RB_ShadowMapDebugMode() == SHADOWMAP_DEBUGMODE_COMPARE_DELTA ) {
		return false;
	}
	return r_shadowMapPointDepthCompare.GetBool() &&
		glConfig.GLSLProgramAvailable &&
		glConfig.cubeMapAvailable &&
		glConfig.glVersion >= 3.0f;
}

static bool RB_PointShadowMapDepthCompareEnabled( void ) {
	if ( !RB_PointShadowMapDepthCompareRequested() ) {
		return false;
	}
	if ( g_pointShadowMapDepthCompareAvailableGeneration == tr.videoRestartCount && !g_pointShadowMapDepthCompareAvailable ) {
		return false;
	}
	return true;
}

static void RB_PointShadowMapDisableDepthCompareForGeneration( void ) {
	g_pointShadowMapDepthCompareAvailable = false;
	g_pointShadowMapDepthCompareAvailableGeneration = tr.videoRestartCount;
}

static float RB_PointShadowMapDepthModeValue( void ) {
	return RB_PointShadowMapHighPrecisionEnabled() ? 1.0f : 0.0f;
}

// GL_DEPTH_CLAMP (core 3.2 / ARB_depth_clamp / NV_depth_clamp) lets point
// casters between the light and the cube-face near plane rasterize with
// clamped depth instead of being clipped away; their color-packed radial
// depth stays correct, so near-caster shadows are preserved.
static bool g_shadowMapDepthClampAvailable = false;
static int g_shadowMapDepthClampGeneration = -1;

static bool RB_ShadowMapDepthClampAvailable( void ) {
	if ( g_shadowMapDepthClampGeneration != tr.videoRestartCount ) {
		g_shadowMapDepthClampGeneration = tr.videoRestartCount;
		g_shadowMapDepthClampAvailable = glConfig.glVersion >= 3.2f
			|| GLCapabilityProbe_HasExtension( "GL_ARB_depth_clamp" )
			|| GLCapabilityProbe_HasExtension( "GL_NV_depth_clamp" );
	}
	return g_shadowMapDepthClampAvailable;
}

static bool RB_TranslucentShadowMomentsRequested( void ) {
	return r_shadowMapTranslucentMoments.GetBool();
}

static bool RB_TranslucentShadowMomentsSupported( void ) {
	return RB_TranslucentShadowMomentsRequested() &&
		glConfig.GLSLProgramAvailable &&
		glConfig.maxTextureUnits >= 9 &&
		glConfig.maxTextureImageUnits >= 9 &&
		glConfig.maxDrawBuffers >= 3 &&
		glConfig.maxColorAttachments >= 3;
}

static bool RB_TranslucentShadowMomentImagesReady( idImage *const images[3] ) {
	return images[0] != NULL && images[1] != NULL && images[2] != NULL;
}

// The third moment sampler occupies unit 8, beyond the compatibility
// renderer's eight tracked units. Never expose it to idImage::Bind/BindNull
// or client texture-coordinate state. Generated moment images are loaded
// through a tracked unit, then bound to the extra GLSL sampler explicitly.
static void RB_ShadowMapBindMomentTexture( const int unit, const GLenum target, idImage *image ) {
	if ( unit >= glConfig.maxTextureImageUnits ) {
		return;
	}
	if ( unit < MAX_MULTITEXTURE_UNITS && unit < glConfig.maxTextureUnits ) {
		GL_SelectTextureNoClient( unit );
		if ( image != NULL ) {
			image->Bind();
		} else {
			globalImages->BindNull();
		}
		return;
	}
	GL_SelectTextureNoClient( 0 );
	if ( image != NULL ) {
		image->Bind();
	}
	glActiveTextureARB( GL_TEXTURE0_ARB + unit );
	glBindTexture( target, image != NULL ? image->GetDeviceHandle() : 0 );
	glActiveTextureARB( GL_TEXTURE0_ARB );
}

static bool RB_ProjectedTranslucentShadowEnabled( void ) {
	return RB_TranslucentShadowMomentsSupported() && g_projectedTranslucentShadowPassReady && RB_TranslucentShadowMomentImagesReady( g_translucentShadowMomentImages );
}

static bool RB_PointTranslucentShadowEnabled( void ) {
	return RB_TranslucentShadowMomentsSupported() && g_pointTranslucentShadowPassReady && RB_TranslucentShadowMomentImagesReady( g_pointTranslucentShadowMomentImages );
}

static void RB_ShadowMapFillTextureBinding( rendererShadowTextureBinding_t &binding, idImage *image, const unsigned int target, const int width, const int height, const bool ready ) {
	memset( &binding, 0, sizeof( binding ) );
	binding.target = target;
	binding.width = Max( 0, width );
	binding.height = Max( 0, height );
	binding.ready = ready && image != NULL && image->IsLoaded() && image->GetDeviceHandle() != 0;
	if ( binding.ready ) {
		binding.texture = image->GetDeviceHandle();
		if ( binding.width <= 0 ) {
			binding.width = image->GetUploadWidth();
		}
		if ( binding.height <= 0 ) {
			binding.height = image->GetUploadHeight();
		}
	}
}

// A point cube is not a shared atlas: the modern GL shader has one
// samplerCube and must see the exact cache image which the classic backend
// most recently selected.  Allocation alone is not evidence that the image
// contains current, globally complete shadow data.
static bool RB_ShadowMapActivePointCacheContentReady( void ) {
	pointShadowMapCacheEntry_t *entry = g_activePointShadowMapCache;
	if ( entry == NULL || !entry->valid ) {
		return false;
	}

	int slotIndex = -1;
	for ( int i = 0; i < SHADOWMAP_CACHE_MAX_SLOTS; ++i ) {
		if ( entry == &g_pointShadowMapCache[i] ) {
			slotIndex = i;
			break;
		}
	}
	const int slotLimit = idMath::ClampInt( 0, SHADOWMAP_CACHE_MAX_SLOTS, r_shadowMapPointCacheSize.GetInteger() );
	if ( slotIndex < 0 || slotIndex >= slotLimit
			|| entry->passKind != static_cast<int>( SHADOWMAP_PASS_GLOBAL )
			|| entry->lastUpdatedFrame < 0 || entry->lastUpdatedFrame > tr.frameCount
			|| entry->size != RB_ShadowMapPointSizeValue()
			|| entry->highPrecision != RB_PointShadowMapHighPrecisionEnabled()
			|| entry->depthCompare != RB_PointShadowMapDepthCompareEnabled()
			|| !( entry->farDistance > 0.0f && entry->farDistance < 1.0e30f )
			|| FLOAT_IS_NAN( entry->lightOrigin[0] )
			|| FLOAT_IS_NAN( entry->lightOrigin[1] )
			|| FLOAT_IS_NAN( entry->lightOrigin[2] )
			|| idMath::Fabs( entry->lightOrigin[0] ) >= 1.0e30f
			|| idMath::Fabs( entry->lightOrigin[1] ) >= 1.0e30f
			|| idMath::Fabs( entry->lightOrigin[2] ) >= 1.0e30f
			|| entry->colorImage == NULL || entry->depthImage == NULL || entry->renderTexture == NULL
			|| entry->colorImage != g_pointShadowMapColorImage
			|| entry->depthImage != g_pointShadowMapDepthImage
			|| entry->renderTexture != g_pointShadowMapRenderTexture
			|| entry->colorStorageGeneration == 0
			|| entry->depthStorageGeneration == 0
			|| entry->colorImage->GetStorageGeneration() != entry->colorStorageGeneration
			|| entry->depthImage->GetStorageGeneration() != entry->depthStorageGeneration
			|| entry->colorImage->IsDefaulted() || entry->depthImage->IsDefaulted()
			|| entry->colorImage->GetOpts().textureType != TT_CUBIC
			|| entry->depthImage->GetOpts().textureType != TT_CUBIC
			|| entry->colorImage->GetOpts().format
				!= ( entry->highPrecision ? FMT_RGBA16F : FMT_RGBA8 )
			|| entry->depthImage->GetOpts().format != FMT_DEPTH
			|| entry->renderTexture->GetWidth() != entry->size
			|| entry->renderTexture->GetHeight() != entry->size ) {
		return false;
	}

	return entry->colorImage->IsLoaded() && entry->colorImage->GetDeviceHandle() != 0
		&& entry->depthImage->IsLoaded() && entry->depthImage->GetDeviceHandle() != 0;
}

bool RB_ShadowMapTextureBindings( rendererShadowTextureBindings_t &bindings ) {
	memset( &bindings, 0, sizeof( bindings ) );
	bindings.pointAtlasLightIndex = -1;
	bindings.pointAtlasContentFrame = -1;
	bindings.projectedDepthCompare = RB_ShadowMapDepthCompareEnabled();
	bindings.pointDepthCompare = RB_PointShadowMapDepthCompareEnabled();
	bindings.pointHighPrecision = RB_PointShadowMapHighPrecisionEnabled();
	bindings.translucentDensity = r_shadowMapTranslucentDensity.GetFloat();
	bindings.translucentFilterRadius = r_shadowMapTranslucentFilterRadius.GetFloat();
	bindings.translucentMinVariance = r_shadowMapTranslucentMinVariance.GetFloat();
	bindings.translucentBleedReduction = r_shadowMapTranslucentBleedReduction.GetFloat();

	const int projectedWidth = g_shadowMapRenderTexture != NULL ? g_shadowMapRenderTexture->GetWidth() : ( g_shadowMapDepthImage != NULL ? g_shadowMapDepthImage->GetUploadWidth() : 0 );
	const int projectedHeight = g_shadowMapRenderTexture != NULL ? g_shadowMapRenderTexture->GetHeight() : ( g_shadowMapDepthImage != NULL ? g_shadowMapDepthImage->GetUploadHeight() : 0 );
	RB_ShadowMapFillTextureBinding(
		bindings.projectedAtlas,
		g_shadowMapDepthImage,
		GL_TEXTURE_2D,
		projectedWidth,
		projectedHeight,
		g_shadowMapDepthImage != NULL && projectedWidth > 0 && projectedHeight > 0 );
	bindings.projectedAtlasReady = bindings.projectedAtlas.ready;

	const int persistentWidth = g_shadowMapAtlasRenderTexture != NULL ? g_shadowMapAtlasRenderTexture->GetWidth() : 0;
	const int persistentHeight = g_shadowMapAtlasRenderTexture != NULL ? g_shadowMapAtlasRenderTexture->GetHeight() : 0;
	RB_ShadowMapFillTextureBinding(
		bindings.projectedPersistentAtlas,
		g_shadowMapAtlasDepthImage,
		GL_TEXTURE_2D,
		persistentWidth,
		persistentHeight,
		g_shadowMapAtlasDepthImage != NULL && persistentWidth > 0 && persistentHeight > 0 );
	bindings.projectedPersistentAtlasReady = bindings.projectedPersistentAtlas.ready;

	// The modern GL programs use a regular samplerCube and decode the manual
	// color-depth encoding. Keep that binding independent of the classic
	// receiver's hardware-compare choice; sampling the depth cube as packed RG
	// corrupts the decoded value when r_shadowMapPointDepthCompare is enabled.
	idImage *pointAtlasImage = g_pointShadowMapColorImage;
	const int pointWidth = g_pointShadowMapRenderTexture != NULL ? g_pointShadowMapRenderTexture->GetWidth() : ( pointAtlasImage != NULL ? pointAtlasImage->GetUploadWidth() : 0 );
	const int pointHeight = g_pointShadowMapRenderTexture != NULL ? g_pointShadowMapRenderTexture->GetHeight() : ( pointAtlasImage != NULL ? pointAtlasImage->GetUploadHeight() : 0 );
	RB_ShadowMapFillTextureBinding(
		bindings.pointAtlas,
		pointAtlasImage,
		GL_TEXTURE_CUBE_MAP,
		pointWidth,
		pointHeight,
		RB_ShadowMapActivePointCacheContentReady() && pointWidth > 0 && pointHeight > 0 );
	bindings.pointAtlasReady = bindings.pointAtlas.ready;
	if ( bindings.pointAtlasReady ) {
		bindings.pointAtlasLightIndex = g_activePointShadowMapCache->lightIndex;
		bindings.pointAtlasSignature = g_activePointShadowMapCache->signature;
		bindings.pointAtlasContentFrame = g_activePointShadowMapCache->lastUpdatedFrame;
	}

	const bool projectedMomentsEnabled = RB_ProjectedTranslucentShadowEnabled();
	const bool pointMomentsEnabled = RB_PointTranslucentShadowEnabled();
	bindings.projectedMomentsReady = projectedMomentsEnabled;
	bindings.pointMomentsReady = pointMomentsEnabled;
	for ( int i = 0; i < RENDERER_SHADOW_TEXTURE_MOMENT_COUNT; ++i ) {
		RB_ShadowMapFillTextureBinding(
			bindings.projectedMoments[i],
			g_translucentShadowMomentImages[i],
			GL_TEXTURE_2D,
			g_translucentShadowMapRenderTexture != NULL ? g_translucentShadowMapRenderTexture->GetWidth() : 0,
			g_translucentShadowMapRenderTexture != NULL ? g_translucentShadowMapRenderTexture->GetHeight() : 0,
			projectedMomentsEnabled );
		bindings.projectedMomentsReady = bindings.projectedMomentsReady && bindings.projectedMoments[i].ready;

		RB_ShadowMapFillTextureBinding(
			bindings.pointMoments[i],
			g_pointTranslucentShadowMomentImages[i],
			GL_TEXTURE_CUBE_MAP,
			g_pointTranslucentShadowMapRenderTexture != NULL ? g_pointTranslucentShadowMapRenderTexture->GetWidth() : 0,
			g_pointTranslucentShadowMapRenderTexture != NULL ? g_pointTranslucentShadowMapRenderTexture->GetHeight() : 0,
			pointMomentsEnabled );
		bindings.pointMomentsReady = bindings.pointMomentsReady && bindings.pointMoments[i].ready;
	}
	return bindings.projectedAtlasReady || bindings.pointAtlasReady || bindings.projectedMomentsReady || bindings.pointMomentsReady;
}

/*
=============
RB_ShadowMapDebugModeName
=============
*/
static const char *RB_ShadowMapDebugModeName( shadowMapDebugMode_t mode ) {
	switch ( mode ) {
	case SHADOWMAP_DEBUGMODE_OFF:
		return "off";
	case SHADOWMAP_DEBUGMODE_ATLAS:
		return "atlas-depth";
	case SHADOWMAP_DEBUGMODE_CASCADE_INDEX:
		return "cascade-index";
	case SHADOWMAP_DEBUGMODE_PROJECTED_UV:
		return "projected-uv";
	case SHADOWMAP_DEBUGMODE_PROJECTED_DEPTH:
		return "projected-depth";
	case SHADOWMAP_DEBUGMODE_PROJECTED_W:
		return "projected-w";
	case SHADOWMAP_DEBUGMODE_INVALID_MASK:
		return "invalid-mask";
	case SHADOWMAP_DEBUGMODE_BIAS_HEATMAP:
		return "bias-heatmap";
	case SHADOWMAP_DEBUGMODE_BIAS_OFF:
		return "bias-off";
	case SHADOWMAP_DEBUGMODE_PCF_OFF:
		return "pcf-off";
	case SHADOWMAP_DEBUGMODE_CASTER_OFFSET_OFF:
		return "caster-offset-off";
	case SHADOWMAP_DEBUGMODE_RECEIVER_PLANE_BIAS_OFF:
		return "receiver-plane-bias-off";
	case SHADOWMAP_DEBUGMODE_COMPARE_DELTA:
		return "compare-depth-delta";
	case SHADOWMAP_DEBUGMODE_RECEIVER_ELIGIBILITY:
		return "receiver-eligibility";
	case SHADOWMAP_DEBUGMODE_RECEIVER_FALLBACK_REASON:
		return "receiver-fallback-reason";
	default:
		return "unknown";
	}
}

static bool RB_ShadowMapDebugModeIs( shadowMapDebugMode_t mode ) {
	return RB_ShadowMapDebugMode() == mode;
}

static bool RB_ShadowMapReceiverDebugMode( void ) {
	const shadowMapDebugMode_t mode = RB_ShadowMapDebugMode();
	return mode == SHADOWMAP_DEBUGMODE_RECEIVER_ELIGIBILITY || mode == SHADOWMAP_DEBUGMODE_RECEIVER_FALLBACK_REASON;
}

static float RB_ShadowMapPolygonFactor( void ) {
	return RB_ShadowMapDebugModeIs( SHADOWMAP_DEBUGMODE_CASTER_OFFSET_OFF ) ? 0.0f : r_shadowMapPolygonFactor.GetFloat();
}

static float RB_ShadowMapPolygonOffset( void ) {
	return RB_ShadowMapDebugModeIs( SHADOWMAP_DEBUGMODE_CASTER_OFFSET_OFF ) ? 0.0f : r_shadowMapPolygonOffset.GetFloat();
}

// The shadow casters write their depth from the fragment shader, which makes
// glPolygonOffset inert; the equivalent slope-scale offset is applied by the
// caster shaders instead. The units term is pre-scaled here to one resolvable
// step of the depth buffer to keep glPolygonOffset's parameter semantics.
static void RB_ShadowMapSetCasterDepthOffsetUniform( GLint location ) {
	if ( location < 0 ) {
		return;
	}
	const float oneDepthStep = 1.0f / 16777216.0f;
	glUniform2fARB( location, RB_ShadowMapPolygonFactor(), RB_ShadowMapPolygonOffset() * oneDepthStep );
}

// Published for the front-end stencil-volume elision policy: the front end
// must keep generating stencil volumes until the backend has proven it can
// create shadow-map resources in this video generation.
static int g_shadowMapProjectedResourcesOkGeneration = -1;
static int g_shadowMapPointResourcesOkGeneration = -1;

bool RB_ShadowMapResourcesKnownGood( bool pointLight ) {
	const int generation = pointLight ? g_shadowMapPointResourcesOkGeneration : g_shadowMapProjectedResourcesOkGeneration;
	return generation == tr.videoRestartCount;
}

// Shadow casters render in light space, where the mirrored-view logic in
// GL_Cull does not apply. idTech4 winding makes the engine-front (visible)
// side of a front-sided material the GL_BACK face, so culling GL_FRONT stores
// the light-facing, near shell. The automatic mode only does that for sealed
// hulls. Open, non-manifold, or otherwise uncertain geometry is rendered
// two-sided: either authored winding can then contribute and props cannot
// silently disappear from the depth map. Storing the far shell is deliberately
// avoided because mesh thickness turns into contact-shadow detachment.
//   r_shadowMapCasterCulling 0: always two-sided
//   r_shadowMapCasterCulling 1: always store the light-facing near shell
//   r_shadowMapCasterCulling 2: automatic (near shell for perfect hulls whose
//     bounds do not contain the light and whose transform preserves winding,
//     two-sided otherwise)
// The authored material orientation is honored whenever one-sided culling is
// active. Mode 0 and AUTO's conservative fallbacks intentionally override it.
static int g_shadowMapCasterCullApplied = -1; // 0 = culling disabled, else the GLenum face

static void RB_ShadowMapApplyCasterCull( const idMaterial *shader,
		const srfTriangles_t *casterGeo = NULL,
		const float *modelMatrix = NULL ) {
	const int mode = idMath::ClampInt( 0, 2, r_shadowMapCasterCulling.GetInteger() );
	const int materialCull = ( shader != NULL ) ? shader->GetCullType() : CT_FRONT_SIDED;
	const bool automaticTwoSided = mode == 2
		&& ( casterGeo == NULL || !casterGeo->perfectHull
			|| R_ShadowMapCasterTransformNeedsTwoSided( modelMatrix )
			|| R_ShadowMapLightOriginInsideCasterBounds( backEnd.vLight,
				modelMatrix, casterGeo->bounds[0].ToFloatPtr(),
				casterGeo->bounds[1].ToFloatPtr() ) );
	int desired = 0;
	if ( mode != 0 && !automaticTwoSided && materialCull != CT_TWO_SIDED ) {
		GLenum cullFace = GL_FRONT;
		if ( materialCull == CT_BACK_SIDED ) {
			cullFace = ( cullFace == GL_FRONT ) ? GL_BACK : GL_FRONT;
		}
		desired = static_cast<int>( cullFace );
	}
	if ( desired == g_shadowMapCasterCullApplied ) {
		return;
	}
	g_shadowMapCasterCullApplied = desired;
	if ( desired == 0 ) {
		glDisable( GL_CULL_FACE );
		return;
	}
	glEnable( GL_CULL_FACE );
	glCullFace( static_cast<GLenum>( desired ) );
}

static void RB_ShadowMapResetCasterCull( void ) {
	g_shadowMapCasterCullApplied = -1;
	RB_ShadowMapApplyCasterCull( NULL );
}

// Subview (mirror/remote camera) shadow updates were previously invisible in
// every report; the accumulator survives per-view stats resets and surfaces
// in the main view's SM metrics line.
static int g_shadowMapSubviewUpdates = 0;
static int g_shadowMapSubviewReusePasses = 0;
static int g_shadowMapSubviewFallbackPasses = 0;

// A shadow-map render failure on a light whose stencil volumes were elided
// would otherwise repeat every frame with an empty fallback; the sticky flag
// restores volume generation for that light from the next frame on.
static void RB_ShadowMapMarkStencilFallbackSticky( const viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->lightDef == NULL || vLight->lightDef->shadowMapStencilFallbackSticky ) {
		return;
	}
	vLight->lightDef->shadowMapStencilFallbackSticky = true;
	common->DPrintf( "shadow map pass failed for lightDef %d; restoring stencil volume generation\n", vLight->lightDef->index );
}

static int RB_CountDrawSurfChain( const drawSurf_t *surf ) {
	int count = 0;
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		count++;
	}
	return count;
}

static void RB_ShadowMapStatsRecordWorldTexelSize( const float worldTexelSize ) {
	if ( worldTexelSize <= 0.0f ) {
		return;
	}
	if ( g_shadowMapStats.minWorldTexelSize <= 0.0f || worldTexelSize < g_shadowMapStats.minWorldTexelSize ) {
		g_shadowMapStats.minWorldTexelSize = worldTexelSize;
	}
	g_shadowMapStats.maxWorldTexelSize = Max( g_shadowMapStats.maxWorldTexelSize, worldTexelSize );
}

static int RB_ShadowMapLightIndex( const viewLight_t *vLight );

static void RB_ShadowMapStatsAccumulateCasterPolicy( const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return;
	}

	g_shadowMapStats.casterCount += vLight->shadowMapCasterCount;
	g_shadowMapStats.alphaCasterCount += vLight->shadowMapAlphaCasterCount;
	g_shadowMapStats.translucentCasterCount += vLight->shadowMapTranslucentCasterCount;
	g_shadowMapStats.staticCasterCount += vLight->shadowMapStaticCasterCount;
	g_shadowMapStats.dynamicCasterCount += vLight->shadowMapDynamicCasterCount;
	g_shadowMapStats.rejectedCasterCount += vLight->shadowMapRejectedCasterCount;
	g_shadowMapStats.expandedCasterCount += vLight->shadowMapExpandedCasterCount;
	g_shadowMapStats.lodCasterTestCount += vLight->shadowMapLODTestCount;
	g_shadowMapStats.lodCasterRejectedCount += vLight->shadowMapLODRejectedCount;
	g_shadowMapStats.lodAlphaCasterRejectedCount += vLight->shadowMapLODAlphaRejectedCount;
	g_shadowMapStats.lodTranslucentCasterRejectedCount += vLight->shadowMapLODTranslucentRejectedCount;
	for ( int i = 0; i < SHADOWMAP_CASTER_REJECT_COUNT; i++ ) {
		g_shadowMapStats.casterRejectReasons[i] += vLight->shadowMapRejectedCasterReasons[i];
	}
}

static bool RB_ShadowMapGpuTimerQueriesAvailable( void ) {
	if ( !r_shadowMapGpuTimerQueries.GetBool() ) {
		return false;
	}
	if ( !( GLEW_ARB_timer_query || GLEW_VERSION_3_3 ) ) {
		return false;
	}
	return glGenQueries != NULL &&
		glBeginQuery != NULL &&
		glEndQuery != NULL &&
		glGetQueryObjectuiv != NULL &&
		glGetQueryObjectui64v != NULL;
}

static bool RB_ShadowMapTimingPhaseValid( const int phase ) {
	return phase >= 0 && phase < SHADOWMAP_TIMING_COUNT;
}

static void RB_ShadowMapRecordGpuTimerResult( const shadowMapGpuTimerQuery_t &slot, const float milliseconds, const bool report ) {
	g_shadowMapStats.gpuTimerMilliseconds += milliseconds;
	g_shadowMapStats.gpuTimerSamples++;
	if ( RB_ShadowMapTimingPhaseValid( slot.phase ) ) {
		g_shadowMapStats.gpuTimerPhaseMilliseconds[slot.phase] += milliseconds;
		g_shadowMapStats.gpuTimerPhaseSamples[slot.phase]++;
	}

	if ( report && idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) >= 2 && g_shadowMapReportThisFrame ) {
		common->Printf(
			"SM gpu-time light=%d pass=%s class=%s phase=%s frame=%d elapsed=%.3fms\n",
			slot.lightIndex,
			RB_ShadowMapPassName( static_cast<shadowMapPassKind_t>( slot.passKind ) ),
			RB_ShadowMapLightClassNameByClass( slot.lightClass ),
			RB_ShadowMapTimingPhaseName( RB_ShadowMapTimingPhaseValid( slot.phase ) ? static_cast<shadowMapTimingPhase_t>( slot.phase ) : SHADOWMAP_TIMING_COUNT ),
			slot.issuedFrame,
			milliseconds );
	}
}

static void RB_ShadowMapHarvestGpuTimerQueries( void ) {
	if ( !RB_ShadowMapGpuTimerQueriesAvailable() ) {
		return;
	}

	int pending = 0;
	memset( g_shadowMapStats.gpuTimerPhasePending, 0, sizeof( g_shadowMapStats.gpuTimerPhasePending ) );
	for ( int i = 0; i < SHADOWMAP_GPU_TIMER_QUERY_SLOTS; i++ ) {
		shadowMapGpuTimerQuery_t &slot = g_shadowMapGpuTimerQuerySlots[i];
		if ( !slot.active || slot.query == 0 ) {
			continue;
		}

		GLuint available = GL_FALSE;
		glGetQueryObjectuiv( slot.query, GL_QUERY_RESULT_AVAILABLE, &available );
		if ( available == GL_FALSE ) {
			pending++;
			if ( RB_ShadowMapTimingPhaseValid( slot.phase ) ) {
				g_shadowMapStats.gpuTimerPhasePending[slot.phase]++;
			}
			continue;
		}

		GLuint64 elapsedNanoseconds = 0;
		glGetQueryObjectui64v( slot.query, GL_QUERY_RESULT, &elapsedNanoseconds );
		const float milliseconds = static_cast<float>( static_cast<double>( elapsedNanoseconds ) / 1000000.0 );
		RB_ShadowMapRecordGpuTimerResult( slot, milliseconds, true );

		slot.active = false;
	}

	g_shadowMapStats.gpuTimerPending = pending;
}

static shadowMapGpuTimerQuery_t *RB_ShadowMapBeginGpuTimerQuery( const viewLight_t *vLight, const shadowMapPassKind_t passKind, const shadowMapTimingPhase_t phase ) {
	if ( !RB_ShadowMapGpuTimerQueriesAvailable() ) {
		return NULL;
	}

	for ( int attempt = 0; attempt < SHADOWMAP_GPU_TIMER_QUERY_SLOTS; attempt++ ) {
		shadowMapGpuTimerQuery_t &slot = g_shadowMapGpuTimerQuerySlots[g_shadowMapGpuTimerQueryCursor];
		g_shadowMapGpuTimerQueryCursor = ( g_shadowMapGpuTimerQueryCursor + 1 ) % SHADOWMAP_GPU_TIMER_QUERY_SLOTS;

		if ( slot.active ) {
			GLuint available = GL_FALSE;
			glGetQueryObjectuiv( slot.query, GL_QUERY_RESULT_AVAILABLE, &available );
			if ( available == GL_FALSE ) {
				continue;
			}

			GLuint64 elapsedNanoseconds = 0;
			glGetQueryObjectui64v( slot.query, GL_QUERY_RESULT, &elapsedNanoseconds );
			RB_ShadowMapRecordGpuTimerResult( slot, static_cast<float>( static_cast<double>( elapsedNanoseconds ) / 1000000.0 ), false );
			slot.active = false;
		}

		if ( slot.query == 0 ) {
			glGenQueries( 1, &slot.query );
			if ( slot.query == 0 ) {
				return NULL;
			}
		}

		slot.active = true;
		slot.lightIndex = RB_ShadowMapLightIndex( vLight );
		slot.passKind = static_cast<int>( passKind );
		slot.lightClass = static_cast<int>( RB_ShadowMapLightClass( vLight ) );
		slot.phase = static_cast<int>( phase );
		slot.issuedFrame = tr.frameCount;
		glBeginQuery( GL_TIME_ELAPSED, slot.query );
		return &slot;
	}

	g_shadowMapStats.gpuTimerPending++;
	return NULL;
}

static void RB_ShadowMapEndGpuTimerQuery( shadowMapGpuTimerQuery_t *slot ) {
	if ( slot == NULL ) {
		return;
	}
	glEndQuery( GL_TIME_ELAPSED );
}

typedef struct shadowMapTimedPhase_s {
	shadowMapTimingPhase_t phase;
	idTimer cpuTimer;
	idTimer gpuSyncTimer;
	shadowMapGpuTimerQuery_t *gpuTimerQuery;
	rendererGpuTimerSlot_t parentGpuTimerSlot;
	bool parentGpuTimerPaused;
	bool active;
} shadowMapTimedPhase_t;

static void RB_ShadowMapBeginTimedPhase( shadowMapTimedPhase_t &timedPhase, const viewLight_t *vLight, const shadowMapPassKind_t passKind, const shadowMapTimingPhase_t phase ) {
	timedPhase.phase = phase;
	timedPhase.cpuTimer.Clear();
	timedPhase.gpuSyncTimer.Clear();
	timedPhase.gpuTimerQuery = NULL;
	timedPhase.parentGpuTimerSlot = RENDERER_GPU_TIMER_COUNT;
	timedPhase.parentGpuTimerPaused = false;
	timedPhase.active = true;
	timedPhase.cpuTimer.Start();
	if ( r_shadowMapGpuSyncTimings.GetBool() ) {
		glFinish();
		timedPhase.gpuSyncTimer.Start();
	}
	// GL_TIME_ELAPSED queries cannot be nested. Renderer metrics normally owns
	// the outer DRAW3D query while shadow-map phase metrics need a finer-grained
	// query, so split the parent interval around the shadow query.
	if ( RB_ShadowMapGpuTimerQueriesAvailable() ) {
		for ( int slot = 0; slot < RENDERER_GPU_TIMER_COUNT; slot++ ) {
			const rendererGpuTimerSlot_t timerSlot =
				static_cast<rendererGpuTimerSlot_t>( slot );
			if ( R_RendererMetrics_PauseGpuTimer( timerSlot ) ) {
				timedPhase.parentGpuTimerSlot = timerSlot;
				timedPhase.parentGpuTimerPaused = true;
				break;
			}
		}
		timedPhase.gpuTimerQuery =
			RB_ShadowMapBeginGpuTimerQuery( vLight, passKind, phase );
		if ( timedPhase.gpuTimerQuery == NULL && timedPhase.parentGpuTimerPaused ) {
			R_RendererMetrics_ResumeGpuTimer( timedPhase.parentGpuTimerSlot, true );
			timedPhase.parentGpuTimerPaused = false;
		}
	}
}

static float RB_ShadowMapEndTimedPhase( shadowMapTimedPhase_t &timedPhase ) {
	if ( !timedPhase.active ) {
		return 0.0f;
	}

	RB_ShadowMapEndGpuTimerQuery( timedPhase.gpuTimerQuery );
	if ( timedPhase.parentGpuTimerPaused ) {
		R_RendererMetrics_ResumeGpuTimer( timedPhase.parentGpuTimerSlot, true );
		timedPhase.parentGpuTimerPaused = false;
	}
	if ( r_shadowMapGpuSyncTimings.GetBool() ) {
		glFinish();
		timedPhase.gpuSyncTimer.Stop();
		const float gpuSyncMilliseconds = timedPhase.gpuSyncTimer.Milliseconds();
		g_shadowMapStats.gpuSyncMilliseconds += gpuSyncMilliseconds;
		if ( RB_ShadowMapTimingPhaseValid( static_cast<int>( timedPhase.phase ) ) ) {
			g_shadowMapStats.gpuSyncPhaseMilliseconds[timedPhase.phase] += gpuSyncMilliseconds;
		}
	}
	timedPhase.cpuTimer.Stop();
	timedPhase.active = false;
	const float cpuMilliseconds = timedPhase.cpuTimer.Milliseconds();
	if ( RB_ShadowMapTimingPhaseValid( static_cast<int>( timedPhase.phase ) ) ) {
		g_shadowMapStats.cpuPhaseMilliseconds[timedPhase.phase] += cpuMilliseconds;
	}
	return cpuMilliseconds;
}

static void RB_ShadowMapDebugOverlayReset( void ) {
	memset( &g_shadowMapDebugOverlayState, 0, sizeof( g_shadowMapDebugOverlayState ) );
	g_shadowMapDebugOverlayState.lightDefIndex = -1;
}

static bool RB_ShadowMapDebugOverlayEnabled( void ) {
	return glConfig.GLSLProgramAvailable && r_shadowMapDebugOverlay.GetBool();
}

static bool RB_ShadowMapShouldReport( void ) {
	if ( !r_useShadowMap.GetBool() ) {
		return false;
	}
	if ( backEnd.viewDef == NULL ) {
		return false;
	}
	if ( backEnd.viewDef->renderWorld == NULL || backEnd.viewDef->viewEntitys == NULL || backEnd.viewDef->viewLights == NULL ) {
		return false;
	}
	if ( backEnd.viewDef->isSubview || backEnd.viewDef->superView != NULL ) {
		return false;
	}
	return true;
}

static int RB_ShadowMapCascadeCountForLight( const viewLight_t *vLight ) {
	return R_ClassifyShadowMapLight( vLight ).cascadeCount;
}

static int RB_ShadowMapAtlasDivForLight( const viewLight_t *vLight ) {
	return R_ClassifyShadowMapLight( vLight ).atlasDiv;
}

static bool RB_ShadowMapValidateProjectedStateContract( const viewLight_t *vLight, const shadowMapProjectedLightState_t &state, const char **reason );

static int RB_ShadowMapTileSizeForLight( const viewLight_t *vLight ) {
	const int atlasDiv = RB_ShadowMapAtlasDivForLight( vLight );
	const int maxTileSize = glConfig.maxTextureSize / atlasDiv;
	if ( maxTileSize < 128 ) {
		return 0;
	}
	return idMath::ClampInt( 128, maxTileSize, r_shadowMapSize.GetInteger() );
}

bool RB_ShadowMapBuildArb2ParityState( const viewLight_t *vLight, const viewDef_t *viewDef, const int shadowMapSize, shadowMapArb2ParityState_t &state ) {
	memset( &state, 0, sizeof( state ) );
	R_ShadowMapResetProjectedLightState( state.projectedState );
	state.projectedFallbackCascade = -1;

	if ( vLight == NULL ) {
		return false;
	}

	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
	state.pointLight = classification.pointLight;
	state.projectedLight = !classification.pointLight;
	state.csmEnabled = classification.csmEnabled;
	state.requestedCascadeCount = Max( 1, classification.cascadeCount );
	state.cascadeCount = Max( 1, classification.cascadeCount );
	state.tileCount = Max( 1, classification.tileCount );
	state.atlasDiv = Max( 1, classification.atlasDiv );

	if ( classification.pointLight ) {
		state.csmEnabled = false;
		state.requestedCascadeCount = 1;
		state.cascadeCount = 1;
		state.tileCount = 6;
		state.atlasDiv = 3;
		state.tileSize = RB_ShadowMapPointSizeValue();
		state.valid = true;
		return true;
	}

	const int maxTextureSize = glConfig.maxTextureSize > 0 ? glConfig.maxTextureSize : 4096;
	const int maxTileSize = maxTextureSize / Max( 1, state.atlasDiv );
	if ( maxTileSize < 128 ) {
		return false;
	}
	state.tileSize = idMath::ClampInt( 128, maxTileSize, shadowMapSize );

	R_BuildShadowMapProjectedLightState( vLight, viewDef, state.tileSize, state.projectedState );
	const char *invariantReason = "ok";
	if ( !RB_ShadowMapValidateProjectedStateContract( vLight, state.projectedState, &invariantReason ) ) {
		return false;
	}

	state.projectedStateReady = true;
	state.projectedCascadeFallback = state.projectedState.cascadeFallback;
	state.projectedFallbackCascade = state.projectedState.fallbackCascade;
	state.requestedCascadeCount = Max( 1, state.projectedState.requestedCascadeCount );
	state.cascadeCount = idMath::ClampInt( 1, SHADOWMAP_PROJECTED_MAX_CASCADES, state.projectedState.cascadeCount );
	state.tileCount = Max( 1, state.cascadeCount );
	state.atlasDiv = Max( 1, state.projectedState.atlasDiv );
	state.valid = true;
	return true;
}

static void RB_ShadowMapSelectProjectedScratchResources( void ) {
	g_activeProjectedShadowMapCache = NULL;
	g_shadowMapDepthImage = g_shadowMapScratchDepthImage;
	g_shadowMapRenderTexture = g_shadowMapScratchRenderTexture;
	g_shadowMapActiveSlotOriginX = 0;
	g_shadowMapActiveSlotOriginY = 0;
}

static void RB_ShadowMapSelectPointScratchResources( void ) {
	g_activePointShadowMapCache = NULL;
	g_pointShadowMapColorImage = g_pointShadowMapScratchColorImage;
	g_pointShadowMapDepthImage = g_pointShadowMapScratchDepthImage;
	g_pointShadowMapRenderTexture = g_pointShadowMapScratchRenderTexture;
}

static int RB_ShadowMapLightIndex( const viewLight_t *vLight ) {
	return ( vLight != NULL && vLight->lightDef != NULL ) ? vLight->lightDef->index : -1;
}

static int RB_ShadowMapHashInt( int hash, const int value ) {
	const unsigned int h = static_cast<unsigned int>( hash );
	const unsigned int v = static_cast<unsigned int>( value );
	return static_cast<int>( ( h ^ v ) * 16777619u );
}

static int RB_ShadowMapHashFloat( int hash, const float value ) {
	return R_ShadowMapHashFloat( hash, value );
}

static int RB_ShadowMapMapNameHash( const viewDef_t *viewDef ) {
	return ( viewDef != NULL && viewDef->renderWorld != NULL )
		? idStr::Hash( viewDef->renderWorld->mapName.c_str() ) : 0;
}

static int RB_ShadowMapBuildPassSignatureForView( const viewLight_t *vLight, const viewDef_t *viewDef, const shadowMapPassKind_t passKind, const bool pointLight ) {
	int hash = static_cast<int>( 2166136261u );
	const int cascadeCount = RB_ShadowMapCascadeCountForLight( vLight );
	const idRenderWorldLocal *renderWorld =
		viewDef != NULL ? viewDef->renderWorld : NULL;
	hash = RB_ShadowMapHashInt( hash,
		renderWorld != NULL ? static_cast<int>( renderWorld->mapFileCRC ) : 0 );
	hash = RB_ShadowMapHashInt( hash, RB_ShadowMapMapNameHash( viewDef ) );
	hash = RB_ShadowMapHashInt( hash, RB_ShadowMapLightIndex( vLight ) );
	hash = RB_ShadowMapHashInt( hash, static_cast<int>( passKind ) );
	hash = RB_ShadowMapHashInt( hash, static_cast<int>( RB_ShadowMapLightClass( vLight ) ) );
	hash = RB_ShadowMapHashInt( hash, pointLight ? 1 : 0 );
	// Resident depth contains only the static opaque chains. Live caster
	// counts must not evict it when a character or cutout enters the light.
	hash = RB_ShadowMapHashInt( hash, vLight != NULL ? vLight->shadowMapStaticCasterCount : 0 );
	hash = RB_ShadowMapHashInt( hash, vLight != NULL ? vLight->shadowMapCasterSignature : 0 );
	hash = RB_ShadowMapHashInt( hash, vLight != NULL ? vLight->shadowMapIncompleteMapMask : 0 );
	hash = RB_ShadowMapHashInt( hash, vLight != NULL ? vLight->shadowMapHybridIncompleteMask : 0 );
	hash = RB_ShadowMapHashInt( hash, vLight != NULL ? vLight->shadowMapPrelightMapMissingMask : 0 );
	hash = RB_ShadowMapHashInt( hash, cascadeCount );
	hash = RB_ShadowMapHashInt( hash, RB_ShadowMapHashedAlphaEnabled() ? 1 : 0 );
	hash = RB_ShadowMapHashInt( hash, r_shadowMapStableAlphaHash.GetBool() ? 1 : 0 );
	hash = RB_ShadowMapHashFloat( hash, RB_ShadowMapPolygonFactor() );
	hash = RB_ShadowMapHashFloat( hash, RB_ShadowMapPolygonOffset() );
	hash = RB_ShadowMapHashFloat( hash, static_cast<float>( idMath::ClampInt( 0, 2, r_shadowMapCasterCulling.GetInteger() ) ) );
	if ( pointLight ) {
		hash = RB_ShadowMapHashInt( hash, RB_ShadowMapPointSizeValue() );
		hash = RB_ShadowMapHashInt( hash, RB_PointShadowMapHighPrecisionEnabled() ? 1 : 0 );
		hash = RB_ShadowMapHashInt( hash, RB_PointShadowMapDepthCompareEnabled() ? 1 : 0 );
		hash = RB_ShadowMapHashFloat( hash, r_shadowMapPointFarScale.GetFloat() );
		hash = RB_ShadowMapHashFloat( hash, R_ShadowMapPointFarDistance( vLight ) );
		if ( vLight != NULL && vLight->lightDef != NULL ) {
			for ( int i = 0; i < 3; ++i ) {
				hash = RB_ShadowMapHashFloat( hash,
					vLight->lightDef->parms.lightCenter[i] );
			}
		}
	} else {
		hash = RB_ShadowMapHashInt( hash, RB_ShadowMapTileSizeForLight( vLight ) );
		hash = RB_ShadowMapHashInt( hash, RB_ShadowMapAtlasDivForLight( vLight ) );
		hash = RB_ShadowMapHashFloat( hash, r_shadowMapProjectionPad.GetFloat() );
		hash = RB_ShadowMapHashFloat( hash, r_shadowMapTexelBiasScale.GetFloat() );
		// Key the fitted projection itself. This includes blend overlap, PCSS
		// guards, distant-source scaling and parallel-light bounds, and allows
		// camera motion that leaves a stabilized fit unchanged to reuse it.
		shadowMapProjectedLightState_t projectedState;
		R_BuildShadowMapProjectedLightState( vLight, viewDef,
			RB_ShadowMapTileSizeForLight( vLight ), projectedState );
		hash = R_ShadowMapProjectedStateHash( hash, projectedState );
	}
	if ( vLight != NULL ) {
		for ( int i = 0; i < 3; i++ ) {
			hash = RB_ShadowMapHashFloat( hash, vLight->globalLightOrigin[i] );
			hash = RB_ShadowMapHashFloat( hash, vLight->lightRadius[i] );
		}
		for ( int planeIndex = 0; planeIndex < 4; planeIndex++ ) {
			for ( int component = 0; component < 4; component++ ) {
				hash = RB_ShadowMapHashFloat( hash, vLight->lightProject[planeIndex][component] );
			}
		}
	}
	return hash;
}

static int RB_ShadowMapBuildPassSignature( const viewLight_t *vLight, const shadowMapPassKind_t passKind, const bool pointLight ) {
	return RB_ShadowMapBuildPassSignatureForView( vLight, backEnd.viewDef, passKind, pointLight );
}

static shadowMapLightHistory_t *RB_ShadowMapFindLightHistory( const int lightIndex ) {
	if ( lightIndex < 0 ) {
		return NULL;
	}
	shadowMapLightHistory_t *oldest = &g_shadowMapLightHistory[0];
	for ( int i = 0; i < SHADOWMAP_LIGHT_HISTORY_SLOTS; i++ ) {
		shadowMapLightHistory_t *history = &g_shadowMapLightHistory[i];
		if ( history->valid && history->lightIndex == lightIndex ) {
			return history;
		}
		if ( !history->valid ) {
			oldest = history;
			break;
		}
		if ( history->lastTouchedFrame < oldest->lastTouchedFrame ) {
			oldest = history;
		}
	}
	memset( oldest, 0, sizeof( *oldest ) );
	oldest->valid = true;
	oldest->lightIndex = lightIndex;
	oldest->lastDynamicFrame = -0x3fffffff;
	return oldest;
}

static const shadowMapLightHistory_t *RB_ShadowMapFindLightHistoryConst( const int lightIndex ) {
	if ( lightIndex < 0 ) {
		return NULL;
	}
	for ( int i = 0; i < SHADOWMAP_LIGHT_HISTORY_SLOTS; i++ ) {
		const shadowMapLightHistory_t *history = &g_shadowMapLightHistory[i];
		if ( history->valid && history->lightIndex == lightIndex ) {
			return history;
		}
	}
	return NULL;
}

static void RB_ShadowMapInvalidateLightCaches( const int lightIndex ) {
	if ( lightIndex < 0 ) {
		return;
	}
	for ( int i = 0; i < SHADOWMAP_CACHE_MAX_SLOTS; i++ ) {
		if ( g_projectedShadowMapCache[i].valid && g_projectedShadowMapCache[i].lightIndex == lightIndex ) {
			g_projectedShadowMapCache[i].valid = false;
		}
		if ( g_pointShadowMapCache[i].valid && g_pointShadowMapCache[i].lightIndex == lightIndex ) {
			g_pointShadowMapCache[i].valid = false;
		}
	}
}

// Cache keys are only meaningful inside one loaded render world. Light
// indices are reused between maps; never expose an earlier world's tiles.
bool RB_ShadowMapPrepareCacheView( const viewDef_t *viewDef ) {
	// HUD, console, and post-process views have no render world. They do not
	// own shadow resources and must not evict the preceding world's cache.
	// A subsequent real world view still performs the full identity check.
	if ( viewDef == NULL || viewDef->renderWorld == NULL ) {
		return false;
	}
	const idRenderWorldLocal *renderWorld = viewDef->renderWorld;
	const unsigned int mapFileCRC =
		renderWorld != NULL ? renderWorld->mapFileCRC : 0;
	const int mapNameHash = RB_ShadowMapMapNameHash( viewDef );
	if ( g_shadowMapCacheRenderWorld == renderWorld
			&& g_shadowMapCacheMapFileCRC == mapFileCRC
			&& g_shadowMapCacheMapNameHash == mapNameHash ) {
		return false;
	}

	for ( int i = 0; i < SHADOWMAP_CACHE_MAX_SLOTS; i++ ) {
		g_projectedShadowMapCache[i].valid = false;
		g_pointShadowMapCache[i].valid = false;
	}
	memset( g_shadowMapLightHistory, 0, sizeof( g_shadowMapLightHistory ) );
	g_activeProjectedShadowMapCache = NULL;
	g_activePointShadowMapCache = NULL;
	g_shadowMapDepthImage = NULL;
	g_shadowMapRenderTexture = NULL;
	g_shadowMapActiveSlotOriginX = 0;
	g_shadowMapActiveSlotOriginY = 0;
	g_pointShadowMapColorImage = NULL;
	g_pointShadowMapDepthImage = NULL;
	g_pointShadowMapRenderTexture = NULL;
	memset( &g_projectedShadowMapState, 0, sizeof( g_projectedShadowMapState ) );
	g_projectedTranslucentShadowPassReady = false;
	g_pointTranslucentShadowPassReady = false;
	g_shadowMapGlobalPassMapped = SHADOWMAP_GLOBAL_MAPPED_NONE;
	g_shadowMapGlobalPassHybrid = false;
	g_shadowMapDeferredGlobalReportPending = false;
	g_shadowMapCacheRenderWorld = renderWorld;
	g_shadowMapCacheMapFileCRC = mapFileCRC;
	g_shadowMapCacheMapNameHash = mapNameHash;
	return true;
}

static bool RB_ShadowMapStaticCacheable( const viewLight_t *vLight, const shadowMapPassKind_t passKind, const bool pointLight, const bool haveTranslucentCasters ) {
	if ( !r_shadowMapStaticCache.GetBool() || vLight == NULL || RB_ShadowMapLightIndex( vLight ) < 0 ) {
		return false;
	}
	// Dynamic casters no longer defeat caching for projected lights: they are
	// excluded from the cache signature and composed over the cached static
	// tiles per frame, so a walking character costs one blit plus its own
	// draws instead of a full static-scene re-render. Point lights keep the
	// old rule until the cube path gains composition (phase 5c).
	const bool dynamicsDefeatCache = pointLight && vLight->shadowMapDynamicCasterCount > 0;
	// Perforated surfaces always live in the dynamic chains. Their animated
	// coverage is composed over cached opaque depth for projected lights.
	if ( haveTranslucentCasters || dynamicsDefeatCache
			|| vLight->shadowMapStaticCasterCount <= 0 ) {
		const int lightIndex = RB_ShadowMapLightIndex( vLight );
		shadowMapLightHistory_t *history = RB_ShadowMapFindLightHistory( lightIndex );
		if ( history != NULL && vLight->shadowMapDynamicCasterCount > 0 ) {
			history->lastDynamicFrame = tr.frameCount;
			history->lastTouchedFrame = tr.frameCount;
			if ( dynamicsDefeatCache ) {
				RB_ShadowMapInvalidateLightCaches( lightIndex );
			}
		}
		return false;
	}
	if ( !pointLight && RB_ShadowMapCascadeCountForLight( vLight ) > 1 && !r_shadowMapCacheCSM.GetBool() ) {
		return false;
	}
	shadowMapLightHistory_t *history = RB_ShadowMapFindLightHistory( RB_ShadowMapLightIndex( vLight ) );
	if ( history != NULL ) {
		history->lastTouchedFrame = tr.frameCount;
		if ( pointLight && tr.frameCount - history->lastDynamicFrame < Max( 0, r_shadowMapStaticHysteresisFrames.GetInteger() ) ) {
			g_shadowMapStats.staticHysteresisPasses++;
			return false;
		}
	}
	return true;
}

static bool RB_ShadowMapStaticCacheableReadOnly( const viewLight_t *vLight, const shadowMapPassKind_t passKind, const bool pointLight, const bool haveTranslucentCasters ) {
	if ( !r_shadowMapStaticCache.GetBool() || vLight == NULL || RB_ShadowMapLightIndex( vLight ) < 0 ) {
		return false;
	}
	if ( haveTranslucentCasters
			|| ( pointLight && vLight->shadowMapDynamicCasterCount > 0 )
			|| vLight->shadowMapStaticCasterCount <= 0 ) {
		return false;
	}
	if ( !pointLight && RB_ShadowMapCascadeCountForLight( vLight ) > 1 && !r_shadowMapCacheCSM.GetBool() ) {
		return false;
	}
	const shadowMapLightHistory_t *history = RB_ShadowMapFindLightHistoryConst( RB_ShadowMapLightIndex( vLight ) );
	if ( history != NULL && pointLight && tr.frameCount - history->lastDynamicFrame < Max( 0, r_shadowMapStaticHysteresisFrames.GetInteger() ) ) {
		return false;
	}
	return true;
}

static int RB_ShadowMapProjectedCacheSlotLimit( void ) {
	return idMath::ClampInt( 0, SHADOWMAP_CACHE_MAX_SLOTS, r_shadowMapProjectedCacheSize.GetInteger() );
}

static int RB_ShadowMapPointCacheSlotLimit( void ) {
	return idMath::ClampInt( 0, SHADOWMAP_CACHE_MAX_SLOTS, r_shadowMapPointCacheSize.GetInteger() );
}

// Approximate resident shadow GPU memory, for the SM cache report line: the
// cost was previously invisible, and nothing but a full vid_restart ever
// reclaimed it.
static double RB_ShadowMapImageBytes( const idImage *image ) {
	if ( image == NULL ) {
		return 0.0;
	}
	const idImageOpts &opts = image->GetOpts();
	const double bytesPerPixel = ( opts.format == FMT_RGBA16F ) ? 8.0 : 4.0;
	const double faces = ( opts.textureType == TT_CUBIC ) ? 6.0 : 1.0;
	return (double)opts.width * (double)opts.height * bytesPerPixel * faces;
}

static double RB_ShadowMapResidentBytes( void ) {
	double bytes = 0.0;
	bytes += RB_ShadowMapImageBytes( g_shadowMapScratchDepthImage );
	bytes += RB_ShadowMapImageBytes( g_shadowMapAtlasDepthImage );
	bytes += RB_ShadowMapImageBytes( g_pointShadowMapScratchColorImage );
	bytes += RB_ShadowMapImageBytes( g_pointShadowMapScratchDepthImage );
	for ( int i = 0; i < 3; i++ ) {
		bytes += RB_ShadowMapImageBytes( g_translucentShadowMomentImages[i] );
		bytes += RB_ShadowMapImageBytes( g_pointTranslucentShadowMomentImages[i] );
	}
	for ( int i = 0; i < SHADOWMAP_CACHE_MAX_SLOTS; i++ ) {
		
		bytes += RB_ShadowMapImageBytes( g_pointShadowMapCache[i].colorImage );
		bytes += RB_ShadowMapImageBytes( g_pointShadowMapCache[i].depthImage );
	}
	return bytes;
}

static bool RB_ShadowMapProjectedCacheEntryStorageValid(
	const projectedShadowMapCacheEntry_t *entry );
static bool RB_ShadowMapPointCacheEntryStorageValid(
	const pointShadowMapCacheEntry_t *entry );

static void RB_ShadowMapExpireCaches( void ) {
	const int residentFrames = Max( 1, r_shadowMapResidentFrames.GetInteger() );
	const int projectedLimit = RB_ShadowMapProjectedCacheSlotLimit();
	const int pointLimit = RB_ShadowMapPointCacheSlotLimit();
	for ( int i = 0; i < SHADOWMAP_CACHE_MAX_SLOTS; i++ ) {
		// Cache-size cvars are live. Entries outside a reduced limit must stop
		// occupying atlas cells immediately or in-range allocation can evict
		// forever without finding a block that the stale metadata still marks.
		if ( g_projectedShadowMapCache[i].valid
				&& ( !RB_ShadowMapProjectedCacheEntryStorageValid(
						&g_projectedShadowMapCache[i] )
					|| i >= projectedLimit
					|| tr.frameCount - g_projectedShadowMapCache[i].lastUsedFrame
						> residentFrames ) ) {
			g_projectedShadowMapCache[i].valid = false;
			g_shadowMapStats.cacheExpired++;
		}
		if ( g_pointShadowMapCache[i].valid
				&& ( !RB_ShadowMapPointCacheEntryStorageValid(
						&g_pointShadowMapCache[i] )
					|| i >= pointLimit
					|| tr.frameCount - g_pointShadowMapCache[i].lastUsedFrame
						> residentFrames ) ) {
			g_pointShadowMapCache[i].valid = false;
			g_shadowMapStats.cacheExpired++;
		}
	}
}

// Persistent cache metadata is usable only while it still names the physical
// atlas allocation that received the render. Image reload/recreation can keep
// the idImage pointer stable while replacing its storage.
static bool RB_ShadowMapProjectedCacheEntryStorageValid(
		const projectedShadowMapCacheEntry_t *entry ) {
	return entry != NULL && entry->valid
		&& entry->atlasStorageGeneration != 0
		&& g_shadowMapAtlasDepthImage != NULL
		&& g_shadowMapAtlasRenderTexture != NULL
		&& g_shadowMapAtlasDepthImage->GetStorageGeneration()
			== entry->atlasStorageGeneration
		&& g_shadowMapAtlasDepthImage->IsLoaded()
		&& !g_shadowMapAtlasDepthImage->IsDefaulted()
		&& g_shadowMapAtlasDepthImage->GetDeviceHandle() != 0
		&& g_shadowMapAtlasDepthImage->GetOpts().textureType == TT_2D
		&& g_shadowMapAtlasDepthImage->GetOpts().format == FMT_DEPTH
		&& g_shadowMapAtlasRenderTexture->GetWidth() > 0
		&& g_shadowMapAtlasRenderTexture->GetHeight() > 0;
}

static bool RB_ShadowMapPointCacheEntryStorageValid(
		const pointShadowMapCacheEntry_t *entry ) {
	return entry != NULL && entry->valid
		&& entry->size > 0
		&& entry->colorImage != NULL && entry->depthImage != NULL
		&& entry->renderTexture != NULL
		&& entry->colorStorageGeneration != 0
		&& entry->depthStorageGeneration != 0
		&& entry->colorImage->GetStorageGeneration()
			== entry->colorStorageGeneration
		&& entry->depthImage->GetStorageGeneration()
			== entry->depthStorageGeneration
		&& entry->colorImage->IsLoaded()
		&& !entry->colorImage->IsDefaulted()
		&& entry->colorImage->GetDeviceHandle() != 0
		&& entry->depthImage->IsLoaded()
		&& !entry->depthImage->IsDefaulted()
		&& entry->depthImage->GetDeviceHandle() != 0
		&& entry->colorImage->GetOpts().textureType == TT_CUBIC
		&& entry->depthImage->GetOpts().textureType == TT_CUBIC
		&& entry->colorImage->GetOpts().format
			== ( entry->highPrecision ? FMT_RGBA16F : FMT_RGBA8 )
		&& entry->depthImage->GetOpts().format == FMT_DEPTH
		&& entry->renderTexture->GetWidth() == entry->size
		&& entry->renderTexture->GetHeight() == entry->size;
}

// Admission uses a compatible allocation's age to prioritise updates. This
// lookup is metadata only: sampling also requires the exact caster signature.
static bool RB_ShadowMapPointCacheEntryProjectionMatches(
		const pointShadowMapCacheEntry_t *entry, const viewLight_t *vLight ) {
	if ( entry == NULL || vLight == NULL ) {
		return false;
	}
	// Exact float identity is intentional. These values are copied directly
	// from the light when the cube is rendered. A changed projection should
	// receive the priority of a light without a compatible resident allocation.
	return entry->farDistance == R_ShadowMapPointFarDistance( vLight )
		&& entry->lightOrigin[0] == vLight->globalLightOrigin[0]
		&& entry->lightOrigin[1] == vLight->globalLightOrigin[1]
		&& entry->lightOrigin[2] == vLight->globalLightOrigin[2];
}

static pointShadowMapCacheEntry_t *RB_ShadowMapNewestCompatiblePointEntry( const viewLight_t *vLight, const shadowMapPassKind_t passKind ) {
	const int slotLimit = RB_ShadowMapPointCacheSlotLimit();
	const int lightIndex = RB_ShadowMapLightIndex( vLight );
	const int requiredSize = RB_ShadowMapPointSizeValue();
	const bool requiredHighPrecision = RB_PointShadowMapHighPrecisionEnabled();
	const bool requiredDepthCompare = RB_PointShadowMapDepthCompareEnabled();
	pointShadowMapCacheEntry_t *newest = NULL;
	for ( int i = 0; i < slotLimit; i++ ) {
		pointShadowMapCacheEntry_t *entry = &g_pointShadowMapCache[i];
		if ( RB_ShadowMapPointCacheEntryStorageValid( entry )
				&& entry->lightIndex == lightIndex
				&& entry->passKind == static_cast<int>( passKind )
				&& entry->size == requiredSize
				&& entry->highPrecision == requiredHighPrecision
				&& entry->depthCompare == requiredDepthCompare
				&& RB_ShadowMapPointCacheEntryProjectionMatches( entry, vLight )
				&& ( newest == NULL
					|| entry->lastUpdatedFrame > newest->lastUpdatedFrame ) ) {
			newest = entry;
		}
	}
	return newest;
}

static projectedShadowMapCacheEntry_t *RB_ShadowMapFindProjectedCacheEntry( const int lightIndex, const shadowMapPassKind_t passKind, const int signature ) {
	const int slotLimit = RB_ShadowMapProjectedCacheSlotLimit();
	for ( int i = 0; i < slotLimit; i++ ) {
		projectedShadowMapCacheEntry_t *entry = &g_projectedShadowMapCache[i];
		if ( RB_ShadowMapProjectedCacheEntryStorageValid( entry )
				&& entry->lightIndex == lightIndex
				&& entry->passKind == static_cast<int>( passKind )
				&& entry->signature == signature ) {
			return entry;
		}
	}
	return NULL;
}

static pointShadowMapCacheEntry_t *RB_ShadowMapFindPointCacheEntry( const int lightIndex, const shadowMapPassKind_t passKind, const int signature ) {
	const int slotLimit = RB_ShadowMapPointCacheSlotLimit();
	for ( int i = 0; i < slotLimit; i++ ) {
		pointShadowMapCacheEntry_t *entry = &g_pointShadowMapCache[i];
		if ( RB_ShadowMapPointCacheEntryStorageValid( entry )
				&& entry->lightIndex == lightIndex
				&& entry->passKind == static_cast<int>( passKind )
				&& entry->signature == signature ) {
			return entry;
		}
	}
	return NULL;
}

// Atlas cell grid bookkeeping: entries occupy span x span cell blocks inside
// the shared atlas. The grid is small (<= 8x8), so occupancy is re-derived
// from the valid entries on every allocation.
static const int SHADOWMAP_ATLAS_MAX_GRID = 8;

static int RB_ShadowMapAtlasSizeValue( void ) {
	const int maxTextureSize =
		glConfig.maxTextureSize > 0 ? glConfig.maxTextureSize : 4096;
	return Min( idMath::ClampInt( 2048, 8192,
		r_shadowMapAtlasSize.GetInteger() ), maxTextureSize );
}

static int RB_ShadowMapAtlasGridDim( void ) {
	if ( g_shadowMapAtlasCellSize <= 0 ) {
		return 0;
	}
	const int atlasSize = g_shadowMapAtlasRenderTexture != NULL
		? Min( g_shadowMapAtlasRenderTexture->GetWidth(),
			g_shadowMapAtlasRenderTexture->GetHeight() )
		: RB_ShadowMapAtlasSizeValue();
	const int gridDim = atlasSize / g_shadowMapAtlasCellSize;
	return gridDim > 0
		? idMath::ClampInt( 1, SHADOWMAP_ATLAS_MAX_GRID, gridDim ) : 0;
}

static bool RB_ShadowMapAtlasFindFreeBlock( const int span, int &cellX, int &cellY ) {
	const int gridDim = RB_ShadowMapAtlasGridDim();
	if ( span <= 0 || span > gridDim ) {
		return false;
	}
	bool occupied[SHADOWMAP_ATLAS_MAX_GRID][SHADOWMAP_ATLAS_MAX_GRID];
	memset( occupied, 0, sizeof( occupied ) );
	for ( int i = 0; i < SHADOWMAP_CACHE_MAX_SLOTS; i++ ) {
		const projectedShadowMapCacheEntry_t &entry = g_projectedShadowMapCache[i];
		if ( !RB_ShadowMapProjectedCacheEntryStorageValid( &entry ) ) {
			continue;
		}
		for ( int y = entry.atlasCellY; y < entry.atlasCellY + entry.atlasCellSpan && y < gridDim; y++ ) {
			for ( int x = entry.atlasCellX; x < entry.atlasCellX + entry.atlasCellSpan && x < gridDim; x++ ) {
				occupied[y][x] = true;
			}
		}
	}
	for ( int y = 0; y + span <= gridDim; y++ ) {
		for ( int x = 0; x + span <= gridDim; x++ ) {
			bool blockFree = true;
			for ( int by = 0; by < span && blockFree; by++ ) {
				for ( int bx = 0; bx < span; bx++ ) {
					if ( occupied[y + by][x + bx] ) {
						blockFree = false;
						break;
					}
				}
			}
			if ( blockFree ) {
				cellX = x;
				cellY = y;
				return true;
			}
		}
	}
	return false;
}

static projectedShadowMapCacheEntry_t *RB_ShadowMapAllocProjectedCacheEntry(
		const int cellSpan, const bool allowEviction = true ) {
	const int slotLimit = RB_ShadowMapProjectedCacheSlotLimit();
	if ( slotLimit <= 0 || g_shadowMapAtlasCellSize <= 0 ) {
		return NULL;
	}

	projectedShadowMapCacheEntry_t *freeEntry = NULL;
	for ( int attempt = 0; attempt <= slotLimit; attempt++ ) {
		// find an unused entry record
		freeEntry = NULL;
		projectedShadowMapCacheEntry_t *oldest = NULL;
		for ( int i = 0; i < slotLimit; i++ ) {
			projectedShadowMapCacheEntry_t *entry = &g_projectedShadowMapCache[i];
			if ( !entry->valid ) {
				if ( freeEntry == NULL ) {
					freeEntry = entry;
				}
				continue;
			}
			if ( oldest == NULL || entry->lastUsedFrame < oldest->lastUsedFrame ) {
				oldest = entry;
			}
		}
		int cellX = 0;
		int cellY = 0;
		if ( freeEntry != NULL && RB_ShadowMapAtlasFindFreeBlock( cellSpan, cellX, cellY ) ) {
			freeEntry->atlasCellX = cellX;
			freeEntry->atlasCellY = cellY;
			freeEntry->atlasCellSpan = cellSpan;
			return freeEntry;
		}
		if ( !allowEviction ) {
			return NULL;
		}
		// no record or no free block: evict the least recently used entry and retry
		if ( oldest == NULL ) {
			return NULL;
		}
		oldest->valid = false;
		g_shadowMapStats.cacheEvictions++;
	}
	return NULL;
}

static pointShadowMapCacheEntry_t *RB_ShadowMapAllocPointCacheEntry(
		const bool allowEviction = true ) {
	const int slotLimit = RB_ShadowMapPointCacheSlotLimit();
	if ( slotLimit <= 0 ) {
		return NULL;
	}
	pointShadowMapCacheEntry_t *oldest = &g_pointShadowMapCache[0];
	for ( int i = 0; i < slotLimit; i++ ) {
		pointShadowMapCacheEntry_t *entry = &g_pointShadowMapCache[i];
		if ( !entry->valid ) {
			return entry;
		}
		if ( entry->lastUsedFrame < oldest->lastUsedFrame ) {
			oldest = entry;
		}
	}
	if ( !allowEviction ) {
		return NULL;
	}
	oldest->valid = false;
	g_shadowMapStats.cacheEvictions++;
	return oldest;
}

static void RB_ShadowMapSelectProjectedCacheEntry( projectedShadowMapCacheEntry_t *entry ) {
	g_activeProjectedShadowMapCache = entry;
	if ( entry != NULL ) {
		g_shadowMapDepthImage = g_shadowMapAtlasDepthImage;
		g_shadowMapRenderTexture = g_shadowMapAtlasRenderTexture;
		g_shadowMapActiveSlotOriginX = entry->atlasCellX * g_shadowMapAtlasCellSize;
		g_shadowMapActiveSlotOriginY = entry->atlasCellY * g_shadowMapAtlasCellSize;
	}
}

static void RB_ShadowMapSelectPointCacheEntry( pointShadowMapCacheEntry_t *entry ) {
	g_activePointShadowMapCache = entry;
	if ( entry != NULL ) {
		g_pointShadowMapColorImage = entry->colorImage;
		g_pointShadowMapDepthImage = entry->depthImage;
		g_pointShadowMapRenderTexture = entry->renderTexture;
	}
}

static void RB_ShadowMapCountCacheSlots( void ) {
	g_shadowMapStats.projectedCacheSlotsUsed = 0;
	g_shadowMapStats.pointCacheSlotsUsed = 0;
	g_shadowMapStats.projectedCacheSlotsTotal = RB_ShadowMapProjectedCacheSlotLimit();
	g_shadowMapStats.pointCacheSlotsTotal = RB_ShadowMapPointCacheSlotLimit();
	for ( int i = 0; i < g_shadowMapStats.projectedCacheSlotsTotal; i++ ) {
		if ( g_projectedShadowMapCache[i].valid ) {
			g_shadowMapStats.projectedCacheSlotsUsed++;
		}
	}
	for ( int i = 0; i < g_shadowMapStats.pointCacheSlotsTotal; i++ ) {
		if ( g_pointShadowMapCache[i].valid ) {
			g_shadowMapStats.pointCacheSlotsUsed++;
		}
	}
}

// When a light has no noSelfShadow (local) casters, the LOCAL pass map
// (global casters only) and the GLOBAL pass map (global + local casters)
// have identical content. Collapsing their cache identity renders and
// stores one map per light instead of two, halving update cost and slot
// pressure for every light without character-class casters nearby - the
// LOCAL pass renders it, the GLOBAL pass cache-hits it the same frame.
static shadowMapPassKind_t RB_ShadowMapCachePassKind( const viewLight_t *vLight, const shadowMapPassKind_t passKind ) {
	if ( vLight != NULL && vLight->localShadowMapCasters == NULL && vLight->localShadowMapDynamicCasters == NULL && vLight->localTranslucentShadowMapCasters == NULL ) {
		return SHADOWMAP_PASS_GLOBAL;
	}
	return passKind;
}

static bool RB_ShadowMapUpdateAdmitted( const int lightIndex ) {
	for ( int i = 0; i < g_shadowMapAdmittedLightCount; i++ ) {
		if ( g_shadowMapAdmittedLightIndexes[i] == lightIndex ) {
			return true;
		}
	}
	return false;
}

static shadowMapSchedule_t RB_ShadowMapSchedulePass( const viewLight_t *vLight, const shadowMapPassKind_t requestedPassKind, const bool pointLight, const bool haveTranslucentCasters ) {
	const shadowMapPassKind_t passKind = RB_ShadowMapCachePassKind( vLight, requestedPassKind );
	const int receiverMask = requestedPassKind == SHADOWMAP_PASS_LOCAL
		? SHADOWMAP_RECEIVER_MASK_LOCAL : SHADOWMAP_RECEIVER_MASK_GLOBAL;
	const int incompleteStencilMask = vLight->shadowMapIncompleteStencilMask
		| ( vLight->shadowMapPrelightStencilRequiredMask
			& ~vLight->shadowMapPrelightStencilReadyMask );
	// Budgets and subview policy may choose stencil only when it represents
	// every admitted caster. Match Vulkan's correctness priority for map-only
	// geometry, including a mixed light with other usable stencil volumes.
	const bool mapRequired = ( incompleteStencilMask & receiverMask ) != 0;
	shadowMapSchedule_t schedule;
	memset( &schedule, 0, sizeof( schedule ) );
	schedule.action = SHADOWMAP_SCHEDULE_UPDATE;
	schedule.signature = RB_ShadowMapBuildPassSignature( vLight, passKind, pointLight );

	// Subview policy may prefer complete stencil coverage. Policy 1 accepts
	// exact cache hits; policy 2 prefers stencil even when a cached map exists.
	// Neither policy may drop map-only casters.
	const int subviewPolicy = idMath::ClampInt( 0, 2, r_shadowMapSubviewPolicy.GetInteger() );
	const bool subviewView = backEnd.viewDef != NULL && backEnd.viewDef->isSubview;
	if ( subviewView && subviewPolicy >= 2 && !mapRequired ) {
		g_shadowMapSubviewFallbackPasses++;
		schedule.action = SHADOWMAP_SCHEDULE_FALLBACK;
		return schedule;
	}

	if ( pointLight ) {
		RB_ShadowMapSelectPointScratchResources();
	} else {
		RB_ShadowMapSelectProjectedScratchResources();
	}
	schedule.cacheable = RB_ShadowMapStaticCacheable( vLight, passKind, pointLight, haveTranslucentCasters );
	const int lightIndex = RB_ShadowMapLightIndex( vLight );
	if ( schedule.cacheable ) {
		if ( pointLight ) {
			schedule.pointEntry = RB_ShadowMapFindPointCacheEntry( lightIndex, passKind, schedule.signature );
			if ( schedule.pointEntry != NULL ) {
				schedule.cacheHit = true;
				schedule.action = SHADOWMAP_SCHEDULE_REUSE;
				schedule.budgetReuse = r_shadowMapMaxUpdatesPerView.GetInteger() > 0 && g_shadowMapStats.shadowMapUpdates >= r_shadowMapMaxUpdatesPerView.GetInteger();
				g_shadowMapStats.cacheHitPasses++;
				g_shadowMapStats.cacheReusePasses++;
				g_shadowMapStats.cacheBudgetReusePasses += schedule.budgetReuse ? 1 : 0;
				g_shadowMapSubviewReusePasses += subviewView ? 1 : 0;
				RB_ShadowMapSelectPointCacheEntry( schedule.pointEntry );
				return schedule;
			}
		} else {
			schedule.projectedEntry = RB_ShadowMapFindProjectedCacheEntry( lightIndex, passKind, schedule.signature );
			if ( schedule.projectedEntry != NULL ) {
				schedule.cacheHit = true;
				schedule.action = SHADOWMAP_SCHEDULE_REUSE;
				schedule.budgetReuse = r_shadowMapMaxUpdatesPerView.GetInteger() > 0 && g_shadowMapStats.shadowMapUpdates >= r_shadowMapMaxUpdatesPerView.GetInteger();
				g_shadowMapStats.cacheHitPasses++;
				g_shadowMapStats.cacheReusePasses++;
				g_shadowMapStats.cacheBudgetReusePasses += schedule.budgetReuse ? 1 : 0;
				g_shadowMapSubviewReusePasses += subviewView ? 1 : 0;
				g_projectedShadowMapState = schedule.projectedEntry->state;
				RB_ShadowMapSelectProjectedCacheEntry( schedule.projectedEntry );
				return schedule;
			}
		}
		g_shadowMapStats.cacheMissPasses++;
	}

	const int updateBudget = r_shadowMapMaxUpdatesPerView.GetInteger();
	const bool budgetExhausted = updateBudget > 0 && g_shadowMapStats.shadowMapUpdates >= updateBudget;
	// importance admissions (M4): when the budget constrains the frame, the
	// most important stale lights were selected up front; anything else is
	// denied a fresh render even before the running count fills up
	const bool admissionDenied = updateBudget > 0 && g_shadowMapAdmissionsActive && !RB_ShadowMapUpdateAdmitted( lightIndex );
	const bool subviewDenied = subviewView && subviewPolicy == 1;
	if ( ( budgetExhausted || admissionDenied || subviewDenied ) && !mapRequired ) {
		if ( admissionDenied && !budgetExhausted ) {
			g_shadowMapStats.admissionDeniedPasses++;
		}
		// Exact hits were handled above. Old depth can contain a door's closed
		// pose after it has moved into the live caster chain, or omit a blocker
		// revealed by an opening portal. Composing today's movers cannot erase
		// that obsolete depth. Use complete current stencil coverage on a miss.
		g_shadowMapSubviewFallbackPasses += subviewDenied ? 1 : 0;
		schedule.action = SHADOWMAP_SCHEDULE_FALLBACK;
		return schedule;
	}

	if ( schedule.cacheable ) {
		if ( pointLight ) {
			schedule.pointEntry = RB_ShadowMapAllocPointCacheEntry();
			RB_ShadowMapSelectPointCacheEntry( schedule.pointEntry );
		} else {
			schedule.projectedEntry = RB_ShadowMapAllocProjectedCacheEntry( RB_ShadowMapAtlasDivForLight( vLight ) );
			RB_ShadowMapSelectProjectedCacheEntry( schedule.projectedEntry );
		}
		if ( ( pointLight && schedule.pointEntry == NULL ) || ( !pointLight && schedule.projectedEntry == NULL ) ) {
			schedule.cacheable = false;
		}
	}

	return schedule;
}

static void RB_ShadowMapCompleteCacheUpdate( const shadowMapSchedule_t &schedule, const viewLight_t *vLight, const shadowMapPassKind_t requestedPassKind, const bool pointLight, const bool renderOk ) {
	// must mirror RB_ShadowMapSchedulePass's collapsed cache identity, or the
	// stored entry would never be found by the collapsed lookup
	const shadowMapPassKind_t passKind = RB_ShadowMapCachePassKind( vLight, requestedPassKind );
	if ( !schedule.cacheable ) {
		return;
	}
	const int lightIndex = RB_ShadowMapLightIndex( vLight );
	if ( pointLight ) {
		pointShadowMapCacheEntry_t *entry = schedule.pointEntry;
		if ( entry == NULL ) {
			return;
		}
		if ( !renderOk ) {
			entry->valid = false;
			return;
		}
		entry->valid = true;
		entry->lightIndex = lightIndex;
		entry->passKind = static_cast<int>( passKind );
		entry->lightClass = static_cast<int>( RB_ShadowMapLightClass( vLight ) );
		entry->signature = schedule.signature;
		entry->size = g_pointShadowMapRenderTexture != NULL ? g_pointShadowMapRenderTexture->GetWidth() : 0;
		entry->highPrecision = RB_PointShadowMapHighPrecisionEnabled();
		entry->depthCompare = RB_PointShadowMapDepthCompareEnabled();
		entry->lightOrigin[0] = vLight->globalLightOrigin[0];
		entry->lightOrigin[1] = vLight->globalLightOrigin[1];
		entry->lightOrigin[2] = vLight->globalLightOrigin[2];
		entry->farDistance = R_ShadowMapPointFarDistance( vLight );
		entry->lastUsedFrame = tr.frameCount;
		entry->lastUpdatedFrame = tr.frameCount;
		entry->colorImage = g_pointShadowMapColorImage;
		entry->depthImage = g_pointShadowMapDepthImage;
		entry->renderTexture = g_pointShadowMapRenderTexture;
		entry->colorStorageGeneration = entry->colorImage != NULL
			? entry->colorImage->GetStorageGeneration() : 0;
		entry->depthStorageGeneration = entry->depthImage != NULL
			? entry->depthImage->GetStorageGeneration() : 0;
	} else {
		projectedShadowMapCacheEntry_t *entry = schedule.projectedEntry;
		if ( entry == NULL ) {
			return;
		}
		if ( !renderOk ) {
			entry->valid = false;
			return;
		}
		entry->valid = true;
		entry->lightIndex = lightIndex;
		entry->passKind = static_cast<int>( passKind );
		entry->lightClass = static_cast<int>( RB_ShadowMapLightClass( vLight ) );
		entry->signature = schedule.signature;
		entry->tileSize = g_projectedShadowMapState.tileSize;
		entry->atlasDiv = g_projectedShadowMapState.atlasDiv;
		entry->cascadeCount = g_projectedShadowMapState.cascadeCount;
		entry->lastUsedFrame = tr.frameCount;
		entry->lastUpdatedFrame = tr.frameCount;
		entry->atlasStorageGeneration = g_shadowMapAtlasDepthImage != NULL
			? g_shadowMapAtlasDepthImage->GetStorageGeneration() : 0;
		entry->state = g_projectedShadowMapState;
		// atlas cell placement was written at allocation; entries hold no textures
		
	}
}

/*
=============
RB_ShadowMapReportProjectedSplits
=============
*/
static void RB_ShadowMapReportProjectedSplits( const viewLight_t *vLight, const float zNear, const float maxDistance, const int cascadeCount ) {
	if ( idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) < 2 || !g_shadowMapReportThisFrame || vLight == NULL ) {
		return;
	}

	const char *shaderName = vLight->lightShader != NULL ? vLight->lightShader->GetName() : "<null>";
	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
	common->Printf( "SM csm '%s' class=%s csm=%d projectedCSM=%d/%d zNear=%.2f max=%.2f cascades=%d debug=%s splits",
		shaderName,
		R_ShadowMapLightClassName( classification.lightClass ),
		classification.csmEnabled ? 1 : 0,
		classification.projectedCSMEnabled ? 1 : 0,
		classification.projectedCSMGateApplies ? 1 : 0,
		zNear,
		maxDistance,
		cascadeCount,
		RB_ShadowMapDebugModeName( RB_ShadowMapDebugMode() ) );
	for ( int splitIndex = 0; splitIndex < cascadeCount - 1; splitIndex++ ) {
		common->Printf( " [%d]=%.2f", splitIndex, g_projectedShadowMapState.splitDepths[splitIndex] );
	}
	common->Printf( "\n" );
}

static bool RB_ShadowMapProjectedFloatReady( const float value ) {
	return value == value && idMath::Fabs( value ) < 1.0e28f;
}

static bool RB_ShadowMapProjectedAtlasRectReady( const idVec4 &rect ) {
	const float epsilon = 0.0001f;
	return RB_ShadowMapProjectedFloatReady( rect.x )
		&& RB_ShadowMapProjectedFloatReady( rect.y )
		&& RB_ShadowMapProjectedFloatReady( rect.z )
		&& RB_ShadowMapProjectedFloatReady( rect.w )
		&& rect.x >= -epsilon
		&& rect.y >= -epsilon
		&& rect.z <= 1.0f + epsilon
		&& rect.w <= 1.0f + epsilon
		&& rect.z > rect.x + epsilon
		&& rect.w > rect.y + epsilon;
}

static bool RB_ShadowMapProjectedClipPlanesReady( const idPlane clipPlanes[4] ) {
	for ( int planeIndex = 0; planeIndex < 4; planeIndex++ ) {
		for ( int componentIndex = 0; componentIndex < 4; componentIndex++ ) {
			if ( !RB_ShadowMapProjectedFloatReady( clipPlanes[planeIndex][componentIndex] ) ) {
				return false;
			}
		}
	}
	return true;
}

static bool RB_ShadowMapProjectedFloatClose( const float lhs, const float rhs, const float epsilon = 0.001f ) {
	return RB_ShadowMapProjectedFloatReady( lhs ) && RB_ShadowMapProjectedFloatReady( rhs ) && idMath::Fabs( lhs - rhs ) <= epsilon;
}

static bool RB_ShadowMapProjectedBaseClipPlanesMatchLight( const viewLight_t *vLight, const shadowMapProjectedLightState_t &state ) {
	if ( vLight == NULL ) {
		return false;
	}

	idPlane expectedBaseClipPlanes[4];
	R_ShadowMapBuildBaseClipPlanesForLight( vLight, expectedBaseClipPlanes );
	for ( int planeIndex = 0; planeIndex < 4; planeIndex++ ) {
		for ( int componentIndex = 0; componentIndex < 4; componentIndex++ ) {
			if ( !RB_ShadowMapProjectedFloatClose( state.baseClipPlanes[planeIndex][componentIndex], expectedBaseClipPlanes[planeIndex][componentIndex] ) ) {
				return false;
			}
		}
	}
	return true;
}

static bool RB_ShadowMapProjectedFitAccountingReady( const shadowMapProjectedCascadeFit_t &fit ) {
	if ( !fit.attempted ) {
		return true;
	}
	return fit.sampleCount > 0
		&& fit.validPoints >= 0
		&& fit.skippedPoints >= 0
		&& fit.validPoints + fit.skippedPoints == fit.sampleCount
		&& fit.positiveWPoints + fit.negativeWPoints + fit.nearZeroWPoints + fit.nanWPoints == fit.sampleCount
		&& ( fit.mixedWSigns == ( fit.positiveWPoints > 0 && fit.negativeWPoints > 0 ) )
		&& ( fit.valid || fit.fallbackReason != SHADOWMAP_PROJECTED_FALLBACK_NONE );
}

static bool RB_ShadowMapProjectedFallbackReasonReady( const shadowMapProjectedLightState_t &state ) {
	if ( !state.cascadeFallback ) {
		return state.fallbackReason == SHADOWMAP_PROJECTED_FALLBACK_NONE;
	}
	if ( state.fallbackCascade < 0 || state.fallbackCascade >= SHADOWMAP_PROJECTED_MAX_CASCADES ) {
		return false;
	}

	const shadowMapProjectedCascadeFit_t &fit = state.cascadeFit[state.fallbackCascade];
	if ( !fit.attempted || fit.fallbackReason != state.fallbackReason || state.fallbackReason == SHADOWMAP_PROJECTED_FALLBACK_NONE ) {
		return false;
	}
	if ( state.fallbackReason == SHADOWMAP_PROJECTED_FALLBACK_MIXED_W_SIGNS ) {
		return fit.mixedWSigns && fit.positiveWPoints > 0 && fit.negativeWPoints > 0;
	}
	if ( state.fallbackReason == SHADOWMAP_PROJECTED_FALLBACK_INSUFFICIENT_VALID_SAMPLES ) {
		return fit.validPoints < 4;
	}
	if ( state.fallbackReason == SHADOWMAP_PROJECTED_FALLBACK_COLLAPSED_BOUNDS ) {
		return fit.collapsedBounds && fit.validPoints >= 4;
	}
	return false;
}

static bool RB_ShadowMapValidateProjectedStateContract( const viewLight_t *vLight, const shadowMapProjectedLightState_t &state, const char **reason ) {
	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
	const int cascadeCount = state.cascadeCount;
	if ( reason != NULL ) {
		*reason = "ok";
	}
	if ( vLight == NULL ) {
		if ( reason != NULL ) {
			*reason = "null light";
		}
		return false;
	}
	if ( classification.pointLight ) {
		if ( reason != NULL ) {
			*reason = "point light reached projected path";
		}
		return false;
	}
	if ( !state.valid ) {
		if ( reason != NULL ) {
			*reason = "state not valid";
		}
		return false;
	}
	if ( !RB_ShadowMapProjectedFloatReady( state.projectionPad ) || state.projectionPad < 0.0f || !RB_ShadowMapProjectedFloatClose( state.projectionScale, R_ShadowMapProjectionScale( state.projectionPad ) ) ) {
		if ( reason != NULL ) {
			*reason = "invalid projected projection pad or scale";
		}
		return false;
	}
	if ( !RB_ShadowMapProjectedClipPlanesReady( state.baseClipPlanes ) || !RB_ShadowMapProjectedBaseClipPlanesMatchLight( vLight, state ) ) {
		if ( reason != NULL ) {
			*reason = "projected base clip planes do not match padded light projection";
		}
		return false;
	}
	if ( cascadeCount < 1 || cascadeCount > SHADOWMAP_PROJECTED_MAX_CASCADES ) {
		if ( reason != NULL ) {
			*reason = "cascade count outside projected-state limits";
		}
		return false;
	}
	if ( state.requestedCascadeCount != classification.cascadeCount ) {
		if ( reason != NULL ) {
			*reason = "requested cascade count differs from classification";
		}
		return false;
	}
	if ( state.tileSize <= 0 || state.atlasDiv < 1 || state.atlasDiv * state.atlasDiv < cascadeCount ) {
		if ( reason != NULL ) {
			*reason = "invalid tile size or atlas grid";
		}
		return false;
	}
	for ( int fitIndex = 0; fitIndex < SHADOWMAP_PROJECTED_MAX_CASCADES; fitIndex++ ) {
		if ( !RB_ShadowMapProjectedFitAccountingReady( state.cascadeFit[fitIndex] ) ) {
			if ( reason != NULL ) {
				*reason = "projected sample fit accounting is inconsistent";
			}
			return false;
		}
	}
	if ( !RB_ShadowMapProjectedFallbackReasonReady( state ) ) {
		if ( reason != NULL ) {
			*reason = "projected fallback reason does not match sample validation";
		}
		return false;
	}
	if ( state.cascadeFallback ) {
		if ( state.requestedCascadeCount <= 1 || cascadeCount != 1 || state.fallbackCascade < 0 || state.fallbackCascade >= state.requestedCascadeCount ) {
			if ( reason != NULL ) {
				*reason = "invalid projected cascade fallback";
			}
			return false;
		}
	} else if ( classification.csmEnabled ) {
		if ( cascadeCount != classification.cascadeCount ) {
			if ( reason != NULL ) {
				*reason = "CSM projected state does not match classification";
			}
			return false;
		}
	} else if ( cascadeCount != 1 ) {
		if ( reason != NULL ) {
			*reason = "single projected state has multiple cascades";
		}
		return false;
	}

	for ( int cascadeIndex = 0; cascadeIndex < cascadeCount; cascadeIndex++ ) {
		if ( !RB_ShadowMapProjectedAtlasRectReady( state.atlasRect[cascadeIndex] ) ) {
			if ( reason != NULL ) {
				*reason = "projected atlas rect outside bounds";
			}
			return false;
		}
		if ( !RB_ShadowMapProjectedClipPlanesReady( state.clipPlanes[cascadeIndex] ) ) {
			if ( reason != NULL ) {
				*reason = "projected clip planes contain invalid values";
			}
			return false;
		}
		if ( !RB_ShadowMapProjectedFloatReady( state.sliceNear[cascadeIndex] ) || !RB_ShadowMapProjectedFloatReady( state.sliceFar[cascadeIndex] ) || state.sliceFar[cascadeIndex] <= state.sliceNear[cascadeIndex] ) {
			if ( reason != NULL ) {
				*reason = "projected slice range is invalid";
			}
			return false;
		}
		if ( !RB_ShadowMapProjectedFloatReady( state.depthRange[cascadeIndex] ) || state.depthRange[cascadeIndex] <= 0.0f ) {
			if ( reason != NULL ) {
				*reason = "projected depth range is invalid";
			}
			return false;
		}
		if ( cascadeIndex > 0 && state.sliceNear[cascadeIndex] < state.sliceFar[cascadeIndex - 1] - 0.25f ) {
			if ( reason != NULL ) {
				*reason = "projected slice ranges are not monotonic";
			}
			return false;
		}
	}
	if ( cascadeCount > 1 ) {
		float previousSplit = 0.0f;
		for ( int splitIndex = 0; splitIndex < cascadeCount - 1; splitIndex++ ) {
			const float splitDepth = state.splitDepths[splitIndex];
			if ( !RB_ShadowMapProjectedFloatReady( splitDepth ) || splitDepth <= previousSplit ) {
				if ( reason != NULL ) {
					*reason = "projected split depths are not monotonic";
				}
				return false;
			}
			previousSplit = splitDepth;
		}
	}

	// Depth-convention invariants: caster and receiver agree by sharing one
	// falloff depth row, which per-cascade cropping must never touch, and the
	// bias depth normalization (falloff extent) is identical for every
	// cascade. These are only consistent by parallel construction today; a
	// refactor that splits the conventions must fail here, not on screen.
	for ( int cascadeIndex = 0; cascadeIndex < cascadeCount; cascadeIndex++ ) {
		for ( int i = 0; i < 4; i++ ) {
			if ( state.clipPlanes[cascadeIndex][2][i] != state.baseClipPlanes[2][i]
				|| state.clipPlanes[cascadeIndex][3][i] != state.baseClipPlanes[3][i] ) {
				if ( reason != NULL ) {
					*reason = "projected cascade depth/Q rows diverge from the base convention";
				}
				return false;
			}
		}
		if ( idMath::Fabs( state.depthRange[cascadeIndex] - state.depthRange[0] ) > 0.01f ) {
			if ( reason != NULL ) {
				*reason = "projected cascade bias depth normalization is not uniform";
			}
			return false;
		}
	}

	return true;
}

static bool RB_ShadowMapValidateProjectedState( const viewLight_t *vLight, const char **reason ) {
	return RB_ShadowMapValidateProjectedStateContract( vLight, g_projectedShadowMapState, reason );
}

static void RB_ShadowMapReportProjectedCascadeFit( const viewLight_t *vLight, const int cascadeIndex, const int cascadeCount, const shadowMapProjectedCascadeFit_t &fit ) {
	if ( ( fit.skippedPoints <= 0 && !fit.mixedWSigns && fit.fallbackReason == SHADOWMAP_PROJECTED_FALLBACK_NONE ) || idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) < 2 || !g_shadowMapReportThisFrame || vLight == NULL ) {
		return;
	}

	const char *shaderName = vLight->lightShader != NULL ? vLight->lightShader->GetName() : "<null>";
	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
	common->Printf(
		"SM csm fit '%s' class=%s projectedCSM=%d/%d cascade=%d/%d range=[%.2f %.2f] samples=%d valid=%d skipped=%d w(+%d -%d zero=%d nan=%d mixed=%d) invalidNdc=%d reason=%s\n",
		shaderName,
		R_ShadowMapLightClassName( classification.lightClass ),
		classification.projectedCSMEnabled ? 1 : 0,
		classification.projectedCSMGateApplies ? 1 : 0,
		cascadeIndex,
		cascadeCount,
		fit.sliceNear,
		fit.sliceFar,
		fit.sampleCount,
		fit.validPoints,
		fit.skippedPoints,
		fit.positiveWPoints,
		fit.negativeWPoints,
		fit.nearZeroWPoints,
		fit.nanWPoints,
		fit.mixedWSigns ? 1 : 0,
		fit.invalidNdcPoints,
		R_ShadowMapProjectedFallbackReasonName( fit.fallbackReason ) );
}

static void RB_ShadowMapReportProjectedCascadeFallback( const viewLight_t *vLight, const int requestedCascadeCount, const int cascadeIndex, const shadowMapProjectedCascadeFit_t &fit, const int fallbackReason ) {
	if ( idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) < 2 || !g_shadowMapReportThisFrame || vLight == NULL ) {
		return;
	}

	const char *shaderName = vLight->lightShader != NULL ? vLight->lightShader->GetName() : "<null>";
	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
	common->Printf(
		"SM csm fallback '%s' class=%s projectedCSM=%d/%d requested=%d failedCascade=%d reason=%s range=[%.2f %.2f] samples=%d valid=%d skipped=%d w(+%d -%d zero=%d nan=%d mixed=%d) invalidNdc=%d collapsed=%d -> single-cascade\n",
		shaderName,
		R_ShadowMapLightClassName( classification.lightClass ),
		classification.projectedCSMEnabled ? 1 : 0,
		classification.projectedCSMGateApplies ? 1 : 0,
		requestedCascadeCount,
		cascadeIndex,
		R_ShadowMapProjectedFallbackReasonName( fallbackReason ),
		fit.sliceNear,
		fit.sliceFar,
		fit.sampleCount,
		fit.validPoints,
		fit.skippedPoints,
		fit.positiveWPoints,
		fit.negativeWPoints,
		fit.nearZeroWPoints,
		fit.nanWPoints,
		fit.mixedWSigns ? 1 : 0,
		fit.invalidNdcPoints,
		fit.collapsedBounds ? 1 : 0 );
}

static void RB_ShadowMapProjectedSampleSummary( const shadowMapProjectedLightState_t &state, int &attemptedCascades, int &sampleCount, int &validCount, int &skippedCount, int &positiveWCount, int &negativeWCount, int &nearZeroWCount, int &nanWCount, int &invalidNdcCount, int &mixedWSignCascades ) {
	attemptedCascades = 0;
	sampleCount = 0;
	validCount = 0;
	skippedCount = 0;
	positiveWCount = 0;
	negativeWCount = 0;
	nearZeroWCount = 0;
	nanWCount = 0;
	invalidNdcCount = 0;
	mixedWSignCascades = 0;

	for ( int cascadeIndex = 0; cascadeIndex < SHADOWMAP_PROJECTED_MAX_CASCADES; cascadeIndex++ ) {
		const shadowMapProjectedCascadeFit_t &fit = state.cascadeFit[cascadeIndex];
		if ( !fit.attempted ) {
			continue;
		}
		attemptedCascades++;
		sampleCount += fit.sampleCount;
		validCount += fit.validPoints;
		skippedCount += fit.skippedPoints;
		positiveWCount += fit.positiveWPoints;
		negativeWCount += fit.negativeWPoints;
		nearZeroWCount += fit.nearZeroWPoints;
		nanWCount += fit.nanWPoints;
		invalidNdcCount += fit.invalidNdcPoints;
		mixedWSignCascades += fit.mixedWSigns ? 1 : 0;
	}
}

/*
=============
RB_ShadowMapBuildProjectedState

Builds projected-light shadow-map state, including interior cascade split planes and per-cascade clip volumes.
=============
*/
static void RB_ShadowMapReportProjectedState( const viewLight_t *vLight ) {
	const int requestedCascadeCount = Max( 1, g_projectedShadowMapState.requestedCascadeCount );
	const int cascadeCount = Max( 1, g_projectedShadowMapState.cascadeCount );
	const int reportLevel = idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() );
	int attemptedCascades = 0;
	int sampleCount = 0;
	int validCount = 0;
	int skippedCount = 0;
	int positiveWCount = 0;
	int negativeWCount = 0;
	int nearZeroWCount = 0;
	int nanWCount = 0;
	int invalidNdcCount = 0;
	int mixedWSignCascades = 0;
	RB_ShadowMapProjectedSampleSummary( g_projectedShadowMapState, attemptedCascades, sampleCount, validCount, skippedCount, positiveWCount, negativeWCount, nearZeroWCount, nanWCount, invalidNdcCount, mixedWSignCascades );
	if ( reportLevel >= 2 && g_shadowMapReportThisFrame && vLight != NULL ) {
		const char *shaderName = vLight->lightShader != NULL ? vLight->lightShader->GetName() : "<null>";
		const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
		common->Printf(
			"SM projected-state '%s' class=%s csm=%d projectedCSM=%d/%d requested=%d cascades=%d tiles=%d atlasDiv=%d tileSize=%d fallback=%d/%d reason=%s projectionPad=%.3f projectionScale=%.6f baseClip0=%.6f,%.6f,%.6f,%.6f sampleValidation(attempted=%d samples=%d valid=%d skipped=%d w=+%d/-%d/zero%d/nan%d invalidNdc=%d mixed=%d) debug=%s\n",
			shaderName,
			R_ShadowMapLightClassName( classification.lightClass ),
			classification.csmEnabled ? 1 : 0,
			classification.projectedCSMEnabled ? 1 : 0,
			classification.projectedCSMGateApplies ? 1 : 0,
			requestedCascadeCount,
			cascadeCount,
			cascadeCount,
			Max( 1, g_projectedShadowMapState.atlasDiv ),
			Max( 1, g_projectedShadowMapState.tileSize ),
			g_projectedShadowMapState.cascadeFallback ? 1 : 0,
			g_projectedShadowMapState.fallbackCascade,
			R_ShadowMapProjectedFallbackReasonName( g_projectedShadowMapState.fallbackReason ),
			g_projectedShadowMapState.projectionPad,
			g_projectedShadowMapState.projectionScale,
			g_projectedShadowMapState.baseClipPlanes[0][0],
			g_projectedShadowMapState.baseClipPlanes[0][1],
			g_projectedShadowMapState.baseClipPlanes[0][2],
			g_projectedShadowMapState.baseClipPlanes[0][3],
			attemptedCascades,
			sampleCount,
			validCount,
			skippedCount,
			positiveWCount,
			negativeWCount,
			nearZeroWCount,
			nanWCount,
			invalidNdcCount,
			mixedWSignCascades,
			RB_ShadowMapDebugModeName( RB_ShadowMapDebugMode() ) );
	}
	if ( requestedCascadeCount > 1 && g_projectedShadowMapState.cascadeFallback ) {
		const int fallbackCascade = idMath::ClampInt( 0, SHADOWMAP_MAX_CASCADES - 1, g_projectedShadowMapState.fallbackCascade );
		const shadowMapProjectedCascadeFit_t &fit = g_projectedShadowMapState.cascadeFit[fallbackCascade];
		RB_ShadowMapReportProjectedCascadeFallback( vLight, requestedCascadeCount, fallbackCascade, fit, g_projectedShadowMapState.fallbackReason );
	} else if ( requestedCascadeCount > 1 ) {
		for ( int cascadeIndex = 0; cascadeIndex < cascadeCount && cascadeIndex < SHADOWMAP_MAX_CASCADES; cascadeIndex++ ) {
			const shadowMapProjectedCascadeFit_t &fit = g_projectedShadowMapState.cascadeFit[cascadeIndex];
			if ( fit.attempted ) {
				RB_ShadowMapReportProjectedCascadeFit( vLight, cascadeIndex, cascadeCount, fit );
			}
		}
	}

	if ( cascadeCount > 1 ) {
		RB_ShadowMapReportProjectedSplits( vLight, g_projectedShadowMapState.sliceNear[0], g_projectedShadowMapState.sliceFar[cascadeCount - 1], cascadeCount );
	}
	if ( reportLevel >= 2 && g_shadowMapReportThisFrame && vLight != NULL ) {
		const char *shaderName = vLight->lightShader != NULL ? vLight->lightShader->GetName() : "<null>";
		const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
		common->Printf(
			"SM projected bias '%s' class=%s csm=%d projectedCSM=%d/%d",
			shaderName,
			R_ShadowMapLightClassName( classification.lightClass ),
			classification.csmEnabled ? 1 : 0,
			classification.projectedCSMEnabled ? 1 : 0,
			classification.projectedCSMGateApplies ? 1 : 0 );
		for ( int cascadeIndex = 0; cascadeIndex < cascadeCount; cascadeIndex++ ) {
			common->Printf( " [%d]=%.3f texel=%.3f texBias=%.6f range=%.2f-%.2f depth=%.2f clipZ=%.3f", cascadeIndex,
				g_projectedShadowMapState.biasScale[cascadeIndex],
				g_projectedShadowMapState.worldTexelSize[cascadeIndex],
				g_projectedShadowMapState.texelDepthBias[cascadeIndex],
				g_projectedShadowMapState.sliceNear[cascadeIndex],
				g_projectedShadowMapState.sliceFar[cascadeIndex],
				g_projectedShadowMapState.depthRange[cascadeIndex],
				g_projectedShadowMapState.clipZExtent[cascadeIndex] );
		}
		common->Printf( "\n" );
	}
}

static void RB_ShadowMapBuildProjectedState( const viewLight_t *vLight, const int tileSize ) {
	R_BuildShadowMapProjectedLightState( vLight, backEnd.viewDef, tileSize, g_projectedShadowMapState );
	const char *invariantReason = "ok";
	if ( g_projectedShadowMapState.valid && !RB_ShadowMapValidateProjectedState( vLight, &invariantReason ) ) {
		common->Warning(
			"SM projected-state invariant failed: light=%d reason=%s requested=%d cascades=%d atlasDiv=%d tileSize=%d fallback=%d/%d fallbackReason=%s",
			vLight != NULL && vLight->lightDef != NULL ? vLight->lightDef->index : -1,
			invariantReason,
			g_projectedShadowMapState.requestedCascadeCount,
			g_projectedShadowMapState.cascadeCount,
			g_projectedShadowMapState.atlasDiv,
			g_projectedShadowMapState.tileSize,
			g_projectedShadowMapState.cascadeFallback ? 1 : 0,
			g_projectedShadowMapState.fallbackCascade,
			R_ShadowMapProjectedFallbackReasonName( g_projectedShadowMapState.fallbackReason ) );
		assert( !"projected shadow-map state invariant failed" );
		R_ShadowMapResetProjectedLightState( g_projectedShadowMapState );
		return;
	}
	RB_ShadowMapReportProjectedState( vLight );
}

static void RB_ShadowMapFreeProgram( void ) {
	if ( g_shadowMapProgram.programObject != 0 ) {
		if ( g_shadowMapProgram.vertexShaderObject != 0 ) {
			glDetachObjectARB( g_shadowMapProgram.programObject, g_shadowMapProgram.vertexShaderObject );
			glDeleteObjectARB( g_shadowMapProgram.vertexShaderObject );
		}
		if ( g_shadowMapProgram.fragmentShaderObject != 0 ) {
			glDetachObjectARB( g_shadowMapProgram.programObject, g_shadowMapProgram.fragmentShaderObject );
			glDeleteObjectARB( g_shadowMapProgram.fragmentShaderObject );
		}
		glDeleteObjectARB( g_shadowMapProgram.programObject );
	}

	memset( &g_shadowMapProgram, 0, sizeof( g_shadowMapProgram ) );
}

static void RB_ShadowMapFreeCasterProgram( void ) {
	if ( g_shadowMapCasterProgram.programObject != 0 ) {
		if ( g_shadowMapCasterProgram.vertexShaderObject != 0 ) {
			glDetachObjectARB( g_shadowMapCasterProgram.programObject, g_shadowMapCasterProgram.vertexShaderObject );
			glDeleteObjectARB( g_shadowMapCasterProgram.vertexShaderObject );
		}
		if ( g_shadowMapCasterProgram.fragmentShaderObject != 0 ) {
			glDetachObjectARB( g_shadowMapCasterProgram.programObject, g_shadowMapCasterProgram.fragmentShaderObject );
			glDeleteObjectARB( g_shadowMapCasterProgram.fragmentShaderObject );
		}
		glDeleteObjectARB( g_shadowMapCasterProgram.programObject );
	}

	memset( &g_shadowMapCasterProgram, 0, sizeof( g_shadowMapCasterProgram ) );
}

static void RB_TranslucentShadowMapFreeCasterProgram( void ) {
	if ( g_translucentShadowCasterProgram.programObject != 0 ) {
		if ( g_translucentShadowCasterProgram.vertexShaderObject != 0 ) {
			glDetachObjectARB( g_translucentShadowCasterProgram.programObject, g_translucentShadowCasterProgram.vertexShaderObject );
			glDeleteObjectARB( g_translucentShadowCasterProgram.vertexShaderObject );
		}
		if ( g_translucentShadowCasterProgram.fragmentShaderObject != 0 ) {
			glDetachObjectARB( g_translucentShadowCasterProgram.programObject, g_translucentShadowCasterProgram.fragmentShaderObject );
			glDeleteObjectARB( g_translucentShadowCasterProgram.fragmentShaderObject );
		}
		glDeleteObjectARB( g_translucentShadowCasterProgram.programObject );
	}

	memset( &g_translucentShadowCasterProgram, 0, sizeof( g_translucentShadowCasterProgram ) );
}

// programGeneration is the load-attempt cache key, so it has to say "never
// attempted" rather than "attempted during video restart 0" after a reset.
static void RB_MaterialInteractionResetProgramState( void ) {
	memset( &g_materialInteractionProgram, 0, sizeof( g_materialInteractionProgram ) );
	g_materialInteractionProgram.programGeneration = -1;
}

static void RB_MaterialInteractionFreeProgram( void ) {
	if ( g_materialInteractionProgram.programObject != 0 ) {
		if ( g_materialInteractionProgram.vertexShaderObject != 0 ) {
			glDetachObjectARB( g_materialInteractionProgram.programObject, g_materialInteractionProgram.vertexShaderObject );
			glDeleteObjectARB( g_materialInteractionProgram.vertexShaderObject );
		}
		if ( g_materialInteractionProgram.fragmentShaderObject != 0 ) {
			glDetachObjectARB( g_materialInteractionProgram.programObject, g_materialInteractionProgram.fragmentShaderObject );
			glDeleteObjectARB( g_materialInteractionProgram.fragmentShaderObject );
		}
		glDeleteObjectARB( g_materialInteractionProgram.programObject );
	}

	RB_MaterialInteractionResetProgramState();
}

static void RB_PointShadowMapFreeProgram( void ) {
	if ( g_pointShadowMapProgram.programObject != 0 ) {
		if ( g_pointShadowMapProgram.vertexShaderObject != 0 ) {
			glDetachObjectARB( g_pointShadowMapProgram.programObject, g_pointShadowMapProgram.vertexShaderObject );
			glDeleteObjectARB( g_pointShadowMapProgram.vertexShaderObject );
		}
		if ( g_pointShadowMapProgram.fragmentShaderObject != 0 ) {
			glDetachObjectARB( g_pointShadowMapProgram.programObject, g_pointShadowMapProgram.fragmentShaderObject );
			glDeleteObjectARB( g_pointShadowMapProgram.fragmentShaderObject );
		}
		glDeleteObjectARB( g_pointShadowMapProgram.programObject );
	}

	memset( &g_pointShadowMapProgram, 0, sizeof( g_pointShadowMapProgram ) );
}

static void RB_PointShadowMapFreeCasterProgram( void ) {
	if ( g_pointShadowCasterProgram.programObject != 0 ) {
		if ( g_pointShadowCasterProgram.vertexShaderObject != 0 ) {
			glDetachObjectARB( g_pointShadowCasterProgram.programObject, g_pointShadowCasterProgram.vertexShaderObject );
			glDeleteObjectARB( g_pointShadowCasterProgram.vertexShaderObject );
		}
		if ( g_pointShadowCasterProgram.fragmentShaderObject != 0 ) {
			glDetachObjectARB( g_pointShadowCasterProgram.programObject, g_pointShadowCasterProgram.fragmentShaderObject );
			glDeleteObjectARB( g_pointShadowCasterProgram.fragmentShaderObject );
		}
		glDeleteObjectARB( g_pointShadowCasterProgram.programObject );
	}

	memset( &g_pointShadowCasterProgram, 0, sizeof( g_pointShadowCasterProgram ) );
}

static void RB_PointTranslucentShadowMapFreeCasterProgram( void ) {
	if ( g_pointTranslucentShadowCasterProgram.programObject != 0 ) {
		if ( g_pointTranslucentShadowCasterProgram.vertexShaderObject != 0 ) {
			glDetachObjectARB( g_pointTranslucentShadowCasterProgram.programObject, g_pointTranslucentShadowCasterProgram.vertexShaderObject );
			glDeleteObjectARB( g_pointTranslucentShadowCasterProgram.vertexShaderObject );
		}
		if ( g_pointTranslucentShadowCasterProgram.fragmentShaderObject != 0 ) {
			glDetachObjectARB( g_pointTranslucentShadowCasterProgram.programObject, g_pointTranslucentShadowCasterProgram.fragmentShaderObject );
			glDeleteObjectARB( g_pointTranslucentShadowCasterProgram.fragmentShaderObject );
		}
		glDeleteObjectARB( g_pointTranslucentShadowCasterProgram.programObject );
	}

	memset( &g_pointTranslucentShadowCasterProgram, 0, sizeof( g_pointTranslucentShadowCasterProgram ) );
}

static void RB_ShadowMapDebugOverlayFreeProgram( void ) {
	if ( g_shadowDebugOverlayProgram.programObject != 0 ) {
		if ( g_shadowDebugOverlayProgram.vertexShaderObject != 0 ) {
			glDetachObjectARB( g_shadowDebugOverlayProgram.programObject, g_shadowDebugOverlayProgram.vertexShaderObject );
			glDeleteObjectARB( g_shadowDebugOverlayProgram.vertexShaderObject );
		}
		if ( g_shadowDebugOverlayProgram.fragmentShaderObject != 0 ) {
			glDetachObjectARB( g_shadowDebugOverlayProgram.programObject, g_shadowDebugOverlayProgram.fragmentShaderObject );
			glDeleteObjectARB( g_shadowDebugOverlayProgram.fragmentShaderObject );
		}
		glDeleteObjectARB( g_shadowDebugOverlayProgram.programObject );
	}

	memset( &g_shadowDebugOverlayProgram, 0, sizeof( g_shadowDebugOverlayProgram ) );
}

static void RB_ShadowMapDestroyRenderTexture( idRenderTexture *&renderTexture ) {
	if ( renderTexture == NULL ) {
		return;
	}
	tr.DestroyRenderTexture( renderTexture );
	renderTexture = NULL;
}

static void RB_ShadowMapResetProgramStateNoGL( void ) {
	memset( &g_shadowMapProgram, 0, sizeof( g_shadowMapProgram ) );
	memset( &g_shadowMapCasterProgram, 0, sizeof( g_shadowMapCasterProgram ) );
	memset( &g_translucentShadowCasterProgram, 0, sizeof( g_translucentShadowCasterProgram ) );
	RB_MaterialInteractionResetProgramState();
	memset( &g_pointShadowMapProgram, 0, sizeof( g_pointShadowMapProgram ) );
	memset( &g_pointShadowCasterProgram, 0, sizeof( g_pointShadowCasterProgram ) );
	memset( &g_pointTranslucentShadowCasterProgram, 0, sizeof( g_pointTranslucentShadowCasterProgram ) );
	memset( &g_shadowDebugOverlayProgram, 0, sizeof( g_shadowDebugOverlayProgram ) );
}

void RB_ShutdownShadowMapResources( void ) {
	if ( glConfig.isInitialized ) {
		RB_ShadowMapFreeProgram();
		RB_ShadowMapFreeCasterProgram();
		RB_TranslucentShadowMapFreeCasterProgram();
		RB_MaterialInteractionFreeProgram();
		RB_PointShadowMapFreeProgram();
		RB_PointShadowMapFreeCasterProgram();
		RB_PointTranslucentShadowMapFreeCasterProgram();
		RB_ShadowMapDebugOverlayFreeProgram();
	} else {
		RB_ShadowMapResetProgramStateNoGL();
	}

	RB_ShadowMapDestroyRenderTexture( g_shadowMapScratchRenderTexture );
	RB_ShadowMapDestroyRenderTexture( g_shadowMapAtlasRenderTexture );
	RB_ShadowMapDestroyRenderTexture( g_translucentShadowMapRenderTexture );
	RB_ShadowMapDestroyRenderTexture( g_pointShadowMapScratchRenderTexture );
	RB_ShadowMapDestroyRenderTexture( g_pointTranslucentShadowMapRenderTexture );

	for ( int i = 0; i < SHADOWMAP_CACHE_MAX_SLOTS; i++ ) {
		
		RB_ShadowMapDestroyRenderTexture( g_pointShadowMapCache[i].renderTexture );
		RB_ShadowMapDestroyRenderTexture(
			g_sharedInteractionProjectedScratch[i].renderTexture );
		RB_ShadowMapDestroyRenderTexture(
			g_sharedInteractionPointScratch[i].renderTexture );
	}

	if ( glConfig.isInitialized && glDeleteQueries != NULL ) {
		for ( int i = 0; i < SHADOWMAP_GPU_TIMER_QUERY_SLOTS; i++ ) {
			if ( g_shadowMapGpuTimerQuerySlots[i].query != 0 ) {
				glDeleteQueries( 1, &g_shadowMapGpuTimerQuerySlots[i].query );
			}
		}
	}

	g_shadowMapDepthImage = NULL;
	g_shadowMapRenderTexture = NULL;
	g_shadowMapScratchDepthImage = NULL;
	g_shadowMapAtlasDepthImage = NULL;
	g_shadowMapAtlasCellSize = 0;
	g_shadowMapScratchRenderTexture = NULL;
	memset( g_translucentShadowMomentImages, 0, sizeof( g_translucentShadowMomentImages ) );
	g_translucentShadowMapRenderTexture = NULL;
	g_pointShadowMapColorImage = NULL;
	g_pointShadowMapDepthImage = NULL;
	g_pointShadowMapRenderTexture = NULL;
	g_pointShadowMapScratchColorImage = NULL;
	g_pointShadowMapScratchDepthImage = NULL;
	g_pointShadowMapScratchRenderTexture = NULL;
	memset( g_pointTranslucentShadowMomentImages, 0, sizeof( g_pointTranslucentShadowMomentImages ) );
	g_pointTranslucentShadowMapRenderTexture = NULL;
	memset( &g_projectedShadowMapState, 0, sizeof( g_projectedShadowMapState ) );
	g_projectedTranslucentShadowPassReady = false;
	g_pointTranslucentShadowPassReady = false;
	g_pointShadowMapDepthCompareAvailable = true;
	g_pointShadowMapDepthCompareAvailableGeneration = -1;
	memset( &g_shadowMapStats, 0, sizeof( g_shadowMapStats ) );
	g_shadowMapLastReportFrame = -0x3fffffff;
	g_shadowMapReportThisFrame = false;
	memset( &g_shadowMapDebugOverlayState, 0, sizeof( g_shadowMapDebugOverlayState ) );
	g_shadowMapDebugOverlayState.lightDefIndex = -1;
	memset( g_projectedShadowMapCache, 0, sizeof( g_projectedShadowMapCache ) );
	memset( g_pointShadowMapCache, 0, sizeof( g_pointShadowMapCache ) );
	memset( g_sharedInteractionProjectedScratch, 0,
		sizeof( g_sharedInteractionProjectedScratch ) );
	memset( g_sharedInteractionPointScratch, 0,
		sizeof( g_sharedInteractionPointScratch ) );
	memset( g_shadowMapLightHistory, 0, sizeof( g_shadowMapLightHistory ) );
	g_shadowMapCacheRenderWorld = NULL;
	g_shadowMapCacheMapFileCRC = 0;
	g_shadowMapCacheMapNameHash = 0;
	g_activeProjectedShadowMapCache = NULL;
	g_activePointShadowMapCache = NULL;
	memset( g_shadowMapGpuTimerQuerySlots, 0, sizeof( g_shadowMapGpuTimerQuerySlots ) );
	g_shadowMapGpuTimerQueryCursor = 0;
}

static void RB_ShadowMapPrintInfoLog( GLhandleARB object, const char *label, const char *name ) {
	GLint logLength = 0;
	glGetObjectParameterivARB( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &logLength );
	if ( logLength <= 1 ) {
		common->Warning( "GLSL %s error in '%s' (no info log)", label, name );
		return;
	}
	if ( logLength > 1024 * 1024 ) {
		common->Warning( "GLSL %s error in '%s' has oversized info log (%d bytes), truncating", label, name, logLength );
		logLength = 1024 * 1024;
	}

	char *logBuffer = (char *)Mem_ClearedAlloc( logLength + 1 );
	GLsizei written = 0;
	glGetInfoLogARB( object, logLength, &written, logBuffer );
	logBuffer[ idMath::ClampInt( 0, logLength, written ) ] = '\0';
	common->Warning( "GLSL %s error in '%s':\n%s", label, name, logBuffer );
	Mem_Free( logBuffer );
}

static bool RB_MaterialInteractionLoadProgram( void ) {
	static const char *programBaseName = "glprogs/material_interaction";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	// Key the cache on the attempt, not on the resulting object. Keying it on
	// programObject != 0 meant a failed compile or a missing .vs/.fs was never
	// remembered, so the whole load ran again for every surface of every light
	// of every frame.
	if ( g_materialInteractionProgram.programGeneration == tr.videoRestartCount ) {
		return g_materialInteractionProgram.programValid;
	}

	RB_MaterialInteractionFreeProgram();

	idStr vertexPath = idStr( programBaseName ) + ".vs";
	idStr fragmentPath = idStr( programBaseName ) + ".fs";

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( vertexPath.c_str(), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( fragmentPath.c_str(), (void **)&fragmentBuffer, NULL );
	if ( vertexBuffer == NULL || fragmentBuffer == NULL ) {
		if ( vertexBuffer != NULL ) {
			fileSystem->FreeFile( vertexBuffer );
		}
		if ( fragmentBuffer != NULL ) {
			fileSystem->FreeFile( fragmentBuffer );
		}
		common->Warning( "Couldn't load enhanced material GLSL sources '%s' and '%s'", vertexPath.c_str(), fragmentPath.c_str() );
		g_materialInteractionProgram.programGeneration = tr.videoRestartCount;
		g_materialInteractionProgram.programValid = false;
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( vertexShader, "vertex shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_materialInteractionProgram.programGeneration = tr.videoRestartCount;
		g_materialInteractionProgram.programValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( fragmentShader, "fragment shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_materialInteractionProgram.programGeneration = tr.videoRestartCount;
		g_materialInteractionProgram.programValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glBindAttribLocationARB( programObject, 8, "attr_TexCoord0" );
	glBindAttribLocationARB( programObject, 9, "attr_Tangent" );
	glBindAttribLocationARB( programObject, 10, "attr_Bitangent" );
	glBindAttribLocationARB( programObject, 11, "attr_Normal" );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( programObject, "program link", programBaseName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		g_materialInteractionProgram.programGeneration = tr.videoRestartCount;
		g_materialInteractionProgram.programValid = false;
		return false;
	}

	g_materialInteractionProgram.programObject = programObject;
	g_materialInteractionProgram.vertexShaderObject = vertexShader;
	g_materialInteractionProgram.fragmentShaderObject = fragmentShader;
	g_materialInteractionProgram.programGeneration = tr.videoRestartCount;
	g_materialInteractionProgram.programValid = true;

	g_materialInteractionProgram.localLightOrigin = glGetUniformLocationARB( programObject, "uLocalLightOrigin" );
	g_materialInteractionProgram.localViewOrigin = glGetUniformLocationARB( programObject, "uLocalViewOrigin" );
	g_materialInteractionProgram.lightProjectionS = glGetUniformLocationARB( programObject, "uLightProjectionS" );
	g_materialInteractionProgram.lightProjectionT = glGetUniformLocationARB( programObject, "uLightProjectionT" );
	g_materialInteractionProgram.lightProjectionQ = glGetUniformLocationARB( programObject, "uLightProjectionQ" );
	g_materialInteractionProgram.lightFalloffS = glGetUniformLocationARB( programObject, "uLightFalloffS" );
	g_materialInteractionProgram.bumpMatrixS = glGetUniformLocationARB( programObject, "uBumpMatrixS" );
	g_materialInteractionProgram.bumpMatrixT = glGetUniformLocationARB( programObject, "uBumpMatrixT" );
	g_materialInteractionProgram.diffuseMatrixS = glGetUniformLocationARB( programObject, "uDiffuseMatrixS" );
	g_materialInteractionProgram.diffuseMatrixT = glGetUniformLocationARB( programObject, "uDiffuseMatrixT" );
	g_materialInteractionProgram.specularMatrixS = glGetUniformLocationARB( programObject, "uSpecularMatrixS" );
	g_materialInteractionProgram.specularMatrixT = glGetUniformLocationARB( programObject, "uSpecularMatrixT" );
	g_materialInteractionProgram.vertexColorParams = glGetUniformLocationARB( programObject, "uVertexColorParams" );
	g_materialInteractionProgram.diffuseColor = glGetUniformLocationARB( programObject, "uDiffuseColor" );
	g_materialInteractionProgram.specularColor = glGetUniformLocationARB( programObject, "uSpecularColor" );
	g_materialInteractionProgram.flatDiffuseParams = glGetUniformLocationARB( programObject, "uFlatDiffuseParams" );
	g_materialInteractionProgram.materialNormalScale = glGetUniformLocationARB( programObject, "uMaterialNormalScale" );
	g_materialInteractionProgram.materialSpecularBoost = glGetUniformLocationARB( programObject, "uMaterialSpecularBoost" );
	g_materialInteractionProgram.materialFresnel = glGetUniformLocationARB( programObject, "uMaterialFresnel" );
	g_materialInteractionProgram.celParams = glGetUniformLocationARB( programObject, "uCelParams" );
	g_materialInteractionProgram.stockInteraction = glGetUniformLocationARB( programObject, "uStockInteraction" );
	g_materialInteractionProgram.ambientLight = glGetUniformLocationARB( programObject, "uAmbientLight" );
	g_materialInteractionProgram.ambientNormalMap = glGetUniformLocationARB( programObject, "uAmbientNormalMap" );
	g_materialInteractionProgram.bumpMap = glGetUniformLocationARB( programObject, "uBumpMap" );
	g_materialInteractionProgram.lightFalloffMap = glGetUniformLocationARB( programObject, "uLightFalloffMap" );
	g_materialInteractionProgram.lightProjectionMap = glGetUniformLocationARB( programObject, "uLightProjectionMap" );
	g_materialInteractionProgram.diffuseMap = glGetUniformLocationARB( programObject, "uDiffuseMap" );
	g_materialInteractionProgram.specularMap = glGetUniformLocationARB( programObject, "uSpecularMap" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static bool RB_ShadowMapLoadProgram( void ) {
static const char *programBaseName = "glprogs/shadow_interaction";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	const bool depthCompareMode = RB_ShadowMapDepthCompareEnabled();
	if ( g_shadowMapProgram.programObject != 0 && g_shadowMapProgram.programGeneration == tr.videoRestartCount && g_shadowMapProgram.depthCompareMode == depthCompareMode ) {
		return g_shadowMapProgram.programValid;
	}

	RB_ShadowMapFreeProgram();

	idStr vertexPath = idStr( programBaseName ) + ".vs";
	idStr fragmentPath = idStr( programBaseName ) + ".fs";

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( vertexPath.c_str(), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( fragmentPath.c_str(), (void **)&fragmentBuffer, NULL );
	if ( vertexBuffer == NULL || fragmentBuffer == NULL ) {
		if ( vertexBuffer != NULL ) {
			fileSystem->FreeFile( vertexBuffer );
		}
		if ( fragmentBuffer != NULL ) {
			fileSystem->FreeFile( fragmentBuffer );
		}
		common->Warning( "Couldn't load shadow-map GLSL sources '%s' and '%s'", vertexPath.c_str(), fragmentPath.c_str() );
		g_shadowMapProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapProgram.programValid = false;
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	if ( depthCompareMode ) {
		const char *fragmentVersionEnd = strchr( fragmentBuffer, '\n' );
		if ( fragmentVersionEnd != NULL ) {
			static const GLcharARB compareDefine[] = "#define OPENQ4_SHADOW_COMPARE 1\n";
			const GLcharARB *fragmentSources[3] = {
				(const GLcharARB *)fragmentBuffer,
				compareDefine,
				(const GLcharARB *)( fragmentVersionEnd + 1 )
			};
			const GLint fragmentLengths[3] = {
				static_cast<GLint>( fragmentVersionEnd - fragmentBuffer + 1 ),
				static_cast<GLint>( sizeof( compareDefine ) - 1 ),
				-1
			};
			glShaderSourceARB( fragmentShader, 3, fragmentSources, fragmentLengths );
		} else {
			glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
		}
	} else {
		glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	}
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( vertexShader, "vertex shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_shadowMapProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapProgram.programValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( fragmentShader, "fragment shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_shadowMapProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapProgram.programValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glBindAttribLocationARB( programObject, 8, "attr_TexCoord0" );
	glBindAttribLocationARB( programObject, 9, "attr_Tangent" );
	glBindAttribLocationARB( programObject, 10, "attr_Bitangent" );
	glBindAttribLocationARB( programObject, 11, "attr_Normal" );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( programObject, "program link", programBaseName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		g_shadowMapProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapProgram.programValid = false;
		return false;
	}

	g_shadowMapProgram.programObject = programObject;
	g_shadowMapProgram.vertexShaderObject = vertexShader;
	g_shadowMapProgram.fragmentShaderObject = fragmentShader;
	g_shadowMapProgram.programGeneration = tr.videoRestartCount;
	g_shadowMapProgram.programValid = true;
	g_shadowMapProgram.depthCompareMode = depthCompareMode;

	g_shadowMapProgram.localLightOrigin = glGetUniformLocationARB( programObject, "uLocalLightOrigin" );
	g_shadowMapProgram.localViewOrigin = glGetUniformLocationARB( programObject, "uLocalViewOrigin" );
	g_shadowMapProgram.lightProjectionS = glGetUniformLocationARB( programObject, "uLightProjectionS" );
	g_shadowMapProgram.lightProjectionT = glGetUniformLocationARB( programObject, "uLightProjectionT" );
	g_shadowMapProgram.lightProjectionQ = glGetUniformLocationARB( programObject, "uLightProjectionQ" );
	g_shadowMapProgram.lightFalloffS = glGetUniformLocationARB( programObject, "uLightFalloffS" );
	g_shadowMapProgram.bumpMatrixS = glGetUniformLocationARB( programObject, "uBumpMatrixS" );
	g_shadowMapProgram.bumpMatrixT = glGetUniformLocationARB( programObject, "uBumpMatrixT" );
	g_shadowMapProgram.diffuseMatrixS = glGetUniformLocationARB( programObject, "uDiffuseMatrixS" );
	g_shadowMapProgram.diffuseMatrixT = glGetUniformLocationARB( programObject, "uDiffuseMatrixT" );
	g_shadowMapProgram.specularMatrixS = glGetUniformLocationARB( programObject, "uSpecularMatrixS" );
	g_shadowMapProgram.specularMatrixT = glGetUniformLocationARB( programObject, "uSpecularMatrixT" );
	g_shadowMapProgram.shadowRow[0] = glGetUniformLocationARB( programObject, "uShadowRow0[0]" );
	g_shadowMapProgram.shadowRow[1] = glGetUniformLocationARB( programObject, "uShadowRow1[0]" );
	g_shadowMapProgram.shadowRow[2] = glGetUniformLocationARB( programObject, "uShadowRow2[0]" );
	g_shadowMapProgram.shadowRow[3] = glGetUniformLocationARB( programObject, "uShadowRow3[0]" );
	g_shadowMapProgram.vertexColorParams = glGetUniformLocationARB( programObject, "uVertexColorParams" );
	g_shadowMapProgram.diffuseColor = glGetUniformLocationARB( programObject, "uDiffuseColor" );
	g_shadowMapProgram.specularColor = glGetUniformLocationARB( programObject, "uSpecularColor" );
	g_shadowMapProgram.flatDiffuseParams = glGetUniformLocationARB( programObject, "uFlatDiffuseParams" );
	g_shadowMapProgram.materialEnhanced = glGetUniformLocationARB( programObject, "uMaterialEnhanced" );
	g_shadowMapProgram.materialNormalScale = glGetUniformLocationARB( programObject, "uMaterialNormalScale" );
	g_shadowMapProgram.materialSpecularBoost = glGetUniformLocationARB( programObject, "uMaterialSpecularBoost" );
	g_shadowMapProgram.materialFresnel = glGetUniformLocationARB( programObject, "uMaterialFresnel" );
	g_shadowMapProgram.celParams = glGetUniformLocationARB( programObject, "uCelParams" );
	g_shadowMapProgram.shadowTexelSize = glGetUniformLocationARB( programObject, "uShadowTexelSize" );
	g_shadowMapProgram.shadowBias = glGetUniformLocationARB( programObject, "uShadowBias" );
	g_shadowMapProgram.shadowNormalBias = glGetUniformLocationARB( programObject, "uShadowNormalBias" );
	g_shadowMapProgram.shadowTexelDepthBias = glGetUniformLocationARB( programObject, "uShadowTexelDepthBias[0]" );
	g_shadowMapProgram.shadowNormalOffsetWorld = glGetUniformLocationARB( programObject, "uShadowNormalOffsetWorld[0]" );
	g_shadowMapProgram.shadowReceiverPlaneBias = glGetUniformLocationARB( programObject, "uShadowReceiverPlaneBias" );
	g_shadowMapProgram.shadowFilterRadius = glGetUniformLocationARB( programObject, "uShadowFilterRadius" );
	g_shadowMapProgram.shadowFilterTaps = glGetUniformLocationARB( programObject, "uShadowFilterTaps" );
	g_shadowMapProgram.shadowFilterMode = glGetUniformLocationARB( programObject, "uShadowFilterMode" );
	g_shadowMapProgram.shadowPCSSLightRadius = glGetUniformLocationARB( programObject, "uShadowPCSSLightRadius" );
	g_shadowMapProgram.shadowPCSSMaxRadius = glGetUniformLocationARB( programObject, "uShadowPCSSMaxRadius" );
	g_shadowMapProgram.shadowAtlasRect = glGetUniformLocationARB( programObject, "uShadowAtlasRect[0]" );
	g_shadowMapProgram.shadowSplitDepths = glGetUniformLocationARB( programObject, "uShadowSplitDepths[0]" );
	g_shadowMapProgram.shadowCascadeBiasScale = glGetUniformLocationARB( programObject, "uShadowCascadeBiasScale[0]" );
	g_shadowMapProgram.shadowCascadeCount = glGetUniformLocationARB( programObject, "uShadowCascadeCount" );
	g_shadowMapProgram.shadowCascadeBlend = glGetUniformLocationARB( programObject, "uShadowCascadeBlend" );
	g_shadowMapProgram.shadowDebugMode = glGetUniformLocationARB( programObject, "uShadowDebugMode" );
	g_shadowMapProgram.shadowReceiverDebugReason = glGetUniformLocationARB( programObject, "uShadowReceiverDebugReason" );
	g_shadowMapProgram.translucentShadowEnabled = glGetUniformLocationARB( programObject, "uTranslucentShadowEnabled" );
	g_shadowMapProgram.translucentShadowDensity = glGetUniformLocationARB( programObject, "uTranslucentShadowDensity" );
	g_shadowMapProgram.translucentShadowFilterRadius = glGetUniformLocationARB( programObject, "uTranslucentShadowFilterRadius" );
	g_shadowMapProgram.translucentShadowMinVariance = glGetUniformLocationARB( programObject, "uTranslucentShadowMinVariance" );
	g_shadowMapProgram.translucentShadowBleedReduction = glGetUniformLocationARB( programObject, "uTranslucentShadowBleedReduction" );
	g_shadowMapProgram.bumpMap = glGetUniformLocationARB( programObject, "uBumpMap" );
	g_shadowMapProgram.lightFalloffMap = glGetUniformLocationARB( programObject, "uLightFalloffMap" );
	g_shadowMapProgram.lightProjectionMap = glGetUniformLocationARB( programObject, "uLightProjectionMap" );
	g_shadowMapProgram.diffuseMap = glGetUniformLocationARB( programObject, "uDiffuseMap" );
	g_shadowMapProgram.specularMap = glGetUniformLocationARB( programObject, "uSpecularMap" );
	g_shadowMapProgram.shadowMap = glGetUniformLocationARB( programObject, "uShadowMap" );
	g_shadowMapProgram.translucentShadowMap[0] = glGetUniformLocationARB( programObject, "uTranslucentShadowMapR" );
	g_shadowMapProgram.translucentShadowMap[1] = glGetUniformLocationARB( programObject, "uTranslucentShadowMapG" );
	g_shadowMapProgram.translucentShadowMap[2] = glGetUniformLocationARB( programObject, "uTranslucentShadowMapB" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static bool RB_ShadowMapLoadCasterProgram( void ) {
static const char *programBaseName = "glprogs/shadow_proj_caster";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	if ( g_shadowMapCasterProgram.programObject != 0 && g_shadowMapCasterProgram.programGeneration == tr.videoRestartCount ) {
		return g_shadowMapCasterProgram.programValid;
	}

	RB_ShadowMapFreeCasterProgram();

	idStr vertexPath = idStr( programBaseName ) + ".vs";
	idStr fragmentPath = idStr( programBaseName ) + ".fs";

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( vertexPath.c_str(), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( fragmentPath.c_str(), (void **)&fragmentBuffer, NULL );
	if ( vertexBuffer == NULL || fragmentBuffer == NULL ) {
		if ( vertexBuffer != NULL ) {
			fileSystem->FreeFile( vertexBuffer );
		}
		if ( fragmentBuffer != NULL ) {
			fileSystem->FreeFile( fragmentBuffer );
		}
		common->Warning( "Couldn't load shadow caster GLSL sources '%s' and '%s'", vertexPath.c_str(), fragmentPath.c_str() );
		g_shadowMapCasterProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapCasterProgram.programValid = false;
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( vertexShader, "vertex shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_shadowMapCasterProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapCasterProgram.programValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( fragmentShader, "fragment shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_shadowMapCasterProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapCasterProgram.programValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( programObject, "program link", programBaseName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		g_shadowMapCasterProgram.programGeneration = tr.videoRestartCount;
		g_shadowMapCasterProgram.programValid = false;
		return false;
	}

	g_shadowMapCasterProgram.programObject = programObject;
	g_shadowMapCasterProgram.vertexShaderObject = vertexShader;
	g_shadowMapCasterProgram.fragmentShaderObject = fragmentShader;
	g_shadowMapCasterProgram.programGeneration = tr.videoRestartCount;
	g_shadowMapCasterProgram.programValid = true;

	g_shadowMapCasterProgram.alphaTexCoordS = glGetUniformLocationARB( programObject, "uAlphaTexCoordS" );
	g_shadowMapCasterProgram.alphaTexCoordT = glGetUniformLocationARB( programObject, "uAlphaTexCoordT" );
	g_shadowMapCasterProgram.shadowDepthRow = glGetUniformLocationARB( programObject, "uShadowDepthRow" );
	g_shadowMapCasterProgram.casterDepthOffset = glGetUniformLocationARB( programObject, "uShadowCasterDepthOffset" );
	g_shadowMapCasterProgram.alphaTestEnabled = glGetUniformLocationARB( programObject, "uAlphaTestEnabled" );
	g_shadowMapCasterProgram.alphaRef = glGetUniformLocationARB( programObject, "uAlphaRef" );
	g_shadowMapCasterProgram.alphaTestMode = glGetUniformLocationARB( programObject, "uAlphaTestMode" );
	g_shadowMapCasterProgram.alphaScale = glGetUniformLocationARB( programObject, "uAlphaScale" );
	g_shadowMapCasterProgram.alphaHashEnabled = glGetUniformLocationARB( programObject, "uAlphaHashEnabled" );
	g_shadowMapCasterProgram.alphaHashStable = glGetUniformLocationARB( programObject, "uAlphaHashStable" );
	g_shadowMapCasterProgram.modelMatrixRow0 = glGetUniformLocationARB( programObject, "uModelMatrixRow0" );
	g_shadowMapCasterProgram.modelMatrixRow1 = glGetUniformLocationARB( programObject, "uModelMatrixRow1" );
	g_shadowMapCasterProgram.modelMatrixRow2 = glGetUniformLocationARB( programObject, "uModelMatrixRow2" );
	g_shadowMapCasterProgram.alphaMap = glGetUniformLocationARB( programObject, "uAlphaMap" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static bool RB_TranslucentShadowMapLoadCasterProgram( void ) {
static const char *programBaseName = "glprogs/shadow_proj_translucent_caster";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	if ( g_translucentShadowCasterProgram.programObject != 0 && g_translucentShadowCasterProgram.programGeneration == tr.videoRestartCount ) {
		return g_translucentShadowCasterProgram.programValid;
	}

	RB_TranslucentShadowMapFreeCasterProgram();

	idStr vertexPath = idStr( programBaseName ) + ".vs";
	idStr fragmentPath = idStr( programBaseName ) + ".fs";

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( vertexPath.c_str(), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( fragmentPath.c_str(), (void **)&fragmentBuffer, NULL );
	if ( vertexBuffer == NULL || fragmentBuffer == NULL ) {
		if ( vertexBuffer != NULL ) {
			fileSystem->FreeFile( vertexBuffer );
		}
		if ( fragmentBuffer != NULL ) {
			fileSystem->FreeFile( fragmentBuffer );
		}
		common->Warning( "Couldn't load translucent shadow caster GLSL sources '%s' and '%s'", vertexPath.c_str(), fragmentPath.c_str() );
		g_translucentShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_translucentShadowCasterProgram.programValid = false;
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( vertexShader, "vertex shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_translucentShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_translucentShadowCasterProgram.programValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( fragmentShader, "fragment shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_translucentShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_translucentShadowCasterProgram.programValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( programObject, "program link", programBaseName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		g_translucentShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_translucentShadowCasterProgram.programValid = false;
		return false;
	}

	g_translucentShadowCasterProgram.programObject = programObject;
	g_translucentShadowCasterProgram.vertexShaderObject = vertexShader;
	g_translucentShadowCasterProgram.fragmentShaderObject = fragmentShader;
	g_translucentShadowCasterProgram.programGeneration = tr.videoRestartCount;
	g_translucentShadowCasterProgram.programValid = true;

	g_translucentShadowCasterProgram.modelMatrixRow0 = glGetUniformLocationARB( programObject, "uModelMatrixRow0" );
	g_translucentShadowCasterProgram.modelMatrixRow1 = glGetUniformLocationARB( programObject, "uModelMatrixRow1" );
	g_translucentShadowCasterProgram.modelMatrixRow2 = glGetUniformLocationARB( programObject, "uModelMatrixRow2" );
	g_translucentShadowCasterProgram.shadowDepthRow = glGetUniformLocationARB( programObject, "uShadowDepthRow" );
	g_translucentShadowCasterProgram.alphaTexCoordS = glGetUniformLocationARB( programObject, "uAlphaTexCoordS" );
	g_translucentShadowCasterProgram.alphaTexCoordT = glGetUniformLocationARB( programObject, "uAlphaTexCoordT" );
	g_translucentShadowCasterProgram.coverageTexCoordS = glGetUniformLocationARB( programObject, "uCoverageTexCoordS" );
	g_translucentShadowCasterProgram.coverageTexCoordT = glGetUniformLocationARB( programObject, "uCoverageTexCoordT" );
	g_translucentShadowCasterProgram.stageColor = glGetUniformLocationARB( programObject, "uStageColor" );
	g_translucentShadowCasterProgram.opacitySourceMode = glGetUniformLocationARB( programObject, "uOpacitySourceMode" );
	g_translucentShadowCasterProgram.vertexColorMode = glGetUniformLocationARB( programObject, "uVertexColorMode" );
	g_translucentShadowCasterProgram.vertexAlphaParams = glGetUniformLocationARB( programObject, "uVertexAlphaParams" );
	g_translucentShadowCasterProgram.coverageStageColor = glGetUniformLocationARB( programObject, "uCoverageStageColor" );
	g_translucentShadowCasterProgram.coverageSourceMode = glGetUniformLocationARB( programObject, "uCoverageSourceMode" );
	g_translucentShadowCasterProgram.coverageVertexColorMode = glGetUniformLocationARB( programObject, "uCoverageVertexColorMode" );
	g_translucentShadowCasterProgram.coverageVertexAlphaParams = glGetUniformLocationARB( programObject, "uCoverageVertexAlphaParams" );
	g_translucentShadowCasterProgram.coverageAlphaTestRef = glGetUniformLocationARB( programObject, "uCoverageAlphaTestRef" );
	g_translucentShadowCasterProgram.coverageAlphaTestMode = glGetUniformLocationARB( programObject, "uCoverageAlphaTestMode" );
	g_translucentShadowCasterProgram.coverageAlphaTestEnabled = glGetUniformLocationARB( programObject, "uCoverageAlphaTestEnabled" );
	g_translucentShadowCasterProgram.translucentMinAlpha = glGetUniformLocationARB( programObject, "uTranslucentMinAlpha" );
	g_translucentShadowCasterProgram.alphaMap = glGetUniformLocationARB( programObject, "uAlphaMap" );
	g_translucentShadowCasterProgram.coverageMap = glGetUniformLocationARB( programObject, "uCoverageMap" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static bool RB_PointShadowMapLoadProgram( void ) {
static const char *programBaseName = "glprogs/shadow_point_interaction";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	const bool depthCompareMode = RB_PointShadowMapDepthCompareEnabled();
	if ( g_pointShadowMapProgram.programObject != 0 && g_pointShadowMapProgram.programGeneration == tr.videoRestartCount && g_pointShadowMapProgram.depthCompareMode == depthCompareMode ) {
		return g_pointShadowMapProgram.programValid;
	}

	RB_PointShadowMapFreeProgram();

	idStr vertexPath = idStr( programBaseName ) + ".vs";
	idStr fragmentPath = idStr( programBaseName ) + ".fs";

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( vertexPath.c_str(), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( fragmentPath.c_str(), (void **)&fragmentBuffer, NULL );
	if ( vertexBuffer == NULL || fragmentBuffer == NULL ) {
		if ( vertexBuffer != NULL ) {
			fileSystem->FreeFile( vertexBuffer );
		}
		if ( fragmentBuffer != NULL ) {
			fileSystem->FreeFile( fragmentBuffer );
		}
		common->Warning( "Couldn't load point shadow-map GLSL sources '%s' and '%s'", vertexPath.c_str(), fragmentPath.c_str() );
		g_pointShadowMapProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowMapProgram.programValid = false;
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	if ( depthCompareMode ) {
		const char *fragmentVersionEnd = strchr( fragmentBuffer, '\n' );
		if ( fragmentVersionEnd != NULL ) {
			static const GLcharARB compareVersion[] = "#version 130\n#define OPENQ4_POINT_SHADOW_COMPARE 1\n";
			const GLcharARB *fragmentSources[2] = {
				compareVersion,
				(const GLcharARB *)( fragmentVersionEnd + 1 )
			};
			const GLint fragmentLengths[2] = {
				static_cast<GLint>( sizeof( compareVersion ) - 1 ),
				-1
			};
			glShaderSourceARB( fragmentShader, 2, fragmentSources, fragmentLengths );
		} else {
			glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
		}
	} else {
		glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	}
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( vertexShader, "vertex shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_pointShadowMapProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowMapProgram.programValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( fragmentShader, "fragment shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_pointShadowMapProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowMapProgram.programValid = false;
		if ( depthCompareMode ) {
			common->Warning( "Point shadow-map depth compare GLSL failed; falling back to color-depth cubemap for this video generation" );
			RB_PointShadowMapDisableDepthCompareForGeneration();
			return RB_PointShadowMapLoadProgram();
		}
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glBindAttribLocationARB( programObject, 8, "attr_TexCoord0" );
	glBindAttribLocationARB( programObject, 9, "attr_Tangent" );
	glBindAttribLocationARB( programObject, 10, "attr_Bitangent" );
	glBindAttribLocationARB( programObject, 11, "attr_Normal" );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( programObject, "program link", programBaseName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		g_pointShadowMapProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowMapProgram.programValid = false;
		if ( depthCompareMode ) {
			common->Warning( "Point shadow-map depth compare GLSL link failed; falling back to color-depth cubemap for this video generation" );
			RB_PointShadowMapDisableDepthCompareForGeneration();
			return RB_PointShadowMapLoadProgram();
		}
		return false;
	}

	g_pointShadowMapProgram.programObject = programObject;
	g_pointShadowMapProgram.vertexShaderObject = vertexShader;
	g_pointShadowMapProgram.fragmentShaderObject = fragmentShader;
	g_pointShadowMapProgram.programGeneration = tr.videoRestartCount;
	g_pointShadowMapProgram.programValid = true;
	g_pointShadowMapProgram.depthCompareMode = depthCompareMode;

	g_pointShadowMapProgram.localLightOrigin = glGetUniformLocationARB( programObject, "uLocalLightOrigin" );
	g_pointShadowMapProgram.localViewOrigin = glGetUniformLocationARB( programObject, "uLocalViewOrigin" );
	g_pointShadowMapProgram.lightProjectionS = glGetUniformLocationARB( programObject, "uLightProjectionS" );
	g_pointShadowMapProgram.lightProjectionT = glGetUniformLocationARB( programObject, "uLightProjectionT" );
	g_pointShadowMapProgram.lightProjectionQ = glGetUniformLocationARB( programObject, "uLightProjectionQ" );
	g_pointShadowMapProgram.lightFalloffS = glGetUniformLocationARB( programObject, "uLightFalloffS" );
	g_pointShadowMapProgram.bumpMatrixS = glGetUniformLocationARB( programObject, "uBumpMatrixS" );
	g_pointShadowMapProgram.bumpMatrixT = glGetUniformLocationARB( programObject, "uBumpMatrixT" );
	g_pointShadowMapProgram.diffuseMatrixS = glGetUniformLocationARB( programObject, "uDiffuseMatrixS" );
	g_pointShadowMapProgram.diffuseMatrixT = glGetUniformLocationARB( programObject, "uDiffuseMatrixT" );
	g_pointShadowMapProgram.specularMatrixS = glGetUniformLocationARB( programObject, "uSpecularMatrixS" );
	g_pointShadowMapProgram.specularMatrixT = glGetUniformLocationARB( programObject, "uSpecularMatrixT" );
	g_pointShadowMapProgram.modelMatrixRow0 = glGetUniformLocationARB( programObject, "uModelMatrixRow0" );
	g_pointShadowMapProgram.modelMatrixRow1 = glGetUniformLocationARB( programObject, "uModelMatrixRow1" );
	g_pointShadowMapProgram.modelMatrixRow2 = glGetUniformLocationARB( programObject, "uModelMatrixRow2" );
	g_pointShadowMapProgram.globalLightOrigin = glGetUniformLocationARB( programObject, "uGlobalLightOrigin" );
	g_pointShadowMapProgram.pointShadowFar = glGetUniformLocationARB( programObject, "uPointShadowFar" );
	g_pointShadowMapProgram.vertexColorParams = glGetUniformLocationARB( programObject, "uVertexColorParams" );
	g_pointShadowMapProgram.diffuseColor = glGetUniformLocationARB( programObject, "uDiffuseColor" );
	g_pointShadowMapProgram.specularColor = glGetUniformLocationARB( programObject, "uSpecularColor" );
	g_pointShadowMapProgram.flatDiffuseParams = glGetUniformLocationARB( programObject, "uFlatDiffuseParams" );
	g_pointShadowMapProgram.materialEnhanced = glGetUniformLocationARB( programObject, "uMaterialEnhanced" );
	g_pointShadowMapProgram.materialNormalScale = glGetUniformLocationARB( programObject, "uMaterialNormalScale" );
	g_pointShadowMapProgram.materialSpecularBoost = glGetUniformLocationARB( programObject, "uMaterialSpecularBoost" );
	g_pointShadowMapProgram.materialFresnel = glGetUniformLocationARB( programObject, "uMaterialFresnel" );
	g_pointShadowMapProgram.celParams = glGetUniformLocationARB( programObject, "uCelParams" );
	g_pointShadowMapProgram.shadowBias = glGetUniformLocationARB( programObject, "uShadowBias" );
	g_pointShadowMapProgram.shadowNormalBias = glGetUniformLocationARB( programObject, "uShadowNormalBias" );
	g_pointShadowMapProgram.pointShadowTexelDepthBias = glGetUniformLocationARB( programObject, "uPointShadowTexelDepthBias" );
	g_pointShadowMapProgram.pointShadowNormalOffsetWorld = glGetUniformLocationARB( programObject, "uPointShadowNormalOffsetWorld" );
	g_pointShadowMapProgram.shadowFilterRadius = glGetUniformLocationARB( programObject, "uShadowFilterRadius" );
	g_pointShadowMapProgram.shadowFilterTaps = glGetUniformLocationARB( programObject, "uShadowFilterTaps" );
	g_pointShadowMapProgram.shadowFilterMode = glGetUniformLocationARB( programObject, "uShadowFilterMode" );
	g_pointShadowMapProgram.shadowDebugMode = glGetUniformLocationARB( programObject, "uShadowDebugMode" );
	g_pointShadowMapProgram.shadowReceiverDebugReason = glGetUniformLocationARB( programObject, "uShadowReceiverDebugReason" );
	g_pointShadowMapProgram.pointShadowTexelScale = glGetUniformLocationARB( programObject, "uPointShadowTexelScale" );
	g_pointShadowMapProgram.pointShadowDepthMode = glGetUniformLocationARB( programObject, "uPointShadowDepthMode" );
	g_pointShadowMapProgram.translucentShadowEnabled = glGetUniformLocationARB( programObject, "uTranslucentShadowEnabled" );
	g_pointShadowMapProgram.translucentShadowDensity = glGetUniformLocationARB( programObject, "uTranslucentShadowDensity" );
	g_pointShadowMapProgram.translucentShadowFilterRadius = glGetUniformLocationARB( programObject, "uTranslucentShadowFilterRadius" );
	g_pointShadowMapProgram.translucentShadowMinVariance = glGetUniformLocationARB( programObject, "uTranslucentShadowMinVariance" );
	g_pointShadowMapProgram.translucentShadowBleedReduction = glGetUniformLocationARB( programObject, "uTranslucentShadowBleedReduction" );
	g_pointShadowMapProgram.bumpMap = glGetUniformLocationARB( programObject, "uBumpMap" );
	g_pointShadowMapProgram.lightFalloffMap = glGetUniformLocationARB( programObject, "uLightFalloffMap" );
	g_pointShadowMapProgram.lightProjectionMap = glGetUniformLocationARB( programObject, "uLightProjectionMap" );
	g_pointShadowMapProgram.diffuseMap = glGetUniformLocationARB( programObject, "uDiffuseMap" );
	g_pointShadowMapProgram.specularMap = glGetUniformLocationARB( programObject, "uSpecularMap" );
	g_pointShadowMapProgram.pointShadowMap = glGetUniformLocationARB( programObject, "uPointShadowMap" );
	g_pointShadowMapProgram.translucentShadowMap[0] = glGetUniformLocationARB( programObject, "uPointTranslucentShadowMapR" );
	g_pointShadowMapProgram.translucentShadowMap[1] = glGetUniformLocationARB( programObject, "uPointTranslucentShadowMapG" );
	g_pointShadowMapProgram.translucentShadowMap[2] = glGetUniformLocationARB( programObject, "uPointTranslucentShadowMapB" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static bool RB_PointShadowMapLoadCasterProgram( void ) {
static const char *programBaseName = "glprogs/shadow_point_caster";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	const bool depthCompareMode = RB_PointShadowMapDepthCompareEnabled();
	if ( g_pointShadowCasterProgram.programObject != 0 && g_pointShadowCasterProgram.programGeneration == tr.videoRestartCount && g_pointShadowCasterProgram.depthCompareMode == depthCompareMode ) {
		return g_pointShadowCasterProgram.programValid;
	}

	RB_PointShadowMapFreeCasterProgram();

	idStr vertexPath = idStr( programBaseName ) + ".vs";
	idStr fragmentPath = idStr( programBaseName ) + ".fs";

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( vertexPath.c_str(), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( fragmentPath.c_str(), (void **)&fragmentBuffer, NULL );
	if ( vertexBuffer == NULL || fragmentBuffer == NULL ) {
		if ( vertexBuffer != NULL ) {
			fileSystem->FreeFile( vertexBuffer );
		}
		if ( fragmentBuffer != NULL ) {
			fileSystem->FreeFile( fragmentBuffer );
		}
		common->Warning( "Couldn't load point shadow caster GLSL sources '%s' and '%s'", vertexPath.c_str(), fragmentPath.c_str() );
		g_pointShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowCasterProgram.programValid = false;
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	if ( depthCompareMode ) {
		// The compare variant writes radial depth to gl_FragDepth; the default
		// variant must not reference gl_FragDepth at all so the rasterized
		// fragment depth stays defined on every execution path.
		const char *fragmentVersionEnd = strchr( fragmentBuffer, '\n' );
		if ( fragmentVersionEnd != NULL ) {
			static const GLcharARB casterDepthDefine[] = "#define OPENQ4_POINT_SHADOW_CASTER_DEPTH 1\n";
			const GLcharARB *fragmentSources[3] = {
				(const GLcharARB *)fragmentBuffer,
				casterDepthDefine,
				(const GLcharARB *)( fragmentVersionEnd + 1 )
			};
			const GLint fragmentLengths[3] = {
				static_cast<GLint>( fragmentVersionEnd - fragmentBuffer + 1 ),
				static_cast<GLint>( sizeof( casterDepthDefine ) - 1 ),
				-1
			};
			glShaderSourceARB( fragmentShader, 3, fragmentSources, fragmentLengths );
		} else {
			glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
		}
	} else {
		glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	}
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( vertexShader, "vertex shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_pointShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowCasterProgram.programValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( fragmentShader, "fragment shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_pointShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowCasterProgram.programValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( programObject, "program link", programBaseName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		g_pointShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointShadowCasterProgram.programValid = false;
		return false;
	}

	g_pointShadowCasterProgram.programObject = programObject;
	g_pointShadowCasterProgram.vertexShaderObject = vertexShader;
	g_pointShadowCasterProgram.fragmentShaderObject = fragmentShader;
	g_pointShadowCasterProgram.programGeneration = tr.videoRestartCount;
	g_pointShadowCasterProgram.programValid = true;

	g_pointShadowCasterProgram.modelMatrixRow0 = glGetUniformLocationARB( programObject, "uModelMatrixRow0" );
	g_pointShadowCasterProgram.modelMatrixRow1 = glGetUniformLocationARB( programObject, "uModelMatrixRow1" );
	g_pointShadowCasterProgram.modelMatrixRow2 = glGetUniformLocationARB( programObject, "uModelMatrixRow2" );
	g_pointShadowCasterProgram.globalLightOrigin = glGetUniformLocationARB( programObject, "uGlobalLightOrigin" );
	g_pointShadowCasterProgram.pointShadowFar = glGetUniformLocationARB( programObject, "uPointShadowFar" );
	g_pointShadowCasterProgram.alphaTexCoordS = glGetUniformLocationARB( programObject, "uAlphaTexCoordS" );
	g_pointShadowCasterProgram.alphaTexCoordT = glGetUniformLocationARB( programObject, "uAlphaTexCoordT" );
	g_pointShadowCasterProgram.alphaRef = glGetUniformLocationARB( programObject, "uAlphaRef" );
	g_pointShadowCasterProgram.alphaTestMode = glGetUniformLocationARB( programObject, "uAlphaTestMode" );
	g_pointShadowCasterProgram.alphaScale = glGetUniformLocationARB( programObject, "uAlphaScale" );
	g_pointShadowCasterProgram.alphaTestEnabled = glGetUniformLocationARB( programObject, "uAlphaTestEnabled" );
	g_pointShadowCasterProgram.alphaHashEnabled = glGetUniformLocationARB( programObject, "uAlphaHashEnabled" );
	g_pointShadowCasterProgram.alphaHashStable = glGetUniformLocationARB( programObject, "uAlphaHashStable" );
	g_pointShadowCasterProgram.pointShadowDepthMode = glGetUniformLocationARB( programObject, "uPointShadowDepthMode" );
	g_pointShadowCasterProgram.casterDepthOffset = glGetUniformLocationARB( programObject, "uShadowCasterDepthOffset" );
	g_pointShadowCasterProgram.depthCompareMode = depthCompareMode;
	g_pointShadowCasterProgram.alphaMap = glGetUniformLocationARB( programObject, "uAlphaMap" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static bool RB_PointTranslucentShadowMapLoadCasterProgram( void ) {
static const char *programBaseName = "glprogs/shadow_point_translucent_caster";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	if ( g_pointTranslucentShadowCasterProgram.programObject != 0 && g_pointTranslucentShadowCasterProgram.programGeneration == tr.videoRestartCount ) {
		return g_pointTranslucentShadowCasterProgram.programValid;
	}

	RB_PointTranslucentShadowMapFreeCasterProgram();

	idStr vertexPath = idStr( programBaseName ) + ".vs";
	idStr fragmentPath = idStr( programBaseName ) + ".fs";

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( vertexPath.c_str(), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( fragmentPath.c_str(), (void **)&fragmentBuffer, NULL );
	if ( vertexBuffer == NULL || fragmentBuffer == NULL ) {
		if ( vertexBuffer != NULL ) {
			fileSystem->FreeFile( vertexBuffer );
		}
		if ( fragmentBuffer != NULL ) {
			fileSystem->FreeFile( fragmentBuffer );
		}
		common->Warning( "Couldn't load point translucent shadow caster GLSL sources '%s' and '%s'", vertexPath.c_str(), fragmentPath.c_str() );
		g_pointTranslucentShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointTranslucentShadowCasterProgram.programValid = false;
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( vertexShader, "vertex shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_pointTranslucentShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointTranslucentShadowCasterProgram.programValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( fragmentShader, "fragment shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_pointTranslucentShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointTranslucentShadowCasterProgram.programValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( programObject, "program link", programBaseName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		g_pointTranslucentShadowCasterProgram.programGeneration = tr.videoRestartCount;
		g_pointTranslucentShadowCasterProgram.programValid = false;
		return false;
	}

	g_pointTranslucentShadowCasterProgram.programObject = programObject;
	g_pointTranslucentShadowCasterProgram.vertexShaderObject = vertexShader;
	g_pointTranslucentShadowCasterProgram.fragmentShaderObject = fragmentShader;
	g_pointTranslucentShadowCasterProgram.programGeneration = tr.videoRestartCount;
	g_pointTranslucentShadowCasterProgram.programValid = true;

	g_pointTranslucentShadowCasterProgram.modelMatrixRow0 = glGetUniformLocationARB( programObject, "uModelMatrixRow0" );
	g_pointTranslucentShadowCasterProgram.modelMatrixRow1 = glGetUniformLocationARB( programObject, "uModelMatrixRow1" );
	g_pointTranslucentShadowCasterProgram.modelMatrixRow2 = glGetUniformLocationARB( programObject, "uModelMatrixRow2" );
	g_pointTranslucentShadowCasterProgram.globalLightOrigin = glGetUniformLocationARB( programObject, "uGlobalLightOrigin" );
	g_pointTranslucentShadowCasterProgram.pointShadowFar = glGetUniformLocationARB( programObject, "uPointShadowFar" );
	g_pointTranslucentShadowCasterProgram.alphaTexCoordS = glGetUniformLocationARB( programObject, "uAlphaTexCoordS" );
	g_pointTranslucentShadowCasterProgram.alphaTexCoordT = glGetUniformLocationARB( programObject, "uAlphaTexCoordT" );
	g_pointTranslucentShadowCasterProgram.coverageTexCoordS = glGetUniformLocationARB( programObject, "uCoverageTexCoordS" );
	g_pointTranslucentShadowCasterProgram.coverageTexCoordT = glGetUniformLocationARB( programObject, "uCoverageTexCoordT" );
	g_pointTranslucentShadowCasterProgram.stageColor = glGetUniformLocationARB( programObject, "uStageColor" );
	g_pointTranslucentShadowCasterProgram.opacitySourceMode = glGetUniformLocationARB( programObject, "uOpacitySourceMode" );
	g_pointTranslucentShadowCasterProgram.vertexColorMode = glGetUniformLocationARB( programObject, "uVertexColorMode" );
	g_pointTranslucentShadowCasterProgram.vertexAlphaParams = glGetUniformLocationARB( programObject, "uVertexAlphaParams" );
	g_pointTranslucentShadowCasterProgram.coverageStageColor = glGetUniformLocationARB( programObject, "uCoverageStageColor" );
	g_pointTranslucentShadowCasterProgram.coverageSourceMode = glGetUniformLocationARB( programObject, "uCoverageSourceMode" );
	g_pointTranslucentShadowCasterProgram.coverageVertexColorMode = glGetUniformLocationARB( programObject, "uCoverageVertexColorMode" );
	g_pointTranslucentShadowCasterProgram.coverageVertexAlphaParams = glGetUniformLocationARB( programObject, "uCoverageVertexAlphaParams" );
	g_pointTranslucentShadowCasterProgram.coverageAlphaTestRef = glGetUniformLocationARB( programObject, "uCoverageAlphaTestRef" );
	g_pointTranslucentShadowCasterProgram.coverageAlphaTestMode = glGetUniformLocationARB( programObject, "uCoverageAlphaTestMode" );
	g_pointTranslucentShadowCasterProgram.coverageAlphaTestEnabled = glGetUniformLocationARB( programObject, "uCoverageAlphaTestEnabled" );
	g_pointTranslucentShadowCasterProgram.translucentMinAlpha = glGetUniformLocationARB( programObject, "uTranslucentMinAlpha" );
	g_pointTranslucentShadowCasterProgram.alphaMap = glGetUniformLocationARB( programObject, "uAlphaMap" );
	g_pointTranslucentShadowCasterProgram.coverageMap = glGetUniformLocationARB( programObject, "uCoverageMap" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static bool RB_ShadowMapDebugOverlayLoadProgram( void ) {
static const char *programBaseName = "glprogs/shadow_debug_overlay";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	if ( g_shadowDebugOverlayProgram.programObject != 0 && g_shadowDebugOverlayProgram.programGeneration == tr.videoRestartCount ) {
		return g_shadowDebugOverlayProgram.programValid;
	}

	RB_ShadowMapDebugOverlayFreeProgram();

	idStr vertexPath = idStr( programBaseName ) + ".vs";
	idStr fragmentPath = idStr( programBaseName ) + ".fs";

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	fileSystem->ReadFile( vertexPath.c_str(), (void **)&vertexBuffer, NULL );
	fileSystem->ReadFile( fragmentPath.c_str(), (void **)&fragmentBuffer, NULL );
	if ( vertexBuffer == NULL || fragmentBuffer == NULL ) {
		if ( vertexBuffer != NULL ) {
			fileSystem->FreeFile( vertexBuffer );
		}
		if ( fragmentBuffer != NULL ) {
			fileSystem->FreeFile( fragmentBuffer );
		}
		common->Warning( "Couldn't load shadow debug overlay GLSL sources '%s' and '%s'", vertexPath.c_str(), fragmentPath.c_str() );
		g_shadowDebugOverlayProgram.programGeneration = tr.videoRestartCount;
		g_shadowDebugOverlayProgram.programValid = false;
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( vertexShader, "vertex shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_shadowDebugOverlayProgram.programGeneration = tr.videoRestartCount;
		g_shadowDebugOverlayProgram.programValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( fragmentShader, "fragment shader compile", programBaseName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		g_shadowDebugOverlayProgram.programGeneration = tr.videoRestartCount;
		g_shadowDebugOverlayProgram.programValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_ShadowMapPrintInfoLog( programObject, "program link", programBaseName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		g_shadowDebugOverlayProgram.programGeneration = tr.videoRestartCount;
		g_shadowDebugOverlayProgram.programValid = false;
		return false;
	}

	g_shadowDebugOverlayProgram.programObject = programObject;
	g_shadowDebugOverlayProgram.vertexShaderObject = vertexShader;
	g_shadowDebugOverlayProgram.fragmentShaderObject = fragmentShader;
	g_shadowDebugOverlayProgram.programGeneration = tr.videoRestartCount;
	g_shadowDebugOverlayProgram.programValid = true;

	g_shadowDebugOverlayProgram.screenSize = glGetUniformLocationARB( programObject, "uScreenSize" );
	g_shadowDebugOverlayProgram.mode = glGetUniformLocationARB( programObject, "uMode" );
	g_shadowDebugOverlayProgram.color = glGetUniformLocationARB( programObject, "uColor" );
	g_shadowDebugOverlayProgram.pointLight = glGetUniformLocationARB( programObject, "uPointLight" );
	g_shadowDebugOverlayProgram.atlasDiv = glGetUniformLocationARB( programObject, "uAtlasDiv" );
	g_shadowDebugOverlayProgram.cascadeCount = glGetUniformLocationARB( programObject, "uCascadeCount" );
	g_shadowDebugOverlayProgram.passMapped = glGetUniformLocationARB( programObject, "uPassMapped" );
	g_shadowDebugOverlayProgram.glyphCode = glGetUniformLocationARB( programObject, "uGlyphCode" );
	g_shadowDebugOverlayProgram.pointShadowDepthMode = glGetUniformLocationARB( programObject, "uPointShadowDepthMode" );
	g_shadowDebugOverlayProgram.shadowAtlasMap = glGetUniformLocationARB( programObject, "uShadowAtlasMap" );
	g_shadowDebugOverlayProgram.pointShadowMap = glGetUniformLocationARB( programObject, "uPointShadowMap" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static bool RB_ShadowMapEnsureResources( const viewLight_t *vLight ) {
	if ( !RB_ShadowMapLoadProgram() || !RB_ShadowMapLoadCasterProgram() ) {
		return false;
	}

	const int shadowMapTileSize = RB_ShadowMapTileSizeForLight( vLight );
	if ( shadowMapTileSize <= 0 ) {
		return false;
	}

	const int shadowMapAtlasDiv = RB_ShadowMapAtlasDivForLight( vLight );
	const int shadowMapSize = shadowMapTileSize * shadowMapAtlasDiv;

	// keep the shared atlas resident whenever the projected path is live: its
	// cell size tracks r_shadowMapSize, and a cell-size change invalidates
	// every cached tile placement
	{
		const int atlasCellSize = idMath::ClampInt( 128, glConfig.maxTextureSize > 0 ? glConfig.maxTextureSize : 4096, r_shadowMapSize.GetInteger() );
		const int atlasSize = RB_ShadowMapAtlasSizeValue();
		if ( atlasCellSize != g_shadowMapAtlasCellSize ) {
			for ( int i = 0; i < SHADOWMAP_CACHE_MAX_SLOTS; i++ ) {
				g_projectedShadowMapCache[i].valid = false;
			}
			g_shadowMapAtlasCellSize = atlasCellSize;
		}
		if ( g_shadowMapAtlasDepthImage == NULL ) {
			idImageOpts opts;
			opts.textureType = TT_2D;
			opts.format = FMT_DEPTH;
			opts.width = atlasSize;
			opts.height = atlasSize;
			opts.numLevels = 1;
			opts.isPersistant = true;
			g_shadowMapAtlasDepthImage = globalImages->ScratchImage( "_shadowMapAtlas", &opts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
		}
		if ( g_shadowMapAtlasRenderTexture == NULL && g_shadowMapAtlasDepthImage != NULL ) {
			g_shadowMapAtlasRenderTexture = tr.CreateRenderTexture( NULL, g_shadowMapAtlasDepthImage );
			if ( g_shadowMapAtlasRenderTexture != NULL ) {
				g_shadowMapAtlasRenderTexture->SetAllowIncomplete();
			}
		} else if ( g_shadowMapAtlasRenderTexture != NULL && ( g_shadowMapAtlasRenderTexture->GetWidth() != atlasSize || g_shadowMapAtlasRenderTexture->GetHeight() != atlasSize ) ) {
			tr.ResizeRenderTexture( g_shadowMapAtlasRenderTexture, atlasSize, atlasSize );
			for ( int i = 0; i < SHADOWMAP_CACHE_MAX_SLOTS; i++ ) {
				g_projectedShadowMapCache[i].valid = false;
			}
		}
	}

	if ( g_activeProjectedShadowMapCache != NULL ) {
		// cached lights render into their cell block of the shared atlas
		if ( g_shadowMapAtlasDepthImage == NULL || g_shadowMapAtlasRenderTexture == NULL ) {
			return false;
		}
		g_shadowMapDepthImage = g_shadowMapAtlasDepthImage;
		g_shadowMapRenderTexture = g_shadowMapAtlasRenderTexture;
	} else {
		if ( g_shadowMapScratchDepthImage == NULL ) {
			idImageOpts opts;
			opts.textureType = TT_2D;
			opts.format = FMT_DEPTH;
			opts.width = shadowMapSize;
			opts.height = shadowMapSize;
			opts.numLevels = 1;
			opts.isPersistant = true;
			g_shadowMapScratchDepthImage = globalImages->ScratchImage( "_shadowMapDepth", &opts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
		}

		if ( g_shadowMapScratchRenderTexture == NULL ) {
			g_shadowMapScratchRenderTexture = tr.CreateRenderTexture( NULL, g_shadowMapScratchDepthImage );
			if ( g_shadowMapScratchRenderTexture != NULL ) {
				// shadow targets are expendable: incompleteness falls back to stencil
				g_shadowMapScratchRenderTexture->SetAllowIncomplete();
			}
		} else if ( g_shadowMapScratchRenderTexture->GetWidth() != shadowMapSize || g_shadowMapScratchRenderTexture->GetHeight() != shadowMapSize ) {
			tr.ResizeRenderTexture( g_shadowMapScratchRenderTexture, shadowMapSize, shadowMapSize );
		}
		g_shadowMapDepthImage = g_shadowMapScratchDepthImage;
		g_shadowMapRenderTexture = g_shadowMapScratchRenderTexture;
	}

	if ( g_shadowMapDepthImage == NULL || g_shadowMapRenderTexture == NULL ) {
		return false;
	}

	GL_SelectTextureNoClient( 5 );
	g_shadowMapDepthImage->Bind();
	{
		// Comparison sampling gets hardware 2x2 PCF from linear filtering; the
		// manual path must read raw depth texels.
		const bool depthCompare = RB_ShadowMapDepthCompareEnabled();
		const GLint depthFilter = depthCompare ? GL_LINEAR : GL_NEAREST;
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, depthCompare ? GL_COMPARE_R_TO_TEXTURE : GL_NONE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, depthFilter );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, depthFilter );
	}
	for ( int i = 0; i < 3; i++ ) {
		RB_ShadowMapBindMomentTexture( 6 + i, GL_TEXTURE_2D,
			RB_ProjectedTranslucentShadowEnabled() ? g_translucentShadowMomentImages[i] : NULL );
	}
	backEnd.glState.currenttmu = -1;
	// Resource probing also runs before the caster pass. Restore a usable
	// texture unit for callers that bind images immediately after the probe.
	GL_SelectTexture( 0 );

	g_shadowMapProjectedResourcesOkGeneration = tr.videoRestartCount;
	return true;
}

static bool RB_PointShadowMapEnsureResources( void ) {
	if ( !glConfig.cubeMapAvailable ) {
		return false;
	}
	if ( !RB_PointShadowMapLoadProgram() || !RB_PointShadowMapLoadCasterProgram() ) {
		return false;
	}

	const int shadowMapSize = RB_ShadowMapPointSizeValue();
	idImage **colorImage = g_activePointShadowMapCache != NULL ? &g_activePointShadowMapCache->colorImage : &g_pointShadowMapScratchColorImage;
	idImage **depthImage = g_activePointShadowMapCache != NULL ? &g_activePointShadowMapCache->depthImage : &g_pointShadowMapScratchDepthImage;
	idRenderTexture **renderTexture = g_activePointShadowMapCache != NULL ? &g_activePointShadowMapCache->renderTexture : &g_pointShadowMapScratchRenderTexture;
	const int cacheSlot = g_activePointShadowMapCache != NULL ? static_cast<int>( g_activePointShadowMapCache - g_pointShadowMapCache ) : -1;

	if ( *colorImage == NULL ) {
		idImageOpts opts;
		opts.textureType = TT_CUBIC;
		opts.format = RB_PointShadowMapHighPrecisionEnabled() ? FMT_RGBA16F : FMT_RGBA8;
		opts.width = shadowMapSize;
		opts.height = shadowMapSize;
		opts.numLevels = 1;
		opts.isPersistant = true;
		*colorImage = globalImages->ScratchImage( cacheSlot >= 0 ? va( "_pointShadowMapColorCache%d", cacheSlot ) : "_pointShadowMapColor", &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	} else if ( ( *colorImage )->GetOpts().format != ( RB_PointShadowMapHighPrecisionEnabled() ? FMT_RGBA16F : FMT_RGBA8 ) ) {
		idImageOpts opts;
		opts.textureType = TT_CUBIC;
		opts.format = RB_PointShadowMapHighPrecisionEnabled() ? FMT_RGBA16F : FMT_RGBA8;
		opts.width = shadowMapSize;
		opts.height = shadowMapSize;
		opts.numLevels = 1;
		opts.isPersistant = true;
		*colorImage = globalImages->ScratchImage( cacheSlot >= 0 ? va( "_pointShadowMapColorCache%d", cacheSlot ) : "_pointShadowMapColor", &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	}

	if ( *depthImage == NULL ) {
		idImageOpts opts;
		opts.textureType = TT_CUBIC;
		opts.format = FMT_DEPTH;
		opts.width = shadowMapSize;
		opts.height = shadowMapSize;
		opts.numLevels = 1;
		opts.isPersistant = true;
		*depthImage = globalImages->ScratchImage( cacheSlot >= 0 ? va( "_pointShadowMapDepthCache%d", cacheSlot ) : "_pointShadowMapDepth", &opts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
	}

	if ( *renderTexture == NULL ) {
		*renderTexture = tr.CreateRenderTexture( *colorImage, *depthImage );
		if ( *renderTexture != NULL ) {
			// shadow targets are expendable: incompleteness falls back to stencil
			( *renderTexture )->SetAllowIncomplete();
		}
	} else if ( ( *renderTexture )->GetWidth() != shadowMapSize || ( *renderTexture )->GetHeight() != shadowMapSize ) {
		tr.ResizeRenderTexture( *renderTexture, shadowMapSize, shadowMapSize );
	}
	g_pointShadowMapColorImage = *colorImage;
	g_pointShadowMapDepthImage = *depthImage;
	g_pointShadowMapRenderTexture = *renderTexture;

	const bool resourcesOk = g_pointShadowMapColorImage != NULL && g_pointShadowMapDepthImage != NULL && g_pointShadowMapRenderTexture != NULL;
	if ( resourcesOk ) {
		g_shadowMapPointResourcesOkGeneration = tr.videoRestartCount;
		// seamless cube filtering removes the face-border discontinuities PCF
		// taps hit when their offsets cross a cube edge (global enable; other
		// cube sampling only benefits)
		static int seamlessGeneration = -1;
		if ( seamlessGeneration != tr.videoRestartCount ) {
			seamlessGeneration = tr.videoRestartCount;
			if ( glConfig.glVersion >= 3.2f || GLCapabilityProbe_HasExtension( "GL_ARB_seamless_cube_map" ) ) {
				glEnable( GL_TEXTURE_CUBE_MAP_SEAMLESS );
			}
		}
	}
	return resourcesOk;
}

static bool RB_ShadowMapEnsureTranslucentResources( const viewLight_t *vLight ) {
	if ( !RB_TranslucentShadowMomentsSupported() || !RB_TranslucentShadowMapLoadCasterProgram() ) {
		return false;
	}

	const int shadowMapTileSize = RB_ShadowMapTileSizeForLight( vLight );
	if ( shadowMapTileSize <= 0 ) {
		return false;
	}

	const int shadowMapAtlasDiv = RB_ShadowMapAtlasDivForLight( vLight );
	const int shadowMapSize = shadowMapTileSize * shadowMapAtlasDiv;

	static const char *imageNames[3] = {
		"_translucentShadowMapR",
		"_translucentShadowMapG",
		"_translucentShadowMapB"
	};

	for ( int i = 0; i < 3; i++ ) {
		if ( g_translucentShadowMomentImages[i] != NULL ) {
			continue;
		}

		idImageOpts opts;
		opts.textureType = TT_2D;
		opts.format = FMT_RGBA16F;
		opts.width = shadowMapSize;
		opts.height = shadowMapSize;
		opts.numLevels = 1;
		opts.isPersistant = true;
		g_translucentShadowMomentImages[i] = globalImages->ScratchImage( imageNames[i], &opts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );
	}

	const int desiredColorAttachments = 3;
	if ( g_translucentShadowMapRenderTexture != NULL && g_translucentShadowMapRenderTexture->GetNumColorImages() != desiredColorAttachments ) {
		tr.DestroyRenderTexture( g_translucentShadowMapRenderTexture );
		g_translucentShadowMapRenderTexture = NULL;
	}

	if ( g_translucentShadowMapRenderTexture == NULL ) {
		g_translucentShadowMapRenderTexture = tr.CreateRenderTexture( g_translucentShadowMomentImages[0], NULL, g_translucentShadowMomentImages[1], g_translucentShadowMomentImages[2] );
		if ( g_translucentShadowMapRenderTexture != NULL ) {
			g_translucentShadowMapRenderTexture->SetAllowIncomplete();
		}
	} else if ( g_translucentShadowMapRenderTexture->GetWidth() != shadowMapSize || g_translucentShadowMapRenderTexture->GetHeight() != shadowMapSize ) {
		tr.ResizeRenderTexture( g_translucentShadowMapRenderTexture, shadowMapSize, shadowMapSize );
	}

	return RB_TranslucentShadowMomentImagesReady( g_translucentShadowMomentImages ) && g_translucentShadowMapRenderTexture != NULL;
}

static bool RB_PointShadowMapEnsureTranslucentResources( void ) {
	if ( !glConfig.cubeMapAvailable || !RB_TranslucentShadowMomentsSupported() || !RB_PointTranslucentShadowMapLoadCasterProgram() ) {
		return false;
	}

	const int shadowMapSize = RB_ShadowMapPointSizeValue();

	static const char *imageNames[3] = {
		"_pointTranslucentShadowMapR",
		"_pointTranslucentShadowMapG",
		"_pointTranslucentShadowMapB"
	};

	for ( int i = 0; i < 3; i++ ) {
		if ( g_pointTranslucentShadowMomentImages[i] != NULL ) {
			continue;
		}

		idImageOpts opts;
		opts.textureType = TT_CUBIC;
		opts.format = FMT_RGBA16F;
		opts.width = shadowMapSize;
		opts.height = shadowMapSize;
		opts.numLevels = 1;
		opts.isPersistant = true;
		g_pointTranslucentShadowMomentImages[i] = globalImages->ScratchImage( imageNames[i], &opts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );
	}

	const int desiredColorAttachments = 3;
	if ( g_pointTranslucentShadowMapRenderTexture != NULL && g_pointTranslucentShadowMapRenderTexture->GetNumColorImages() != desiredColorAttachments ) {
		tr.DestroyRenderTexture( g_pointTranslucentShadowMapRenderTexture );
		g_pointTranslucentShadowMapRenderTexture = NULL;
	}

	if ( g_pointTranslucentShadowMapRenderTexture == NULL ) {
		g_pointTranslucentShadowMapRenderTexture = tr.CreateRenderTexture( g_pointTranslucentShadowMomentImages[0], NULL, g_pointTranslucentShadowMomentImages[1], g_pointTranslucentShadowMomentImages[2] );
		if ( g_pointTranslucentShadowMapRenderTexture != NULL ) {
			g_pointTranslucentShadowMapRenderTexture->SetAllowIncomplete();
		}
	} else if ( g_pointTranslucentShadowMapRenderTexture->GetWidth() != shadowMapSize || g_pointTranslucentShadowMapRenderTexture->GetHeight() != shadowMapSize ) {
		tr.ResizeRenderTexture( g_pointTranslucentShadowMapRenderTexture, shadowMapSize, shadowMapSize );
	}

	return RB_TranslucentShadowMomentImagesReady( g_pointTranslucentShadowMomentImages ) && g_pointTranslucentShadowMapRenderTexture != NULL;
}

// Keep shadow-map support reporting aligned with the interaction shadow admission rules.
static bool RB_ShadowMapTranslucentGlobalReceiverNeeded(
		const viewLight_t *vLight ) {
	return vLight != NULL
		&& vLight->translucentInteractions != NULL
		&& r_shadowMapTranslucentReceivers.GetBool()
		&& !r_skipTranslucent.GetBool();
}

static shadowMapLightSupportReason_t RB_ShadowMapLightPolicySupportReason( const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return SHADOWMAP_SUPPORT_NULL_LIGHT;
	}

	if ( vLight->lightDef != NULL ) {
		if ( vLight->lightDef->parms.noShadows ) {
			return SHADOWMAP_SUPPORT_NO_SHADOWS_FLAG;
		}
		if ( vLight->lightDef->parms.noDynamicShadows ) {
			return SHADOWMAP_SUPPORT_NO_DYNAMIC_SHADOWS_FLAG;
		}
	}

	if ( vLight->lightShader == NULL || vLight->lightShader->IsAmbientLight() ) {
		return SHADOWMAP_SUPPORT_AMBIENT_LIGHT;
	}

	if ( !vLight->lightShader->LightCastsShadows() ) {
		return SHADOWMAP_SUPPORT_LIGHT_SHADER_NO_SHADOWS;
	}

	return SHADOWMAP_SUPPORT_OK;
}

static shadowMapLightSupportReason_t RB_ShadowMapLightSupportReason( const viewLight_t *vLight ) {
	if ( !r_useShadowMap.GetBool() || !r_shadows.GetBool() ) {
		return !r_useShadowMap.GetBool() ? SHADOWMAP_SUPPORT_DISABLED : SHADOWMAP_SUPPORT_SHADOWS_DISABLED;
	}

	const shadowMapLightSupportReason_t policyReason = RB_ShadowMapLightPolicySupportReason( vLight );
	if ( policyReason != SHADOWMAP_SUPPORT_OK ) {
		return policyReason;
	}
	if ( glConfig.maxTextureUnits < 6 || glConfig.maxTextureImageUnits < 6 ) {
		return SHADOWMAP_SUPPORT_TEXTURE_LIMIT;
	}
	if ( vLight->globalInteractions == NULL
		&& vLight->localInteractions == NULL
		&& !RB_ShadowMapTranslucentGlobalReceiverNeeded( vLight ) ) {
		return SHADOWMAP_SUPPORT_NO_INTERACTIONS;
	}
	// parallel lights carry pointLight=true but render through the projected
	// path with a synthesized orthographic projection
	if ( R_ClassifyShadowMapLight( vLight ).pointLight ) {
		if ( !r_shadowMapPointLights.GetBool() ) {
			return SHADOWMAP_SUPPORT_POINT_DISABLED;
		}
		if ( !glConfig.cubeMapAvailable ) {
			return SHADOWMAP_SUPPORT_CUBEMAP_UNAVAILABLE;
		}
		return RB_PointShadowMapEnsureResources() ? SHADOWMAP_SUPPORT_OK : SHADOWMAP_SUPPORT_RESOURCE_FAILURE;
	}
	return RB_ShadowMapEnsureResources( vLight ) ? SHADOWMAP_SUPPORT_OK : SHADOWMAP_SUPPORT_RESOURCE_FAILURE;
}

static void RB_ShadowMapPurgeImage( idImage *image ) {
	if ( image != NULL && image->IsLoaded() ) {
		image->PurgeImage();
	}
}

static bool RB_ShadowMapAnyResidentResources( void ) {
	if ( g_shadowMapScratchRenderTexture != NULL || g_pointShadowMapScratchRenderTexture != NULL || g_shadowMapAtlasRenderTexture != NULL
		|| g_translucentShadowMapRenderTexture != NULL || g_pointTranslucentShadowMapRenderTexture != NULL ) {
		return true;
	}
	for ( int i = 0; i < SHADOWMAP_CACHE_MAX_SLOTS; i++ ) {
		if ( g_pointShadowMapCache[i].renderTexture != NULL
				|| g_sharedInteractionProjectedScratch[i].renderTexture != NULL
				|| g_sharedInteractionPointScratch[i].renderTexture != NULL ) {
			return true;
		}
	}
	return false;
}

// Release all shadow GPU memory once the feature has been off for a while;
// previously only a full vid_restart reclaimed it. Images re-allocate through
// ScratchImage (which re-allocs unloaded images) when shadow maps re-enable.
static void RB_ShadowMapPurgeIdleResources( void ) {
	RB_ShadowMapPurgeImage( g_shadowMapScratchDepthImage );
	RB_ShadowMapPurgeImage( g_shadowMapAtlasDepthImage );
	RB_ShadowMapPurgeImage( g_pointShadowMapScratchColorImage );
	RB_ShadowMapPurgeImage( g_pointShadowMapScratchDepthImage );
	for ( int i = 0; i < 3; i++ ) {
		RB_ShadowMapPurgeImage( g_translucentShadowMomentImages[i] );
		RB_ShadowMapPurgeImage( g_pointTranslucentShadowMomentImages[i] );
	}
	for ( int i = 0; i < SHADOWMAP_CACHE_MAX_SLOTS; i++ ) {
		
		RB_ShadowMapPurgeImage( g_pointShadowMapCache[i].colorImage );
		RB_ShadowMapPurgeImage( g_pointShadowMapCache[i].depthImage );
		RB_ShadowMapPurgeImage(
			g_sharedInteractionProjectedScratch[i].depthImage );
		RB_ShadowMapPurgeImage(
			g_sharedInteractionPointScratch[i].colorImage );
		RB_ShadowMapPurgeImage(
			g_sharedInteractionPointScratch[i].depthImage );
		
		g_pointShadowMapCache[i].colorImage = NULL;
		g_pointShadowMapCache[i].depthImage = NULL;
		g_projectedShadowMapCache[i].valid = false;
		g_pointShadowMapCache[i].valid = false;
	}
	RB_ShutdownShadowMapResources();
}

static void RB_ShadowMapStatsReset( void ) {
	memset( &g_shadowMapStats, 0, sizeof( g_shadowMapStats ) );
	RB_ShadowMapPrepareCacheView( backEnd.viewDef );
	g_shadowMapStats.csmRequested = r_shadowMapCSM.GetBool();
	g_shadowMapStats.projectedCsmRequested = r_shadowMapProjectedCSM.GetBool();
	g_shadowMapStats.requestedCascadeCount = idMath::ClampInt( 1, SHADOWMAP_CLASSIFICATION_MAX_CASCADES, r_shadowMapCascadeCount.GetInteger() );

	static int lastShadowMapActiveFrame = -1;
	if ( r_useShadowMap.GetBool() || lastShadowMapActiveFrame < 0 ) {
		lastShadowMapActiveFrame = tr.frameCount;
	} else if ( tr.frameCount - lastShadowMapActiveFrame > 600 && RB_ShadowMapAnyResidentResources() ) {
		RB_ShadowMapPurgeIdleResources();
	}

	// subviews render before the main view; the main view's report claims
	// the frame's accumulated subview counters
	if ( backEnd.viewDef == NULL || !backEnd.viewDef->isSubview ) {
		g_shadowMapStats.subviewUpdates = g_shadowMapSubviewUpdates;
		g_shadowMapSubviewUpdates = 0;
		g_shadowMapStats.subviewReusePasses = g_shadowMapSubviewReusePasses;
		g_shadowMapSubviewReusePasses = 0;
		g_shadowMapStats.subviewFallbackPasses = g_shadowMapSubviewFallbackPasses;
		g_shadowMapSubviewFallbackPasses = 0;
	}

	RB_ShadowMapExpireCaches();
	RB_ShadowMapCountCacheSlots();
	g_shadowMapReportThisFrame = false;

	if ( !RB_ShadowMapShouldReport() ) {
		return;
	}

	const int reportLevel = idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() );
	if ( reportLevel <= 0 ) {
		return;
	}

	const int reportInterval = Max( 1, r_shadowMapReportInterval.GetInteger() );
	if ( tr.frameCount - g_shadowMapLastReportFrame < reportInterval ) {
		return;
	}

	g_shadowMapLastReportFrame = tr.frameCount;
	g_shadowMapReportThisFrame = true;
	RB_ShadowMapHarvestGpuTimerQueries();
}

static void RB_ShadowMapStatsReport( void ) {
	RB_ShadowMapCountCacheSlots();
	const int reportLevel = idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() );
	if ( reportLevel <= 0 || !g_shadowMapReportThisFrame || g_shadowMapStats.totalLights <= 0 ) {
		return;
	}
	const int receiverFallbackInvalid = g_shadowMapStats.receiverFallbackSurfaces[SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE];
	const int receiverFallbackCustomGLSL = g_shadowMapStats.receiverFallbackSurfaces[SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL];
	const int receiverFallbackGenerated = g_shadowMapStats.receiverFallbackSurfaces[SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY];
	const int receiverFallbackSurfaces = receiverFallbackInvalid + receiverFallbackCustomGLSL + receiverFallbackGenerated;

	idStr unsupportedSummary;
	for ( int i = 0; i < SHADOWMAP_SUPPORT_COUNT; i++ ) {
		if ( g_shadowMapStats.unsupportedLights[i] <= 0 ) {
			continue;
		}
		if ( unsupportedSummary.Length() > 0 ) {
			unsupportedSummary += ", ";
		}
		unsupportedSummary += va( "%s=%d", RB_ShadowMapSupportReasonName( static_cast<shadowMapLightSupportReason_t>( i ) ), g_shadowMapStats.unsupportedLights[i] );
	}

	if ( unsupportedSummary.Length() > 0 ) {
		common->Printf(
			"SM summary: lights=%d supported=%d point=%d projected=%d parallel=%d global=%d csm=%d/%d projectedCSM=%d/%d/%d nonProjectedCSM=%d cvars(csm=%d projectedCSM=%d cascadeReq=%d) mapped(local=%d global=%d) unshadowed(local=%d global=%d) fallback(local=%d global=%d) debug=%s unsupported[%s]\n",
			g_shadowMapStats.totalLights,
			g_shadowMapStats.supportedLights,
			g_shadowMapStats.pointLights,
			g_shadowMapStats.projectedLights,
			g_shadowMapStats.parallelLights,
			g_shadowMapStats.globalLights,
			g_shadowMapStats.cascadeLights,
			g_shadowMapStats.cascadeCount,
			g_shadowMapStats.projectedCsmEligibleLights,
			g_shadowMapStats.projectedCsmCascadeLights,
			g_shadowMapStats.projectedCsmSuppressedLights,
			g_shadowMapStats.nonProjectedCsmLights,
			g_shadowMapStats.csmRequested ? 1 : 0,
			g_shadowMapStats.projectedCsmRequested ? 1 : 0,
			g_shadowMapStats.requestedCascadeCount,
			g_shadowMapStats.mappedLocalPasses,
			g_shadowMapStats.mappedGlobalPasses,
			g_shadowMapStats.unshadowedLocalPasses,
			g_shadowMapStats.unshadowedGlobalPasses,
			g_shadowMapStats.fallbackLocalPasses,
			g_shadowMapStats.fallbackGlobalPasses,
			RB_ShadowMapDebugModeName( RB_ShadowMapDebugMode() ),
			unsupportedSummary.c_str() );
		common->Printf(
			"SM detail: failures(render local=%d global=%d, mask local=%d global=%d) receiverFallback(local=%d global=%d surfaces=%d invalid=%d customGLSL=%d generatedSkinned=%d) receiverWrapped(customGLSL=%d local=%d global=%d)\n",
			g_shadowMapStats.renderFailLocalPasses,
			g_shadowMapStats.renderFailGlobalPasses,
			g_shadowMapStats.maskFailLocalPasses,
			g_shadowMapStats.maskFailGlobalPasses,
			g_shadowMapStats.receiverFallbackLocalPasses,
			g_shadowMapStats.receiverFallbackGlobalPasses,
			receiverFallbackSurfaces,
			receiverFallbackInvalid,
			receiverFallbackCustomGLSL,
			receiverFallbackGenerated,
			g_shadowMapStats.receiverWrappedCustomGLSLSurfaces,
			g_shadowMapStats.receiverWrappedCustomGLSLLocalPasses,
			g_shadowMapStats.receiverWrappedCustomGLSLGlobalPasses );
	} else {
		common->Printf(
			"SM summary: lights=%d supported=%d point=%d projected=%d parallel=%d global=%d csm=%d/%d projectedCSM=%d/%d/%d nonProjectedCSM=%d cvars(csm=%d projectedCSM=%d cascadeReq=%d) mapped(local=%d global=%d) unshadowed(local=%d global=%d) fallback(local=%d global=%d) debug=%s\n",
			g_shadowMapStats.totalLights,
			g_shadowMapStats.supportedLights,
			g_shadowMapStats.pointLights,
			g_shadowMapStats.projectedLights,
			g_shadowMapStats.parallelLights,
			g_shadowMapStats.globalLights,
			g_shadowMapStats.cascadeLights,
			g_shadowMapStats.cascadeCount,
			g_shadowMapStats.projectedCsmEligibleLights,
			g_shadowMapStats.projectedCsmCascadeLights,
			g_shadowMapStats.projectedCsmSuppressedLights,
			g_shadowMapStats.nonProjectedCsmLights,
			g_shadowMapStats.csmRequested ? 1 : 0,
			g_shadowMapStats.projectedCsmRequested ? 1 : 0,
			g_shadowMapStats.requestedCascadeCount,
			g_shadowMapStats.mappedLocalPasses,
			g_shadowMapStats.mappedGlobalPasses,
			g_shadowMapStats.unshadowedLocalPasses,
			g_shadowMapStats.unshadowedGlobalPasses,
			g_shadowMapStats.fallbackLocalPasses,
			g_shadowMapStats.fallbackGlobalPasses,
			RB_ShadowMapDebugModeName( RB_ShadowMapDebugMode() ) );
		common->Printf(
			"SM detail: failures(render local=%d global=%d, mask local=%d global=%d) receiverFallback(local=%d global=%d surfaces=%d invalid=%d customGLSL=%d generatedSkinned=%d) receiverWrapped(customGLSL=%d local=%d global=%d)\n",
			g_shadowMapStats.renderFailLocalPasses,
			g_shadowMapStats.renderFailGlobalPasses,
			g_shadowMapStats.maskFailLocalPasses,
			g_shadowMapStats.maskFailGlobalPasses,
			g_shadowMapStats.receiverFallbackLocalPasses,
			g_shadowMapStats.receiverFallbackGlobalPasses,
			receiverFallbackSurfaces,
			receiverFallbackInvalid,
			receiverFallbackCustomGLSL,
			receiverFallbackGenerated,
			g_shadowMapStats.receiverWrappedCustomGLSLSurfaces,
			g_shadowMapStats.receiverWrappedCustomGLSLLocalPasses,
			g_shadowMapStats.receiverWrappedCustomGLSLGlobalPasses );
	}

	common->Printf(
		"SM metrics: updates=%d composed=%d subview(updates=%d reuse=%d fallback=%d) translucentReceivers=%d admissionDenied=%d scheduledSkip=%d atlasTiles=%d/%d pointFaces=%d pointHiP=%d pointCmp=%d cpu(render=%.2f mask=%.2f) gpuSync=%.2f gpuQuery=%.2f/%d pending=%d texelWorld=[%.3f %.3f]\n",
		g_shadowMapStats.shadowMapUpdates,
		g_shadowMapStats.composePasses,
		g_shadowMapStats.subviewUpdates,
		g_shadowMapStats.subviewReusePasses,
		g_shadowMapStats.subviewFallbackPasses,
		g_shadowMapStats.translucentReceiverPasses,
		g_shadowMapStats.admissionDeniedPasses,
		g_shadowMapStats.scheduledSkipPasses,
		g_shadowMapStats.projectedAtlasTilesRendered,
		g_shadowMapStats.projectedAtlasTilesAllocated,
		g_shadowMapStats.pointFacesRendered,
		g_shadowMapStats.pointHighPrecisionPasses,
		g_shadowMapStats.pointDepthComparePasses,
		g_shadowMapStats.cpuRenderMilliseconds,
		g_shadowMapStats.cpuMaskMilliseconds,
		g_shadowMapStats.gpuSyncMilliseconds,
		g_shadowMapStats.gpuTimerMilliseconds,
		g_shadowMapStats.gpuTimerSamples,
		g_shadowMapStats.gpuTimerPending,
		g_shadowMapStats.minWorldTexelSize,
		g_shadowMapStats.maxWorldTexelSize );
	common->Printf(
		"SM timings: cpu(map=%.2f reuse=%.2f mask=%.2f receiverFallback=%.2f stencilFallback=%.2f) gpuSync(map=%.2f reuse=%.2f mask=%.2f receiverFallback=%.2f stencilFallback=%.2f) gpuQuery(map=%.2f/%d reuse=%.2f/%d mask=%.2f/%d receiverFallback=%.2f/%d stencilFallback=%.2f/%d pending=%d:%d/%d/%d/%d/%d)\n",
		g_shadowMapStats.cpuPhaseMilliseconds[SHADOWMAP_TIMING_MAP_RENDER],
		g_shadowMapStats.cpuPhaseMilliseconds[SHADOWMAP_TIMING_CACHE_REUSE],
		g_shadowMapStats.cpuPhaseMilliseconds[SHADOWMAP_TIMING_MASK_PASS],
		g_shadowMapStats.cpuPhaseMilliseconds[SHADOWMAP_TIMING_RECEIVER_FALLBACK],
		g_shadowMapStats.cpuPhaseMilliseconds[SHADOWMAP_TIMING_STENCIL_FALLBACK],
		g_shadowMapStats.gpuSyncPhaseMilliseconds[SHADOWMAP_TIMING_MAP_RENDER],
		g_shadowMapStats.gpuSyncPhaseMilliseconds[SHADOWMAP_TIMING_CACHE_REUSE],
		g_shadowMapStats.gpuSyncPhaseMilliseconds[SHADOWMAP_TIMING_MASK_PASS],
		g_shadowMapStats.gpuSyncPhaseMilliseconds[SHADOWMAP_TIMING_RECEIVER_FALLBACK],
		g_shadowMapStats.gpuSyncPhaseMilliseconds[SHADOWMAP_TIMING_STENCIL_FALLBACK],
		g_shadowMapStats.gpuTimerPhaseMilliseconds[SHADOWMAP_TIMING_MAP_RENDER],
		g_shadowMapStats.gpuTimerPhaseSamples[SHADOWMAP_TIMING_MAP_RENDER],
		g_shadowMapStats.gpuTimerPhaseMilliseconds[SHADOWMAP_TIMING_CACHE_REUSE],
		g_shadowMapStats.gpuTimerPhaseSamples[SHADOWMAP_TIMING_CACHE_REUSE],
		g_shadowMapStats.gpuTimerPhaseMilliseconds[SHADOWMAP_TIMING_MASK_PASS],
		g_shadowMapStats.gpuTimerPhaseSamples[SHADOWMAP_TIMING_MASK_PASS],
		g_shadowMapStats.gpuTimerPhaseMilliseconds[SHADOWMAP_TIMING_RECEIVER_FALLBACK],
		g_shadowMapStats.gpuTimerPhaseSamples[SHADOWMAP_TIMING_RECEIVER_FALLBACK],
		g_shadowMapStats.gpuTimerPhaseMilliseconds[SHADOWMAP_TIMING_STENCIL_FALLBACK],
		g_shadowMapStats.gpuTimerPhaseSamples[SHADOWMAP_TIMING_STENCIL_FALLBACK],
		g_shadowMapStats.gpuTimerPending,
		g_shadowMapStats.gpuTimerPhasePending[SHADOWMAP_TIMING_MAP_RENDER],
		g_shadowMapStats.gpuTimerPhasePending[SHADOWMAP_TIMING_CACHE_REUSE],
		g_shadowMapStats.gpuTimerPhasePending[SHADOWMAP_TIMING_MASK_PASS],
		g_shadowMapStats.gpuTimerPhasePending[SHADOWMAP_TIMING_RECEIVER_FALLBACK],
		g_shadowMapStats.gpuTimerPhasePending[SHADOWMAP_TIMING_STENCIL_FALLBACK] );
	common->Printf(
		"SM cache: hit=%d miss=%d reuse=%d budgetReuse=%d evict=%d expired=%d hysteresis=%d projected=%d/%d point=%d/%d vramMB=%.1f\n",
		g_shadowMapStats.cacheHitPasses,
		g_shadowMapStats.cacheMissPasses,
		g_shadowMapStats.cacheReusePasses,
		g_shadowMapStats.cacheBudgetReusePasses,
		g_shadowMapStats.cacheEvictions,
		g_shadowMapStats.cacheExpired,
		g_shadowMapStats.staticHysteresisPasses,
		g_shadowMapStats.projectedCacheSlotsUsed,
		g_shadowMapStats.projectedCacheSlotsTotal,
		g_shadowMapStats.pointCacheSlotsUsed,
		g_shadowMapStats.pointCacheSlotsTotal,
		RB_ShadowMapResidentBytes() / ( 1024.0 * 1024.0 ) );
	{
		// modern consumption of the persistent atlas (5c): how many lights
		// the planner resolved live atlas slots for, and whether the modern
		// per-light gate would let a modern-composed frame keep its shadows
		const modernShadowPlannerStats_t &modernShadowStats = R_ModernShadowPlanner_Stats();
		if ( modernShadowStats.frameValid ) {
			const rendererClusteredLightingStats_t &clusterStats = R_ModernClusteredLighting_Stats();
			common->Printf(
				"SM modern: atlasSlots=%d mapped=%d cacheReuse=%d slotBlocked=%d samplingBlocked=%d fallback=%d skipped=%d\n",
				modernShadowStats.arb2AtlasSlotReadyLights,
				modernShadowStats.mappedLights,
				modernShadowStats.arb2CacheReuseLights,
				clusterStats.shadowAtlasSlotBlockedLights,
				clusterStats.shadowReceiverBlockedLights,
				modernShadowStats.fallbackLights,
				modernShadowStats.skippedLights );
		}
	}
	common->Printf(
		"SM casters: total=%d static=%d dynamic=%d alpha=%d translucent=%d expanded=%d rejected=%d lod(tests=%d rejected=%d alpha=%d translucent=%d)\n",
		g_shadowMapStats.casterCount,
		g_shadowMapStats.staticCasterCount,
		g_shadowMapStats.dynamicCasterCount,
		g_shadowMapStats.alphaCasterCount,
		g_shadowMapStats.translucentCasterCount,
		g_shadowMapStats.expandedCasterCount,
		g_shadowMapStats.rejectedCasterCount,
		g_shadowMapStats.lodCasterTestCount,
		g_shadowMapStats.lodCasterRejectedCount,
		g_shadowMapStats.lodAlphaCasterRejectedCount,
		g_shadowMapStats.lodTranslucentCasterRejectedCount );

	if ( g_shadowMapStats.rejectedCasterCount > 0 ) {
		idStr rejectSummary;
		for ( int i = 0; i < SHADOWMAP_CASTER_REJECT_COUNT; i++ ) {
			if ( g_shadowMapStats.casterRejectReasons[i] <= 0 ) {
				continue;
			}
			if ( rejectSummary.Length() > 0 ) {
				rejectSummary += ", ";
			}
			rejectSummary += va( "%s=%d", RB_ShadowMapCasterRejectReasonName( static_cast<shadowMapCasterRejectReason_t>( i ) ), g_shadowMapStats.casterRejectReasons[i] );
		}
		if ( rejectSummary.Length() > 0 ) {
			common->Printf( "SM caster-reject: %s\n", rejectSummary.c_str() );
		}
	}

}

static void RB_ShadowMapJoinedDecisionReport( const char *phase, const viewLight_t *vLight, const shadowMapLightClassification_t &classification, const shadowMapLightSupportReason_t supportReason, const bool hasPass, const shadowMapPassKind_t passKind, const bool pointLight, const shadowMapPassResult_t passResult, const int primaryCasterCount, const int secondaryCasterCount, const int tertiaryCasterCount, const int quaternaryCasterCount, const int primaryShadowSurfCount, const int secondaryShadowSurfCount, const int receiverCount, const int receiverFallbackSurfaces, const int receiverWrappedCustomGLSLSurfaces, const int *receiverFallbackReasons ) {
	const int lightDefIndex = ( vLight != NULL && vLight->lightDef != NULL ) ? vLight->lightDef->index : -1;
	const char *shaderName = ( vLight != NULL && vLight->lightShader != NULL ) ? vLight->lightShader->GetName() : "<null>";
	const modernShadowLightDescriptor_t *planner = R_ModernShadowPlanner_DescriptorForLight( vLight );
	const int plannerDescriptorIndex = ( planner != NULL ) ? planner->descriptorIndex : -1;
	const shadowMapModernPackedJoin_t packed = RB_ShadowMapModernPackedJoinForLight( lightDefIndex, plannerDescriptorIndex );
	const rendererModernShadowDescriptor_t *packedShadow = RB_ModernClusteredShadowDescriptorByIndex( packed.shadowDescriptorIndex );
	const int fallbackInvalid = ( receiverFallbackReasons != NULL ) ? receiverFallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE] : 0;
	const int fallbackCustomGLSL = ( receiverFallbackReasons != NULL ) ? receiverFallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL] : 0;
	const int fallbackGenerated = ( receiverFallbackReasons != NULL ) ? receiverFallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY] : 0;

	common->Printf(
		"SM join phase=%s light=%d '%s' class=%s support=%s planner(desc=%d map=%s policy=%s fallback=%s budgetClass=%d budgetMask=0x%x priority=%d fairness=%d/%d/%d tiles=%d cascades=%d/%d atlasDiv=%d casters=%d/%d/%d receivers=%d/%d/%d lod=%d/%d/%d/%d cache=%d/%d/%d ready=%d/%d/%d) arb2(pass=%s type=%s result=%s csm=%d projectedCSM=%d/%d casters=%d/%d/%d/%d shadowSurfs=%d/%d receivers=%d receiverFallback=%d/%d/%d/%d wrappedCustomGLSL=%d) modernPacked(count=%d lightDesc=%d name='%s' type=%s shadowDesc=%d policy=%s fallback=%s flags=0x%x shadowMap=%s shadowPolicy=%s shadowFallback=%s shadowTiles=%d shadowCascades=%d/%d shadowAtlasDiv=%d shadowFlags=0x%x)\n",
		phase != NULL ? phase : "<null>",
		lightDefIndex,
		shaderName,
		R_ShadowMapLightClassName( classification.lightClass ),
		RB_ShadowMapSupportReasonName( supportReason ),
		planner != NULL ? planner->descriptorIndex : -1,
		planner != NULL ? ModernShadowMapType_Name( planner->mapType ) : "none",
		planner != NULL ? ModernShadowPolicy_Name( planner->policy ) : "none",
		planner != NULL ? ModernShadowFallbackReason_Name( planner->fallbackReason ) : "none",
		planner != NULL ? planner->budgetClass : 0,
		planner != NULL ? planner->budgetThrottleReasonMask : 0,
		planner != NULL ? planner->priority : 0,
		planner != NULL ? planner->fairnessPriority : 0,
		planner != NULL ? planner->fairnessAge : 0,
		planner != NULL ? planner->fairnessBoost : 0,
		planner != NULL ? planner->tileCount : 0,
		planner != NULL ? planner->cascadeCount : 0,
		planner != NULL ? planner->requestedCascadeCount : 0,
		planner != NULL ? planner->atlasDiv : 0,
		planner != NULL ? planner->localCasterCount : 0,
		planner != NULL ? planner->globalCasterCount : 0,
		planner != NULL ? planner->translucentCasterCount : 0,
		planner != NULL ? planner->localReceiverCount : 0,
		planner != NULL ? planner->globalReceiverCount : 0,
		planner != NULL ? planner->translucentReceiverCount : 0,
		planner != NULL ? planner->lodCasterTestCount : 0,
		planner != NULL ? planner->lodCasterRejectedCount : 0,
		planner != NULL ? planner->lodAlphaCasterRejectedCount : 0,
		planner != NULL ? planner->lodTranslucentCasterRejectedCount : 0,
		planner != NULL ? planner->arb2ShadowPasses : 0,
		planner != NULL ? planner->arb2CacheablePasses : 0,
		planner != NULL ? planner->arb2CacheHitPasses : 0,
		planner != NULL && planner->atlasTileReady ? 1 : 0,
		planner != NULL && planner->casterPassReady ? 1 : 0,
		planner != NULL && planner->modernReceiverSamplingReady ? 1 : 0,
		hasPass ? RB_ShadowMapPassName( passKind ) : "none",
		hasPass ? ( pointLight ? "point" : "projected" ) : ( classification.pointLight ? "point" : "projected" ),
		hasPass ? RB_ShadowMapPassResultName( passResult ) : "none",
		classification.csmEnabled ? 1 : 0,
		classification.projectedCSMEnabled ? 1 : 0,
		classification.projectedCSMGateApplies ? 1 : 0,
		primaryCasterCount,
		secondaryCasterCount,
		tertiaryCasterCount,
		quaternaryCasterCount,
		primaryShadowSurfCount,
		secondaryShadowSurfCount,
		receiverCount,
		receiverFallbackSurfaces,
		fallbackInvalid,
		fallbackCustomGLSL,
		fallbackGenerated,
		receiverWrappedCustomGLSLSurfaces,
		packed.descriptorCount,
		packed.lightDescriptorIndex,
		packed.debugName,
		RB_ModernPackedLightTypeName( packed.lightType ),
		packed.shadowDescriptorIndex,
		ModernShadowPolicy_Name( static_cast<modernShadowPolicy_t>( packed.shadowPolicy ) ),
		ModernShadowFallbackReason_Name( static_cast<modernShadowFallbackReason_t>( packed.shadowFallbackReason ) ),
		packed.flags,
		packedShadow != NULL ? ModernShadowMapType_Name( static_cast<modernShadowMapType_t>( packedShadow->mapType ) ) : "none",
		packedShadow != NULL ? ModernShadowPolicy_Name( static_cast<modernShadowPolicy_t>( packedShadow->policy ) ) : "none",
		packedShadow != NULL ? ModernShadowFallbackReason_Name( static_cast<modernShadowFallbackReason_t>( packedShadow->fallbackReason ) ) : "none",
		packedShadow != NULL ? packedShadow->tileCount : 0,
		packedShadow != NULL ? packedShadow->cascadeCount : 0,
		packedShadow != NULL ? packedShadow->requestedCascadeCount : 0,
		packedShadow != NULL ? packedShadow->atlasDiv : 0,
		packedShadow != NULL ? packedShadow->flags : 0 );
}

static void RB_ShadowMapLightReport( const viewLight_t *vLight, shadowMapLightSupportReason_t reason ) {
	if ( idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) < 2 || !g_shadowMapReportThisFrame ) {
		return;
	}

	const char *shaderName = ( vLight != NULL && vLight->lightShader != NULL ) ? vLight->lightShader->GetName() : "<null>";
	idVec3 origin( 0.0f, 0.0f, 0.0f );
	if ( vLight != NULL ) {
		origin = vLight->globalLightOrigin;
	}
	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
	const shadowMapProjectedFilterSettings_t filterSettings = R_ShadowMapProjectedFilterSettings( vLight );
	const float reportedFilterRadius = classification.pointLight
		? Max( 0.0f, r_shadowMapPointFilterRadius.GetFloat() )
		: filterSettings.filterRadius;
	const float reportedEffectiveFilterRadius = classification.pointLight
		? reportedFilterRadius
		: filterSettings.effectiveFilterRadius;
	const float reportedPCSSLightRadius = classification.pointLight ? 0.0f : filterSettings.pcssLightRadius;
	const float reportedPCSSMaxRadius = classification.pointLight ? 0.0f : filterSettings.pcssMaxRadius;
	common->Printf(
		"SM light[%d] '%s' origin=(%.1f %.1f %.1f) class=%s csm=%d projectedCSM=%d/%d cascades=%d tiles=%d atlasDiv=%d filter(distant=%d scale=%.2f radius=%.2f effective=%.2f pcss=%.2f/%.2f) interactions(local=%d global=%d) shadows(local=%d global=%d) casters(total=%d alpha=%d trans=%d expanded=%d reject=%d lod=%d/%d/%d/%d) debug=%s support=%s\n",
		( vLight != NULL && vLight->lightDef != NULL ) ? vLight->lightDef->index : -1,
		shaderName,
		origin[0], origin[1], origin[2],
		R_ShadowMapLightClassName( classification.lightClass ),
		classification.csmEnabled ? 1 : 0,
		classification.projectedCSMEnabled ? 1 : 0,
		classification.projectedCSMGateApplies ? 1 : 0,
		classification.cascadeCount,
		classification.tileCount,
		classification.atlasDiv,
		filterSettings.distantSource ? 1 : 0,
		filterSettings.filterScale,
		reportedFilterRadius,
		reportedEffectiveFilterRadius,
		reportedPCSSLightRadius,
		reportedPCSSMaxRadius,
		RB_CountDrawSurfChain( vLight != NULL ? vLight->localInteractions : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->globalInteractions : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->localShadows : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->globalShadows : NULL ),
		vLight != NULL ? vLight->shadowMapCasterCount : 0,
		vLight != NULL ? vLight->shadowMapAlphaCasterCount : 0,
		vLight != NULL ? vLight->shadowMapTranslucentCasterCount : 0,
		vLight != NULL ? vLight->shadowMapExpandedCasterCount : 0,
		vLight != NULL ? vLight->shadowMapRejectedCasterCount : 0,
		vLight != NULL ? vLight->shadowMapLODTestCount : 0,
		vLight != NULL ? vLight->shadowMapLODRejectedCount : 0,
		vLight != NULL ? vLight->shadowMapLODAlphaRejectedCount : 0,
		vLight != NULL ? vLight->shadowMapLODTranslucentRejectedCount : 0,
		RB_ShadowMapDebugModeName( RB_ShadowMapDebugMode() ),
		RB_ShadowMapSupportReasonName( reason ) );

	RB_ShadowMapJoinedDecisionReport(
		"light",
		vLight,
		classification,
		reason,
		false,
		SHADOWMAP_PASS_LOCAL,
		classification.pointLight,
		SHADOWMAP_PASS_RESULT_STENCIL_ONLY,
		RB_CountDrawSurfChain( vLight != NULL ? vLight->globalShadowMapCasters : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->localShadowMapCasters : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->globalTranslucentShadowMapCasters : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->localTranslucentShadowMapCasters : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->globalShadows : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->localShadows : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->localInteractions : NULL ) + RB_CountDrawSurfChain( vLight != NULL ? vLight->globalInteractions : NULL ),
		0,
		0,
		NULL );
}

static void RB_ShadowMapPassReport( const viewLight_t *vLight, shadowMapPassKind_t passKind, bool pointLight, shadowMapPassResult_t result, const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *tertiaryCasters, const drawSurf_t *quaternaryCasters, const drawSurf_t *primaryShadowSurfs, const drawSurf_t *secondaryShadowSurfs, const drawSurf_t *interactions ) {
	// every pass outcome flows through here, so this is the single place the
	// GLOBAL pass's mapped state is recorded for the translucent receiver
	// draw that follows the pass pair (C3)
	if ( passKind == SHADOWMAP_PASS_GLOBAL ) {
		if ( result == SHADOWMAP_PASS_RESULT_MAPPED || result == SHADOWMAP_PASS_RESULT_CACHE_REUSE ) {
			g_shadowMapGlobalPassMapped = pointLight ? SHADOWMAP_GLOBAL_MAPPED_POINT : SHADOWMAP_GLOBAL_MAPPED_PROJECTED;
		} else {
			g_shadowMapGlobalPassMapped = SHADOWMAP_GLOBAL_MAPPED_NONE;
		}
	}
	if ( idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) < 2 || !g_shadowMapReportThisFrame ) {
		return;
	}

	const char *shaderName = ( vLight != NULL && vLight->lightShader != NULL ) ? vLight->lightShader->GetName() : "<null>";
	const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
	int receiverFallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_COUNT];
	memset( receiverFallbackReasons, 0, sizeof( receiverFallbackReasons ) );
	int receiverFallbackSurfaces = 0;
	int receiverWrappedCustomGLSLSurfaces = 0;
	if ( result == SHADOWMAP_PASS_RESULT_MAPPED || result == SHADOWMAP_PASS_RESULT_CACHE_REUSE || result == SHADOWMAP_PASS_RESULT_RECEIVER_FALLBACK ) {
		receiverFallbackSurfaces = RB_CountShadowMapReceiverFallbackSurfaces( interactions, receiverFallbackReasons );
		receiverWrappedCustomGLSLSurfaces = RB_CountShadowMapReceiverWrappedCustomGLSLSurfaces( interactions );
	}
	common->Printf(
		"SM pass %s[%d] '%s' class=%s type=%s csm=%d projectedCSM=%d/%d cascades=%d atlasDiv=%d debug=%s result=%s casters(a=%d b=%d c=%d d=%d) shadowSurfs(primary=%d secondary=%d) receivers=%d receiverFallback=%d wrappedCustomGLSL=%d invalid=%d customGLSL=%d generatedSkinned=%d\n",
		RB_ShadowMapPassName( passKind ),
		( vLight != NULL && vLight->lightDef != NULL ) ? vLight->lightDef->index : -1,
		shaderName,
		R_ShadowMapLightClassName( classification.lightClass ),
		pointLight ? "point" : "projected",
		classification.csmEnabled ? 1 : 0,
		classification.projectedCSMEnabled ? 1 : 0,
		classification.projectedCSMGateApplies ? 1 : 0,
		classification.cascadeCount,
		classification.atlasDiv,
		RB_ShadowMapDebugModeName( RB_ShadowMapDebugMode() ),
		RB_ShadowMapPassResultName( result ),
		RB_CountDrawSurfChain( primaryCasters ),
		RB_CountDrawSurfChain( secondaryCasters ),
		RB_CountDrawSurfChain( tertiaryCasters ),
		RB_CountDrawSurfChain( quaternaryCasters ),
		RB_CountDrawSurfChain( primaryShadowSurfs ),
		RB_CountDrawSurfChain( secondaryShadowSurfs ),
		RB_CountDrawSurfChain( interactions ),
		receiverFallbackSurfaces,
		receiverWrappedCustomGLSLSurfaces,
		receiverFallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE],
		receiverFallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL],
		receiverFallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY] );

	RB_ShadowMapJoinedDecisionReport(
		"pass",
		vLight,
		classification,
		SHADOWMAP_SUPPORT_OK,
		true,
		passKind,
		pointLight,
		result,
		RB_CountDrawSurfChain( primaryCasters ),
		RB_CountDrawSurfChain( secondaryCasters ),
		RB_CountDrawSurfChain( tertiaryCasters ),
		RB_CountDrawSurfChain( quaternaryCasters ),
		RB_CountDrawSurfChain( primaryShadowSurfs ),
		RB_CountDrawSurfChain( secondaryShadowSurfs ),
		RB_CountDrawSurfChain( interactions ),
		receiverFallbackSurfaces,
		receiverWrappedCustomGLSLSurfaces,
		receiverFallbackReasons );
}

static void RB_ShadowMapReportCasterSkip( const drawSurf_t *surf, const srfTriangles_t *casterGeo, const char *reason ) {
	if ( idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) < 2 || !g_shadowMapReportThisFrame ) {
		return;
	}

	const char *lightShader = ( backEnd.vLight != NULL && backEnd.vLight->lightShader != NULL ) ? backEnd.vLight->lightShader->GetName() : "<null>";
	const char *surfaceShader = ( surf != NULL && surf->material != NULL ) ? surf->material->GetName() : "<null>";
	const srfTriangles_t *tri = casterGeo != NULL ? casterGeo : ( surf != NULL ? surf->geo : NULL );
	const int numVerts = tri != NULL ? tri->numVerts : 0;
	const int numIndexes = tri != NULL ? tri->numIndexes : 0;

	common->Printf(
		"SM caster-skip light='%s' type=%s surf='%s' reason=%s verts=%d indexes=%d\n",
		lightShader,
		( backEnd.vLight != NULL && backEnd.vLight->pointLight ) ? "point" : "projected",
		surfaceShader,
		reason,
		numVerts,
		numIndexes );
}

static bool RB_ShadowMapResolveCasterDrawData( const drawSurf_t *surf, srfTriangles_t *&casterGeoOut, vertCache_s *&ambientCacheOut ) {
	casterGeoOut = NULL;
	ambientCacheOut = NULL;

	if ( surf == NULL || surf->geo == NULL ) {
		RB_ShadowMapReportCasterSkip( surf, NULL, "no-geometry" );
		return false;
	}

	const srfTriangles_t *casterGeo = surf->geo;
	if ( casterGeo->ambientSurface != NULL ) {
		casterGeo = casterGeo->ambientSurface;
	}

	if ( casterGeo == NULL ) {
		RB_ShadowMapReportCasterSkip( surf, surf->geo, "no-ambient-surface" );
		return false;
	}

	srfTriangles_t *ambientGeo = const_cast<srfTriangles_t *>( casterGeo );
	if ( ambientGeo->numVerts <= 0 || ambientGeo->numIndexes <= 0 ) {
		RB_ShadowMapReportCasterSkip( surf, ambientGeo, "empty-geometry" );
		return false;
	}

	if ( ambientGeo->ambientCache == NULL ) {
		if ( ambientGeo->verts == NULL ) {
			RB_ShadowMapReportCasterSkip( surf, ambientGeo, "no-vertex-data" );
			return false;
		}
		if ( !R_CreateAmbientCache( ambientGeo, false ) ) {
			RB_ShadowMapReportCasterSkip( surf, ambientGeo, "ambient-cache-create-failed" );
			return false;
		}
	}

	vertCache_s *ambientCache = ambientGeo->ambientCache;
	if ( ambientCache == NULL ) {
		ambientCache = surf->geo->ambientCache;
	}
	if ( ambientCache == NULL ) {
		RB_ShadowMapReportCasterSkip( surf, ambientGeo, "no-ambient-cache" );
		return false;
	}

	if ( ambientGeo->indexCache == NULL && ambientGeo->indexes == NULL ) {
		RB_ShadowMapReportCasterSkip( surf, ambientGeo, "no-index-data" );
		return false;
	}

	casterGeoOut = ambientGeo;
	ambientCacheOut = ambientCache;
	return true;
}

static void RB_ShadowMapModelMatrixRows( const float modelMatrix[16], float row0[4], float row1[4], float row2[4] );

static bool RB_ShadowMapTextureStageHasBindableImage( const textureStage_t *texture ) {
	return texture != NULL && ( texture->image != NULL || texture->cinematic != NULL );
}

static float RB_ShadowMapAlphaTestModeValue( const int alphaTestMode ) {
	if ( alphaTestMode == GL_LESS ) {
		return -1.0f;
	}
	if ( alphaTestMode == GL_EQUAL ) {
		return 0.0f;
	}
	return 1.0f;
}

static void RB_ShadowMapTouchCache( vertCache_t *cache ) {
	if ( cache == NULL ) {
		return;
	}

	// Shadow caster draw surfs can be finalized through the deform path, which
	// may replace the geometry with frame-temp vertex cache blocks. Those are
	// already valid for the current frame and idVertexCache::Touch() rejects them.
	if ( cache->tag != TAG_TEMP ) {
		vertexCache.Touch( cache );
	}
}

static void RB_ShadowMapSetCasterDepthRow( const drawSurf_t *surf, const int cascadeIndex ) {
	if ( surf == NULL || surf->space == NULL || g_shadowMapCasterProgram.shadowDepthRow < 0 ) {
		return;
	}

	idPlane localDepthPlane;
	const int safeCascadeIndex = idMath::ClampInt( 0, SHADOWMAP_MAX_CASCADES - 1, cascadeIndex );
	R_GlobalPlaneToLocal( surf->space->modelMatrix, g_projectedShadowMapState.clipPlanes[safeCascadeIndex][2], localDepthPlane );
	glUniform4fvARB( g_shadowMapCasterProgram.shadowDepthRow, 1, localDepthPlane.ToFloatPtr() );
}

static void RB_TranslucentShadowMapSetCasterDepthRow( const drawSurf_t *surf, const int cascadeIndex ) {
	if ( surf == NULL || surf->space == NULL || g_translucentShadowCasterProgram.shadowDepthRow < 0 ) {
		return;
	}

	idPlane localDepthPlane;
	const int safeCascadeIndex = idMath::ClampInt( 0, SHADOWMAP_MAX_CASCADES - 1, cascadeIndex );
	R_GlobalPlaneToLocal( surf->space->modelMatrix, g_projectedShadowMapState.clipPlanes[safeCascadeIndex][2], localDepthPlane );
	glUniform4fvARB( g_translucentShadowCasterProgram.shadowDepthRow, 1, localDepthPlane.ToFloatPtr() );
}

static void RB_ShadowMapSetCasterModelRows( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL ) {
		return;
	}

	float row0[4];
	float row1[4];
	float row2[4];
	RB_ShadowMapModelMatrixRows( surf->space->modelMatrix, row0, row1, row2 );
	if ( g_shadowMapCasterProgram.modelMatrixRow0 >= 0 ) {
		glUniform4fvARB( g_shadowMapCasterProgram.modelMatrixRow0, 1, row0 );
	}
	if ( g_shadowMapCasterProgram.modelMatrixRow1 >= 0 ) {
		glUniform4fvARB( g_shadowMapCasterProgram.modelMatrixRow1, 1, row1 );
	}
	if ( g_shadowMapCasterProgram.modelMatrixRow2 >= 0 ) {
		glUniform4fvARB( g_shadowMapCasterProgram.modelMatrixRow2, 1, row2 );
	}
}

static void RB_ShadowMapDisableCasterAlphaTest( void ) {
	if ( g_shadowMapCasterProgram.alphaTestEnabled >= 0 ) {
		glUniform1fARB( g_shadowMapCasterProgram.alphaTestEnabled, 0.0f );
	}
}

// The caster program stays bound for the whole pass; the chain sets the
// per-space uniforms and the perforated path restores the alpha-test default.
static void RB_ShadowMapDrawOpaqueCasterProgram( const srfTriangles_t *casterGeo ) {
	RB_DrawElementsWithCounters( casterGeo );
}

static bool RB_ShadowMapCanProgramPerforatedCaster( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return false;
	}

	bool haveActiveStage = false;
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	const int stageCount = shader->GetNumStages();
	for ( int stage = 0; stage < stageCount; stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}

		haveActiveStage = true;
		if ( !RB_ShadowMapTextureStageHasBindableImage( &pStage->texture ) || pStage->texture.texgen != TG_EXPLICIT ) {
			return false;
		}
	}

	return haveActiveStage;
}

static translucentShadowStageMode_t RB_TranslucentShadowStageMode( const shaderStage_t *pStage ) {
	if ( pStage == NULL || pStage->newStage != NULL ) {
		return TRANSLUCENT_SHADOW_STAGE_NONE;
	}

	const int blendBits = pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
	if ( blendBits == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) ||
		blendBits == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) ) {
		return ( RB_ShadowMapTextureStageHasBindableImage( &pStage->texture ) && pStage->texture.texgen == TG_EXPLICIT ) ?
			TRANSLUCENT_SHADOW_STAGE_TEXTURE_ALPHA :
			TRANSLUCENT_SHADOW_STAGE_NONE;
	}

		if ( blendBits == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) ) {
		if ( RB_ShadowMapTextureStageHasBindableImage( &pStage->texture ) && pStage->texture.texgen == TG_EXPLICIT ) {
			return TRANSLUCENT_SHADOW_STAGE_TEXTURE_ADDITIVE;
		}

		if ( RB_ShadowMapTextureStageHasBindableImage( &pStage->texture ) &&
			pStage->texture.texgen == TG_REFLECT_CUBE &&
			glConfig.cubeMapAvailable ) {
			return TRANSLUCENT_SHADOW_STAGE_CUBEMAP_ADDITIVE;
		}
	}

	return TRANSLUCENT_SHADOW_STAGE_NONE;
}

static bool RB_TranslucentShadowStageSupported( const shaderStage_t *pStage, const float *regs ) {
	return pStage != NULL &&
		regs != NULL &&
		regs[ pStage->conditionRegister ] != 0.0f &&
		RB_TranslucentShadowStageMode( pStage ) != TRANSLUCENT_SHADOW_STAGE_NONE;
}

static bool RB_TranslucentShadowStageCanProvideCoverage( const shaderStage_t *pStage, const float *regs ) {
	return pStage != NULL &&
		regs != NULL &&
		pStage->newStage == NULL &&
		regs[ pStage->conditionRegister ] != 0.0f &&
		RB_ShadowMapTextureStageHasBindableImage( &pStage->texture ) &&
		pStage->texture.texgen == TG_EXPLICIT;
}

static int RB_TranslucentShadowCoverageStagePriority( const shaderStage_t *pStage, const float *regs ) {
	if ( !RB_TranslucentShadowStageCanProvideCoverage( pStage, regs ) ) {
		return -1;
	}

	int priority = 100;
	if ( pStage->hasAlphaTest ) {
		priority += 400;
	}

	switch ( RB_TranslucentShadowStageMode( pStage ) ) {
	case TRANSLUCENT_SHADOW_STAGE_TEXTURE_ALPHA:
		priority += 300;
		break;
	case TRANSLUCENT_SHADOW_STAGE_TEXTURE_ADDITIVE:
		priority += 150;
		break;
	default:
		break;
	}

	if ( pStage->lighting == SL_AMBIENT ) {
		priority += 20;
	}

	return priority;
}

static const shaderStage_t *RB_FindTranslucentShadowCoverageStage( const idMaterial *shader, const float *regs ) {
	if ( shader == NULL || regs == NULL ) {
		return NULL;
	}

	const shaderStage_t *bestStage = NULL;
	int bestPriority = -1;

	const int stageCount = shader->GetNumStages();
	for ( int stage = 0; stage < stageCount; ++stage ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		const int priority = RB_TranslucentShadowCoverageStagePriority( pStage, regs );
		if ( priority > bestPriority ) {
			bestPriority = priority;
			bestStage = pStage;
		}
	}

	return bestStage;
}

static float RB_TranslucentShadowStageOpacityScale( const shaderStage_t *pStage, const float *regs, const translucentShadowStageMode_t mode ) {
	if ( pStage == NULL || regs == NULL ) {
		return 0.0f;
	}

	const float colorLuma = regs[ pStage->color.registers[0] ] * 0.2126f +
		regs[ pStage->color.registers[1] ] * 0.7152f +
		regs[ pStage->color.registers[2] ] * 0.0722f;

	if ( mode == TRANSLUCENT_SHADOW_STAGE_TEXTURE_ALPHA ) {
		return regs[ pStage->color.registers[3] ];
	}
	if ( mode == TRANSLUCENT_SHADOW_STAGE_CUBEMAP_ADDITIVE ) {
		return Max( 0.0f, 1.0f - idMath::ClampFloat( 0.0f, 1.0f, colorLuma ) );
	}

	return Max( regs[ pStage->color.registers[3] ], colorLuma );
}

static float RB_TranslucentShadowCoverageScale( const shaderStage_t *pStage, const float *regs ) {
	if ( pStage == NULL || regs == NULL ) {
		return 1.0f;
	}

	const translucentShadowStageMode_t mode = RB_TranslucentShadowStageMode( pStage );
	if ( mode != TRANSLUCENT_SHADOW_STAGE_NONE ) {
		return RB_TranslucentShadowStageOpacityScale( pStage, regs, mode );
	}

	return Max( regs[ pStage->color.registers[3] ], 0.0f );
}

static float RB_TranslucentShadowStageOpacityModeValue( const translucentShadowStageMode_t mode ) {
	switch ( mode ) {
	case TRANSLUCENT_SHADOW_STAGE_TEXTURE_ADDITIVE:
		return 1.0f;
	case TRANSLUCENT_SHADOW_STAGE_CUBEMAP_ADDITIVE:
		return 2.0f;
	default:
		return 0.0f;
	}
}

static float RB_TranslucentShadowCoverageSourceModeValue( const shaderStage_t *pStage ) {
	if ( pStage == NULL ) {
		return 0.0f;
	}

	const translucentShadowStageMode_t mode = RB_TranslucentShadowStageMode( pStage );
	if ( pStage->hasAlphaTest || mode == TRANSLUCENT_SHADOW_STAGE_TEXTURE_ALPHA ) {
		return 1.0f;
	}

	return 2.0f;
}

static float RB_TranslucentShadowVertexColorModeValue( const shaderStage_t *pStage ) {
	if ( pStage == NULL ) {
		return 0.0f;
	}

	if ( pStage->vertexColor == SVC_MODULATE ) {
		return 1.0f;
	}
	if ( pStage->vertexColor == SVC_INVERSE_MODULATE ) {
		return 2.0f;
	}
	return 0.0f;
}

static void RB_TranslucentShadowStageColor( const shaderStage_t *pStage, const float *regs, float color[4] ) {
	color[0] = 1.0f;
	color[1] = 1.0f;
	color[2] = 1.0f;
	color[3] = 1.0f;

	if ( pStage == NULL || regs == NULL ) {
		return;
	}

	color[0] = regs[ pStage->color.registers[0] ];
	color[1] = regs[ pStage->color.registers[1] ];
	color[2] = regs[ pStage->color.registers[2] ];
	color[3] = regs[ pStage->color.registers[3] ];
}

static void RB_TranslucentShadowVertexAlphaParams( const shaderStage_t *pStage, float params[2] ) {
	params[0] = 0.0f;
	params[1] = 1.0f;

	if ( pStage == NULL ) {
		return;
	}

	if ( pStage->vertexColor == SVC_MODULATE ) {
		params[0] = 1.0f;
		params[1] = 0.0f;
	} else if ( pStage->vertexColor == SVC_INVERSE_MODULATE ) {
		params[0] = -1.0f;
		params[1] = 1.0f;
	}
}

static void RB_ShadowMapSetTexCoordUniforms( const GLint texCoordS, const GLint texCoordT, const float *regs, const textureStage_t *texture ) {
	float rowS[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	float rowT[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

	if ( regs != NULL && texture != NULL && texture->hasMatrix ) {
		float matrix[16];
		RB_GetShaderTextureMatrix( regs, texture, matrix );
		rowS[0] = matrix[0];
		rowS[1] = matrix[4];
		rowS[2] = matrix[8];
		rowS[3] = matrix[12];
		rowT[0] = matrix[1];
		rowT[1] = matrix[5];
		rowT[2] = matrix[9];
		rowT[3] = matrix[13];
	}

	if ( texCoordS >= 0 ) {
		glUniform4fvARB( texCoordS, 1, rowS );
	}
	if ( texCoordT >= 0 ) {
		glUniform4fvARB( texCoordT, 1, rowT );
	}
}

static void RB_ShadowMapSetAlphaTexCoordIdentity( void ) {
	RB_ShadowMapSetTexCoordUniforms( g_shadowMapCasterProgram.alphaTexCoordS, g_shadowMapCasterProgram.alphaTexCoordT, NULL, NULL );
}

static void RB_ShadowMapSetAlphaTexCoordMatrix( const float *regs, const textureStage_t *texture ) {
	RB_ShadowMapSetTexCoordUniforms( g_shadowMapCasterProgram.alphaTexCoordS, g_shadowMapCasterProgram.alphaTexCoordT, regs, texture );
}

static void RB_TranslucentShadowMapSetAlphaTexCoordIdentity( void ) {
	RB_ShadowMapSetTexCoordUniforms( g_translucentShadowCasterProgram.alphaTexCoordS, g_translucentShadowCasterProgram.alphaTexCoordT, NULL, NULL );
}

static void RB_TranslucentShadowMapSetAlphaTexCoordMatrix( const float *regs, const textureStage_t *texture ) {
	RB_ShadowMapSetTexCoordUniforms( g_translucentShadowCasterProgram.alphaTexCoordS, g_translucentShadowCasterProgram.alphaTexCoordT, regs, texture );
}

static void RB_TranslucentShadowMapSetCoverageTexCoordIdentity( void ) {
	RB_ShadowMapSetTexCoordUniforms( g_translucentShadowCasterProgram.coverageTexCoordS, g_translucentShadowCasterProgram.coverageTexCoordT, NULL, NULL );
}

static void RB_TranslucentShadowMapSetCoverageTexCoordMatrix( const float *regs, const textureStage_t *texture ) {
	RB_ShadowMapSetTexCoordUniforms( g_translucentShadowCasterProgram.coverageTexCoordS, g_translucentShadowCasterProgram.coverageTexCoordT, regs, texture );
}

static void RB_ShadowMapDrawPerforatedCasterProgram( const drawSurf_t *surf, const srfTriangles_t *casterGeo, idDrawVert *ac, const int cascadeIndex, const bool useHashedAlpha ) {
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	bool didDraw = false;

	if ( shader == NULL || regs == NULL ) {
		RB_ShadowMapDrawOpaqueCasterProgram( casterGeo );
		return;
	}

	if ( g_shadowMapCasterProgram.alphaTestEnabled >= 0 ) {
		glUniform1fARB( g_shadowMapCasterProgram.alphaTestEnabled, 1.0f );
	}
	if ( g_shadowMapCasterProgram.alphaHashEnabled >= 0 ) {
		glUniform1fARB( g_shadowMapCasterProgram.alphaHashEnabled, useHashedAlpha ? 1.0f : 0.0f );
	}
	if ( g_shadowMapCasterProgram.alphaHashStable >= 0 ) {
		glUniform1fARB( g_shadowMapCasterProgram.alphaHashStable, r_shadowMapStableAlphaHash.GetBool() ? 1.0f : 0.0f );
	}

	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );

	const int stageCount = shader->GetNumStages();
	for ( int stage = 0; stage < stageCount; stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}

		didDraw = true;
		const float alphaScale = regs[ pStage->color.registers[3] ];
		if ( alphaScale <= 0.0f ) {
			continue;
		}

		if ( g_shadowMapCasterProgram.alphaRef >= 0 ) {
			glUniform1fARB( g_shadowMapCasterProgram.alphaRef, regs[ pStage->alphaTestRegister ] );
		}
		if ( g_shadowMapCasterProgram.alphaTestMode >= 0 ) {
			glUniform1fARB( g_shadowMapCasterProgram.alphaTestMode, RB_ShadowMapAlphaTestModeValue( pStage->alphaTestMode ) );
		}
		if ( g_shadowMapCasterProgram.alphaScale >= 0 ) {
			glUniform1fARB( g_shadowMapCasterProgram.alphaScale, alphaScale );
		}
		RB_ShadowMapSetAlphaTexCoordMatrix( regs, &pStage->texture );
		RB_BindVariableStageImage( &pStage->texture, regs );
		RB_DrawElementsWithCounters( casterGeo );
	}

	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 0 );
	RB_ShadowMapSetAlphaTexCoordIdentity();
	RB_ShadowMapDisableCasterAlphaTest();

	if ( !didDraw ) {
		RB_ShadowMapDrawOpaqueCasterProgram( casterGeo );
	}
}

// Conservative per-cascade caster rejection: all eight world-space bounds
// corners project outside the same side of the cascade's cropped NDC square.
// Apex-side corners (w near zero) keep the caster, staying conservative.
// Without this, CSM re-rasterized every caster into every cascade tile.
static bool RB_ShadowMapCasterOutsideCascade( const drawSurf_t *surf, const srfTriangles_t *casterGeo, const int cascadeIndex ) {
	if ( surf == NULL || surf->space == NULL || casterGeo == NULL || casterGeo->bounds.IsCleared() ) {
		return false;
	}
	const int safeCascadeIndex = idMath::ClampInt( 0, SHADOWMAP_MAX_CASCADES - 1, cascadeIndex );
	const idPlane *planes = g_projectedShadowMapState.clipPlanes[safeCascadeIndex];
	const idBounds &bounds = casterGeo->bounds;
	int outsideMask = 0x0F;
	for ( int i = 0; i < 8; i++ ) {
		const idVec3 corner( bounds[( i >> 0 ) & 1][0], bounds[( i >> 1 ) & 1][1], bounds[( i >> 2 ) & 1][2] );
		idVec3 world;
		R_LocalPointToGlobal( surf->space->modelMatrix, corner, world );
		const float w = planes[3].Distance( world );
		if ( w <= 1.0e-5f ) {
			return false;
		}
		int mask = 0;
		if ( planes[0].Distance( world ) < -w ) {
			mask |= 1;
		} else if ( planes[0].Distance( world ) > w ) {
			mask |= 2;
		}
		if ( planes[1].Distance( world ) < -w ) {
			mask |= 4;
		} else if ( planes[1].Distance( world ) > w ) {
			mask |= 8;
		}
		outsideMask &= mask;
		if ( outsideMask == 0 ) {
			return false;
		}
	}
	return outsideMask != 0;
}

static bool RB_ShadowMapDrawCasterChain( const drawSurf_t *surf, const int cascadeIndex, const bool useHashedAlpha ) {
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		srfTriangles_t *casterGeo = NULL;
		vertCache_s *ambientCache = NULL;
		if ( !RB_ShadowMapResolveCasterDrawData( surf, casterGeo, ambientCache ) ) {
			return false;
		}
		if ( surf->space == NULL ) {
			RB_ShadowMapReportCasterSkip( surf, casterGeo, "no-space" );
			return false;
		}

		if ( g_projectedShadowMapState.cascadeCount > 1 && RB_ShadowMapCasterOutsideCascade( surf, casterGeo, cascadeIndex ) ) {
			// Culled from this tile, not missing from the map.
			continue;
		}

		RB_ShadowMapTouchCache( ambientCache );
		RB_ShadowMapTouchCache( casterGeo->indexCache );

		if ( surf->space != backEnd.currentSpace ) {
			backEnd.currentSpace = surf->space;
			glLoadMatrixf( surf->space->modelMatrix );
			RB_ShadowMapSetCasterModelRows( surf );
			RB_ShadowMapSetCasterDepthRow( surf, cascadeIndex );
		}

		RB_ShadowMapApplyCasterCull( surf->material, casterGeo,
			surf->space->modelMatrix );

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );

		const idMaterial *shader = surf->material;
		if ( shader != NULL && shader->Coverage() == MC_PERFORATED ) {
			if ( RB_ShadowMapCanProgramPerforatedCaster( surf ) ) {
				RB_ShadowMapDrawPerforatedCasterProgram( surf, casterGeo, ac, cascadeIndex, useHashedAlpha );
			} else {
				RB_ShadowMapReportCasterSkip( surf, casterGeo, "unsupported-alpha-texgen-solid-depth" );
				RB_ShadowMapDrawOpaqueCasterProgram( casterGeo );
			}
			continue;
		}

		RB_ShadowMapDrawOpaqueCasterProgram( casterGeo );
	}

	return true;
}

static void RB_ShadowMapDrawTranslucentCasterChain( const drawSurf_t *surf, const int cascadeIndex ) {
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glEnableClientState( GL_COLOR_ARRAY );

	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		srfTriangles_t *casterGeo = NULL;
		vertCache_s *ambientCache = NULL;
		if ( !RB_ShadowMapResolveCasterDrawData( surf, casterGeo, ambientCache ) ) {
			continue;
		}
		if ( surf->space == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
			RB_ShadowMapReportCasterSkip( surf, casterGeo, "translucent-missing-shader" );
			continue;
		}

		RB_ShadowMapTouchCache( ambientCache );
		RB_ShadowMapTouchCache( casterGeo->indexCache );

		if ( surf->space != backEnd.currentSpace ) {
			backEnd.currentSpace = surf->space;
			glLoadMatrixf( surf->space->modelMatrix );
			RB_TranslucentShadowMapSetCasterDepthRow( surf, cascadeIndex );
		}

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );
		glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );
		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, color ) ) );

		const idMaterial *shader = surf->material;
		const float *regs = surf->shaderRegisters;
		const shaderStage_t *coverageStage = RB_FindTranslucentShadowCoverageStage( shader, regs );
		bool drewStage = false;

		const int stageCount = shader->GetNumStages();
		for ( int stage = 0; stage < stageCount; stage++ ) {
			const shaderStage_t *pStage = shader->GetStage( stage );
			if ( !RB_TranslucentShadowStageSupported( pStage, regs ) ) {
				continue;
			}

			const translucentShadowStageMode_t stageMode = RB_TranslucentShadowStageMode( pStage );
			const shaderStage_t *stageCoverage = RB_TranslucentShadowStageCanProvideCoverage( pStage, regs ) ? pStage : coverageStage;
			const float alphaScale = RB_TranslucentShadowStageOpacityScale( pStage, regs, stageMode );
			const float combinedScale = alphaScale * RB_TranslucentShadowCoverageScale( stageCoverage, regs );
			if ( combinedScale <= r_shadowMapTranslucentMinAlpha.GetFloat() ) {
				continue;
			}

			float stageColor[4];
			float vertexAlphaParams[2];
			float coverageColor[4];
			float coverageVertexAlphaParams[2];
			RB_TranslucentShadowStageColor( pStage, regs, stageColor );
			RB_TranslucentShadowVertexAlphaParams( pStage, vertexAlphaParams );
			RB_TranslucentShadowStageColor( stageCoverage, regs, coverageColor );
			RB_TranslucentShadowVertexAlphaParams( stageCoverage, coverageVertexAlphaParams );

			if ( g_translucentShadowCasterProgram.stageColor >= 0 ) {
				glUniform4fvARB( g_translucentShadowCasterProgram.stageColor, 1, stageColor );
			}
			if ( g_translucentShadowCasterProgram.opacitySourceMode >= 0 ) {
				glUniform1fARB( g_translucentShadowCasterProgram.opacitySourceMode, RB_TranslucentShadowStageOpacityModeValue( stageMode ) );
			}
			if ( g_translucentShadowCasterProgram.vertexColorMode >= 0 ) {
				glUniform1fARB( g_translucentShadowCasterProgram.vertexColorMode, RB_TranslucentShadowVertexColorModeValue( pStage ) );
			}
			if ( g_translucentShadowCasterProgram.vertexAlphaParams >= 0 ) {
				glUniform2fvARB( g_translucentShadowCasterProgram.vertexAlphaParams, 1, vertexAlphaParams );
			}
			if ( g_translucentShadowCasterProgram.coverageStageColor >= 0 ) {
				glUniform4fvARB( g_translucentShadowCasterProgram.coverageStageColor, 1, coverageColor );
			}
			if ( g_translucentShadowCasterProgram.coverageSourceMode >= 0 ) {
				glUniform1fARB( g_translucentShadowCasterProgram.coverageSourceMode, RB_TranslucentShadowCoverageSourceModeValue( stageCoverage ) );
			}
			if ( g_translucentShadowCasterProgram.coverageVertexColorMode >= 0 ) {
				glUniform1fARB( g_translucentShadowCasterProgram.coverageVertexColorMode, RB_TranslucentShadowVertexColorModeValue( stageCoverage ) );
			}
			if ( g_translucentShadowCasterProgram.coverageVertexAlphaParams >= 0 ) {
				glUniform2fvARB( g_translucentShadowCasterProgram.coverageVertexAlphaParams, 1, coverageVertexAlphaParams );
			}
			if ( g_translucentShadowCasterProgram.coverageAlphaTestEnabled >= 0 ) {
				glUniform1fARB( g_translucentShadowCasterProgram.coverageAlphaTestEnabled, ( stageCoverage != NULL && stageCoverage->hasAlphaTest ) ? 1.0f : 0.0f );
			}
			if ( g_translucentShadowCasterProgram.coverageAlphaTestRef >= 0 ) {
				const float alphaRef = ( stageCoverage != NULL && stageCoverage->hasAlphaTest ) ? regs[ stageCoverage->alphaTestRegister ] : 0.0f;
				glUniform1fARB( g_translucentShadowCasterProgram.coverageAlphaTestRef, alphaRef );
			}
			if ( g_translucentShadowCasterProgram.coverageAlphaTestMode >= 0 ) {
				const float alphaTestMode = ( stageCoverage != NULL && stageCoverage->hasAlphaTest ) ? RB_ShadowMapAlphaTestModeValue( stageCoverage->alphaTestMode ) : 1.0f;
				glUniform1fARB( g_translucentShadowCasterProgram.coverageAlphaTestMode, alphaTestMode );
			}
			if ( stageMode == TRANSLUCENT_SHADOW_STAGE_CUBEMAP_ADDITIVE ) {
				RB_TranslucentShadowMapSetAlphaTexCoordIdentity();
				GL_SelectTexture( 0 );
				RB_BindVariableStageImage( &pStage->texture, regs );
			} else {
				RB_TranslucentShadowMapSetAlphaTexCoordMatrix( regs, &pStage->texture );
				GL_SelectTexture( 0 );
				RB_BindVariableStageImage( &pStage->texture, regs );
			}
			if ( stageCoverage != NULL ) {
				RB_TranslucentShadowMapSetCoverageTexCoordMatrix( regs, &stageCoverage->texture );
				GL_SelectTexture( 1 );
				RB_BindVariableStageImage( &stageCoverage->texture, regs );
				GL_SelectTexture( 0 );
			} else {
				RB_TranslucentShadowMapSetCoverageTexCoordIdentity();
				GL_SelectTexture( 1 );
				globalImages->whiteImage->Bind();
				GL_SelectTexture( 0 );
			}
			RB_DrawElementsWithCounters( casterGeo );
			drewStage = true;
		}

		if ( !drewStage ) {
			RB_ShadowMapReportCasterSkip( surf, casterGeo, "no-translucent-shadow-stage" );
		}
	}

	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	RB_TranslucentShadowMapSetAlphaTexCoordIdentity();
	RB_TranslucentShadowMapSetCoverageTexCoordIdentity();
}

static void RB_ShadowMapModelMatrixRows( const float modelMatrix[16], float row0[4], float row1[4], float row2[4] ) {
	row0[0] = modelMatrix[0];
	row0[1] = modelMatrix[4];
	row0[2] = modelMatrix[8];
	row0[3] = modelMatrix[12];

	row1[0] = modelMatrix[1];
	row1[1] = modelMatrix[5];
	row1[2] = modelMatrix[9];
	row1[3] = modelMatrix[13];

	row2[0] = modelMatrix[2];
	row2[1] = modelMatrix[6];
	row2[2] = modelMatrix[10];
	row2[3] = modelMatrix[14];
}

static void RB_PointShadowMapBuildViewAxis( const int cubeFace, idMat3 &axis ) {
	memset( &axis, 0, sizeof( axis ) );

	switch ( cubeFace ) {
	case 0:
		axis[0][0] = 1.0f;
		axis[1][2] = 1.0f;
		axis[2][1] = -1.0f;
		break;
	case 1:
		axis[0][0] = -1.0f;
		axis[1][2] = -1.0f;
		axis[2][1] = -1.0f;
		break;
	case 2:
		axis[0][1] = 1.0f;
		axis[1][0] = -1.0f;
		axis[2][2] = 1.0f;
		break;
	case 3:
		axis[0][1] = -1.0f;
		axis[1][0] = -1.0f;
		axis[2][2] = -1.0f;
		break;
	case 4:
		axis[0][2] = 1.0f;
		axis[1][0] = -1.0f;
		axis[2][1] = -1.0f;
		break;
	default:
		axis[0][2] = -1.0f;
		axis[1][0] = 1.0f;
		axis[2][1] = -1.0f;
		break;
	}
}

static void RB_PointShadowMapBuildModelViewMatrix( const idVec3 &origin, const int cubeFace, float matrix[16] ) {
	viewDef_t shadowView;
	memset( &shadowView, 0, sizeof( shadowView ) );
	shadowView.renderView.vieworg = origin;
	RB_PointShadowMapBuildViewAxis( cubeFace, shadowView.renderView.viewaxis );
	R_SetViewMatrix( &shadowView );
	memcpy( matrix, shadowView.worldSpace.modelViewMatrix, sizeof( shadowView.worldSpace.modelViewMatrix ) );
}

static void RB_PointShadowMapBuildProjectionMatrix( const float zNear, const float zFar, float matrix[16] ) {
	memset( matrix, 0, 16 * sizeof( float ) );
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = -( zFar + zNear ) / ( zFar - zNear );
	matrix[11] = -1.0f;
	matrix[14] = -( 2.0f * zFar * zNear ) / ( zFar - zNear );
}

static void RB_PointShadowMapSetAlphaTexCoordIdentity( void ) {
	static const float rowS[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	static const float rowT[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

	if ( g_pointShadowCasterProgram.alphaTexCoordS >= 0 ) {
		glUniform4fvARB( g_pointShadowCasterProgram.alphaTexCoordS, 1, rowS );
	}
	if ( g_pointShadowCasterProgram.alphaTexCoordT >= 0 ) {
		glUniform4fvARB( g_pointShadowCasterProgram.alphaTexCoordT, 1, rowT );
	}
}

static void RB_PointShadowMapDisableAlphaTest( void ) {
	if ( g_pointShadowCasterProgram.alphaTestEnabled >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaTestEnabled, 0.0f );
	}
}

static bool RB_PointShadowMapCanProcessPerforatedCaster( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return false;
	}

	bool haveActiveStage = false;
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	const int stageCount = shader->GetNumStages();
	for ( int stage = 0; stage < stageCount; stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}

		haveActiveStage = true;
		if ( !RB_ShadowMapTextureStageHasBindableImage( &pStage->texture ) || pStage->texture.texgen != TG_EXPLICIT ) {
			return false;
		}
	}

	return haveActiveStage;
}

static void RB_PointShadowMapSetAlphaTexCoordMatrix( const float *regs, const textureStage_t *texture ) {
	float rowS[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	float rowT[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

	if ( regs != NULL && texture != NULL && texture->hasMatrix ) {
		float matrix[16];
		RB_GetShaderTextureMatrix( regs, texture, matrix );
		rowS[0] = matrix[0];
		rowS[1] = matrix[4];
		rowS[2] = matrix[8];
		rowS[3] = matrix[12];
		rowT[0] = matrix[1];
		rowT[1] = matrix[5];
		rowT[2] = matrix[9];
		rowT[3] = matrix[13];
	}

	if ( g_pointShadowCasterProgram.alphaTexCoordS >= 0 ) {
		glUniform4fvARB( g_pointShadowCasterProgram.alphaTexCoordS, 1, rowS );
	}
	if ( g_pointShadowCasterProgram.alphaTexCoordT >= 0 ) {
		glUniform4fvARB( g_pointShadowCasterProgram.alphaTexCoordT, 1, rowT );
	}
}

static void RB_PointTranslucentShadowMapSetAlphaTexCoordIdentity( void ) {
	RB_ShadowMapSetTexCoordUniforms( g_pointTranslucentShadowCasterProgram.alphaTexCoordS, g_pointTranslucentShadowCasterProgram.alphaTexCoordT, NULL, NULL );
}

static void RB_PointTranslucentShadowMapSetAlphaTexCoordMatrix( const float *regs, const textureStage_t *texture ) {
	RB_ShadowMapSetTexCoordUniforms( g_pointTranslucentShadowCasterProgram.alphaTexCoordS, g_pointTranslucentShadowCasterProgram.alphaTexCoordT, regs, texture );
}

static void RB_PointTranslucentShadowMapSetCoverageTexCoordIdentity( void ) {
	RB_ShadowMapSetTexCoordUniforms( g_pointTranslucentShadowCasterProgram.coverageTexCoordS, g_pointTranslucentShadowCasterProgram.coverageTexCoordT, NULL, NULL );
}

static void RB_PointTranslucentShadowMapSetCoverageTexCoordMatrix( const float *regs, const textureStage_t *texture ) {
	RB_ShadowMapSetTexCoordUniforms( g_pointTranslucentShadowCasterProgram.coverageTexCoordS, g_pointTranslucentShadowCasterProgram.coverageTexCoordT, regs, texture );
}

static bool RB_PointShadowMapPrepareAlphaTestStage( const shaderStage_t *pStage, idDrawVert *ac, const float *regs, const float alphaScale ) {
	if ( pStage == NULL || regs == NULL || !RB_ShadowMapTextureStageHasBindableImage( &pStage->texture ) ) {
		return false;
	}

	// Phase 1 implements the common explicit-ST cutout path and preserves matrix animation.
	if ( pStage->texture.texgen != TG_EXPLICIT ) {
		return false;
	}

	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );
	RB_PointShadowMapSetAlphaTexCoordMatrix( regs, &pStage->texture );

	if ( g_pointShadowCasterProgram.alphaRef >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaRef, regs[ pStage->alphaTestRegister ] );
	}
	if ( g_pointShadowCasterProgram.alphaTestMode >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaTestMode, RB_ShadowMapAlphaTestModeValue( pStage->alphaTestMode ) );
	}
	if ( g_pointShadowCasterProgram.alphaScale >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaScale, alphaScale );
	}
	if ( g_pointShadowCasterProgram.alphaTestEnabled >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaTestEnabled, 1.0f );
	}
	if ( g_pointShadowCasterProgram.alphaHashEnabled >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaHashEnabled, RB_ShadowMapHashedAlphaEnabled() ? 1.0f : 0.0f );
	}
	if ( g_pointShadowCasterProgram.alphaHashStable >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaHashStable, r_shadowMapStableAlphaHash.GetBool() ? 1.0f : 0.0f );
	}

	RB_BindVariableStageImage( &pStage->texture, regs );
	return true;
}

static void RB_PointShadowMapFinishAlphaTestStage( void ) {
	RB_PointShadowMapDisableAlphaTest();
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 0 );
}

static void RB_PointShadowMapDrawPerforatedCaster( const drawSurf_t *surf, const srfTriangles_t *casterGeo, idDrawVert *ac ) {
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	bool didDraw = false;

	if ( shader == NULL || regs == NULL ) {
		RB_DrawElementsWithCounters( casterGeo );
		return;
	}

	const int stageCount = shader->GetNumStages();
	for ( int stage = 0; stage < stageCount; stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );

		if ( !pStage->hasAlphaTest ) {
			continue;
		}

		if ( regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}

		didDraw = true;
		const float alphaScale = regs[ pStage->color.registers[3] ];
		if ( alphaScale <= 0.0f ) {
			continue;
		}

		if ( !RB_PointShadowMapPrepareAlphaTestStage( pStage, ac, regs, alphaScale ) ) {
			continue;
		}

		RB_DrawElementsWithCounters( casterGeo );
		RB_PointShadowMapFinishAlphaTestStage();
	}

	RB_PointShadowMapDisableAlphaTest();
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 0 );
	RB_PointShadowMapSetAlphaTexCoordIdentity();

	if ( !didDraw ) {
		RB_DrawElementsWithCounters( casterGeo );
	}
}

// Share the conservative transformed-bounds test with the Vulkan cube path.
static bool RB_PointShadowMapCasterOutsideFace( const drawSurf_t *surf, const srfTriangles_t *casterGeo, const int cubeFace ) {
	if ( surf == NULL || surf->space == NULL || casterGeo == NULL || casterGeo->bounds.IsCleared() || backEnd.vLight == NULL ) {
		return false;
	}
	return R_ShadowMapCasterOutsidePointFace( backEnd.vLight,
		surf->space->modelMatrix, casterGeo->bounds[0].ToFloatPtr(),
		casterGeo->bounds[1].ToFloatPtr(), cubeFace );
}

static bool RB_PointShadowMapDrawCasterChain( const drawSurf_t *surf, const float lightModelViewMatrix[16], const int cubeFace ) {
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		srfTriangles_t *casterGeo = NULL;
		vertCache_s *ambientCache = NULL;
		if ( !RB_ShadowMapResolveCasterDrawData( surf, casterGeo, ambientCache ) ) {
			return false;
		}
		if ( surf->space == NULL ) {
			RB_ShadowMapReportCasterSkip( surf, casterGeo, "no-space" );
			return false;
		}

		if ( RB_PointShadowMapCasterOutsideFace( surf, casterGeo, cubeFace ) ) {
			// Culled from this face, not missing from the map.
			continue;
		}

		RB_ShadowMapTouchCache( ambientCache );
		RB_ShadowMapTouchCache( casterGeo->indexCache );

		if ( surf->space != backEnd.currentSpace ) {
			float modelViewMatrix[16];
			float row0[4];
			float row1[4];
			float row2[4];

			backEnd.currentSpace = surf->space;
			myGlMultMatrix( surf->space->modelMatrix, lightModelViewMatrix, modelViewMatrix );
			glLoadMatrixf( modelViewMatrix );

			RB_ShadowMapModelMatrixRows( surf->space->modelMatrix, row0, row1, row2 );
			glUniform4fvARB( g_pointShadowCasterProgram.modelMatrixRow0, 1, row0 );
			glUniform4fvARB( g_pointShadowCasterProgram.modelMatrixRow1, 1, row1 );
			glUniform4fvARB( g_pointShadowCasterProgram.modelMatrixRow2, 1, row2 );
		}

		RB_ShadowMapApplyCasterCull( surf->material, casterGeo,
			surf->space->modelMatrix );

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );

		const idMaterial *shader = surf->material;
		if ( shader != NULL && shader->Coverage() == MC_PERFORATED ) {
			if ( RB_PointShadowMapCanProcessPerforatedCaster( surf ) ) {
				RB_PointShadowMapDrawPerforatedCaster( surf, casterGeo, ac );
			} else {
				RB_DrawElementsWithCounters( casterGeo );
			}
			continue;
		}

		RB_DrawElementsWithCounters( casterGeo );
	}

	return true;
}

static void RB_PointShadowMapDrawTranslucentCasterChain( const drawSurf_t *surf, const float lightModelViewMatrix[16] ) {
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glEnableClientState( GL_COLOR_ARRAY );

	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		srfTriangles_t *casterGeo = NULL;
		vertCache_s *ambientCache = NULL;
		if ( !RB_ShadowMapResolveCasterDrawData( surf, casterGeo, ambientCache ) ) {
			continue;
		}
		if ( surf->space == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
			RB_ShadowMapReportCasterSkip( surf, casterGeo, "translucent-missing-shader" );
			continue;
		}

		RB_ShadowMapTouchCache( ambientCache );
		RB_ShadowMapTouchCache( casterGeo->indexCache );

		if ( surf->space != backEnd.currentSpace ) {
			float modelViewMatrix[16];
			float row0[4];
			float row1[4];
			float row2[4];

			backEnd.currentSpace = surf->space;
			myGlMultMatrix( surf->space->modelMatrix, lightModelViewMatrix, modelViewMatrix );
			glLoadMatrixf( modelViewMatrix );

			RB_ShadowMapModelMatrixRows( surf->space->modelMatrix, row0, row1, row2 );
			if ( g_pointTranslucentShadowCasterProgram.modelMatrixRow0 >= 0 ) {
				glUniform4fvARB( g_pointTranslucentShadowCasterProgram.modelMatrixRow0, 1, row0 );
			}
			if ( g_pointTranslucentShadowCasterProgram.modelMatrixRow1 >= 0 ) {
				glUniform4fvARB( g_pointTranslucentShadowCasterProgram.modelMatrixRow1, 1, row1 );
			}
			if ( g_pointTranslucentShadowCasterProgram.modelMatrixRow2 >= 0 ) {
				glUniform4fvARB( g_pointTranslucentShadowCasterProgram.modelMatrixRow2, 1, row2 );
			}
		}

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );
		glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );
		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, color ) ) );

		const idMaterial *shader = surf->material;
		const float *regs = surf->shaderRegisters;
		const shaderStage_t *coverageStage = RB_FindTranslucentShadowCoverageStage( shader, regs );
		bool drewStage = false;

		const int stageCount = shader->GetNumStages();
		for ( int stage = 0; stage < stageCount; stage++ ) {
			const shaderStage_t *pStage = shader->GetStage( stage );
			if ( !RB_TranslucentShadowStageSupported( pStage, regs ) ) {
				continue;
			}

			const translucentShadowStageMode_t stageMode = RB_TranslucentShadowStageMode( pStage );
			const shaderStage_t *stageCoverage = RB_TranslucentShadowStageCanProvideCoverage( pStage, regs ) ? pStage : coverageStage;
			const float alphaScale = RB_TranslucentShadowStageOpacityScale( pStage, regs, stageMode );
			const float combinedScale = alphaScale * RB_TranslucentShadowCoverageScale( stageCoverage, regs );
			if ( combinedScale <= r_shadowMapTranslucentMinAlpha.GetFloat() ) {
				continue;
			}

			float stageColor[4];
			float vertexAlphaParams[2];
			float coverageColor[4];
			float coverageVertexAlphaParams[2];
			RB_TranslucentShadowStageColor( pStage, regs, stageColor );
			RB_TranslucentShadowVertexAlphaParams( pStage, vertexAlphaParams );
			RB_TranslucentShadowStageColor( stageCoverage, regs, coverageColor );
			RB_TranslucentShadowVertexAlphaParams( stageCoverage, coverageVertexAlphaParams );

			if ( g_pointTranslucentShadowCasterProgram.stageColor >= 0 ) {
				glUniform4fvARB( g_pointTranslucentShadowCasterProgram.stageColor, 1, stageColor );
			}
			if ( g_pointTranslucentShadowCasterProgram.opacitySourceMode >= 0 ) {
				glUniform1fARB( g_pointTranslucentShadowCasterProgram.opacitySourceMode, RB_TranslucentShadowStageOpacityModeValue( stageMode ) );
			}
			if ( g_pointTranslucentShadowCasterProgram.vertexColorMode >= 0 ) {
				glUniform1fARB( g_pointTranslucentShadowCasterProgram.vertexColorMode, RB_TranslucentShadowVertexColorModeValue( pStage ) );
			}
			if ( g_pointTranslucentShadowCasterProgram.vertexAlphaParams >= 0 ) {
				glUniform2fvARB( g_pointTranslucentShadowCasterProgram.vertexAlphaParams, 1, vertexAlphaParams );
			}
			if ( g_pointTranslucentShadowCasterProgram.coverageStageColor >= 0 ) {
				glUniform4fvARB( g_pointTranslucentShadowCasterProgram.coverageStageColor, 1, coverageColor );
			}
			if ( g_pointTranslucentShadowCasterProgram.coverageSourceMode >= 0 ) {
				glUniform1fARB( g_pointTranslucentShadowCasterProgram.coverageSourceMode, RB_TranslucentShadowCoverageSourceModeValue( stageCoverage ) );
			}
			if ( g_pointTranslucentShadowCasterProgram.coverageVertexColorMode >= 0 ) {
				glUniform1fARB( g_pointTranslucentShadowCasterProgram.coverageVertexColorMode, RB_TranslucentShadowVertexColorModeValue( stageCoverage ) );
			}
			if ( g_pointTranslucentShadowCasterProgram.coverageVertexAlphaParams >= 0 ) {
				glUniform2fvARB( g_pointTranslucentShadowCasterProgram.coverageVertexAlphaParams, 1, coverageVertexAlphaParams );
			}
			if ( g_pointTranslucentShadowCasterProgram.coverageAlphaTestEnabled >= 0 ) {
				glUniform1fARB( g_pointTranslucentShadowCasterProgram.coverageAlphaTestEnabled, ( stageCoverage != NULL && stageCoverage->hasAlphaTest ) ? 1.0f : 0.0f );
			}
			if ( g_pointTranslucentShadowCasterProgram.coverageAlphaTestRef >= 0 ) {
				const float alphaRef = ( stageCoverage != NULL && stageCoverage->hasAlphaTest ) ? regs[ stageCoverage->alphaTestRegister ] : 0.0f;
				glUniform1fARB( g_pointTranslucentShadowCasterProgram.coverageAlphaTestRef, alphaRef );
			}
			if ( g_pointTranslucentShadowCasterProgram.coverageAlphaTestMode >= 0 ) {
				const float alphaTestMode = ( stageCoverage != NULL && stageCoverage->hasAlphaTest ) ? RB_ShadowMapAlphaTestModeValue( stageCoverage->alphaTestMode ) : 1.0f;
				glUniform1fARB( g_pointTranslucentShadowCasterProgram.coverageAlphaTestMode, alphaTestMode );
			}
			if ( stageMode == TRANSLUCENT_SHADOW_STAGE_CUBEMAP_ADDITIVE ) {
				RB_PointTranslucentShadowMapSetAlphaTexCoordIdentity();
				GL_SelectTexture( 0 );
				RB_BindVariableStageImage( &pStage->texture, regs );
			} else {
				RB_PointTranslucentShadowMapSetAlphaTexCoordMatrix( regs, &pStage->texture );
				GL_SelectTexture( 0 );
				RB_BindVariableStageImage( &pStage->texture, regs );
			}
			if ( stageCoverage != NULL ) {
				RB_PointTranslucentShadowMapSetCoverageTexCoordMatrix( regs, &stageCoverage->texture );
				GL_SelectTexture( 1 );
				RB_BindVariableStageImage( &stageCoverage->texture, regs );
				GL_SelectTexture( 0 );
			} else {
				RB_PointTranslucentShadowMapSetCoverageTexCoordIdentity();
				GL_SelectTexture( 1 );
				globalImages->whiteImage->Bind();
				GL_SelectTexture( 0 );
			}
			RB_DrawElementsWithCounters( casterGeo );
			drewStage = true;
		}

		if ( !drewStage ) {
			RB_ShadowMapReportCasterSkip( surf, casterGeo, "no-translucent-shadow-stage" );
		}
	}

	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	RB_PointTranslucentShadowMapSetAlphaTexCoordIdentity();
	RB_PointTranslucentShadowMapSetCoverageTexCoordIdentity();
}

// SHADOWMAP_RENDER_FULL: build state, clear tiles, draw every chain (the
// scratch/uncacheable path, and cacheable lights without dynamic casters).
// SHADOWMAP_RENDER_STATIC_ONLY: same, but draws only the static chains
// (primary/secondary) - renders a light's cached static tiles.
// SHADOWMAP_RENDER_COMPOSE_DYNAMIC: keep the current state (the cached
// entry's), blit the entry's static tiles from the atlas over the scratch
// target, then draw only the dynamic chains (tertiary/quaternary) on top
// with no clear - a moving character costs one blit plus its own draws.
typedef enum {
	SHADOWMAP_RENDER_FULL = 0,
	SHADOWMAP_RENDER_STATIC_ONLY,
	SHADOWMAP_RENDER_COMPOSE_DYNAMIC
} shadowMapRenderMode_t;

static bool RB_RenderShadowMap( const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *tertiaryCasters, const drawSurf_t *quaternaryCasters,
	const shadowMapRenderMode_t renderMode = SHADOWMAP_RENDER_FULL, const projectedShadowMapCacheEntry_t *composeFrom = NULL ) {
	if ( !RB_ShadowMapEnsureResources( backEnd.vLight ) ) {
		return false;
	}

	const bool composeMode = renderMode == SHADOWMAP_RENDER_COMPOSE_DYNAMIC;
	const bool drawStaticChains = renderMode != SHADOWMAP_RENDER_COMPOSE_DYNAMIC;
	const bool drawDynamicChains = renderMode != SHADOWMAP_RENDER_STATIC_ONLY;
	bool allCastersRendered = true;
	const GLboolean blendWasEnabled = glIsEnabled( GL_BLEND );
	const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
	const GLboolean stencilWasEnabled = glIsEnabled( GL_STENCIL_TEST );
	const int savedFaceCulling = backEnd.glState.faceCulling;

	if ( composeMode ) {
		// the state must stay the cached entry's: dynamic casters draw with
		// the exact matrices the static tiles were rendered with
		if ( composeFrom == NULL || !g_projectedShadowMapState.valid || glBlitFramebuffer == NULL
			|| g_shadowMapAtlasRenderTexture == NULL || g_shadowMapRenderTexture == g_shadowMapAtlasRenderTexture ) {
			return false;
		}
		g_shadowMapStats.composePasses++;
	} else {
		RB_ShadowMapBuildProjectedState( backEnd.vLight, RB_ShadowMapTileSizeForLight( backEnd.vLight ) );
		if ( !g_projectedShadowMapState.valid ) {
			return false;
		}

		g_shadowMapStats.shadowMapUpdates++;
		if ( backEnd.viewDef != NULL && backEnd.viewDef->isSubview ) {
			g_shadowMapSubviewUpdates++;
		}
		g_shadowMapStats.projectedAtlasTilesRendered += g_projectedShadowMapState.cascadeCount;
		g_shadowMapStats.projectedAtlasTilesAllocated += Max( 1, g_projectedShadowMapState.atlasDiv * g_projectedShadowMapState.atlasDiv );
		for ( int cascadeIndex = 0; cascadeIndex < g_projectedShadowMapState.cascadeCount; cascadeIndex++ ) {
			RB_ShadowMapStatsRecordWorldTexelSize( g_projectedShadowMapState.worldTexelSize[cascadeIndex] );
		}
	}

	g_shadowMapRenderTexture->MakeCurrent();
	if ( g_shadowMapRenderTexture->IsKnownIncomplete() ) {
		// incomplete shadow framebuffer: fail this pass so the light takes the
		// stencil fallback instead of FatalError'ing the whole session
		if ( backEnd.renderTexture != NULL ) {
			backEnd.renderTexture->MakeCurrent();
		} else {
			idRenderTexture::BindNull();
			glDrawBuffer( GL_BACK );
			glReadBuffer( GL_BACK );
		}
		return false;
	}

	if ( composeMode ) {
		// import the cached static tiles: blit the entry's atlas cell block
		// over the scratch target (blit obeys scissor, so disable it first)
		const int blockPixels = g_projectedShadowMapState.tileSize * g_projectedShadowMapState.atlasDiv;
		const int srcX = composeFrom->atlasCellX * g_shadowMapAtlasCellSize;
		const int srcY = composeFrom->atlasCellY * g_shadowMapAtlasCellSize;
		glDisable( GL_SCISSOR_TEST );
		glBindFramebuffer( GL_READ_FRAMEBUFFER, g_shadowMapAtlasRenderTexture->GetDeviceHandle() );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, g_shadowMapRenderTexture->GetDeviceHandle() );
		glBlitFramebuffer( srcX, srcY, srcX + blockPixels, srcY + blockPixels,
			0, 0, blockPixels, blockPixels, GL_DEPTH_BUFFER_BIT, GL_NEAREST );
		glBindFramebuffer( GL_FRAMEBUFFER, g_shadowMapRenderTexture->GetDeviceHandle() );
	}

	// One caster program serves every opaque and perforated caster in this
	// pass; per-surface rebinding is unnecessary. The slope-scale caster
	// offset lives in the shader because shader-written depth ignores
	// glPolygonOffset.
	glUseProgramObjectARB( g_shadowMapCasterProgram.programObject );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	RB_ShadowMapSetCasterDepthOffsetUniform( g_shadowMapCasterProgram.casterDepthOffset );
	RB_ShadowMapDisableCasterAlphaTest();
	if ( g_shadowMapCasterProgram.alphaMap >= 0 ) {
		glUniform1iARB( g_shadowMapCasterProgram.alphaMap, 0 );
	}
	if ( g_shadowMapCasterProgram.alphaRef >= 0 ) {
		glUniform1fARB( g_shadowMapCasterProgram.alphaRef, 0.0f );
	}
	if ( g_shadowMapCasterProgram.alphaScale >= 0 ) {
		glUniform1fARB( g_shadowMapCasterProgram.alphaScale, 1.0f );
	}

	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glDisable( GL_BLEND );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_SCISSOR_TEST );
	RB_ShadowMapResetCasterCull();
	const bool useHashedAlpha = RB_ShadowMapHashedAlphaEnabled();

	for ( int cascadeIndex = 0; cascadeIndex < g_projectedShadowMapState.cascadeCount; cascadeIndex++ ) {
		float clipMatrix[16];
		// cascade tile offsets are relative to the light's slot in the shared
		// atlas (origin 0 when rendering into the per-light scratch texture)
		const int tileX = g_shadowMapActiveSlotOriginX + ( ( g_projectedShadowMapState.atlasDiv > 1 ) ? ( cascadeIndex % g_projectedShadowMapState.atlasDiv ) * g_projectedShadowMapState.tileSize : 0 );
		const int tileY = g_shadowMapActiveSlotOriginY + ( ( g_projectedShadowMapState.atlasDiv > 1 ) ? ( cascadeIndex / g_projectedShadowMapState.atlasDiv ) * g_projectedShadowMapState.tileSize : 0 );

		R_ShadowMapClipPlanesToGLMatrix( g_projectedShadowMapState.clipPlanes[cascadeIndex], clipMatrix );
		glViewport( tileX, tileY, g_projectedShadowMapState.tileSize, g_projectedShadowMapState.tileSize );
		glScissor( tileX, tileY, g_projectedShadowMapState.tileSize, g_projectedShadowMapState.tileSize );
		if ( !composeMode ) {
			// compose mode draws over the blitted static tiles
			glClear( GL_DEPTH_BUFFER_BIT );
		}

		glMatrixMode( GL_PROJECTION );
		glLoadMatrixf( clipMatrix );
		glMatrixMode( GL_MODELVIEW );

		backEnd.currentSpace = NULL;
		if ( drawStaticChains ) {
			allCastersRendered &= RB_ShadowMapDrawCasterChain( primaryCasters, cascadeIndex, useHashedAlpha );
			allCastersRendered &= RB_ShadowMapDrawCasterChain( secondaryCasters, cascadeIndex, useHashedAlpha );
		}
		if ( drawDynamicChains ) {
			allCastersRendered &= RB_ShadowMapDrawCasterChain( tertiaryCasters, cascadeIndex, useHashedAlpha );
			allCastersRendered &= RB_ShadowMapDrawCasterChain( quaternaryCasters, cascadeIndex, useHashedAlpha );
		}
	}

	// a two-sided caster may have left face culling disabled; the state
	// restore below cannot re-enable it from the invalidated cull cache
	glEnable( GL_CULL_FACE );
	glUseProgramObjectARB( 0 );

	if ( backEnd.renderTexture != NULL ) {
		backEnd.renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
		glDrawBuffer( GL_BACK );
		glReadBuffer( GL_BACK );
	}

	glViewport( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );
	glScissor( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	if ( scissorWasEnabled ) {
		glEnable( GL_SCISSOR_TEST );
	} else {
		glDisable( GL_SCISSOR_TEST );
	}
	if ( blendWasEnabled ) {
		glEnable( GL_BLEND );
	} else {
		glDisable( GL_BLEND );
	}
	// The stencil shadow path relies on the depth-fill pass's one-time enable
	// staying in effect for the whole view (RB_StencilShadowPass never enables it).
	if ( stencilWasEnabled ) {
		glEnable( GL_STENCIL_TEST );
	} else {
		glDisable( GL_STENCIL_TEST );
	}
	backEnd.glState.faceCulling = -1;
	if ( savedFaceCulling >= CT_FRONT_SIDED && savedFaceCulling <= CT_TWO_SIDED ) {
		GL_Cull( savedFaceCulling );
	}

	backEnd.currentSpace = NULL;
	backEnd.currentScissor.Clear();
	backEnd.glState.currenttmu = -1;
	GL_ClearStateDelta();
	GL_SelectTexture( 0 );

	// One drawable caster cannot stand in for a different missing caster.
	// This also applies to dynamics composed over valid cached static depth.
	return allCastersRendered;
}

static bool RB_RenderPointShadowMap( const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *tertiaryCasters, const drawSurf_t *quaternaryCasters ) {
	if ( !RB_PointShadowMapEnsureResources() ) {
		return false;
	}

	bool allCastersRendered = true;
	const GLboolean blendWasEnabled = glIsEnabled( GL_BLEND );
	const GLboolean stencilWasEnabled = glIsEnabled( GL_STENCIL_TEST );
	const int savedFaceCulling = backEnd.glState.faceCulling;

	const float farClip = R_ShadowMapPointFarDistance( backEnd.vLight );
	// with depth clamp, casters inside the near plane still rasterize (their
	// color-packed radial depth stays exact), so the near plane only shapes the
	// ordering-depth distribution; without it, shrink the clipped-away zone
	const float nearClip = idMath::ClampFloat( 0.5f, RB_ShadowMapDepthClampAvailable() ? 16.0f : 4.0f, farClip * 0.01f );
	float projectionMatrix[16];
	RB_PointShadowMapBuildProjectionMatrix( nearClip, farClip, projectionMatrix );
	g_shadowMapStats.shadowMapUpdates++;
	if ( backEnd.viewDef != NULL && backEnd.viewDef->isSubview ) {
		g_shadowMapSubviewUpdates++;
	}
	g_shadowMapStats.pointFacesRendered += 6;
	if ( RB_PointShadowMapHighPrecisionEnabled() ) {
		g_shadowMapStats.pointHighPrecisionPasses++;
	}
	if ( RB_PointShadowMapDepthCompareEnabled() ) {
		g_shadowMapStats.pointDepthComparePasses++;
	}

	const float globalLightOrigin[4] = {
		backEnd.vLight->globalLightOrigin[0],
		backEnd.vLight->globalLightOrigin[1],
		backEnd.vLight->globalLightOrigin[2],
		1.0f
	};
	GLfloat clearColor[4];
	glGetFloatv( GL_COLOR_CLEAR_VALUE, clearColor );

	const int shadowMapWidth = g_pointShadowMapRenderTexture->GetWidth();
	const int shadowMapHeight = g_pointShadowMapRenderTexture->GetHeight();

	// probe framebuffer completeness before touching pass state: an
	// incomplete point shadow target fails the pass into the stencil
	// fallback instead of FatalError'ing the whole session
	g_pointShadowMapRenderTexture->MakeCurrent( 0 );
	if ( g_pointShadowMapRenderTexture->IsKnownIncomplete() ) {
		if ( backEnd.renderTexture != NULL ) {
			backEnd.renderTexture->MakeCurrent();
		} else {
			idRenderTexture::BindNull();
			glDrawBuffer( GL_BACK );
			glReadBuffer( GL_BACK );
		}
		return false;
	}

	glUseProgramObjectARB( g_pointShadowCasterProgram.programObject );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUniform4fvARB( g_pointShadowCasterProgram.globalLightOrigin, 1, globalLightOrigin );
	glUniform1fARB( g_pointShadowCasterProgram.pointShadowFar, farClip );
	if ( g_pointShadowCasterProgram.pointShadowDepthMode >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.pointShadowDepthMode, RB_PointShadowMapDepthModeValue() );
	}
	RB_ShadowMapSetCasterDepthOffsetUniform( g_pointShadowCasterProgram.casterDepthOffset );
	if ( g_pointShadowCasterProgram.alphaMap >= 0 ) {
		glUniform1iARB( g_pointShadowCasterProgram.alphaMap, 0 );
	}
	if ( g_pointShadowCasterProgram.alphaRef >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaRef, 0.0f );
	}
	if ( g_pointShadowCasterProgram.alphaScale >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaScale, 1.0f );
	}
	RB_PointShadowMapDisableAlphaTest();
	if ( g_pointShadowCasterProgram.alphaHashEnabled >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaHashEnabled, RB_ShadowMapHashedAlphaEnabled() ? 1.0f : 0.0f );
	}
	if ( g_pointShadowCasterProgram.alphaHashStable >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaHashStable, r_shadowMapStableAlphaHash.GetBool() ? 1.0f : 0.0f );
	}
	RB_PointShadowMapSetAlphaTexCoordIdentity();

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glDisable( GL_BLEND );
	glDisable( GL_STENCIL_TEST );
	RB_ShadowMapResetCasterCull();
	if ( RB_ShadowMapDepthClampAvailable() ) {
		glEnable( GL_DEPTH_CLAMP_NV );
	}
	glClearColor( 1.0f, 1.0f, 1.0f, 1.0f );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	for ( int cubeFace = 0; cubeFace < 6; cubeFace++ ) {
		float lightModelViewMatrix[16];
		RB_PointShadowMapBuildModelViewMatrix( backEnd.vLight->globalLightOrigin, cubeFace, lightModelViewMatrix );

		g_pointShadowMapRenderTexture->MakeCurrent( cubeFace );
		glViewport( 0, 0, shadowMapWidth, shadowMapHeight );
		glScissor( 0, 0, shadowMapWidth, shadowMapHeight );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

		backEnd.currentSpace = NULL;
		allCastersRendered &= RB_PointShadowMapDrawCasterChain( primaryCasters, lightModelViewMatrix, cubeFace );
		allCastersRendered &= RB_PointShadowMapDrawCasterChain( secondaryCasters, lightModelViewMatrix, cubeFace );
		allCastersRendered &= RB_PointShadowMapDrawCasterChain( tertiaryCasters, lightModelViewMatrix, cubeFace );
		allCastersRendered &= RB_PointShadowMapDrawCasterChain( quaternaryCasters, lightModelViewMatrix, cubeFace );
	}

	if ( RB_ShadowMapDepthClampAvailable() ) {
		glDisable( GL_DEPTH_CLAMP_NV );
	}
	// a two-sided caster may have left face culling disabled; the state
	// restore below cannot re-enable it from the invalidated cull cache
	glEnable( GL_CULL_FACE );
	glUseProgramObjectARB( 0 );
	glClearColor( clearColor[0], clearColor[1], clearColor[2], clearColor[3] );

	if ( backEnd.renderTexture != NULL ) {
		backEnd.renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
		glDrawBuffer( GL_BACK );
		glReadBuffer( GL_BACK );
	}

	glViewport( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );
	glScissor( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	if ( blendWasEnabled ) {
		glEnable( GL_BLEND );
	} else {
		glDisable( GL_BLEND );
	}
	// Restore the view-lifetime stencil-test enable (see RB_RenderShadowMap).
	if ( stencilWasEnabled ) {
		glEnable( GL_STENCIL_TEST );
	} else {
		glDisable( GL_STENCIL_TEST );
	}
	backEnd.glState.faceCulling = -1;
	if ( savedFaceCulling >= CT_FRONT_SIDED && savedFaceCulling <= CT_TWO_SIDED ) {
		GL_Cull( savedFaceCulling );
	}

	backEnd.currentSpace = NULL;
	backEnd.currentScissor.Clear();
	backEnd.glState.currenttmu = -1;
	GL_ClearStateDelta();
	GL_SelectTexture( 0 );

	// See the projected-light path above: mapped point lights must not treat an
	// all-skipped legacy caster chain as a successful, empty shadow map.
	return allCastersRendered;
}

static bool RB_RenderTranslucentShadowMap( const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters ) {
	g_projectedTranslucentShadowPassReady = false;

	if ( !RB_ShadowMapEnsureTranslucentResources( backEnd.vLight ) ) {
		return false;
	}
	if ( !g_projectedShadowMapState.valid ) {
		return false;
	}

	const GLboolean blendWasEnabled = glIsEnabled( GL_BLEND );
	const GLboolean depthWasEnabled = glIsEnabled( GL_DEPTH_TEST );
	const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
	const GLboolean stencilWasEnabled = glIsEnabled( GL_STENCIL_TEST );
	const int savedFaceCulling = backEnd.glState.faceCulling;
	GLfloat clearColor[4];
	glGetFloatv( GL_COLOR_CLEAR_VALUE, clearColor );

	g_translucentShadowMapRenderTexture->MakeCurrent();
	glUseProgramObjectARB( g_translucentShadowCasterProgram.programObject );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	if ( g_translucentShadowCasterProgram.alphaMap >= 0 ) {
		glUniform1iARB( g_translucentShadowCasterProgram.alphaMap, 0 );
	}
	if ( g_translucentShadowCasterProgram.coverageMap >= 0 ) {
		glUniform1iARB( g_translucentShadowCasterProgram.coverageMap, 1 );
	}
	if ( g_translucentShadowCasterProgram.translucentMinAlpha >= 0 ) {
		glUniform1fARB( g_translucentShadowCasterProgram.translucentMinAlpha, r_shadowMapTranslucentMinAlpha.GetFloat() );
	}
	RB_TranslucentShadowMapSetAlphaTexCoordIdentity();
	RB_TranslucentShadowMapSetCoverageTexCoordIdentity();

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glDisable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ONE );
	glEnable( GL_SCISSOR_TEST );
	glEnable( GL_CULL_FACE );
	glCullFace( GL_BACK );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );

	for ( int cascadeIndex = 0; cascadeIndex < g_projectedShadowMapState.cascadeCount; cascadeIndex++ ) {
		float clipMatrix[16];
		const int tileX = ( g_projectedShadowMapState.atlasDiv > 1 ) ? ( cascadeIndex % g_projectedShadowMapState.atlasDiv ) * g_projectedShadowMapState.tileSize : 0;
		const int tileY = ( g_projectedShadowMapState.atlasDiv > 1 ) ? ( cascadeIndex / g_projectedShadowMapState.atlasDiv ) * g_projectedShadowMapState.tileSize : 0;

		R_ShadowMapClipPlanesToGLMatrix( g_projectedShadowMapState.clipPlanes[cascadeIndex], clipMatrix );
		glViewport( tileX, tileY, g_projectedShadowMapState.tileSize, g_projectedShadowMapState.tileSize );
		glScissor( tileX, tileY, g_projectedShadowMapState.tileSize, g_projectedShadowMapState.tileSize );
		glClear( GL_COLOR_BUFFER_BIT );

		glMatrixMode( GL_PROJECTION );
		glLoadMatrixf( clipMatrix );
		glMatrixMode( GL_MODELVIEW );

		backEnd.currentSpace = NULL;
		RB_ShadowMapDrawTranslucentCasterChain( primaryCasters, cascadeIndex );
		RB_ShadowMapDrawTranslucentCasterChain( secondaryCasters, cascadeIndex );
	}

	glUseProgramObjectARB( 0 );
	glClearColor( clearColor[0], clearColor[1], clearColor[2], clearColor[3] );

	if ( backEnd.renderTexture != NULL ) {
		backEnd.renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
		glDrawBuffer( GL_BACK );
		glReadBuffer( GL_BACK );
	}

	glViewport( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );
	glScissor( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	if ( depthWasEnabled ) {
		glEnable( GL_DEPTH_TEST );
	} else {
		glDisable( GL_DEPTH_TEST );
	}
	if ( scissorWasEnabled ) {
		glEnable( GL_SCISSOR_TEST );
	} else {
		glDisable( GL_SCISSOR_TEST );
	}
	if ( blendWasEnabled ) {
		glEnable( GL_BLEND );
	} else {
		glDisable( GL_BLEND );
	}
	// Restore the view-lifetime stencil-test enable (see RB_RenderShadowMap).
	if ( stencilWasEnabled ) {
		glEnable( GL_STENCIL_TEST );
	} else {
		glDisable( GL_STENCIL_TEST );
	}
	backEnd.glState.faceCulling = -1;
	if ( savedFaceCulling >= CT_FRONT_SIDED && savedFaceCulling <= CT_TWO_SIDED ) {
		GL_Cull( savedFaceCulling );
	}

	backEnd.currentSpace = NULL;
	backEnd.currentScissor.Clear();
	backEnd.glState.currenttmu = -1;
	GL_ClearStateDelta();
	GL_SelectTexture( 0 );

	g_projectedTranslucentShadowPassReady = true;
	return true;
}

static bool RB_RenderPointTranslucentShadowMap( const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters ) {
	g_pointTranslucentShadowPassReady = false;

	if ( !RB_PointShadowMapEnsureTranslucentResources() ) {
		return false;
	}

	const GLboolean blendWasEnabled = glIsEnabled( GL_BLEND );
	const GLboolean depthWasEnabled = glIsEnabled( GL_DEPTH_TEST );
	const GLboolean stencilWasEnabled = glIsEnabled( GL_STENCIL_TEST );
	const int savedFaceCulling = backEnd.glState.faceCulling;
	const float farClip = R_ShadowMapPointFarDistance( backEnd.vLight );
	// with depth clamp, casters inside the near plane still rasterize (their
	// color-packed radial depth stays exact), so the near plane only shapes the
	// ordering-depth distribution; without it, shrink the clipped-away zone
	const float nearClip = idMath::ClampFloat( 0.5f, RB_ShadowMapDepthClampAvailable() ? 16.0f : 4.0f, farClip * 0.01f );
	float projectionMatrix[16];
	RB_PointShadowMapBuildProjectionMatrix( nearClip, farClip, projectionMatrix );

	const float globalLightOrigin[4] = {
		backEnd.vLight->globalLightOrigin[0],
		backEnd.vLight->globalLightOrigin[1],
		backEnd.vLight->globalLightOrigin[2],
		1.0f
	};
	GLfloat clearColor[4];
	glGetFloatv( GL_COLOR_CLEAR_VALUE, clearColor );

	const int shadowMapWidth = g_pointTranslucentShadowMapRenderTexture->GetWidth();
	const int shadowMapHeight = g_pointTranslucentShadowMapRenderTexture->GetHeight();

	glUseProgramObjectARB( g_pointTranslucentShadowCasterProgram.programObject );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	if ( g_pointTranslucentShadowCasterProgram.globalLightOrigin >= 0 ) {
		glUniform4fvARB( g_pointTranslucentShadowCasterProgram.globalLightOrigin, 1, globalLightOrigin );
	}
	if ( g_pointTranslucentShadowCasterProgram.pointShadowFar >= 0 ) {
		glUniform1fARB( g_pointTranslucentShadowCasterProgram.pointShadowFar, farClip );
	}
	if ( g_pointTranslucentShadowCasterProgram.alphaMap >= 0 ) {
		glUniform1iARB( g_pointTranslucentShadowCasterProgram.alphaMap, 0 );
	}
	if ( g_pointTranslucentShadowCasterProgram.coverageMap >= 0 ) {
		glUniform1iARB( g_pointTranslucentShadowCasterProgram.coverageMap, 1 );
	}
	if ( g_pointTranslucentShadowCasterProgram.translucentMinAlpha >= 0 ) {
		glUniform1fARB( g_pointTranslucentShadowCasterProgram.translucentMinAlpha, r_shadowMapTranslucentMinAlpha.GetFloat() );
	}
	RB_PointTranslucentShadowMapSetAlphaTexCoordIdentity();
	RB_PointTranslucentShadowMapSetCoverageTexCoordIdentity();

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glDisable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ONE );
	glEnable( GL_CULL_FACE );
	glCullFace( GL_BACK );
	if ( RB_ShadowMapDepthClampAvailable() ) {
		glEnable( GL_DEPTH_CLAMP_NV );
	}
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	for ( int cubeFace = 0; cubeFace < 6; cubeFace++ ) {
		float lightModelViewMatrix[16];
		RB_PointShadowMapBuildModelViewMatrix( backEnd.vLight->globalLightOrigin, cubeFace, lightModelViewMatrix );

		g_pointTranslucentShadowMapRenderTexture->MakeCurrent( cubeFace );
		glViewport( 0, 0, shadowMapWidth, shadowMapHeight );
		glScissor( 0, 0, shadowMapWidth, shadowMapHeight );
		glClear( GL_COLOR_BUFFER_BIT );

		backEnd.currentSpace = NULL;
		RB_PointShadowMapDrawTranslucentCasterChain( primaryCasters, lightModelViewMatrix );
		RB_PointShadowMapDrawTranslucentCasterChain( secondaryCasters, lightModelViewMatrix );
	}

	if ( RB_ShadowMapDepthClampAvailable() ) {
		glDisable( GL_DEPTH_CLAMP_NV );
	}
	glUseProgramObjectARB( 0 );
	glClearColor( clearColor[0], clearColor[1], clearColor[2], clearColor[3] );

	if ( backEnd.renderTexture != NULL ) {
		backEnd.renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
		glDrawBuffer( GL_BACK );
		glReadBuffer( GL_BACK );
	}

	glViewport( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );
	glScissor( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	if ( depthWasEnabled ) {
		glEnable( GL_DEPTH_TEST );
	} else {
		glDisable( GL_DEPTH_TEST );
	}
	if ( blendWasEnabled ) {
		glEnable( GL_BLEND );
	} else {
		glDisable( GL_BLEND );
	}
	// Restore the view-lifetime stencil-test enable (see RB_RenderShadowMap).
	if ( stencilWasEnabled ) {
		glEnable( GL_STENCIL_TEST );
	} else {
		glDisable( GL_STENCIL_TEST );
	}
	backEnd.glState.faceCulling = -1;
	if ( savedFaceCulling >= CT_FRONT_SIDED && savedFaceCulling <= CT_TWO_SIDED ) {
		GL_Cull( savedFaceCulling );
	}

	backEnd.currentSpace = NULL;
	backEnd.currentScissor.Clear();
	backEnd.glState.currenttmu = -1;
	GL_ClearStateDelta();
	GL_SelectTexture( 0 );

	g_pointTranslucentShadowPassReady = true;
	return true;
}

// Value cache for the GLSL interaction uniforms sent per interaction draw.
// localLightOrigin/localViewOrigin/lightProjection are per-(light, space)
// constants and the stage matrices are identity for most materials, so the
// receiver loops re-send byte-identical values for nearly every draw; a
// 16-byte compare is far cheaper than the driver call (same rationale as
// g_arb2EnvParamCache above). Slots are logical uniforms, not GL locations:
// only one of the three GLSL interaction programs is ever bound within a
// receiver pass and the cache is invalidated at every pass entry, so values
// can never leak between programs, across lights, or across relinks.
enum {
	GLSL_INTERACTION_CACHE_LOCAL_LIGHT_ORIGIN = 0,
	GLSL_INTERACTION_CACHE_LOCAL_VIEW_ORIGIN,
	GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_S,
	GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_T,
	GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_Q,
	GLSL_INTERACTION_CACHE_LIGHT_FALLOFF_S,
	GLSL_INTERACTION_CACHE_BUMP_MATRIX_S,
	GLSL_INTERACTION_CACHE_BUMP_MATRIX_T,
	GLSL_INTERACTION_CACHE_DIFFUSE_MATRIX_S,
	GLSL_INTERACTION_CACHE_DIFFUSE_MATRIX_T,
	GLSL_INTERACTION_CACHE_SPECULAR_MATRIX_S,
	GLSL_INTERACTION_CACHE_SPECULAR_MATRIX_T,
	GLSL_INTERACTION_CACHE_DIFFUSE_COLOR,
	GLSL_INTERACTION_CACHE_SPECULAR_COLOR,
	GLSL_INTERACTION_CACHE_MODEL_MATRIX_ROW_0,
	GLSL_INTERACTION_CACHE_MODEL_MATRIX_ROW_1,
	GLSL_INTERACTION_CACHE_MODEL_MATRIX_ROW_2,
	GLSL_INTERACTION_CACHE_GLOBAL_LIGHT_ORIGIN,
	GLSL_INTERACTION_CACHE_VERTEX_COLOR_PARAMS,
	GLSL_INTERACTION_CACHE_SLOTS
};
typedef struct {
	float			values[GLSL_INTERACTION_CACHE_SLOTS][4];
	unsigned int	validMask;
} glslInteractionUniformCache_t;
static glslInteractionUniformCache_t g_glslInteractionUniformCache;

ID_INLINE static void RB_GLSLInteractionUniformCacheInvalidate( void ) {
	g_glslInteractionUniformCache.validMask = 0;
}

ID_INLINE static void RB_GLSLInteractionUniform4fv( const GLint location, const int slot, const float *v ) {
	if ( r_useRedundantStateFiltering.GetBool() ) {
		const unsigned int bit = 1u << slot;
		float *cached = g_glslInteractionUniformCache.values[slot];
		if ( ( g_glslInteractionUniformCache.validMask & bit )
			&& cached[0] == v[0] && cached[1] == v[1] && cached[2] == v[2] && cached[3] == v[3] ) {
			return;
		}
		cached[0] = v[0];
		cached[1] = v[1];
		cached[2] = v[2];
		cached[3] = v[3];
		g_glslInteractionUniformCache.validMask |= bit;
	}
	glUniform4fvARB( location, 1, v );
}

ID_INLINE static void RB_GLSLInteractionUniform2fv( const GLint location, const int slot, const float *v ) {
	if ( r_useRedundantStateFiltering.GetBool() ) {
		const unsigned int bit = 1u << slot;
		float *cached = g_glslInteractionUniformCache.values[slot];
		if ( ( g_glslInteractionUniformCache.validMask & bit )
			&& cached[0] == v[0] && cached[1] == v[1] ) {
			return;
		}
		cached[0] = v[0];
		cached[1] = v[1];
		g_glslInteractionUniformCache.validMask |= bit;
	}
	glUniform2fvARB( location, 1, v );
}

static bool RB_EnhancedMaterialShadingActive( void ) {
	return glConfig.GLSLProgramAvailable && r_enhancedMaterials.GetBool();
}

/*
==================
RB_SetCelInteractionUniform

Cel banding is decided per surface (world, model and view weapon all have their
own gate), so uCelParams is refreshed for every interaction rather than once per
program bind. Programs built before the uniform existed report -1 and are left
untouched.
==================
*/
static void RB_SetCelInteractionUniform( const GLint location, const drawSurf_t *surf ) {
	if ( location < 0 ) {
		return;
	}

	const bool banded = R_CelShadingSurfaceActive( surf );
	const float celParams[4] = {
		banded ? 1.0f : 0.0f,
		(float)R_CelBandCount(),
		( banded && r_celShadingSpecular.GetBool() ) ? 1.0f : 0.0f,
		banded ? R_CelBandSoftness() : 0.0f
	};

	glUniform4fvARB( location, 1, celParams );
}

static float RB_EnhancedMaterialNormalScaleValue( void ) {
	return RB_EnhancedMaterialShadingActive() ? idMath::ClampFloat( 0.5f, 2.0f, r_enhancedMaterialNormalScale.GetFloat() ) : 1.0f;
}

static float RB_EnhancedMaterialSpecularBoostValue( void ) {
	return RB_EnhancedMaterialShadingActive() ? Max( 0.0f, r_enhancedMaterialSpecularBoost.GetFloat() ) : 1.0f;
}

static float RB_EnhancedMaterialFresnelValue( void ) {
	return RB_EnhancedMaterialShadingActive() ? idMath::ClampFloat( 0.0f, 1.0f, r_enhancedMaterialFresnel.GetFloat() ) : 0.0f;
}

static float RB_EnhancedMaterialEnabledValue( void ) {
	return RB_EnhancedMaterialShadingActive() ? 1.0f : 0.0f;
}

static void RB_MaterialInteractionSetEnhancementUniforms( const GLint normalScaleLocation, const GLint specularBoostLocation, const GLint fresnelLocation, const GLint enhancedLocation = -1, const bool forceNeutralValues = false ) {
	const float enhancedValue = forceNeutralValues ? 0.0f : RB_EnhancedMaterialEnabledValue();
	const float normalScaleValue = forceNeutralValues ? 1.0f : RB_EnhancedMaterialNormalScaleValue();
	const float specularBoostValue = forceNeutralValues ? 1.0f : RB_EnhancedMaterialSpecularBoostValue();
	const float fresnelValue = forceNeutralValues ? 0.0f : RB_EnhancedMaterialFresnelValue();

	if ( enhancedLocation >= 0 ) {
		glUniform1fARB( enhancedLocation, enhancedValue );
	}
	if ( normalScaleLocation >= 0 ) {
		glUniform1fARB( normalScaleLocation, normalScaleValue );
	}
	if ( specularBoostLocation >= 0 ) {
		glUniform1fARB( specularBoostLocation, specularBoostValue );
	}
	if ( fresnelLocation >= 0 ) {
		glUniform1fARB( fresnelLocation, fresnelValue );
	}
}

static void RB_GLSLMaterial_DrawInteraction( const drawInteraction_t *din ) {
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.localLightOrigin, GLSL_INTERACTION_CACHE_LOCAL_LIGHT_ORIGIN, din->localLightOrigin.ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.localViewOrigin, GLSL_INTERACTION_CACHE_LOCAL_VIEW_ORIGIN, din->localViewOrigin.ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.lightProjectionS, GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_S, din->lightProjection[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.lightProjectionT, GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_T, din->lightProjection[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.lightProjectionQ, GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_Q, din->lightProjection[2].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.lightFalloffS, GLSL_INTERACTION_CACHE_LIGHT_FALLOFF_S, din->lightProjection[3].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.bumpMatrixS, GLSL_INTERACTION_CACHE_BUMP_MATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.bumpMatrixT, GLSL_INTERACTION_CACHE_BUMP_MATRIX_T, din->bumpMatrix[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.diffuseMatrixS, GLSL_INTERACTION_CACHE_DIFFUSE_MATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.diffuseMatrixT, GLSL_INTERACTION_CACHE_DIFFUSE_MATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.specularMatrixS, GLSL_INTERACTION_CACHE_SPECULAR_MATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.specularMatrixT, GLSL_INTERACTION_CACHE_SPECULAR_MATRIX_T, din->specularMatrix[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.diffuseColor, GLSL_INTERACTION_CACHE_DIFFUSE_COLOR, din->diffuseColor.ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_materialInteractionProgram.specularColor, GLSL_INTERACTION_CACHE_SPECULAR_COLOR, din->specularColor.ToFloatPtr() );
	if ( g_materialInteractionProgram.flatDiffuseParams >= 0 ) {
		glUniform4fvARB( g_materialInteractionProgram.flatDiffuseParams, 1, din->flatDiffuseParams.ToFloatPtr() );
	}
	glUniform1fARB( g_materialInteractionProgram.ambientLight, din->ambientLight ? 1.0f : 0.0f );

	float modulate = 0.0f;
	float add = 1.0f;
	switch ( din->vertexColor ) {
	case SVC_MODULATE:
		modulate = 1.0f;
		add = 0.0f;
		break;
	case SVC_INVERSE_MODULATE:
		modulate = -1.0f;
		add = 1.0f;
		break;
	case SVC_IGNORE:
	default:
		break;
	}
	const float vertexColorParams[2] = { modulate, add };
	RB_GLSLInteractionUniform2fv( g_materialInteractionProgram.vertexColorParams, GLSL_INTERACTION_CACHE_VERTEX_COLOR_PARAMS, vertexColorParams );

	GL_SelectTextureNoClient( 0 );
	din->bumpImage->Bind();
	GL_SelectTextureNoClient( 1 );
	din->lightFalloffImage->Bind();
	GL_SelectTextureNoClient( 2 );
	din->lightImage->Bind();
	GL_SelectTextureNoClient( 3 );
	din->diffuseImage->Bind();
	GL_SelectTextureNoClient( 4 );
	din->specularImage->Bind();
	GL_SelectTextureNoClient( 5 );
	globalImages->ambientNormalMap->Bind();

	const idMaterial *surfaceMaterial = din->surf->material;
	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
	}

	RB_DrawElementsWithCounters( din->surf->geo );

	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

static int g_shadowMapInteractionDrawCount = 0;
static int g_pointShadowMapInteractionDrawCount = 0;
static float g_shadowMapReceiverDebugReason = 0.0f;
static float g_pointShadowMapReceiverDebugReason = 0.0f;
// receiver space whose localized cascade rows are currently uploaded; reset at
// pass setup so rows are never reused across lights (clip planes are per light)
static const viewEntity_t *g_shadowMapInteractionLastSpace = NULL;

static bool RB_SurfaceUsesWrappedCustomGLSLShadowReceiver( const drawSurf_t *surf );

static float RB_ShadowMapReceiverDebugReasonValue( const drawSurf_t *surf, const shadowMapReceiverFallbackReason_t fallbackReason ) {
	if ( fallbackReason == SHADOWMAP_RECEIVER_FALLBACK_NONE ) {
		return RB_SurfaceUsesWrappedCustomGLSLShadowReceiver( surf ) ? 1.0f : 0.0f;
	}
	switch ( fallbackReason ) {
	case SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE:
		return 2.0f;
	case SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL:
		return 3.0f;
	case SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY:
		return 4.0f;
	default:
		return 2.0f;
	}
}

static void RB_GLSLShadowMap_DrawInteraction( const drawInteraction_t *din ) {
	g_shadowMapInteractionDrawCount++;

	if ( din->surf->space != g_shadowMapInteractionLastSpace ) {
		g_shadowMapInteractionLastSpace = din->surf->space;

		idPlane shadowClipLocal[SHADOWMAP_MAX_CASCADES][4];
		float shadowRow0[SHADOWMAP_MAX_CASCADES * 4];
		float shadowRow1[SHADOWMAP_MAX_CASCADES * 4];
		float shadowRow2[SHADOWMAP_MAX_CASCADES * 4];
		float shadowRow3[SHADOWMAP_MAX_CASCADES * 4];
		const int cascadeCount = idMath::ClampInt( 1, SHADOWMAP_MAX_CASCADES, g_projectedShadowMapState.cascadeCount );

		memset( shadowRow0, 0, sizeof( shadowRow0 ) );
		memset( shadowRow1, 0, sizeof( shadowRow1 ) );
		memset( shadowRow2, 0, sizeof( shadowRow2 ) );
		memset( shadowRow3, 0, sizeof( shadowRow3 ) );

		for ( int cascadeIndex = 0; cascadeIndex < cascadeCount; cascadeIndex++ ) {
			for ( int planeIndex = 0; planeIndex < 4; planeIndex++ ) {
				R_GlobalPlaneToLocal( din->surf->space->modelMatrix, g_projectedShadowMapState.clipPlanes[cascadeIndex][planeIndex], shadowClipLocal[cascadeIndex][planeIndex] );
			}

			memcpy( shadowRow0 + cascadeIndex * 4, shadowClipLocal[cascadeIndex][0].ToFloatPtr(), 4 * sizeof( float ) );
			memcpy( shadowRow1 + cascadeIndex * 4, shadowClipLocal[cascadeIndex][1].ToFloatPtr(), 4 * sizeof( float ) );
			memcpy( shadowRow2 + cascadeIndex * 4, shadowClipLocal[cascadeIndex][2].ToFloatPtr(), 4 * sizeof( float ) );
			memcpy( shadowRow3 + cascadeIndex * 4, shadowClipLocal[cascadeIndex][3].ToFloatPtr(), 4 * sizeof( float ) );
		}

		glUniform4fvARB( g_shadowMapProgram.shadowRow[0], cascadeCount, shadowRow0 );
		glUniform4fvARB( g_shadowMapProgram.shadowRow[1], cascadeCount, shadowRow1 );
		glUniform4fvARB( g_shadowMapProgram.shadowRow[2], cascadeCount, shadowRow2 );
		glUniform4fvARB( g_shadowMapProgram.shadowRow[3], cascadeCount, shadowRow3 );
	}

	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.localLightOrigin, GLSL_INTERACTION_CACHE_LOCAL_LIGHT_ORIGIN, din->localLightOrigin.ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.localViewOrigin, GLSL_INTERACTION_CACHE_LOCAL_VIEW_ORIGIN, din->localViewOrigin.ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.lightProjectionS, GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_S, din->lightProjection[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.lightProjectionT, GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_T, din->lightProjection[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.lightProjectionQ, GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_Q, din->lightProjection[2].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.lightFalloffS, GLSL_INTERACTION_CACHE_LIGHT_FALLOFF_S, din->lightProjection[3].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.bumpMatrixS, GLSL_INTERACTION_CACHE_BUMP_MATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.bumpMatrixT, GLSL_INTERACTION_CACHE_BUMP_MATRIX_T, din->bumpMatrix[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.diffuseMatrixS, GLSL_INTERACTION_CACHE_DIFFUSE_MATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.diffuseMatrixT, GLSL_INTERACTION_CACHE_DIFFUSE_MATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.specularMatrixS, GLSL_INTERACTION_CACHE_SPECULAR_MATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.specularMatrixT, GLSL_INTERACTION_CACHE_SPECULAR_MATRIX_T, din->specularMatrix[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.diffuseColor, GLSL_INTERACTION_CACHE_DIFFUSE_COLOR, din->diffuseColor.ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_shadowMapProgram.specularColor, GLSL_INTERACTION_CACHE_SPECULAR_COLOR, din->specularColor.ToFloatPtr() );
	if ( g_shadowMapProgram.flatDiffuseParams >= 0 ) {
		glUniform4fvARB( g_shadowMapProgram.flatDiffuseParams, 1, din->flatDiffuseParams.ToFloatPtr() );
	}

	float modulate = 0.0f;
	float add = 1.0f;
	switch ( din->vertexColor ) {
	case SVC_MODULATE:
		modulate = 1.0f;
		add = 0.0f;
		break;
	case SVC_INVERSE_MODULATE:
		modulate = -1.0f;
		add = 1.0f;
		break;
	case SVC_IGNORE:
	default:
		break;
	}
	const float vertexColorParams[2] = { modulate, add };
	RB_GLSLInteractionUniform2fv( g_shadowMapProgram.vertexColorParams, GLSL_INTERACTION_CACHE_VERTEX_COLOR_PARAMS, vertexColorParams );
	if ( g_shadowMapProgram.shadowReceiverDebugReason >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowReceiverDebugReason, g_shadowMapReceiverDebugReason );
	}

	GL_SelectTextureNoClient( 0 );
	din->bumpImage->Bind();
	GL_SelectTextureNoClient( 1 );
	din->lightFalloffImage->Bind();
	GL_SelectTextureNoClient( 2 );
	din->lightImage->Bind();
	GL_SelectTextureNoClient( 3 );
	din->diffuseImage->Bind();
	GL_SelectTextureNoClient( 4 );
	din->specularImage->Bind();

	const idMaterial *surfaceMaterial = din->surf->material;
	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
	}

	RB_DrawElementsWithCounters( din->surf->geo );

	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

static void RB_GLSLPointShadowMap_DrawInteraction( const drawInteraction_t *din ) {
	g_pointShadowMapInteractionDrawCount++;

	const float globalLightOrigin[4] = {
		backEnd.vLight->globalLightOrigin[0],
		backEnd.vLight->globalLightOrigin[1],
		backEnd.vLight->globalLightOrigin[2],
		1.0f
	};
	float row0[4];
	float row1[4];
	float row2[4];

	RB_ShadowMapModelMatrixRows( din->surf->space->modelMatrix, row0, row1, row2 );

	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.localLightOrigin, GLSL_INTERACTION_CACHE_LOCAL_LIGHT_ORIGIN, din->localLightOrigin.ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.localViewOrigin, GLSL_INTERACTION_CACHE_LOCAL_VIEW_ORIGIN, din->localViewOrigin.ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.lightProjectionS, GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_S, din->lightProjection[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.lightProjectionT, GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_T, din->lightProjection[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.lightProjectionQ, GLSL_INTERACTION_CACHE_LIGHT_PROJECTION_Q, din->lightProjection[2].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.lightFalloffS, GLSL_INTERACTION_CACHE_LIGHT_FALLOFF_S, din->lightProjection[3].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.bumpMatrixS, GLSL_INTERACTION_CACHE_BUMP_MATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.bumpMatrixT, GLSL_INTERACTION_CACHE_BUMP_MATRIX_T, din->bumpMatrix[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.diffuseMatrixS, GLSL_INTERACTION_CACHE_DIFFUSE_MATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.diffuseMatrixT, GLSL_INTERACTION_CACHE_DIFFUSE_MATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.specularMatrixS, GLSL_INTERACTION_CACHE_SPECULAR_MATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.specularMatrixT, GLSL_INTERACTION_CACHE_SPECULAR_MATRIX_T, din->specularMatrix[1].ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.modelMatrixRow0, GLSL_INTERACTION_CACHE_MODEL_MATRIX_ROW_0, row0 );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.modelMatrixRow1, GLSL_INTERACTION_CACHE_MODEL_MATRIX_ROW_1, row1 );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.modelMatrixRow2, GLSL_INTERACTION_CACHE_MODEL_MATRIX_ROW_2, row2 );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.globalLightOrigin, GLSL_INTERACTION_CACHE_GLOBAL_LIGHT_ORIGIN, globalLightOrigin );
	// pointShadowFar is uploaded once at program-bind time in
	// RB_GLSLPointShadowMap_CreateDrawInteractions and is constant across the
	// whole receiver chain (backEnd.vLight does not change), so the per-draw
	// re-upload here (and its sqrt in R_ShadowMapPointFarDistance) was redundant.
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.diffuseColor, GLSL_INTERACTION_CACHE_DIFFUSE_COLOR, din->diffuseColor.ToFloatPtr() );
	RB_GLSLInteractionUniform4fv( g_pointShadowMapProgram.specularColor, GLSL_INTERACTION_CACHE_SPECULAR_COLOR, din->specularColor.ToFloatPtr() );
	if ( g_pointShadowMapProgram.flatDiffuseParams >= 0 ) {
		glUniform4fvARB( g_pointShadowMapProgram.flatDiffuseParams, 1, din->flatDiffuseParams.ToFloatPtr() );
	}

	float modulate = 0.0f;
	float add = 1.0f;
	switch ( din->vertexColor ) {
	case SVC_MODULATE:
		modulate = 1.0f;
		add = 0.0f;
		break;
	case SVC_INVERSE_MODULATE:
		modulate = -1.0f;
		add = 1.0f;
		break;
	case SVC_IGNORE:
	default:
		break;
	}
	const float vertexColorParams[2] = { modulate, add };
	RB_GLSLInteractionUniform2fv( g_pointShadowMapProgram.vertexColorParams, GLSL_INTERACTION_CACHE_VERTEX_COLOR_PARAMS, vertexColorParams );
	if ( g_pointShadowMapProgram.shadowReceiverDebugReason >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.shadowReceiverDebugReason, g_pointShadowMapReceiverDebugReason );
	}

	GL_SelectTextureNoClient( 0 );
	din->bumpImage->Bind();
	GL_SelectTextureNoClient( 1 );
	din->lightFalloffImage->Bind();
	GL_SelectTextureNoClient( 2 );
	din->lightImage->Bind();
	GL_SelectTextureNoClient( 3 );
	din->diffuseImage->Bind();
	GL_SelectTextureNoClient( 4 );
	din->specularImage->Bind();

	const idMaterial *surfaceMaterial = din->surf->material;
	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
	}

	RB_DrawElementsWithCounters( din->surf->geo );

	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

static bool RB_GLSLPrepareInteractionVertexCache( const drawSurf_t *surf, idDrawVert *&ambientVertexPointer ) {
	ambientVertexPointer = NULL;

	if ( surf == NULL || surf->geo == NULL ) {
		return false;
	}

	const srfTriangles_t *tri = surf->geo;
	srfTriangles_t *mutableTri = const_cast<srfTriangles_t *>( tri );
	srfTriangles_t *ambientTri = ( tri->ambientSurface != NULL ) ? tri->ambientSurface : mutableTri;
	const bool needsLighting = ( surf->material != NULL ) ? surf->material->ReceivesLighting() : false;

	if ( mutableTri->ambientCache == NULL ) {
		if ( ambientTri == NULL ) {
			return false;
		}
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		if ( ambientTri->ambientCache == NULL && ambientTri->primBatchMesh != NULL ) {
			if ( !R_CreatePackedSurfaceFrameCaches( ambientTri, needsLighting, false ) ) {
				return false;
			}
		}
#endif
		if ( ambientTri->ambientCache == NULL && ambientTri->verts != NULL ) {
			if ( !R_CreateAmbientCache( ambientTri, needsLighting ) ) {
				return false;
			}
		}

		if ( ambientTri->ambientCache != NULL ) {
			mutableTri->ambientCache = ambientTri->ambientCache;
			mutableTri->tempAmbientCache = ambientTri->tempAmbientCache;
		}
	}

	if ( mutableTri->ambientCache == NULL ) {
		return false;
	}

	// Do NOT frame-temp-allocate an indexCache here when the frontend left it
	// NULL: surf->geo can be a heap-owned persistent tri (interaction lightTris,
	// DM_CACHED models at rest), and AllocFrameTemp headers are recycled at
	// EndFrame -- a stored handle dereferences a stale/freed header next frame
	// (see tr_trisurf.cpp). The frontend index policy (R_StaticIndexCacheAllowed)
	// already decides VBO vs client-memory indexes; NULL means client memory.

	R_TouchVertexCache( mutableTri->ambientCache );
	if ( mutableTri->indexCache != NULL ) {
		R_TouchVertexCache( mutableTri->indexCache );
	}

	ambientVertexPointer = (idDrawVert *)vertexCache.Position( mutableTri->ambientCache );
	return true;
}

// GPU-posed geometry is geometry whose vertex cache holds bind-pose positions
// that a vertex program moves into the final pose, so gl_Vertex is not where
// the surface is actually drawn. Only the MD5R primitive-batch path does that:
// it uploads rest-pose vertices plus a skin-to-model transform table and poses
// them in md5rinteraction.vp.
//
// srfTriangles_t::deformedSurface is NOT that flag. Model.h defines it as
// "indexes, silIndexes, mirrorVerts and silEdges are pointers into the original
// surface and should not be freed", and idMD5Mesh::UpdateSurface sets it on
// every CPU-skinned MD5 surface -- which is every character and the viewmodel.
// Reading it as "GPU-posed" dropped all of them onto diffuse-only
// SimpleInteraction.vfp and onto hard stencil shadow receivers, which is the
// flat, bump-less, shadow-less character lighting reported in issue #73. Their
// ambient caches already hold final-pose model-space vertices that the
// interaction and receiver programs sample exactly like static geometry.
static bool RB_SurfaceUsesGPUPosedGeometry( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->geo == NULL ) {
		return true;
	}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	const srfTriangles_t *tri = surf->geo;
	const srfTriangles_t *ambientTri = ( tri->ambientSurface != NULL ) ? tri->ambientSurface : tri;

	if ( tri->primBatchMesh != NULL || ambientTri->primBatchMesh != NULL ) {
		return true;
	}
	if ( tri->skinToModelTransforms != NULL || tri->numSkinToModelTransforms > 0
		|| ambientTri->skinToModelTransforms != NULL || ambientTri->numSkinToModelTransforms > 0 ) {
		return true;
	}
#endif

	return false;
}

static bool RB_SurfaceEligibleForStockGLSLInteraction( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->geo == NULL || surf->space == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return false;
	}
	if ( surf->geo->numIndexes <= 0 || surf->geo->ambientCache == NULL ) {
		return false;
	}
	if ( RB_SurfaceUsesGPUPosedGeometry( surf ) ) {
		return false;
	}

	return true;
}

static bool RB_DrawSurfChainEligibleForStockGLSLInteractions( const drawSurf_t *surf ) {
	bool sawSurface = false;
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		sawSurface = true;
		if ( !RB_SurfaceEligibleForStockGLSLInteraction( surf ) ) {
			return false;
		}
	}
	return sawSurface;
}

static bool RB_SurfaceHasActiveCustomGLSLLighting( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return false;
	}

	return surf->material->HasActiveCustomGLSLLighting( surf->shaderRegisters );
}

static bool RB_DrawSurfChainHasCustomGLSLLighting( const drawSurf_t *surf ) {
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		if ( RB_SurfaceHasActiveCustomGLSLLighting( surf ) ) {
			return true;
		}
	}
	return false;
}

static bool RB_AppleGL21AutomaticInteractionPath( void ) {
	return g_appleGL21AutomaticInteractionPath && !glConfig.disableARB2Interactions;
}

// "family=simple" in the startup log only describes which ARB pair is armed as
// the per-surface fallback; it says nothing about how many surfaces actually
// take the GLSL corridor. Without that number a reporter screenshot of flat
// lighting cannot be told apart from a corridor that never ran (issue #73).
typedef struct {
	int		glslCorridor;
	int		simpleFallback;
	int		fallbackCustomGLSL;
	int		fallbackGPUPosed;
	int		fallbackUnprepared;
} appleGL21RouteCounts_t;

static appleGL21RouteCounts_t g_appleGL21RouteCounts = { 0, 0, 0, 0, 0 };
static bool g_appleGL21RouteSummaryPrinted = false;

static void RB_RecordAppleGL21FallbackReason( const drawSurf_t *surf ) {
	if ( RB_SurfaceHasActiveCustomGLSLLighting( surf ) ) {
		g_appleGL21RouteCounts.fallbackCustomGLSL++;
	} else if ( RB_SurfaceUsesGPUPosedGeometry( surf ) ) {
		g_appleGL21RouteCounts.fallbackGPUPosed++;
	} else {
		g_appleGL21RouteCounts.fallbackUnprepared++;
	}
}

void RB_ResetAppleGL21RouteCounters( void ) {
	memset( &g_appleGL21RouteCounts, 0, sizeof( g_appleGL21RouteCounts ) );
	g_appleGL21RouteSummaryPrinted = false;
}

void RB_ReportAppleGL21RouteCounters( void ) {
	const int total = g_appleGL21RouteCounts.glslCorridor + g_appleGL21RouteCounts.simpleFallback;
	if ( total <= 0 || g_appleGL21RouteSummaryPrinted ) {
		return;
	}
	g_appleGL21RouteSummaryPrinted = true;
	common->Printf(
		"Apple GL 2.1 interaction routes: glsl=%d simpleARB=%d (customGLSL=%d gpuPosed=%d unprepared=%d)\n",
		g_appleGL21RouteCounts.glslCorridor,
		g_appleGL21RouteCounts.simpleFallback,
		g_appleGL21RouteCounts.fallbackCustomGLSL,
		g_appleGL21RouteCounts.fallbackGPUPosed,
		g_appleGL21RouteCounts.fallbackUnprepared );
}

static bool RB_SurfaceEligibleForAppleGL21StockGLSLInteraction( const drawSurf_t *surf ) {
	return RB_SurfaceEligibleForStockGLSLInteraction( surf ) &&
		!RB_SurfaceHasActiveCustomGLSLLighting( surf );
}

static bool RB_SurfaceUsesWrappedCustomGLSLShadowReceiver( const drawSurf_t *surf ) {
	if ( !r_shadowMapCustomGLSLReceiverWrapper.GetBool() ) {
		return false;
	}
	if ( surf == NULL || surf->geo == NULL || surf->space == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return false;
	}
	if ( surf->geo->numIndexes <= 0 || RB_SurfaceUsesGPUPosedGeometry( surf ) ) {
		return false;
	}

	return surf->material->CanUseStockShadowMapReceiverForCustomGLSLLighting( surf->shaderRegisters );
}

static bool RB_ShadowMapReceiverStageFilter( const shaderStage_t *surfaceStage, const float *surfaceRegs ) {
	(void)surfaceRegs;
	if ( surfaceStage == NULL ) {
		return false;
	}

	const newShaderStage_t *newStage = surfaceStage->newStage;
	return newStage == NULL || !newStage->customLighting || !newStage->glslProgram;
}

typedef bool (*drawSurfFilter_t)( const drawSurf_t *surf );

// Receivers that passed eligibility but whose vertex caches could not be
// prepared are skipped by the mask pass. Tracking their count lets the run
// pass stencil-fallback ONLY those surfaces instead of failing the whole
// mask, which re-drew every already-lit interaction additively (visible
// double-lighting whenever a single surface prepare failed).
static int g_shadowMapMaskPrepareFailures = 0;

static shadowMapReceiverFallbackReason_t RB_SurfaceShadowMapReceiverFallbackReason( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->geo == NULL || surf->space == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE;
	}
	if ( surf->geo->numIndexes <= 0 ) {
		return SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE;
	}
	if ( RB_SurfaceHasActiveCustomGLSLLighting( surf ) && !RB_SurfaceUsesWrappedCustomGLSLShadowReceiver( surf ) ) {
		return SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL;
	}
	if ( RB_SurfaceUsesGPUPosedGeometry( surf ) ) {
		return SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY;
	}

	return SHADOWMAP_RECEIVER_FALLBACK_NONE;
}

static bool RB_SurfaceEligibleForShadowMapReceiver( const drawSurf_t *surf ) {
	return RB_SurfaceShadowMapReceiverFallbackReason( surf ) == SHADOWMAP_RECEIVER_FALLBACK_NONE;
}

static bool RB_ShadowMapCasterCompletenessSelfTest( void ) {
	const projectedShadowMapState_t savedState = g_projectedShadowMapState;
	viewLight_t *savedLight = backEnd.vLight;
	viewLight_t light;
	viewEntity_t space;
	srfTriangles_t geo;
	drawSurf_t culled, missing;
	vertCache_t cache;
	glIndex_t indexes[3] = { 0, 0, 0 };
	memset( &light, 0, sizeof( light ) );
	memset( &space, 0, sizeof( space ) );
	memset( &geo, 0, sizeof( geo ) );
	memset( &culled, 0, sizeof( culled ) );
	memset( &missing, 0, sizeof( missing ) );
	memset( &cache, 0, sizeof( cache ) );
	space.modelMatrix[0] = space.modelMatrix[5] = space.modelMatrix[10] = space.modelMatrix[15] = 1.0f;
	geo.numVerts = 1;
	geo.numIndexes = 3;
	geo.indexes = indexes;
	geo.ambientCache = &cache;
	geo.bounds = idBounds( idVec3( 9, 99, -1 ), idVec3( 11, 101, 1 ) );
	culled.geo = &geo;
	culled.space = &space;
	backEnd.vLight = &light;
	g_projectedShadowMapState.cascadeCount = 2;
	g_projectedShadowMapState.clipPlanes[0][0] = idPlane( 1, 0, 0, 0 );
	g_projectedShadowMapState.clipPlanes[0][1] = idPlane( 0, 1, 0, 0 );
	g_projectedShadowMapState.clipPlanes[0][3] = idPlane( 0, 0, 0, 1 );
	// Entirely culled geometry never touches this inert cache or the GPU.
	bool ok = RB_ShadowMapDrawCasterChain( &culled, 0, false )
		&& RB_PointShadowMapDrawCasterChain( &culled, space.modelMatrix, 0 );
	culled.nextOnLight = &missing;
	ok = ok && !RB_ShadowMapDrawCasterChain( &culled, 0, false )
		&& !RB_PointShadowMapDrawCasterChain( &culled, space.modelMatrix, 0 );
	culled.nextOnLight = NULL;
	missing.nextOnLight = &culled;
	ok = ok && !RB_ShadowMapDrawCasterChain( &missing, 0, false )
		&& !RB_PointShadowMapDrawCasterChain( &missing, space.modelMatrix, 0 );
	geo.bounds = idBounds( idVec3( -1, -1, -1 ), idVec3( 1, 1, 1 ) );
	space.modelMatrix[12] = 15.0f;
	space.modelMatrix[13] = 20.0f;
	ok = ok && RB_PointShadowMapCasterOutsideFace( &culled, &geo, 0 );
	space.modelMatrix[0] = -10.0f;
	space.modelMatrix[5] = space.modelMatrix[10] = 10.0f;
	ok = ok && !RB_PointShadowMapCasterOutsideFace( &culled, &geo, 0 );
	g_projectedShadowMapState = savedState;
	backEnd.vLight = savedLight;
	common->Printf( "Shadow caster completeness/scaled-bounds self-test %s\n", ok ? "passed" : "FAILED" );
	return ok;
}

static bool RB_SurfaceNeedsShadowMapReceiverFallback( const drawSurf_t *surf ) {
	return RB_SurfaceShadowMapReceiverFallbackReason( surf ) != SHADOWMAP_RECEIVER_FALLBACK_NONE;
}

// Filter for the partial mask-failure fallback: eligible receivers whose
// vertex caches could not be prepared this frame (the prepare call is
// idempotent, so re-evaluating it here selects exactly the surfaces the
// mask pass skipped).
static bool RB_SurfaceShadowMapReceiverPrepareFailed( const drawSurf_t *surf ) {
	if ( RB_SurfaceShadowMapReceiverFallbackReason( surf ) != SHADOWMAP_RECEIVER_FALLBACK_NONE ) {
		return false;	// handled by the receiver-fallback pass
	}
	idDrawVert *ambientVertexPointer = NULL;
	return !RB_GLSLPrepareInteractionVertexCache( surf, ambientVertexPointer );
}

static bool RB_DrawSurfChainHasFilteredSurface( const drawSurf_t *surf, drawSurfFilter_t filter ) {
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		if ( filter == NULL || filter( surf ) ) {
			return true;
		}
	}
	return false;
}

static int RB_CountShadowMapReceiverFallbackSurfaces( const drawSurf_t *surf, int *reasons ) {
	if ( reasons != NULL ) {
		for ( int i = 0; i < SHADOWMAP_RECEIVER_FALLBACK_COUNT; i++ ) {
			reasons[i] = 0;
		}
	}

	int count = 0;
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		const shadowMapReceiverFallbackReason_t reason = RB_SurfaceShadowMapReceiverFallbackReason( surf );
		if ( reason == SHADOWMAP_RECEIVER_FALLBACK_NONE ) {
			continue;
		}
		count++;
		if ( reasons != NULL && reason >= 0 && reason < SHADOWMAP_RECEIVER_FALLBACK_COUNT ) {
			reasons[reason]++;
		}
	}
	return count;
}

static int RB_CountShadowMapReceiverWrappedCustomGLSLSurfaces( const drawSurf_t *surf ) {
	int count = 0;
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		if ( RB_SurfaceUsesWrappedCustomGLSLShadowReceiver( surf ) ) {
			count++;
		}
	}
	return count;
}

bool RB_ShadowMapArb2ReceiverFallbackSelfTest( void ) {
	if ( !RB_ShadowMapCasterCompletenessSelfTest() ) {
		return false;
	}
	if ( tr.defaultMaterial == NULL ) {
		common->Printf( "ARB2 receiver fallback self-test passed (default material unavailable)\n" );
		return true;
	}

	float shaderRegisters[MAX_EXPRESSION_REGISTERS];
	for ( int i = 0; i < MAX_EXPRESSION_REGISTERS; i++ ) {
		shaderRegisters[i] = 1.0f;
	}

	if ( !R_MaterialCustomGLSLReceiverHelperSelfTest() ) {
		common->Printf( "ARB2 receiver fallback self-test failed: custom GLSL material helper mismatch\n" );
		return false;
	}

	newShaderStage_t customLightingStage;
	memset( &customLightingStage, 0, sizeof( customLightingStage ) );
	customLightingStage.glslProgram = true;
	customLightingStage.customLighting = true;
	shaderStage_t stockFilterStage;
	shaderStage_t customFilterStage;
	memset( &stockFilterStage, 0, sizeof( stockFilterStage ) );
	memset( &customFilterStage, 0, sizeof( customFilterStage ) );
	customFilterStage.newStage = &customLightingStage;
	if ( !RB_ShadowMapReceiverStageFilter( &stockFilterStage, shaderRegisters )
		|| RB_ShadowMapReceiverStageFilter( &customFilterStage, shaderRegisters ) ) {
		common->Printf( "ARB2 receiver fallback self-test failed: custom GLSL receiver stage filter mismatch\n" );
		return false;
	}

	srfTriangles_t receiverGeo;
	memset( &receiverGeo, 0, sizeof( receiverGeo ) );
	receiverGeo.numIndexes = 3;

	// A CPU-skinned MD5 surface -- every character and the viewmodel -- is
	// exactly this shape: deformedSurface set by idMD5Mesh::UpdateSurface,
	// no primitive batch, no skin-to-model transform table. It is a first
	// class mapped receiver, because its ambient cache already holds
	// final-pose model-space vertices.
	srfTriangles_t cpuSkinnedGeo;
	memset( &cpuSkinnedGeo, 0, sizeof( cpuSkinnedGeo ) );
	cpuSkinnedGeo.numIndexes = 3;
	cpuSkinnedGeo.deformedSurface = true;

	// GPU-posed MD5R geometry is the class that genuinely cannot use the
	// mapped receiver program: its cached vertices are the bind pose and
	// md5rinteraction.vp moves them.
	float gpuPosedSkinTransform[16];
	memset( gpuPosedSkinTransform, 0, sizeof( gpuPosedSkinTransform ) );
	srfTriangles_t gpuPosedGeo;
	memset( &gpuPosedGeo, 0, sizeof( gpuPosedGeo ) );
	gpuPosedGeo.numIndexes = 3;
	gpuPosedGeo.deformedSurface = true;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	gpuPosedGeo.skinToModelTransforms = gpuPosedSkinTransform;
	gpuPosedGeo.numSkinToModelTransforms = 1;
#endif

	viewEntity_t staticSpace;
	viewEntity_t generatedSpace;
	memset( &staticSpace, 0, sizeof( staticSpace ) );
	memset( &generatedSpace, 0, sizeof( generatedSpace ) );

	idJointMat generatedJoint;
	generatedJoint.SetRotation( mat3_identity );
	generatedJoint.SetTranslation( vec3_origin );
	idRenderEntityLocal generatedEntity;
	generatedEntity.parms.numJoints = 1;
	generatedEntity.parms.joints = &generatedJoint;
	generatedSpace.entityDef = &generatedEntity;

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	const bool gpuPosingSupported = true;
#else
	const bool gpuPosingSupported = false;
	(void)gpuPosedSkinTransform;
#endif

	drawSurf_t eligibleReceiver;
	drawSurf_t skinnedReceiver;
	drawSurf_t generatedReceiver;
	drawSurf_t invalidReceiver;
	memset( &eligibleReceiver, 0, sizeof( eligibleReceiver ) );
	memset( &skinnedReceiver, 0, sizeof( skinnedReceiver ) );
	memset( &generatedReceiver, 0, sizeof( generatedReceiver ) );
	memset( &invalidReceiver, 0, sizeof( invalidReceiver ) );
	eligibleReceiver.geo = &receiverGeo;
	eligibleReceiver.space = &staticSpace;
	eligibleReceiver.material = tr.defaultMaterial;
	eligibleReceiver.shaderRegisters = shaderRegisters;
	eligibleReceiver.nextOnLight = &skinnedReceiver;
	// pins the narrowed contract against the shape the engine really
	// produces: a CPU-skinned MD5 surface carries deformedSurface and is
	// still a first class mapped receiver, not a hard-stencil fallback
	skinnedReceiver.geo = &cpuSkinnedGeo;
	skinnedReceiver.space = &generatedSpace;
	skinnedReceiver.material = tr.defaultMaterial;
	skinnedReceiver.shaderRegisters = shaderRegisters;
	skinnedReceiver.nextOnLight = &generatedReceiver;
	generatedReceiver.geo = &gpuPosedGeo;
	generatedReceiver.space = &generatedSpace;
	generatedReceiver.material = tr.defaultMaterial;
	generatedReceiver.shaderRegisters = shaderRegisters;
	generatedReceiver.nextOnLight = &invalidReceiver;

	const shadowMapReceiverFallbackReason_t expectedGeneratedReason =
		gpuPosingSupported ? SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY : SHADOWMAP_RECEIVER_FALLBACK_NONE;
	const int expectedGeneratedCount = gpuPosingSupported ? 1 : 0;

	int fallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_COUNT];
	const int fallbackSurfaceCount = RB_CountShadowMapReceiverFallbackSurfaces( &eligibleReceiver, fallbackReasons );
	if ( !RB_SurfaceEligibleForShadowMapReceiver( &eligibleReceiver )
		|| !RB_SurfaceEligibleForShadowMapReceiver( &skinnedReceiver )
		|| RB_SurfaceShadowMapReceiverFallbackReason( &generatedReceiver ) != expectedGeneratedReason
		|| fallbackSurfaceCount != 1 + expectedGeneratedCount
		|| fallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY] != expectedGeneratedCount
		|| fallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE] != 1
		|| fallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL] != 0 ) {
		common->Printf(
			"ARB2 receiver fallback self-test failed: eligible=%d skinnedEligible=%d generatedReason=%s count=%d invalid=%d custom=%d generated=%d\n",
			RB_SurfaceEligibleForShadowMapReceiver( &eligibleReceiver ) ? 1 : 0,
			RB_SurfaceEligibleForShadowMapReceiver( &skinnedReceiver ) ? 1 : 0,
			RB_ShadowMapReceiverFallbackReasonName( RB_SurfaceShadowMapReceiverFallbackReason( &generatedReceiver ) ),
			fallbackSurfaceCount,
			fallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE],
			fallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL],
			fallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY] );
		return false;
	}

	const idMaterial *projectedLightShader = declManager != NULL ? declManager->FindMaterial( "lights/defaultProjectedLight" ) : NULL;
	if ( projectedLightShader == NULL ) {
		projectedLightShader = tr.defaultMaterial;
	}

	drawSurf_t casterSurf;
	memset( &casterSurf, 0, sizeof( casterSurf ) );
	casterSurf.geo = &receiverGeo;
	casterSurf.space = &staticSpace;
	casterSurf.material = tr.defaultMaterial;
	casterSurf.shaderRegisters = shaderRegisters;

	viewLight_t light;
	idRenderLightLocal lightDef;
	viewDef_t view;
	memset( &light, 0, sizeof( light ) );
	memset( &view, 0, sizeof( view ) );
	lightDef.index = 991;
	lightDef.lightShader = projectedLightShader;
	lightDef.parms.noShadows = false;
	lightDef.parms.noDynamicShadows = false;
	light.lightDef = &lightDef;
	light.lightShader = projectedLightShader;
	light.globalInteractions = &eligibleReceiver;
	light.globalShadowMapCasters = &casterSurf;
	light.globalShadows = &casterSurf;

	const shadowMapLightSupportReason_t supportReason = RB_ShadowMapLightPolicySupportReason( &light );
	if ( supportReason != SHADOWMAP_SUPPORT_OK ) {
		common->Printf( "ARB2 receiver fallback self-test passed (cache estimate skipped: light support=%s)\n", RB_ShadowMapSupportReasonName( supportReason ) );
		return true;
	}

	shadowMapArb2CacheEstimate_t estimate;
	if ( !RB_ShadowMapEstimateArb2CacheOwnership( &light, &view, estimate ) || estimate.shadowPasses != 1 || estimate.receiverFallbackPasses != 1 || estimate.freshUpdatePasses != 1 ) {
		common->Printf(
			"ARB2 receiver fallback self-test failed: mixed estimate valid=%d passes=%d receiverFallback=%d fresh=%d stencilOnly=%d unshadowed=%d\n",
			estimate.valid ? 1 : 0,
			estimate.shadowPasses,
			estimate.receiverFallbackPasses,
			estimate.freshUpdatePasses,
			estimate.stencilOnlyPasses,
			estimate.unshadowedPasses );
		return false;
	}

	generatedReceiver.nextOnLight = NULL;
	light.globalInteractions = &generatedReceiver;
	if ( !RB_ShadowMapEstimateArb2CacheOwnership( &light, &view, estimate ) || estimate.shadowPasses != 1 || estimate.receiverFallbackPasses != 1 || estimate.freshUpdatePasses != 0 ) {
		common->Printf(
			"ARB2 receiver fallback self-test failed: all-fallback estimate valid=%d passes=%d receiverFallback=%d fresh=%d stencilOnly=%d unshadowed=%d\n",
			estimate.valid ? 1 : 0,
			estimate.shadowPasses,
			estimate.receiverFallbackPasses,
			estimate.freshUpdatePasses,
			estimate.stencilOnlyPasses,
			estimate.unshadowedPasses );
		return false;
	}

	common->Printf( "ARB2 receiver fallback self-test passed\n" );
	return true;
}

static unsigned int RB_ShadowMapArb2CachePassMask( const shadowMapPassKind_t passKind ) {
	return ( passKind == SHADOWMAP_PASS_LOCAL ) ? SHADOWMAP_ARB2_CACHE_PASS_LOCAL : SHADOWMAP_ARB2_CACHE_PASS_GLOBAL;
}

static bool RB_ShadowMapHybridAvailableForPass( const viewLight_t *vLight,
	const shadowMapPassKind_t passKind );
static bool RB_ShadowMapMapOrHybridAvailableForPass(
	const viewLight_t *vLight, const shadowMapPassKind_t passKind,
	bool *hybridOut );

static const drawSurf_t *RB_ShadowMapGlobalReceiverPlanningChain(
		const viewLight_t *vLight, bool &deferReceiverDraw ) {
	deferReceiverDraw = false;
	if ( vLight == NULL ) {
		return NULL;
	}
	const bool opaqueEligible = RB_DrawSurfChainHasFilteredSurface(
		vLight->globalInteractions, RB_SurfaceEligibleForShadowMapReceiver );
	const bool translucentEligible =
		RB_ShadowMapTranslucentGlobalReceiverNeeded( vLight )
		&& RB_DrawSurfChainHasFilteredSurface( vLight->translucentInteractions,
			RB_SurfaceEligibleForShadowMapReceiver );
	if ( !opaqueEligible && translucentEligible ) {
		deferReceiverDraw = true;
		return vLight->translucentInteractions;
	}
	return vLight->globalInteractions;
}

static void RB_ShadowMapArb2CacheSlotCountsReadOnly( shadowMapArb2CacheEstimate_t &estimate ) {
	estimate.projectedCacheSlotsTotal = RB_ShadowMapProjectedCacheSlotLimit();
	estimate.pointCacheSlotsTotal = RB_ShadowMapPointCacheSlotLimit();
	for ( int i = 0; i < estimate.projectedCacheSlotsTotal; i++ ) {
		if ( RB_ShadowMapProjectedCacheEntryStorageValid(
				&g_projectedShadowMapCache[i] ) ) {
			estimate.projectedCacheSlotsUsed++;
		}
	}
	for ( int i = 0; i < estimate.pointCacheSlotsTotal; i++ ) {
		if ( RB_ShadowMapPointCacheEntryStorageValid(
				&g_pointShadowMapCache[i] ) ) {
			estimate.pointCacheSlotsUsed++;
		}
	}
}

static void RB_ShadowMapEstimateArb2CachePass( const viewLight_t *vLight, const viewDef_t *viewDef, shadowMapArb2CacheEstimate_t &estimate, const shadowMapPassKind_t passKind, const bool pointLight, const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *tertiaryCasters, const drawSurf_t *quaternaryCasters, const drawSurf_t *primaryShadowSurfs, const drawSurf_t *secondaryShadowSurfs, const drawSurf_t *interactions ) {
	if ( interactions == NULL ) {
		return;
	}

	const unsigned int passMask = RB_ShadowMapArb2CachePassMask( passKind );
	const shadowMapPassKind_t cachePassKind =
		RB_ShadowMapCachePassKind( vLight, passKind );
	const unsigned int cacheKeyMask =
		RB_ShadowMapArb2CachePassMask( cachePassKind );
	estimate.shadowPasses++;

	const drawSurf_t *translucentPrimaryCasters = vLight != NULL ? vLight->globalTranslucentShadowMapCasters : NULL;
	const drawSurf_t *translucentSecondaryCasters = ( passKind == SHADOWMAP_PASS_GLOBAL && vLight != NULL ) ? vLight->localTranslucentShadowMapCasters : NULL;
	const bool haveOpaqueCasters = primaryCasters != NULL || secondaryCasters != NULL || tertiaryCasters != NULL || quaternaryCasters != NULL;
	const bool haveTranslucentCasters = translucentPrimaryCasters != NULL || translucentSecondaryCasters != NULL;
	const bool haveStencilShadowSurfs = primaryShadowSurfs != NULL || secondaryShadowSurfs != NULL;

	if ( !haveOpaqueCasters && !haveStencilShadowSurfs && !haveTranslucentCasters ) {
		estimate.unshadowedPasses++;
		return;
	}

	if ( !haveOpaqueCasters && !haveTranslucentCasters ) {
		estimate.stencilOnlyPasses++;
		return;
	}

	const bool haveEligibleReceivers = RB_DrawSurfChainHasFilteredSurface( interactions, RB_SurfaceEligibleForShadowMapReceiver );
	const bool haveReceiverFallbacks = RB_DrawSurfChainHasFilteredSurface( interactions, RB_SurfaceNeedsShadowMapReceiverFallback );
	if ( !haveEligibleReceivers ) {
		estimate.receiverFallbackPasses++;
		return;
	}
	if ( haveReceiverFallbacks ) {
		estimate.receiverFallbackPasses++;
	}

	if ( !RB_ShadowMapMapOrHybridAvailableForPass(
			vLight, passKind, NULL ) ) {
		// Mirror the direct draw's fail-closed gate. An ownership that cannot be
		// represented by either a complete map or an exact map+stencil hybrid
		// takes full stencil before scheduling and consumes no map-update budget.
		estimate.stencilOnlyPasses++;
		return;
	}

	const bool cacheable = RB_ShadowMapStaticCacheableReadOnly( vLight, cachePassKind, pointLight, haveTranslucentCasters );
	if ( cacheable ) {
		estimate.cacheablePasses++;
		estimate.cacheablePassMask |= passMask;
		const int signature = RB_ShadowMapBuildPassSignatureForView( vLight, viewDef, cachePassKind, pointLight );
		const int lightIndex = RB_ShadowMapLightIndex( vLight );
		const bool cacheHit = pointLight
			? RB_ShadowMapFindPointCacheEntry( lightIndex, cachePassKind, signature ) != NULL
			: RB_ShadowMapFindProjectedCacheEntry( lightIndex, cachePassKind, signature ) != NULL;
		if ( cacheHit ) {
			estimate.cacheHitPasses++;
			estimate.cacheHitPassMask |= passMask;
			estimate.cacheKeyHitMask |= cacheKeyMask;
			return;
		}
		// LOCAL and GLOBAL receivers can share one canonical GLOBAL map when
		// there are no local/noSelf caster chains. Model the later receiver as
		// a same-frame reuse instead of charging a phantom second update.
		if ( ( estimate.plannedCacheUpdateKeyMask & cacheKeyMask ) != 0 ) {
			estimate.cacheHitPasses++;
			estimate.cacheHitPassMask |= passMask;
			return;
		}
		estimate.cacheMissPasses++;
		estimate.cacheMissPassMask |= passMask;
	}

	const int updateBudget = r_shadowMapMaxUpdatesPerView.GetInteger();
	if ( updateBudget > 0 && estimate.freshUpdatePasses >= updateBudget ) {
		estimate.budgetFallbackPasses++;
		estimate.budgetFallbackPassMask |= passMask;
		return;
	}

	estimate.freshUpdatePasses++;
	estimate.freshUpdatePassMask |= passMask;
	if ( cacheable ) {
		estimate.plannedCacheUpdateKeyMask |= cacheKeyMask;
	}
}

bool RB_ShadowMapEstimateArb2CacheOwnership( const viewLight_t *vLight, const viewDef_t *viewDef, shadowMapArb2CacheEstimate_t &estimate ) {
	memset( &estimate, 0, sizeof( estimate ) );
	estimate.lightIndex = RB_ShadowMapLightIndex( vLight );
	// classification routes parallel lights through the projected path
	const bool estimatePointLight = R_ClassifyShadowMapLight( vLight ).pointLight;
	estimate.pointLight = estimatePointLight;
	estimate.staticCacheEnabled = r_shadowMapStaticCache.GetBool();
	RB_ShadowMapArb2CacheSlotCountsReadOnly( estimate );

	if ( vLight == NULL || !r_useShadowMap.GetBool() || !r_shadows.GetBool() ) {
		return false;
	}
	if ( RB_ShadowMapLightPolicySupportReason( vLight ) != SHADOWMAP_SUPPORT_OK ) {
		return false;
	}
	// Mirror the read-only capability half of RB_ShadowMapLightSupportReason.
	// The estimator must not allocate resources, but an impossible light must
	// not consume admission priority and starve a viable update.
	if ( glConfig.maxTextureUnits < 6 || glConfig.maxTextureImageUnits < 6 ) {
		return false;
	}
	bool deferGlobalReceiverDraw = false;
	const drawSurf_t *globalReceiverInteractions =
		RB_ShadowMapGlobalReceiverPlanningChain(
			vLight, deferGlobalReceiverDraw );
	if ( globalReceiverInteractions == NULL && vLight->localInteractions == NULL ) {
		return false;
	}
	if ( estimatePointLight ) {
		if ( !r_shadowMapPointLights.GetBool() || !glConfig.cubeMapAvailable ) {
			return false;
		}
	}

	estimate.valid = true;
	if ( estimatePointLight ) {
		RB_ShadowMapEstimateArb2CachePass( vLight, viewDef, estimate, SHADOWMAP_PASS_LOCAL, true, vLight->globalShadowMapCasters, NULL, vLight->globalShadowMapDynamicCasters, NULL, vLight->globalShadows, NULL, vLight->localInteractions );
		RB_ShadowMapEstimateArb2CachePass( vLight, viewDef, estimate, SHADOWMAP_PASS_GLOBAL, true, vLight->globalShadowMapCasters, vLight->localShadowMapCasters, vLight->globalShadowMapDynamicCasters, vLight->localShadowMapDynamicCasters, vLight->globalShadows, vLight->localShadows, globalReceiverInteractions );
	} else {
		RB_ShadowMapEstimateArb2CachePass( vLight, viewDef, estimate, SHADOWMAP_PASS_LOCAL, false, vLight->globalShadowMapCasters, NULL, vLight->globalShadowMapDynamicCasters, NULL, vLight->globalShadows, NULL, vLight->localInteractions );
		RB_ShadowMapEstimateArb2CachePass( vLight, viewDef, estimate, SHADOWMAP_PASS_GLOBAL, false, vLight->globalShadowMapCasters, vLight->localShadowMapCasters, vLight->globalShadowMapDynamicCasters, vLight->localShadowMapDynamicCasters, vLight->globalShadows, vLight->localShadows, globalReceiverInteractions );
	}
	if ( deferGlobalReceiverDraw
		&& RB_DrawSurfChainHasFilteredSurface( vLight->globalInteractions,
			RB_SurfaceNeedsShadowMapReceiverFallback ) ) {
		// The deferred translucent chain owns map admission, but opaque GLOBAL
		// fallback surfaces still consume the same full-stencil ownership.
		estimate.receiverFallbackPasses++;
	}
	return estimate.shadowPasses > 0;
}

static projectedShadowMapCacheEntry_t *RB_ShadowMapNewestProjectedGlobalEntry( const int lightIndex );

// Importance-ordered update admissions (M4): with a constrained update
// budget, spend it on the most important stale lights instead of whichever
// came first in the light loop. Round-robin fairness comes from the
// staleness term - a starved light's score grows every frame until it wins
// a slot. The modern planner's fairness boost folds in when the planner ran
// this frame, making it authoritative where it is live. Deterministic:
// score ties break on light index.
static void RB_ShadowMapBuildUpdateAdmissions( void ) {
	g_shadowMapAdmissionsActive = false;
	g_shadowMapAdmittedLightCount = 0;
	const int updateBudget = r_shadowMapMaxUpdatesPerView.GetInteger();
	if ( updateBudget <= 0 || backEnd.viewDef == NULL || backEnd.viewDef->viewLights == NULL ) {
		return;
	}
	if ( backEnd.viewDef->isSubview && idMath::ClampInt( 0, 2, r_shadowMapSubviewPolicy.GetInteger() ) > 0 ) {
		// the subview policy already forbids fresh renders
		return;
	}

	struct shadowMapAdmissionCandidate_t {
		int	lightIndex;
		int	cost;
		int	score;
	};
	shadowMapAdmissionCandidate_t candidates[SHADOWMAP_MAX_ADMITTED_LIGHTS];
	int candidateCount = 0;
	int totalCost = 0;
	for ( const viewLight_t *vLight = backEnd.viewDef->viewLights; vLight != NULL && candidateCount < SHADOWMAP_MAX_ADMITTED_LIGHTS; vLight = vLight->next ) {
		shadowMapArb2CacheEstimate_t estimate;
		if ( !RB_ShadowMapEstimateArb2CacheOwnership( vLight, backEnd.viewDef, estimate ) ) {
			continue;
		}
		// budgetFallbackPasses folds back in: the estimate simulates the
		// per-light budget, but admission needs the light's full desired
		// update count. The estimator already canonicalizes LOCAL/GLOBAL cache
		// ownership and models a same-frame reuse, so this sum is the real cost.
		const int cost = estimate.freshUpdatePasses
			+ estimate.budgetFallbackPasses;
		if ( cost <= 0 ) {
			continue;
		}
		int lastUpdatedFrame = -1;
		if ( estimate.pointLight ) {
			const pointShadowMapCacheEntry_t *entry = RB_ShadowMapNewestCompatiblePointEntry( vLight, SHADOWMAP_PASS_GLOBAL );
			if ( entry != NULL ) {
				lastUpdatedFrame = entry->lastUpdatedFrame;
			}
		} else {
			const projectedShadowMapCacheEntry_t *entry = RB_ShadowMapNewestProjectedGlobalEntry( estimate.lightIndex );
			if ( entry != NULL ) {
				lastUpdatedFrame = entry->lastUpdatedFrame;
			}
		}
		const int staleness = lastUpdatedFrame < 0 ? 64 : idMath::ClampInt( 0, 64, tr.frameCount - lastUpdatedFrame );
		const idScreenRect &rect = vLight->scissorRect;
		const int scissorArea = rect.IsEmpty() ? 0 : ( rect.x2 + 1 - rect.x1 ) * ( rect.y2 + 1 - rect.y1 );
		int score = scissorArea / 32 + staleness * 512 + ( vLight->viewInsideLight ? 8192 : 0 );
		const modernShadowLightDescriptor_t *planned = R_ModernShadowPlanner_DescriptorForLight( vLight );
		if ( planned != NULL ) {
			score += planned->fairnessBoost;
		}
		candidates[candidateCount].lightIndex = estimate.lightIndex;
		candidates[candidateCount].cost = cost;
		candidates[candidateCount].score = score;
		candidateCount++;
		totalCost += cost;
	}
	if ( candidateCount == 0 || totalCost <= updateBudget ) {
		// everything fits; first-come order is already correct
		return;
	}

	// insertion sort by (score desc, lightIndex asc); counts are small
	for ( int i = 1; i < candidateCount; i++ ) {
		const shadowMapAdmissionCandidate_t key = candidates[i];
		int j = i - 1;
		while ( j >= 0 && ( candidates[j].score < key.score || ( candidates[j].score == key.score && candidates[j].lightIndex > key.lightIndex ) ) ) {
			candidates[j + 1] = candidates[j];
			j--;
		}
		candidates[j + 1] = key;
	}

	int remaining = updateBudget;
	for ( int i = 0; i < candidateCount && remaining > 0; i++ ) {
		// Let the highest-priority light make partial progress when its LOCAL
		// and GLOBAL passes exceed the entire budget. Per-pass scheduling still
		// enforces the limit; the second pass can fill after the first cache hit.
		// Otherwise a budget of one permanently starves every two-pass light.
		if ( candidates[i].cost > remaining && g_shadowMapAdmittedLightCount > 0 ) {
			continue;
		}
		g_shadowMapAdmittedLightIndexes[g_shadowMapAdmittedLightCount++] = candidates[i].lightIndex;
		remaining = Max( 0, remaining - candidates[i].cost );
	}
	g_shadowMapAdmissionsActive = true;
}

// A light can briefly own two GLOBAL entries: a signature change allocates a
// new record without invalidating the old one. Consumers of the persistent-atlas slot
// need the entry ARB2 is actually maintaining, which is always the most
// recently rendered one - a first-match lookup could export a stale sibling
// as current content.
static projectedShadowMapCacheEntry_t *RB_ShadowMapNewestProjectedGlobalEntry( const int lightIndex ) {
	const int slotLimit = RB_ShadowMapProjectedCacheSlotLimit();
	projectedShadowMapCacheEntry_t *newest = NULL;
	for ( int i = 0; i < slotLimit; i++ ) {
		projectedShadowMapCacheEntry_t *entry = &g_projectedShadowMapCache[i];
		if ( RB_ShadowMapProjectedCacheEntryStorageValid( entry )
				&& entry->lightIndex == lightIndex
				&& entry->passKind == static_cast<int>( SHADOWMAP_PASS_GLOBAL )
			&& ( newest == NULL || entry->lastUpdatedFrame > newest->lastUpdatedFrame ) ) {
			newest = entry;
		}
	}
	return newest;
}

// Exposes a cached projected light's persistent-atlas placement so the modern
// path can reference real tiles instead of aliasing whatever per-light target
// the last ARB2 shadow pass selected. Read-only: must not touch LRU state.
// Only the exact GLOBAL signature qualifies - a light can retain an older
// sibling, and "newest" is not necessarily the signature
// this view estimated as current. Rects use the ARB2 receiver's atlas math.
bool RB_ShadowMapProjectedAtlasSlotForLight( const viewLight_t *vLight,
		const viewDef_t *viewDef, shadowMapArb2AtlasSlot_t &slot ) {
	memset( &slot, 0, sizeof( slot ) );
	const int lightDefIndex = RB_ShadowMapLightIndex( vLight );
	slot.lightIndex = lightDefIndex;
	if ( vLight == NULL || viewDef == NULL || viewDef->renderWorld == NULL
			|| lightDefIndex < 0
			|| g_shadowMapCacheRenderWorld != viewDef->renderWorld
			|| g_shadowMapCacheMapFileCRC != viewDef->renderWorld->mapFileCRC
			|| g_shadowMapCacheMapNameHash != RB_ShadowMapMapNameHash( viewDef )
			|| g_shadowMapAtlasDepthImage == NULL
			|| !g_shadowMapAtlasDepthImage->IsLoaded()
			|| g_shadowMapAtlasDepthImage->IsDefaulted()
			|| g_shadowMapAtlasDepthImage->GetDeviceHandle() == 0
			|| g_shadowMapAtlasDepthImage->GetOpts().textureType != TT_2D
			|| g_shadowMapAtlasDepthImage->GetOpts().format != FMT_DEPTH
			|| g_shadowMapAtlasRenderTexture == NULL
			|| g_shadowMapAtlasCellSize <= 0 ) {
		return false;
	}
	const int signature = RB_ShadowMapBuildPassSignatureForView(
		vLight, viewDef, SHADOWMAP_PASS_GLOBAL, false );
	const projectedShadowMapCacheEntry_t *entry =
		RB_ShadowMapFindProjectedCacheEntry(
			lightDefIndex, SHADOWMAP_PASS_GLOBAL, signature );
	if ( entry == NULL || !entry->state.valid || entry->atlasCellSpan <= 0 ) {
		return false;
	}
	const int atlasWidth = g_shadowMapAtlasRenderTexture->GetWidth();
	const int atlasHeight = g_shadowMapAtlasRenderTexture->GetHeight();
	if ( atlasWidth <= 0 || atlasHeight <= 0
			|| entry->atlasCellX < 0 || entry->atlasCellY < 0
			|| ( entry->atlasCellX + entry->atlasCellSpan )
				* g_shadowMapAtlasCellSize > atlasWidth
			|| ( entry->atlasCellY + entry->atlasCellSpan )
				* g_shadowMapAtlasCellSize > atlasHeight ) {
		return false;
	}
	slot.signature = entry->signature;
	slot.storageGeneration = entry->atlasStorageGeneration;
	slot.cellX = entry->atlasCellX;
	slot.cellY = entry->atlasCellY;
	slot.cellSpan = entry->atlasCellSpan;
	slot.cellSizePixels = g_shadowMapAtlasCellSize;
	slot.atlasWidthPixels = atlasWidth;
	slot.atlasHeightPixels = atlasHeight;
	slot.tileSize = entry->state.tileSize;
	slot.atlasDiv = entry->state.atlasDiv;
	slot.cascadeCount = idMath::ClampInt( 1, SHADOWMAP_PROJECTED_MAX_CASCADES, entry->state.cascadeCount );
	slot.lastUpdatedFrame = entry->lastUpdatedFrame;
	slot.lastUsedFrame = entry->lastUsedFrame;
	const float slotU0 = ( entry->atlasCellX * g_shadowMapAtlasCellSize ) / (float)atlasWidth;
	const float slotV0 = ( entry->atlasCellY * g_shadowMapAtlasCellSize ) / (float)atlasHeight;
	const float slotSpanU = ( entry->state.tileSize * entry->state.atlasDiv ) / (float)atlasWidth;
	const float slotSpanV = ( entry->state.tileSize * entry->state.atlasDiv ) / (float)atlasHeight;
	for ( int i = 0; i < SHADOWMAP_PROJECTED_MAX_CASCADES; i++ ) {
		const idVec4 &rect = entry->state.atlasRect[i];
		slot.cascadeAtlasRect[i][0] = slotU0 + rect.x * slotSpanU;
		slot.cascadeAtlasRect[i][1] = slotV0 + rect.y * slotSpanV;
		slot.cascadeAtlasRect[i][2] = slotU0 + rect.z * slotSpanU;
		slot.cascadeAtlasRect[i][3] = slotV0 + rect.w * slotSpanV;
	}
	slot.valid = true;
	return true;
}

// Pins an atlas entry against idle expiry while modern receivers are
// presenting it. Callers must gate this on live modern consumption -
// diagnostics-only planner runs must stay strictly read-only or they would
// distort ARB2's cache LRU behavior. Revalidate the exact planned cell at the
// consumption point so an intervening eviction cannot retarget its UVs.
bool RB_ShadowMapProjectedAtlasSlotMarkUsed( const int lightDefIndex,
		const int signature, const std::uint64_t storageGeneration,
		const int cellX, const int cellY, const int cellSpan ) {
	projectedShadowMapCacheEntry_t *entry =
		RB_ShadowMapFindProjectedCacheEntry(
			lightDefIndex, SHADOWMAP_PASS_GLOBAL, signature );
	if ( entry == NULL
			|| entry->atlasStorageGeneration != storageGeneration
			|| entry->atlasCellX != cellX
			|| entry->atlasCellY != cellY
			|| entry->atlasCellSpan != cellSpan ) {
		return false;
	}
	entry->lastUsedFrame = tr.frameCount;
	return true;
}

// Exports provenance for the one cube the classic GL path has actually left
// selected.  The render-world scope and GLOBAL-pass signature must both match
// the descriptor being built; a valid cube belonging to another point light
// is intentionally unavailable because the modern shader has no cube array.
bool RB_ShadowMapPointCubeForLight( const viewLight_t *vLight,
		const viewDef_t *viewDef, shadowMapArb2PointCube_t &cube ) {
	memset( &cube, 0, sizeof( cube ) );
	cube.lightIndex = RB_ShadowMapLightIndex( vLight );
	if ( vLight == NULL || viewDef == NULL || viewDef->renderWorld == NULL
			|| cube.lightIndex < 0
			|| g_shadowMapCacheRenderWorld != viewDef->renderWorld
			|| g_shadowMapCacheMapFileCRC != viewDef->renderWorld->mapFileCRC
			|| g_shadowMapCacheMapNameHash != RB_ShadowMapMapNameHash( viewDef )
			|| !RB_ShadowMapActivePointCacheContentReady() ) {
		return false;
	}

	const pointShadowMapCacheEntry_t *entry = g_activePointShadowMapCache;
	const int signature = RB_ShadowMapBuildPassSignatureForView(
		vLight, viewDef, SHADOWMAP_PASS_GLOBAL, true );
	if ( entry->lightIndex != cube.lightIndex
			|| entry->passKind != static_cast<int>( SHADOWMAP_PASS_GLOBAL )
			|| entry->signature != signature
			|| entry->size != RB_ShadowMapPointSizeValue()
			|| entry->highPrecision != RB_PointShadowMapHighPrecisionEnabled()
			|| !RB_ShadowMapPointCacheEntryProjectionMatches( entry, vLight ) ) {
		return false;
	}

	cube.valid = true;
	cube.signature = entry->signature;
	cube.size = entry->size;
	cube.highPrecision = entry->highPrecision;
	cube.lastUpdatedFrame = entry->lastUpdatedFrame;
	return true;
}

void RB_ShadowMapPointCubeMarkUsed( const int lightDefIndex ) {
	if ( RB_ShadowMapActivePointCacheContentReady()
			&& g_activePointShadowMapCache->lightIndex == lightDefIndex ) {
		g_activePointShadowMapCache->lastUsedFrame = tr.frameCount;
	}
}

static void RB_ARB2_UploadCustomGLSLShaderParms( const shaderStage_t *stage, const float *regs, const drawInteraction_t *din ) {
	newShaderStage_t *newStage = stage->newStage;

	for ( int parmIndex = 0; parmIndex < newStage->numShaderParms; parmIndex++ ) {
		const int location = newStage->shaderParmLocations[parmIndex];
		if ( location < 0 ) {
			continue;
		}

		if ( RB_BindGLSLShaderParm( newStage->shaderParmBindings[parmIndex], location, stage, din ) ) {
			continue;
		}

		const int numRegisters = newStage->shaderParmNumRegisters[parmIndex];
		if ( numRegisters <= 0 || regs == NULL ) {
			continue;
		}

		float parm[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		for ( int component = 0; component < numRegisters && component < 4; component++ ) {
			parm[component] = regs[ newStage->shaderParmRegisters[parmIndex][component] ];
		}

		switch ( numRegisters ) {
		case 1:
			glUniform1fvARB( location, 1, parm );
			break;
		case 2:
			glUniform2fvARB( location, 1, parm );
			break;
		case 3:
			glUniform3fvARB( location, 1, parm );
			break;
		default:
			glUniform4fvARB( location, 1, parm );
			break;
		}
	}
}

static void RB_ARB2_BindCustomGLSLShaderTextures( const newShaderStage_t *newStage, const drawInteraction_t *din ) {
	for ( int textureIndex = 0; textureIndex < newStage->numShaderTextures; textureIndex++ ) {
		idImage *image = RB_ResolveGLSLShaderTextureImage( newStage, textureIndex, din );
		if ( image == NULL ) {
			continue;
		}

		GL_SelectTextureNoClient( textureIndex );
		image->SetSamplerState( newStage->shaderTextureFilters[textureIndex], newStage->shaderTextureRepeats[textureIndex] );
		image->Bind();
		if ( newStage->shaderTextureLocations[textureIndex] >= 0 ) {
			glUniform1iARB( newStage->shaderTextureLocations[textureIndex], textureIndex );
		}
	}
}

static void RB_ARB2_UnbindCustomGLSLShaderTextures( const newShaderStage_t *newStage ) {
	for ( int textureIndex = newStage->numShaderTextures - 1; textureIndex >= 0; textureIndex-- ) {
		if ( textureIndex == 0 || RB_ResolveGLSLShaderTextureImage( newStage, textureIndex, NULL ) != NULL ) {
			GL_SelectTextureNoClient( textureIndex );
			globalImages->BindNull();
		}
	}
	GL_SelectTexture( 0 );
}

static void RB_ARB2_DrawCustomGLSLInteractionStage( const shaderStage_t *stage, const float *regs, const drawInteraction_t *din ) {
	if ( stage == NULL || stage->newStage == NULL || din == NULL ) {
		return;
	}

	newShaderStage_t *newStage = stage->newStage;
	if ( !newStage->customLighting || !newStage->glslProgram || !R_ValidateGLSLProgram( newStage ) ) {
		return;
	}

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( (GLhandleARB)newStage->glslProgramObject );

	RB_ARB2_UploadCustomGLSLShaderParms( stage, regs, din );
	RB_ARB2_BindCustomGLSLShaderTextures( newStage, din );

	const idMaterial *surfaceMaterial = din->surf->material;
	if ( surfaceMaterial != NULL && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
	}

	RB_DrawElementsWithCounters( din->surf->geo );

	if ( surfaceMaterial != NULL && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}

	RB_ARB2_UnbindCustomGLSLShaderTextures( newStage );
	glUseProgramObjectARB( 0 );
}

static void RB_ARB2_CreateCustomGLSLDrawInteractionsForSurface( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->geo == NULL || surf->space == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return;
	}
	if ( surf->geo->ambientCache == NULL ) {
		return;
	}

	const idMaterial *surfaceShader = surf->material;
	const float *surfaceRegs = surf->shaderRegisters;
	const viewLight_t *vLight = backEnd.vLight;
	const idMaterial *lightShader = vLight->lightShader;
	const float *lightRegs = vLight->shaderRegisters;

	if ( surf->space != backEnd.currentSpace ) {
		backEnd.currentSpace = surf->space;
		glLoadMatrixf( surf->space->modelViewMatrix );
	}

	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
		backEnd.currentScissor = surf->scissorRect;
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}

	bool depthHack = false;
	if ( surf->space->weaponDepthHack ) {
		RB_EnterWeaponDepthHack();
		depthHack = true;
	}
	if ( surf->space->modelDepthHack != 0.0f ) {
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
		depthHack = true;
	}

	drawInteraction_t inter;
	memset( &inter, 0, sizeof( inter ) );
	inter.surf = surf;
	inter.lightFalloffImage = vLight->falloffImage;
	inter.ambientLight = lightShader->IsAmbientLight();
	R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, inter.localLightOrigin.ToVec3() );
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, inter.localViewOrigin.ToVec3() );
	inter.localLightOrigin[3] = 0.0f;
	inter.localViewOrigin[3] = 1.0f;

	idPlane lightProject[4];
	for ( int i = 0; i < 4; i++ ) {
		R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i] );
	}

	const int lightStageCount = lightShader->GetNumStages();
	const int surfaceStageCount = surfaceShader->GetNumStages();
	for ( int lightStageNum = 0; lightStageNum < lightStageCount; lightStageNum++ ) {
		const shaderStage_t *lightStage = lightShader->GetStage( lightStageNum );
		if ( lightRegs != NULL && lightRegs[ lightStage->conditionRegister ] == 0.0f ) {
			continue;
		}

		inter.lightImage = lightStage->texture.image;
		memcpy( inter.lightProjection, lightProject, sizeof( inter.lightProjection ) );
		if ( lightStage->texture.hasMatrix ) {
			RB_GetShaderTextureMatrix( lightRegs, &lightStage->texture, backEnd.lightTextureMatrix );
			RB_BakeTextureMatrixIntoTexgen( reinterpret_cast<idPlane *>( inter.lightProjection ), backEnd.lightTextureMatrix );
		}

		const float lightColor[4] = {
			backEnd.lightScale * lightRegs[ lightStage->color.registers[0] ],
			backEnd.lightScale * lightRegs[ lightStage->color.registers[1] ],
			backEnd.lightScale * lightRegs[ lightStage->color.registers[2] ],
			lightRegs[ lightStage->color.registers[3] ]
		};

		for ( int surfaceStageNum = 0; surfaceStageNum < surfaceStageCount; surfaceStageNum++ ) {
			const shaderStage_t *surfaceStage = surfaceShader->GetStage( surfaceStageNum );
			if ( surfaceStage == NULL || surfaceStage->newStage == NULL ) {
				continue;
			}
			if ( !surfaceStage->newStage->customLighting || !surfaceStage->newStage->glslProgram ) {
				continue;
			}
			if ( surfaceRegs[ surfaceStage->conditionRegister ] == 0.0f ) {
				continue;
			}

			idImage *unusedImage = NULL;
			float surfaceColor[4];
			R_SetDrawInteraction( surfaceStage, surfaceRegs, &unusedImage, inter.diffuseMatrix, surfaceColor );
			inter.bumpMatrix[0] = inter.diffuseMatrix[0];
			inter.bumpMatrix[1] = inter.diffuseMatrix[1];
			inter.specularMatrix[0] = inter.diffuseMatrix[0];
			inter.specularMatrix[1] = inter.diffuseMatrix[1];
			for ( int component = 0; component < 4; component++ ) {
				inter.diffuseColor[component] = surfaceColor[component] * lightColor[component];
				inter.specularColor[component] = surfaceColor[component] * lightColor[component];
			}
			idImage *flatDiffuseImage = globalImages->whiteImage;
			RB_ApplyFlatDiffuseStage( surf, &flatDiffuseImage,
				inter.diffuseColor.ToFloatPtr(), inter.flatDiffuseParams );
			inter.vertexColor = surfaceStage->vertexColor;

			RB_ARB2_DrawCustomGLSLInteractionStage( surfaceStage, surfaceRegs, &inter );
		}
	}

	if ( depthHack ) {
		RB_LeaveDepthHack();
	}
}

static void RB_ARB2_CreateCustomGLSLDrawInteractions( const drawSurf_t *surf ) {
	if ( !RB_DrawSurfChainHasCustomGLSLLighting( surf ) ) {
		return;
	}

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glDisableVertexAttribArrayARB( 1 );
	glDisableVertexAttribArrayARB( 2 );
	glDisableVertexAttribArrayARB( 5 );
	glDisableVertexAttribArrayARB( 6 );
	glDisableVertexAttribArrayARB( 7 );
	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );
	glEnableClientState( GL_NORMAL_ARRAY );
	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		if ( !RB_SurfaceHasActiveCustomGLSLLighting( surf ) || surf->geo == NULL ) {
			continue;
		}

		idDrawVert *ac = NULL;
		if ( !RB_GLSLPrepareInteractionVertexCache( surf, ac ) ) {
			continue;
		}

		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_COLOR_OFFSET ) );
		glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
		glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET ) );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET ) );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET ) );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET ) );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET ) );

		RB_ARB2_CreateCustomGLSLDrawInteractionsForSurface( surf );
	}

	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glUseProgramObjectARB( 0 );
	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
}

static bool RB_GLSLMaterial_CreateDrawInteractions( const drawSurf_t *surf, const bool forceNeutralEnhancements ) {
	if ( surf == NULL || !RB_MaterialInteractionLoadProgram() ) {
		return false;
	}
	bool submittedInteractions = false;

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( g_materialInteractionProgram.programObject );
	RB_GLSLInteractionUniformCacheInvalidate();

	if ( g_materialInteractionProgram.bumpMap >= 0 ) {
		glUniform1iARB( g_materialInteractionProgram.bumpMap, 0 );
	}
	if ( g_materialInteractionProgram.lightFalloffMap >= 0 ) {
		glUniform1iARB( g_materialInteractionProgram.lightFalloffMap, 1 );
	}
	if ( g_materialInteractionProgram.lightProjectionMap >= 0 ) {
		glUniform1iARB( g_materialInteractionProgram.lightProjectionMap, 2 );
	}
	if ( g_materialInteractionProgram.diffuseMap >= 0 ) {
		glUniform1iARB( g_materialInteractionProgram.diffuseMap, 3 );
	}
	if ( g_materialInteractionProgram.specularMap >= 0 ) {
		glUniform1iARB( g_materialInteractionProgram.specularMap, 4 );
	}
	if ( g_materialInteractionProgram.ambientNormalMap >= 0 ) {
		glUniform1iARB( g_materialInteractionProgram.ambientNormalMap, 5 );
	}
	RB_MaterialInteractionSetEnhancementUniforms(
		g_materialInteractionProgram.materialNormalScale,
		g_materialInteractionProgram.materialSpecularBoost,
		g_materialInteractionProgram.materialFresnel,
		-1,
		forceNeutralEnhancements );
	if ( g_materialInteractionProgram.stockInteraction >= 0 ) {
		glUniform1fARB( g_materialInteractionProgram.stockInteraction, forceNeutralEnhancements ? 1.0f : 0.0f );
	}

	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );

	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		idDrawVert *ac = NULL;
		if ( !RB_GLSLPrepareInteractionVertexCache( surf, ac ) ) {
			continue;
		}

		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_COLOR_OFFSET ) );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET ) );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET ) );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET ) );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET ) );

		RB_SetCelInteractionUniform( g_materialInteractionProgram.celParams, surf );
		RB_CreateSingleDrawInteractions( surf, RB_GLSLMaterial_DrawInteraction );
		submittedInteractions = true;
	}

	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );

	GL_SelectTextureNoClient( 4 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 5 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 3 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 2 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 0 );
	globalImages->BindNull();

	glUseProgramObjectARB( 0 );
	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
	return submittedInteractions;
}

/*
==================
RB_DrawSurfChainNeedsCelBanding

True when at least one surface on this light's chain wants banded lighting.
Checked per chain rather than per surface because the interaction path is
chosen for the whole chain at once.
==================
*/
static bool RB_DrawSurfChainNeedsCelBanding( const drawSurf_t *surf ) {
	if ( !R_CelShadingAnyEnabled() ) {
		return false;
	}

	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		if ( R_CelShadingSurfaceActive( surf ) ) {
			return true;
		}
	}

	return false;
}

static bool RB_DrawSurfChainNeedsFlatDiffuseSweep( const drawSurf_t *surf ) {
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		if ( RB_FlatDiffuseSweepActive( surf ) ) {
			return true;
		}
	}
	return false;
}

static void RB_DrawMaterialInteractions( const drawSurf_t *surf ) {
	if ( surf == NULL ) {
		return;
	}

	if ( RB_AppleGL21AutomaticInteractionPath() ) {
		// The Apple 2.1 driver corridor is intentionally decided per surface.
		// Ordinary stock geometry avoids the fragile ARB interaction program,
		// while custom/generated/unprepared geometry gets the fail-closed simple
		// ARB fallback instead of disabling lighting for the entire view.
		for ( ; surf != NULL; surf = surf->nextOnLight ) {
			drawSurf_t singleSurf = *surf;
			singleSurf.nextOnLight = NULL;
			if ( RB_SurfaceEligibleForAppleGL21StockGLSLInteraction( &singleSurf ) &&
				RB_GLSLMaterial_CreateDrawInteractions( &singleSurf, true ) ) {
				g_appleGL21RouteCounts.glslCorridor++;
				continue;
			}
			g_appleGL21RouteCounts.simpleFallback++;
			RB_RecordAppleGL21FallbackReason( &singleSurf );
			RB_ARB2_CreateDrawInteractions( &singleSurf );
		}
		return;
	}

	// The stock ARB interaction program can carry the flat base color (the CPU
	// substitutes a white diffuse map) but has no model-local coordinate for
	// the moving band.  Split only chains containing a swept pickup so one
	// unrelated custom/GPU-posed surface cannot force the entire light chain
	// back to ARB and silently lose the item cue.
	const bool enhanced = RB_EnhancedMaterialShadingActive();
	if ( glConfig.GLSLProgramAvailable && RB_DrawSurfChainNeedsFlatDiffuseSweep( surf ) ) {
		for ( const drawSurf_t *chainSurf = surf; chainSurf != NULL; chainSurf = chainSurf->nextOnLight ) {
			drawSurf_t singleSurf = *chainSurf;
			singleSurf.nextOnLight = NULL;
			const bool celBandedSurface = !enhanced && R_CelShadingSurfaceActive( &singleSurf );
			const bool needsGLSL = enhanced || celBandedSurface || RB_FlatDiffuseSweepActive( &singleSurf );
			if ( !RB_SurfaceHasActiveCustomGLSLLighting( &singleSurf )
				&& needsGLSL
				&& RB_SurfaceEligibleForStockGLSLInteraction( &singleSurf )
				&& RB_GLSLMaterial_CreateDrawInteractions( &singleSurf, !enhanced ) ) {
				continue;
			}
			RB_ARB2_CreateDrawInteractions( &singleSurf );
		}
		return;
	}

	// Cel banding lives in the GLSL interaction shaders, and the stock ARB
	// assembly programs ship with the game so they cannot carry it. A cel-shaded
	// chain therefore borrows the GLSL path even when enhanced materials are
	// off; forcing neutral enhancements keeps the stock lighting model, so the
	// banding is the only visible difference.
	const bool celBanded = !enhanced && glConfig.GLSLProgramAvailable && RB_DrawSurfChainNeedsCelBanding( surf );

	if ( ( enhanced || celBanded )
		&& !RB_DrawSurfChainHasCustomGLSLLighting( surf )
		&& RB_DrawSurfChainEligibleForStockGLSLInteractions( surf )
		&& RB_GLSLMaterial_CreateDrawInteractions( surf, !enhanced ) ) {
		return;
	}

	RB_ARB2_CreateDrawInteractions( surf );
}

static bool RB_GLSLShadowMap_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( surf == NULL || !RB_ShadowMapLoadProgram() || !g_projectedShadowMapState.valid ) {
		return false;
	}

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( g_shadowMapProgram.programObject );
	RB_GLSLInteractionUniformCacheInvalidate();
	const shadowMapProjectedFilterSettings_t filterSettings = R_ShadowMapProjectedFilterSettings( backEnd.vLight );

	if ( g_shadowMapProgram.bumpMap >= 0 ) {
		glUniform1iARB( g_shadowMapProgram.bumpMap, 0 );
	}
	if ( g_shadowMapProgram.lightFalloffMap >= 0 ) {
		glUniform1iARB( g_shadowMapProgram.lightFalloffMap, 1 );
	}
	if ( g_shadowMapProgram.lightProjectionMap >= 0 ) {
		glUniform1iARB( g_shadowMapProgram.lightProjectionMap, 2 );
	}
	if ( g_shadowMapProgram.diffuseMap >= 0 ) {
		glUniform1iARB( g_shadowMapProgram.diffuseMap, 3 );
	}
	if ( g_shadowMapProgram.specularMap >= 0 ) {
		glUniform1iARB( g_shadowMapProgram.specularMap, 4 );
	}
	if ( g_shadowMapProgram.shadowMap >= 0 ) {
		glUniform1iARB( g_shadowMapProgram.shadowMap, 5 );
	}
	for ( int i = 0; i < 3; i++ ) {
		if ( g_shadowMapProgram.translucentShadowMap[i] >= 0 && glConfig.maxTextureUnits >= 7 + i && glConfig.maxTextureImageUnits >= 7 + i ) {
			glUniform1iARB( g_shadowMapProgram.translucentShadowMap[i], 6 + i );
		}
	}
	if ( g_shadowMapProgram.shadowTexelSize >= 0 ) {
		const float texelSize[2] = {
			1.0f / Max( 1, g_shadowMapRenderTexture->GetWidth() ),
			1.0f / Max( 1, g_shadowMapRenderTexture->GetHeight() )
		};
		glUniform2fvARB( g_shadowMapProgram.shadowTexelSize, 1, texelSize );
	}
	if ( g_shadowMapProgram.shadowBias >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowBias, r_shadowMapBias.GetFloat() );
	}
	if ( g_shadowMapProgram.shadowNormalBias >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowNormalBias, r_shadowMapNormalBias.GetFloat() );
	}
	if ( g_shadowMapProgram.shadowTexelDepthBias >= 0 ) {
		glUniform1fvARB( g_shadowMapProgram.shadowTexelDepthBias, SHADOWMAP_MAX_CASCADES, g_projectedShadowMapState.texelDepthBias );
	}
	if ( g_shadowMapProgram.shadowNormalOffsetWorld >= 0 ) {
		float normalOffsets[SHADOWMAP_MAX_CASCADES];
		const float normalOffsetScale = Max( 0.0f, r_shadowMapNormalOffsetScale.GetFloat() );
		for ( int cascadeIndex = 0; cascadeIndex < SHADOWMAP_MAX_CASCADES; cascadeIndex++ ) {
			normalOffsets[cascadeIndex] = g_projectedShadowMapState.worldTexelSize[cascadeIndex] * normalOffsetScale;
		}
		glUniform1fvARB( g_shadowMapProgram.shadowNormalOffsetWorld, SHADOWMAP_MAX_CASCADES, normalOffsets );
	}
	if ( g_shadowMapProgram.shadowReceiverPlaneBias >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowReceiverPlaneBias, r_shadowMapReceiverPlaneBias.GetBool() ? 1.0f : 0.0f );
	}
	if ( g_shadowMapProgram.shadowFilterRadius >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowFilterRadius, filterSettings.filterRadius );
	}
	if ( g_shadowMapProgram.shadowFilterTaps >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowFilterTaps, static_cast<float>( filterSettings.filterTaps ) );
	}
	if ( g_shadowMapProgram.shadowFilterMode >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowFilterMode, static_cast<float>( filterSettings.filterMode ) );
	}
	if ( g_shadowMapProgram.shadowPCSSLightRadius >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowPCSSLightRadius, filterSettings.pcssLightRadius );
	}
	if ( g_shadowMapProgram.shadowPCSSMaxRadius >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowPCSSMaxRadius, filterSettings.pcssMaxRadius );
	}
	if ( g_shadowMapProgram.shadowAtlasRect >= 0 ) {
		// State rects stay in the per-light convention (planner parity depends
		// on it); the light's atlas slot is composed here at upload time. For
		// scratch renders the slot spans the whole texture, so the composition
		// is the identity.
		const float invAtlasWidth = 1.0f / Max( 1, g_shadowMapRenderTexture->GetWidth() );
		const float invAtlasHeight = 1.0f / Max( 1, g_shadowMapRenderTexture->GetHeight() );
		const float slotU0 = g_shadowMapActiveSlotOriginX * invAtlasWidth;
		const float slotV0 = g_shadowMapActiveSlotOriginY * invAtlasHeight;
		const float slotSpanU = ( g_projectedShadowMapState.tileSize * g_projectedShadowMapState.atlasDiv ) * invAtlasWidth;
		const float slotSpanV = ( g_projectedShadowMapState.tileSize * g_projectedShadowMapState.atlasDiv ) * invAtlasHeight;
		float composedRects[SHADOWMAP_MAX_CASCADES][4];
		for ( int i = 0; i < SHADOWMAP_MAX_CASCADES; i++ ) {
			const idVec4 &rect = g_projectedShadowMapState.atlasRect[i];
			composedRects[i][0] = slotU0 + rect.x * slotSpanU;
			composedRects[i][1] = slotV0 + rect.y * slotSpanV;
			composedRects[i][2] = slotU0 + rect.z * slotSpanU;
			composedRects[i][3] = slotV0 + rect.w * slotSpanV;
		}
		glUniform4fvARB( g_shadowMapProgram.shadowAtlasRect, SHADOWMAP_MAX_CASCADES, composedRects[0] );
	}
	if ( g_shadowMapProgram.shadowSplitDepths >= 0 ) {
		glUniform1fvARB( g_shadowMapProgram.shadowSplitDepths, SHADOWMAP_MAX_CASCADES, g_projectedShadowMapState.splitDepths );
	}
	if ( g_shadowMapProgram.shadowCascadeBiasScale >= 0 ) {
		glUniform1fvARB( g_shadowMapProgram.shadowCascadeBiasScale, SHADOWMAP_MAX_CASCADES, g_projectedShadowMapState.biasScale );
	}
	if ( g_shadowMapProgram.shadowCascadeCount >= 0 ) {
		glUniform1iARB( g_shadowMapProgram.shadowCascadeCount, g_projectedShadowMapState.cascadeCount );
	}
	if ( g_shadowMapProgram.shadowCascadeBlend >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowCascadeBlend, idMath::ClampFloat( 0.0f, 0.5f, r_shadowMapCascadeBlend.GetFloat() ) );
	}
	if ( g_shadowMapProgram.shadowDebugMode >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowDebugMode, (float)RB_ShadowMapDebugMode() );
	}
	if ( g_shadowMapProgram.translucentShadowEnabled >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.translucentShadowEnabled, RB_ProjectedTranslucentShadowEnabled() ? 1.0f : 0.0f );
	}
	if ( g_shadowMapProgram.translucentShadowDensity >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.translucentShadowDensity, r_shadowMapTranslucentDensity.GetFloat() );
	}
	if ( g_shadowMapProgram.translucentShadowFilterRadius >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.translucentShadowFilterRadius, r_shadowMapTranslucentFilterRadius.GetFloat() );
	}
	if ( g_shadowMapProgram.translucentShadowMinVariance >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.translucentShadowMinVariance, r_shadowMapTranslucentMinVariance.GetFloat() );
	}
	if ( g_shadowMapProgram.translucentShadowBleedReduction >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.translucentShadowBleedReduction, r_shadowMapTranslucentBleedReduction.GetFloat() );
	}
	for ( int i = 0; i < 3; i++ ) {
		RB_ShadowMapBindMomentTexture( 6 + i, GL_TEXTURE_2D,
			RB_ProjectedTranslucentShadowEnabled() ? g_translucentShadowMomentImages[i] : NULL );
	}
	// units 5-8 are constant for the whole receiver pass; only units 0-4 are
	// rebound per interaction draw, so bind the shadow depth map once here
	if ( g_shadowMapDepthImage != NULL ) {
		GL_SelectTextureNoClient( 5 );
		g_shadowMapDepthImage->Bind();
		const bool depthCompare = RB_ShadowMapDepthCompareEnabled();
		const GLint depthFilter = depthCompare ? GL_LINEAR : GL_NEAREST;
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, depthCompare ? GL_COMPARE_R_TO_TEXTURE : GL_NONE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, depthFilter );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, depthFilter );
	}
	if ( g_shadowMapProgram.materialEnhanced >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.materialEnhanced, RB_EnhancedMaterialShadingActive() ? 1.0f : 0.0f );
	}
	RB_MaterialInteractionSetEnhancementUniforms(
		g_shadowMapProgram.materialNormalScale,
		g_shadowMapProgram.materialSpecularBoost,
		g_shadowMapProgram.materialFresnel,
		g_shadowMapProgram.materialEnhanced );

	glStencilMask( 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	glStencilFunc( GL_ALWAYS, 128, 255 );

	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );

	g_shadowMapInteractionLastSpace = NULL;
	const int drawCountStart = g_shadowMapInteractionDrawCount;
	int debugSurfaceCount = 0;
	int debugSkippedCount = 0;
	int debugPreparedCount = 0;
	int debugPrepareFailedCount = 0;
	int debugInvalidCount = 0;
	int debugCustomCount = 0;
	int debugGeneratedCount = 0;
	int debugWrappedCustomCount = 0;
	const char *debugFirstMaterial = NULL;
	const bool receiverDebugMode = RB_ShadowMapReceiverDebugMode();
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		debugSurfaceCount++;
		if ( debugFirstMaterial == NULL && surf->material != NULL ) {
			debugFirstMaterial = surf->material->GetName();
		}
		const shadowMapReceiverFallbackReason_t fallbackReason = RB_SurfaceShadowMapReceiverFallbackReason( surf );
		g_shadowMapReceiverDebugReason = RB_ShadowMapReceiverDebugReasonValue( surf, fallbackReason );
		if ( fallbackReason == SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE ) {
			debugInvalidCount++;
		} else if ( fallbackReason == SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL ) {
			debugCustomCount++;
		} else if ( fallbackReason == SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY ) {
			debugGeneratedCount++;
		}
		if ( RB_SurfaceUsesWrappedCustomGLSLShadowReceiver( surf ) ) {
			debugWrappedCustomCount++;
		}
		if ( fallbackReason != SHADOWMAP_RECEIVER_FALLBACK_NONE
			&& ( !receiverDebugMode || fallbackReason == SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE ) ) {
			debugSkippedCount++;
			continue;
		}
		idDrawVert *ac = NULL;
		if ( !RB_GLSLPrepareInteractionVertexCache( surf, ac ) ) {
			debugPrepareFailedCount++;
			continue;
		}
		debugPreparedCount++;

		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_COLOR_OFFSET ) );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET ) );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET ) );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET ) );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET ) );

		const drawInteractionStageFilter_t stageFilter = ( receiverDebugMode && fallbackReason != SHADOWMAP_RECEIVER_FALLBACK_NONE ) ? NULL : RB_ShadowMapReceiverStageFilter;
		RB_SetCelInteractionUniform( g_shadowMapProgram.celParams, surf );
		RB_CreateSingleDrawInteractionsFiltered( surf, RB_GLSLShadowMap_DrawInteraction, stageFilter );
	}

	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );

	for ( int i = 0; i < 3; i++ ) {
		RB_ShadowMapBindMomentTexture( 6 + i, GL_TEXTURE_2D, NULL );
	}
	GL_SelectTextureNoClient( 5 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 4 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 3 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 2 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 0 );
	globalImages->BindNull();

	glUseProgramObjectARB( 0 );
	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
	const bool submittedInteractions = g_shadowMapInteractionDrawCount != drawCountStart;
	const bool processedReceivers = debugPreparedCount > 0;
	if ( RB_ShadowMapVerboseReport() && !submittedInteractions ) {
		common->Printf( "SM mask-submit type=projected light=%d submitted=0 surfaces=%d skipped=%d prepared=%d prepareFailed=%d invalid=%d customGLSL=%d generatedSkinned=%d wrappedCustomGLSL=%d firstMaterial='%s'\n",
			RB_ShadowMapLightIndex( backEnd.vLight ),
			debugSurfaceCount,
			debugSkippedCount,
			debugPreparedCount,
			debugPrepareFailedCount,
			debugInvalidCount,
			debugCustomCount,
			debugGeneratedCount,
			debugWrappedCustomCount,
			debugFirstMaterial != NULL ? debugFirstMaterial : "<none>" );
	}
	g_shadowMapMaskPrepareFailures = debugPrepareFailedCount;
	return submittedInteractions || processedReceivers;
}

static bool RB_GLSLPointShadowMap_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( surf == NULL || !RB_PointShadowMapLoadProgram() ) {
		return false;
	}

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( g_pointShadowMapProgram.programObject );
	RB_GLSLInteractionUniformCacheInvalidate();
	const int pointFaceSize = g_pointShadowMapRenderTexture != NULL
		? Max( 1, g_pointShadowMapRenderTexture->GetWidth() )
		: RB_ShadowMapPointSizeValue();
	const float pointFarDistance =
		R_ShadowMapPointFarDistance( backEnd.vLight );
	const shadowMapPointReceiverSettings_t baseReceiverSettings =
		R_ShadowMapPointReceiverSettings(
			pointFarDistance, pointFaceSize );
	const shadowMapPointReceiverSettings_t receiverSettings =
		R_ShadowMapPointStorageAdjustedReceiverSettings(
			baseReceiverSettings, pointFarDistance, pointFaceSize,
			RB_PointShadowMapDepthCompareEnabled(),
			RB_PointShadowMapHighPrecisionEnabled() );

	if ( g_pointShadowMapProgram.bumpMap >= 0 ) {
		glUniform1iARB( g_pointShadowMapProgram.bumpMap, 0 );
	}
	if ( g_pointShadowMapProgram.lightFalloffMap >= 0 ) {
		glUniform1iARB( g_pointShadowMapProgram.lightFalloffMap, 1 );
	}
	if ( g_pointShadowMapProgram.lightProjectionMap >= 0 ) {
		glUniform1iARB( g_pointShadowMapProgram.lightProjectionMap, 2 );
	}
	if ( g_pointShadowMapProgram.diffuseMap >= 0 ) {
		glUniform1iARB( g_pointShadowMapProgram.diffuseMap, 3 );
	}
	if ( g_pointShadowMapProgram.specularMap >= 0 ) {
		glUniform1iARB( g_pointShadowMapProgram.specularMap, 4 );
	}
	if ( g_pointShadowMapProgram.pointShadowMap >= 0 ) {
		glUniform1iARB( g_pointShadowMapProgram.pointShadowMap, 5 );
	}
	for ( int i = 0; i < 3; i++ ) {
		if ( g_pointShadowMapProgram.translucentShadowMap[i] >= 0 && glConfig.maxTextureUnits >= 7 + i && glConfig.maxTextureImageUnits >= 7 + i ) {
			glUniform1iARB( g_pointShadowMapProgram.translucentShadowMap[i], 6 + i );
		}
	}
	if ( g_pointShadowMapProgram.shadowBias >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.shadowBias,
			receiverSettings.constantBias );
	}
	if ( g_pointShadowMapProgram.shadowNormalBias >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.shadowNormalBias,
			receiverSettings.normalBias );
	}
	if ( g_pointShadowMapProgram.pointShadowTexelDepthBias >= 0 ) {
		glUniform1fARB(
			g_pointShadowMapProgram.pointShadowTexelDepthBias,
			receiverSettings.texelBiasScale / pointFaceSize );
	}
	if ( g_pointShadowMapProgram.pointShadowNormalOffsetWorld >= 0 ) {
		// per-distance texel factor: a cube face spans 2*distance across
		// width texels, so one texel at distance d is (2*d/width) world units
		glUniform1fARB(
			g_pointShadowMapProgram.pointShadowNormalOffsetWorld,
			2.0f * receiverSettings.normalOffsetScale / pointFaceSize );
	}
	if ( g_pointShadowMapProgram.shadowFilterRadius >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.shadowFilterRadius, r_shadowMapPointFilterRadius.GetFloat() );
	}
	if ( g_pointShadowMapProgram.shadowFilterTaps >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.shadowFilterTaps, static_cast<float>( idMath::ClampInt( 1, 13, r_shadowMapPointFilterTaps.GetInteger() ) ) );
	}
	if ( g_pointShadowMapProgram.shadowFilterMode >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.shadowFilterMode, static_cast<float>( idMath::ClampInt( 0, 1, r_shadowMapPointFilterMode.GetInteger() ) ) );
	}
	if ( g_pointShadowMapProgram.shadowDebugMode >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.shadowDebugMode, (float)RB_ShadowMapDebugMode() );
	}
	if ( g_pointShadowMapProgram.globalLightOrigin >= 0 ) {
		const float globalLightOrigin[4] = {
			backEnd.vLight->globalLightOrigin[0],
			backEnd.vLight->globalLightOrigin[1],
			backEnd.vLight->globalLightOrigin[2],
			1.0f
		};
		glUniform4fvARB( g_pointShadowMapProgram.globalLightOrigin, 1, globalLightOrigin );
	}
	if ( g_pointShadowMapProgram.pointShadowFar >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.pointShadowFar,
			pointFarDistance );
	}
	if ( g_pointShadowMapProgram.pointShadowTexelScale >= 0 ) {
		const float texelScale = 2.0f / pointFaceSize;
		glUniform1fARB( g_pointShadowMapProgram.pointShadowTexelScale, texelScale );
	}
	if ( g_pointShadowMapProgram.pointShadowDepthMode >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.pointShadowDepthMode, RB_PointShadowMapDepthModeValue() );
	}
	if ( g_pointShadowMapProgram.translucentShadowEnabled >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.translucentShadowEnabled, RB_PointTranslucentShadowEnabled() ? 1.0f : 0.0f );
	}
	if ( g_pointShadowMapProgram.translucentShadowDensity >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.translucentShadowDensity, r_shadowMapTranslucentDensity.GetFloat() );
	}
	if ( g_pointShadowMapProgram.translucentShadowFilterRadius >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.translucentShadowFilterRadius, r_shadowMapTranslucentFilterRadius.GetFloat() );
	}
	if ( g_pointShadowMapProgram.translucentShadowMinVariance >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.translucentShadowMinVariance, r_shadowMapTranslucentMinVariance.GetFloat() );
	}
	if ( g_pointShadowMapProgram.translucentShadowBleedReduction >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.translucentShadowBleedReduction, r_shadowMapTranslucentBleedReduction.GetFloat() );
	}
	if ( g_pointShadowMapProgram.materialEnhanced >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.materialEnhanced, RB_EnhancedMaterialShadingActive() ? 1.0f : 0.0f );
	}
	RB_MaterialInteractionSetEnhancementUniforms(
		g_pointShadowMapProgram.materialNormalScale,
		g_pointShadowMapProgram.materialSpecularBoost,
		g_pointShadowMapProgram.materialFresnel,
		g_pointShadowMapProgram.materialEnhanced );

	// units 5-8 are constant for the whole receiver pass; only units 0-4 are
	// rebound per interaction draw, so bind the cube map and moments once here
	GL_SelectTextureNoClient( 5 );
	if ( RB_PointShadowMapDepthCompareEnabled() && g_pointShadowMapDepthImage != NULL ) {
		g_pointShadowMapDepthImage->Bind();
		glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE );
		glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL );
		glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	} else if ( g_pointShadowMapColorImage != NULL ) {
		g_pointShadowMapColorImage->Bind();
		glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_NONE );
	}
	for ( int i = 0; i < 3; i++ ) {
		RB_ShadowMapBindMomentTexture( 6 + i, GL_TEXTURE_CUBE_MAP,
			RB_PointTranslucentShadowEnabled() ? g_pointTranslucentShadowMomentImages[i] : NULL );
	}

	glStencilMask( 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	glStencilFunc( GL_ALWAYS, 128, 255 );

	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );

	const int drawCountStart = g_pointShadowMapInteractionDrawCount;
	int pointDebugSurfaceCount = 0;
	int pointDebugSkippedCount = 0;
	int pointDebugPreparedCount = 0;
	int pointDebugPrepareFailedCount = 0;
	int pointDebugInvalidCount = 0;
	int pointDebugCustomCount = 0;
	int pointDebugGeneratedCount = 0;
	int pointDebugWrappedCustomCount = 0;
	int pointDebugTriCacheCount = 0;
	int pointDebugTriVertsCount = 0;
	int pointDebugTriPrimBatchCount = 0;
	int pointDebugAmbientSurfaceCount = 0;
	int pointDebugAmbientCacheCount = 0;
	int pointDebugAmbientVertsCount = 0;
	int pointDebugAmbientPrimBatchCount = 0;
	const char *pointDebugFirstMaterial = NULL;
	const bool pointReceiverDebugMode = RB_ShadowMapReceiverDebugMode();
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		pointDebugSurfaceCount++;
		if ( pointDebugFirstMaterial == NULL && surf->material != NULL ) {
			pointDebugFirstMaterial = surf->material->GetName();
		}
		const shadowMapReceiverFallbackReason_t fallbackReason = RB_SurfaceShadowMapReceiverFallbackReason( surf );
		g_pointShadowMapReceiverDebugReason = RB_ShadowMapReceiverDebugReasonValue( surf, fallbackReason );
		if ( fallbackReason == SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE ) {
			pointDebugInvalidCount++;
		} else if ( fallbackReason == SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL ) {
			pointDebugCustomCount++;
		} else if ( fallbackReason == SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY ) {
			pointDebugGeneratedCount++;
		}
		if ( RB_SurfaceUsesWrappedCustomGLSLShadowReceiver( surf ) ) {
			pointDebugWrappedCustomCount++;
		}
		if ( fallbackReason != SHADOWMAP_RECEIVER_FALLBACK_NONE
			&& ( !pointReceiverDebugMode || fallbackReason == SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE ) ) {
			pointDebugSkippedCount++;
			continue;
		}
		if ( surf->geo != NULL ) {
			if ( surf->geo->ambientCache != NULL ) {
				pointDebugTriCacheCount++;
			}
			if ( surf->geo->verts != NULL ) {
				pointDebugTriVertsCount++;
			}
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
			if ( surf->geo->primBatchMesh != NULL ) {
				pointDebugTriPrimBatchCount++;
			}
#endif
			if ( surf->geo->ambientSurface != NULL ) {
				pointDebugAmbientSurfaceCount++;
				if ( surf->geo->ambientSurface->ambientCache != NULL ) {
					pointDebugAmbientCacheCount++;
				}
				if ( surf->geo->ambientSurface->verts != NULL ) {
					pointDebugAmbientVertsCount++;
				}
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
				if ( surf->geo->ambientSurface->primBatchMesh != NULL ) {
					pointDebugAmbientPrimBatchCount++;
				}
#endif
			}
		}
		idDrawVert *ac = NULL;
		if ( !RB_GLSLPrepareInteractionVertexCache( surf, ac ) ) {
			pointDebugPrepareFailedCount++;
			continue;
		}
		pointDebugPreparedCount++;

		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_COLOR_OFFSET ) );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET ) );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET ) );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET ) );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET ) );

		const drawInteractionStageFilter_t stageFilter = ( pointReceiverDebugMode && fallbackReason != SHADOWMAP_RECEIVER_FALLBACK_NONE ) ? NULL : RB_ShadowMapReceiverStageFilter;
		RB_SetCelInteractionUniform( g_pointShadowMapProgram.celParams, surf );
		RB_CreateSingleDrawInteractionsFiltered( surf, RB_GLSLPointShadowMap_DrawInteraction, stageFilter );
	}

	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );

	for ( int i = 0; i < 3; i++ ) {
		RB_ShadowMapBindMomentTexture( 6 + i, GL_TEXTURE_CUBE_MAP, NULL );
	}
	GL_SelectTextureNoClient( 5 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 4 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 3 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 2 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 0 );
	globalImages->BindNull();

	glUseProgramObjectARB( 0 );
	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
	const bool submittedInteractions = g_pointShadowMapInteractionDrawCount != drawCountStart;
	const bool processedReceivers = pointDebugPreparedCount > 0;
	if ( RB_ShadowMapVerboseReport() && !submittedInteractions ) {
		common->Printf( "SM mask-submit type=point light=%d submitted=0 surfaces=%d skipped=%d prepared=%d prepareFailed=%d invalid=%d customGLSL=%d generatedSkinned=%d wrappedCustomGLSL=%d tri(cache=%d verts=%d prim=%d) ambient(surface=%d cache=%d verts=%d prim=%d) firstMaterial='%s'\n",
			RB_ShadowMapLightIndex( backEnd.vLight ),
			pointDebugSurfaceCount,
			pointDebugSkippedCount,
			pointDebugPreparedCount,
			pointDebugPrepareFailedCount,
			pointDebugInvalidCount,
			pointDebugCustomCount,
			pointDebugGeneratedCount,
			pointDebugWrappedCustomCount,
			pointDebugTriCacheCount,
			pointDebugTriVertsCount,
			pointDebugTriPrimBatchCount,
			pointDebugAmbientSurfaceCount,
			pointDebugAmbientCacheCount,
			pointDebugAmbientVertsCount,
			pointDebugAmbientPrimBatchCount,
			pointDebugFirstMaterial != NULL ? pointDebugFirstMaterial : "<none>" );
	}
	g_shadowMapMaskPrepareFailures = pointDebugPrepareFailedCount;
	return submittedInteractions || processedReceivers;
}

static void RB_DrawMaterialInteractionsFiltered( const drawSurf_t *surf, drawSurfFilter_t filter ) {
	if ( filter == NULL ) {
		RB_DrawMaterialInteractions( surf );
		return;
	}

	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		if ( !filter( surf ) ) {
			continue;
		}

		drawSurf_t singleSurf = *surf;
		singleSurf.nextOnLight = NULL;
		RB_DrawMaterialInteractions( &singleSurf );
	}
}

static void RB_ShadowMapStencilFallbackFiltered( const drawSurf_t *primaryShadowSurfs, const drawSurf_t *secondaryShadowSurfs, const drawSurf_t *interactions, drawSurfFilter_t filter ) {
	if ( interactions == NULL ) {
		return;
	}
	if ( !RB_DrawSurfChainHasFilteredSurface( interactions, filter ) ) {
		return;
	}

	if ( primaryShadowSurfs != NULL || secondaryShadowSurfs != NULL ) {
		backEnd.currentScissor = backEnd.vLight->scissorRect;
		if ( r_useScissor.GetBool() ) {
			glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
				backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		}
		glClear( GL_STENCIL_BUFFER_BIT );

		if ( r_useShadowVertexProgram.GetBool() && R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW, "stencil shadow fallback vertex program", true ) ) {
			glEnable( GL_VERTEX_PROGRAM_ARB );
			RB_StencilShadowPass( primaryShadowSurfs );
			RB_StencilShadowPass( secondaryShadowSurfs );
			RB_DrawMaterialInteractionsFiltered( interactions, filter );
			glDisable( GL_VERTEX_PROGRAM_ARB );
		} else {
			RB_StencilShadowPass( primaryShadowSurfs );
			RB_StencilShadowPass( secondaryShadowSurfs );
			RB_DrawMaterialInteractionsFiltered( interactions, filter );
		}
	} else {
		glStencilFunc( GL_ALWAYS, 128, 255 );
		RB_DrawMaterialInteractionsFiltered( interactions, filter );
	}
}

static void RB_ShadowMapStencilFallback( const viewLight_t *vLight, const shadowMapPassKind_t passKind, const drawSurf_t *primaryShadowSurfs, const drawSurf_t *secondaryShadowSurfs, const drawSurf_t *interactions ) {
	shadowMapTimedPhase_t timedPhase;
	RB_ShadowMapBeginTimedPhase( timedPhase, vLight, passKind, SHADOWMAP_TIMING_STENCIL_FALLBACK );
	RB_ShadowMapStencilFallbackFiltered( primaryShadowSurfs, secondaryShadowSurfs, interactions, NULL );
	RB_ShadowMapEndTimedPhase( timedPhase );
}

static void RB_ShadowMapDebugOverlayCapture( const viewLight_t *vLight, const shadowMapPassKind_t passKind, const bool pointLight, const bool mapped, const bool renderOk, const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *tertiaryCasters, const drawSurf_t *quaternaryCasters, const drawSurf_t *primaryShadowSurfs, const drawSurf_t *secondaryShadowSurfs, const drawSurf_t *interactions ) {
	if ( !RB_ShadowMapDebugOverlayEnabled() || !renderOk ) {
		return;
	}

	const int lightDefIndex = ( vLight != NULL && vLight->lightDef != NULL ) ? vLight->lightDef->index : -1;
	const int singleLight = r_singleLight.GetInteger();
	if ( singleLight >= 0 && lightDefIndex != singleLight ) {
		return;
	}
	if ( g_shadowMapDebugOverlayState.valid && singleLight < 0 && g_shadowMapDebugOverlayState.mapped && !mapped ) {
		return;
	}

	g_shadowMapDebugOverlayState.valid = true;
	g_shadowMapDebugOverlayState.pointLight = pointLight;
	g_shadowMapDebugOverlayState.globalPass = ( passKind == SHADOWMAP_PASS_GLOBAL );
	g_shadowMapDebugOverlayState.mapped = mapped;
	g_shadowMapDebugOverlayState.lightDefIndex = lightDefIndex;
	g_shadowMapDebugOverlayState.tileCount = pointLight ? 6 : Max( 1, g_projectedShadowMapState.cascadeCount );
	g_shadowMapDebugOverlayState.atlasDiv = pointLight ? 3 : Max( 1, g_projectedShadowMapState.atlasDiv );
	g_shadowMapDebugOverlayState.cascadeCount = pointLight ? 1 : Max( 1, g_projectedShadowMapState.cascadeCount );
	g_shadowMapDebugOverlayState.slotU0 = 0.0f;
	g_shadowMapDebugOverlayState.slotV0 = 0.0f;
	g_shadowMapDebugOverlayState.slotU1 = 1.0f;
	g_shadowMapDebugOverlayState.slotV1 = 1.0f;
	if ( !pointLight && g_shadowMapRenderTexture != NULL ) {
		const float invAtlasWidth = 1.0f / Max( 1, g_shadowMapRenderTexture->GetWidth() );
		const float invAtlasHeight = 1.0f / Max( 1, g_shadowMapRenderTexture->GetHeight() );
		g_shadowMapDebugOverlayState.slotU0 = g_shadowMapActiveSlotOriginX * invAtlasWidth;
		g_shadowMapDebugOverlayState.slotV0 = g_shadowMapActiveSlotOriginY * invAtlasHeight;
		g_shadowMapDebugOverlayState.slotU1 = g_shadowMapDebugOverlayState.slotU0 + ( g_projectedShadowMapState.tileSize * g_projectedShadowMapState.atlasDiv ) * invAtlasWidth;
		g_shadowMapDebugOverlayState.slotV1 = g_shadowMapDebugOverlayState.slotV0 + ( g_projectedShadowMapState.tileSize * g_projectedShadowMapState.atlasDiv ) * invAtlasHeight;
	}
	g_shadowMapDebugOverlayState.casterCount =
		RB_CountDrawSurfChain( primaryCasters ) +
		RB_CountDrawSurfChain( secondaryCasters ) +
		RB_CountDrawSurfChain( tertiaryCasters ) +
		RB_CountDrawSurfChain( quaternaryCasters );
	g_shadowMapDebugOverlayState.alphaCasterCount = vLight != NULL ? vLight->shadowMapAlphaCasterCount : 0;
	g_shadowMapDebugOverlayState.translucentCasterCount = vLight != NULL ? vLight->shadowMapTranslucentCasterCount : 0;
	g_shadowMapDebugOverlayState.rejectedCasterCount = vLight != NULL ? vLight->shadowMapRejectedCasterCount : 0;
	g_shadowMapDebugOverlayState.expandedCasterCount = vLight != NULL ? vLight->shadowMapExpandedCasterCount : 0;
	g_shadowMapDebugOverlayState.shadowSurfCount =
		RB_CountDrawSurfChain( primaryShadowSurfs ) +
		RB_CountDrawSurfChain( secondaryShadowSurfs );
	g_shadowMapDebugOverlayState.interactionCount = RB_CountDrawSurfChain( interactions );
	g_shadowMapDebugOverlayState.cpuMilliseconds = g_shadowMapStats.cpuRenderMilliseconds
		+ g_shadowMapStats.cpuMaskMilliseconds
		+ g_shadowMapStats.cpuPhaseMilliseconds[SHADOWMAP_TIMING_RECEIVER_FALLBACK]
		+ g_shadowMapStats.cpuPhaseMilliseconds[SHADOWMAP_TIMING_STENCIL_FALLBACK];
	g_shadowMapDebugOverlayState.gpuMilliseconds = g_shadowMapStats.gpuSyncMilliseconds;
	g_shadowMapDebugOverlayState.worldTexelSize = g_shadowMapStats.maxWorldTexelSize;
}

static void RB_ShadowMapDebugOverlayEnsureFallbackState( void ) {
	if ( g_shadowMapDebugOverlayState.valid || g_shadowMapStats.supportedLights <= 0 ) {
		return;
	}

	const bool preferPointLight = ( g_shadowMapStats.pointLights > 0 ) &&
		( g_pointShadowMapColorImage != NULL ) &&
		( g_shadowMapStats.projectedLights == 0 || g_projectedShadowMapState.cascadeCount <= 0 );

	g_shadowMapDebugOverlayState.valid = true;
	g_shadowMapDebugOverlayState.pointLight = preferPointLight;
	g_shadowMapDebugOverlayState.globalPass = ( g_shadowMapStats.mappedGlobalPasses >= g_shadowMapStats.mappedLocalPasses );
	g_shadowMapDebugOverlayState.mapped = ( g_shadowMapStats.mappedLocalPasses + g_shadowMapStats.mappedGlobalPasses ) > 0;
	g_shadowMapDebugOverlayState.lightDefIndex = -1;
	g_shadowMapDebugOverlayState.tileCount = preferPointLight ? 6 : Max( 1, g_projectedShadowMapState.cascadeCount );
	g_shadowMapDebugOverlayState.atlasDiv = preferPointLight ? 3 : Max( 1, g_projectedShadowMapState.atlasDiv );
	g_shadowMapDebugOverlayState.cascadeCount = preferPointLight ? 1 : Max( 1, g_projectedShadowMapState.cascadeCount );
	g_shadowMapDebugOverlayState.casterCount = -1;
	g_shadowMapDebugOverlayState.alphaCasterCount = -1;
	g_shadowMapDebugOverlayState.translucentCasterCount = -1;
	g_shadowMapDebugOverlayState.rejectedCasterCount = -1;
	g_shadowMapDebugOverlayState.expandedCasterCount = -1;
	g_shadowMapDebugOverlayState.shadowSurfCount = -1;
	g_shadowMapDebugOverlayState.interactionCount = -1;
}

enum {
	SHADOW_DEBUG_OVERLAY_MODE_PANEL = 0,
	SHADOW_DEBUG_OVERLAY_MODE_SOLID = 1,
	SHADOW_DEBUG_OVERLAY_MODE_GLYPH = 2
};

static void RB_ShadowMapDebugOverlaySubmitQuad( const float x, const float y, const float w, const float h, const float s1, const float t1, const float s2, const float t2 ) {
	glBegin( GL_QUADS );
	glTexCoord2f( s1, t1 );
	glVertex2f( x, y );
	glTexCoord2f( s2, t1 );
	glVertex2f( x + w, y );
	glTexCoord2f( s2, t2 );
	glVertex2f( x + w, y + h );
	glTexCoord2f( s1, t2 );
	glVertex2f( x, y + h );
	glEnd();
}

static void RB_ShadowMapDebugOverlaySetMode( const float mode, const idVec4 &color, const float glyphCode ) {
	if ( g_shadowDebugOverlayProgram.mode >= 0 ) {
		glUniform1fARB( g_shadowDebugOverlayProgram.mode, mode );
	}
	if ( g_shadowDebugOverlayProgram.color >= 0 ) {
		glUniform4fvARB( g_shadowDebugOverlayProgram.color, 1, color.ToFloatPtr() );
	}
	if ( g_shadowDebugOverlayProgram.glyphCode >= 0 ) {
		glUniform1fARB( g_shadowDebugOverlayProgram.glyphCode, glyphCode );
	}
}

static void RB_ShadowMapDebugOverlayDrawString( const float x, const float y, const float scale, const idVec4 &color, const char *text ) {
	if ( text == NULL || text[0] == '\0' ) {
		return;
	}

	const float glyphW = 6.0f * scale;
	const float glyphH = 9.0f * scale;
	const float advance = glyphW + scale;

	idVec4 shadowColor( 0.0f, 0.0f, 0.0f, color[3] * 0.85f );
	for ( int pass = 0; pass < 2; pass++ ) {
		const idVec4 &passColor = ( pass == 0 ) ? shadowColor : color;
		const float dx = ( pass == 0 ) ? 1.0f : 0.0f;
		const float dy = ( pass == 0 ) ? 1.0f : 0.0f;
		float drawX = x + dx;
		for ( const char *c = text; *c != '\0'; c++ ) {
			RB_ShadowMapDebugOverlaySetMode( SHADOW_DEBUG_OVERLAY_MODE_GLYPH, passColor, static_cast<float>( static_cast<unsigned char>( *c ) ) );
			RB_ShadowMapDebugOverlaySubmitQuad( drawX, y + dy, glyphW, glyphH, 0.0f, 0.0f, 1.0f, 1.0f );
			drawX += advance;
		}
	}
}

static void RB_ShadowMapDebugOverlayDrawLabels( const float panelX, const float panelY, const float panelW, const float panelH ) {
	const idVec4 labelColor( 0.95f, 0.95f, 0.95f, 0.95f );
	char label[8];

	if ( g_shadowMapDebugOverlayState.pointLight ) {
		const float tileW = panelW / 3.0f;
		const float tileH = panelH / 2.0f;
		for ( int faceIndex = 0; faceIndex < 6; faceIndex++ ) {
			const int col = faceIndex % 3;
			const int row = faceIndex / 3;
			idStr::snPrintf( label, sizeof( label ), "%d", faceIndex );
			RB_ShadowMapDebugOverlayDrawString( panelX + col * tileW + 4.0f, panelY + row * tileH + 4.0f, 0.9f, labelColor, label );
		}
		return;
	}

	const int atlasDiv = Max( 1, g_shadowMapDebugOverlayState.atlasDiv );
	const float tileW = panelW / atlasDiv;
	const float tileH = panelH / atlasDiv;
	for ( int cascadeIndex = 0; cascadeIndex < g_shadowMapDebugOverlayState.tileCount; cascadeIndex++ ) {
		const int col = cascadeIndex % atlasDiv;
		const int row = cascadeIndex / atlasDiv;
		idStr::snPrintf( label, sizeof( label ), "%d", cascadeIndex );
		RB_ShadowMapDebugOverlayDrawString( panelX + col * tileW + 4.0f, panelY + row * tileH + 4.0f, 0.9f, labelColor, label );
	}
}

static void RB_ShadowMapDebugOverlayFormatLine( char *dest, int destSize, const char *fmt, ... ) {
	va_list argptr;
	va_start( argptr, fmt );
	idStr::vsnPrintf( dest, destSize, fmt, argptr );
	va_end( argptr );
}

static void RB_ShadowMapDebugOverlayDraw( void ) {
	if ( !RB_ShadowMapDebugOverlayEnabled() || !RB_ShadowMapDebugOverlayLoadProgram() ) {
		return;
	}

	RB_ShadowMapDebugOverlayEnsureFallbackState();

	const GLboolean depthWasEnabled = glIsEnabled( GL_DEPTH_TEST );
	const GLboolean blendWasEnabled = glIsEnabled( GL_BLEND );
	const GLboolean stencilWasEnabled = glIsEnabled( GL_STENCIL_TEST );
	const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
	GLint viewport[4] = { 0, 0, 0, 0 };
	GLint scissorBox[4] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, viewport );
	glGetIntegerv( GL_SCISSOR_BOX, scissorBox );

	const int savedFaceCulling = backEnd.glState.faceCulling;

	glViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	if ( scissorWasEnabled ) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	}

	GL_State( GLS_DEPTHFUNC_ALWAYS | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	GL_Cull( CT_TWO_SIDED );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( g_shadowDebugOverlayProgram.programObject );

	if ( g_shadowDebugOverlayProgram.screenSize >= 0 ) {
		const float screenSize[2] = { static_cast<float>( glConfig.vidWidth ), static_cast<float>( glConfig.vidHeight ) };
		glUniform2fvARB( g_shadowDebugOverlayProgram.screenSize, 1, screenSize );
	}
	if ( g_shadowDebugOverlayProgram.pointLight >= 0 ) {
		glUniform1fARB( g_shadowDebugOverlayProgram.pointLight, g_shadowMapDebugOverlayState.pointLight ? 1.0f : 0.0f );
	}
	if ( g_shadowDebugOverlayProgram.atlasDiv >= 0 ) {
		glUniform1fARB( g_shadowDebugOverlayProgram.atlasDiv, static_cast<float>( g_shadowMapDebugOverlayState.pointLight ? 3 : Max( 1, g_shadowMapDebugOverlayState.atlasDiv ) ) );
	}
	if ( g_shadowDebugOverlayProgram.cascadeCount >= 0 ) {
		glUniform1fARB( g_shadowDebugOverlayProgram.cascadeCount, static_cast<float>( Max( 1, g_shadowMapDebugOverlayState.tileCount ) ) );
	}
	if ( g_shadowDebugOverlayProgram.passMapped >= 0 ) {
		glUniform1fARB( g_shadowDebugOverlayProgram.passMapped, g_shadowMapDebugOverlayState.mapped ? 1.0f : 0.0f );
	}
	if ( g_shadowDebugOverlayProgram.pointShadowDepthMode >= 0 ) {
		glUniform1fARB( g_shadowDebugOverlayProgram.pointShadowDepthMode, RB_PointShadowMapDepthModeValue() );
	}

	GL_SelectTextureNoClient( 0 );
	if ( g_shadowMapDepthImage != NULL ) {
		g_shadowMapDepthImage->Bind();
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	} else {
		globalImages->BindNull();
	}
	if ( g_shadowDebugOverlayProgram.shadowAtlasMap >= 0 ) {
		glUniform1iARB( g_shadowDebugOverlayProgram.shadowAtlasMap, 0 );
	}

	GL_SelectTextureNoClient( 1 );
	if ( g_pointShadowMapColorImage != NULL ) {
		g_pointShadowMapColorImage->Bind();
	} else {
		globalImages->BindNull();
	}
	if ( g_shadowDebugOverlayProgram.pointShadowMap >= 0 ) {
		glUniform1iARB( g_shadowDebugOverlayProgram.pointShadowMap, 1 );
	}
	GL_SelectTextureNoClient( 0 );

	const float margin = 8.0f;
	const float panelW = 192.0f;
	const float panelH = g_shadowMapDebugOverlayState.pointLight ? 128.0f : 192.0f;
	const float statsH = 52.0f;
	const float outerW = panelW + 12.0f;
	const float outerH = panelH + statsH + 18.0f;
	const float panelX = margin + 6.0f;
	const float panelY = margin + 6.0f;
	const float statsY = panelY + panelH + 8.0f;
	const idVec4 outerColor( 0.02f, 0.03f, 0.04f, 0.78f );
	const idVec4 panelFrameColor = g_shadowMapDebugOverlayState.pointLight ?
		idVec4( 0.92f, 0.70f, 0.18f, 0.95f ) :
		idVec4( 0.15f, 0.78f, 0.95f, 0.95f );
	const idVec4 failColor( 0.85f, 0.24f, 0.20f, 0.95f );
	const idVec4 textColor( 0.96f, 0.96f, 0.92f, 0.98f );

	RB_ShadowMapDebugOverlaySetMode( SHADOW_DEBUG_OVERLAY_MODE_SOLID, outerColor, 0.0f );
	RB_ShadowMapDebugOverlaySubmitQuad( margin, margin, outerW, outerH, 0.0f, 0.0f, 1.0f, 1.0f );

	RB_ShadowMapDebugOverlaySetMode( SHADOW_DEBUG_OVERLAY_MODE_SOLID, g_shadowMapDebugOverlayState.mapped ? panelFrameColor : failColor, 0.0f );
	RB_ShadowMapDebugOverlaySubmitQuad( panelX - 2.0f, panelY - 2.0f, panelW + 4.0f, panelH + 4.0f, 0.0f, 0.0f, 1.0f, 1.0f );

	if ( g_shadowMapDebugOverlayState.valid ) {
		RB_ShadowMapDebugOverlaySetMode( SHADOW_DEBUG_OVERLAY_MODE_PANEL, idVec4( 1.0f, 1.0f, 1.0f, 1.0f ), 0.0f );
		RB_ShadowMapDebugOverlaySubmitQuad( panelX, panelY, panelW, panelH,
			g_shadowMapDebugOverlayState.slotU0, g_shadowMapDebugOverlayState.slotV0,
			g_shadowMapDebugOverlayState.slotU1, g_shadowMapDebugOverlayState.slotV1 );
		RB_ShadowMapDebugOverlayDrawLabels( panelX, panelY, panelW, panelH );
	}

	char line1[128];
	char line2[128];
	char line3[128];
	char line4[128];
	if ( g_shadowMapDebugOverlayState.valid ) {
		const char *lightLabel = ( g_shadowMapDebugOverlayState.lightDefIndex >= 0 ) ?
			va( "L%d", g_shadowMapDebugOverlayState.lightDefIndex ) : "L?";
		RB_ShadowMapDebugOverlayFormatLine( line1, sizeof( line1 ), "%s %c %s %c%d %s",
			g_shadowMapDebugOverlayState.pointLight ? "POINT" : "PROJ",
			g_shadowMapDebugOverlayState.globalPass ? 'G' : 'L',
			lightLabel,
			g_shadowMapDebugOverlayState.pointLight ? 'F' : 'C',
			g_shadowMapDebugOverlayState.pointLight ? 6 : g_shadowMapDebugOverlayState.cascadeCount,
			g_shadowMapDebugOverlayState.mapped ? "MAP" : "FB" );
		RB_ShadowMapDebugOverlayFormatLine( line2, sizeof( line2 ), "CAST %s SURF %s INTR %s",
			( g_shadowMapDebugOverlayState.casterCount >= 0 ) ? va( "%d", g_shadowMapDebugOverlayState.casterCount ) : "?",
			( g_shadowMapDebugOverlayState.shadowSurfCount >= 0 ) ? va( "%d", g_shadowMapDebugOverlayState.shadowSurfCount ) : "?",
			( g_shadowMapDebugOverlayState.interactionCount >= 0 ) ? va( "%d", g_shadowMapDebugOverlayState.interactionCount ) : "?" );
		idStr::snPrintf( line4, sizeof( line4 ), "A %s T %s R %s E %s",
			( g_shadowMapDebugOverlayState.alphaCasterCount >= 0 ) ? va( "%d", g_shadowMapDebugOverlayState.alphaCasterCount ) : "?",
			( g_shadowMapDebugOverlayState.translucentCasterCount >= 0 ) ? va( "%d", g_shadowMapDebugOverlayState.translucentCasterCount ) : "?",
			( g_shadowMapDebugOverlayState.rejectedCasterCount >= 0 ) ? va( "%d", g_shadowMapDebugOverlayState.rejectedCasterCount ) : "?",
			( g_shadowMapDebugOverlayState.expandedCasterCount >= 0 ) ? va( "%d", g_shadowMapDebugOverlayState.expandedCasterCount ) : "?" );
	} else {
		idStr::Copynz( line1, "NO MAP", sizeof( line1 ) );
		RB_ShadowMapDebugOverlayFormatLine( line2, sizeof( line2 ), "SUP %d/%d",
			g_shadowMapStats.supportedLights,
			g_shadowMapStats.totalLights );
		idStr::snPrintf( line4, sizeof( line4 ), "NO CAST" );
	}
	RB_ShadowMapDebugOverlayFormatLine( line3, sizeof( line3 ), "SUP %d/%d MAP %d/%d FB %d/%d",
		g_shadowMapStats.supportedLights,
		g_shadowMapStats.totalLights,
		g_shadowMapStats.mappedLocalPasses,
		g_shadowMapStats.mappedGlobalPasses,
		g_shadowMapStats.fallbackLocalPasses,
		g_shadowMapStats.fallbackGlobalPasses );

	RB_ShadowMapDebugOverlayDrawString( panelX, statsY, 0.9f, textColor, line1 );
	RB_ShadowMapDebugOverlayDrawString( panelX, statsY + 10.0f, 0.8f, textColor, line2 );
	RB_ShadowMapDebugOverlayDrawString( panelX, statsY + 20.0f, 0.8f, textColor, line3 );
	RB_ShadowMapDebugOverlayDrawString( panelX, statsY + 30.0f, 0.8f, textColor, line4 );

	glUseProgramObjectARB( 0 );
	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 0 );
	globalImages->BindNull();

	if ( depthWasEnabled ) {
		glEnable( GL_DEPTH_TEST );
	} else {
		glDisable( GL_DEPTH_TEST );
	}
	if ( blendWasEnabled ) {
		glEnable( GL_BLEND );
	} else {
		glDisable( GL_BLEND );
	}
	if ( stencilWasEnabled ) {
		glEnable( GL_STENCIL_TEST );
	} else {
		glDisable( GL_STENCIL_TEST );
	}
	if ( scissorWasEnabled ) {
		glScissor( scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3] );
	} else {
		glDisable( GL_SCISSOR_TEST );
	}
	glViewport( viewport[0], viewport[1], viewport[2], viewport[3] );

	backEnd.glState.faceCulling = -1;
	if ( savedFaceCulling >= CT_FRONT_SIDED && savedFaceCulling <= CT_TWO_SIDED ) {
		GL_Cull( savedFaceCulling );
	}

	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
}

static void RB_ShadowMapTrackWrappedCustomGLSLReceivers( const viewLight_t *vLight, const shadowMapPassKind_t passKind, const drawSurf_t *interactions ) {
	const int wrappedSurfaces = RB_CountShadowMapReceiverWrappedCustomGLSLSurfaces( interactions );
	if ( wrappedSurfaces <= 0 ) {
		return;
	}

	if ( passKind == SHADOWMAP_PASS_LOCAL ) {
		g_shadowMapStats.receiverWrappedCustomGLSLLocalPasses++;
	} else {
		g_shadowMapStats.receiverWrappedCustomGLSLGlobalPasses++;
	}
	g_shadowMapStats.receiverWrappedCustomGLSLSurfaces += wrappedSurfaces;

	if ( RB_ShadowMapVerboseReport() ) {
		common->Printf(
			"SM receiver-wrapper %s[%d] type=%s customGLSL=%d path=stock-shadowmap-interaction\n",
			RB_ShadowMapPassName( passKind ),
			RB_ShadowMapLightIndex( vLight ),
			( vLight != NULL && vLight->pointLight ) ? "point" : "projected",
			wrappedSurfaces );
	}
}

static void RB_ShadowMapDrawReceiverFallbacks( const viewLight_t *vLight, const shadowMapPassKind_t passKind, const drawSurf_t *primaryShadowSurfs, const drawSurf_t *secondaryShadowSurfs, const drawSurf_t *interactions, const bool drawInteractions = true ) {
	int receiverFallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_COUNT];
	const int receiverFallbackSurfaces = RB_CountShadowMapReceiverFallbackSurfaces( interactions, receiverFallbackReasons );
	if ( receiverFallbackSurfaces <= 0 ) {
		return;
	}

	// Fallback receivers are shadowed by the light's stencil volumes even on
	// mapped frames. If the front end elided those volumes, restore them from
	// the next frame on so these receivers do not stay unshadowed.
	if ( primaryShadowSurfs == NULL && secondaryShadowSurfs == NULL ) {
		RB_ShadowMapMarkStencilFallbackSticky( vLight );
	}

	if ( passKind == SHADOWMAP_PASS_LOCAL ) {
		g_shadowMapStats.receiverFallbackLocalPasses++;
	} else {
		g_shadowMapStats.receiverFallbackGlobalPasses++;
	}
	for ( int i = 0; i < SHADOWMAP_RECEIVER_FALLBACK_COUNT; i++ ) {
		g_shadowMapStats.receiverFallbackSurfaces[i] += receiverFallbackReasons[i];
	}
	if ( RB_ShadowMapVerboseReport() ) {
		common->Printf(
			"SM receiver-fallback %s[%d] type=%s receivers=%d %s=%d %s=%d %s=%d path=stencil-filtered\n",
			RB_ShadowMapPassName( passKind ),
			RB_ShadowMapLightIndex( vLight ),
			( vLight != NULL && vLight->pointLight ) ? "point" : "projected",
			receiverFallbackSurfaces,
			RB_ShadowMapReceiverFallbackReasonName( SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE ),
			receiverFallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_INVALID_SURFACE],
			RB_ShadowMapReceiverFallbackReasonName( SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL ),
			receiverFallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_CUSTOM_GLSL],
			RB_ShadowMapReceiverFallbackReasonName( SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY ),
			receiverFallbackReasons[SHADOWMAP_RECEIVER_FALLBACK_GENERATED_GEOMETRY] );
	}
	if ( !drawInteractions ) {
		return;
	}
	shadowMapTimedPhase_t timedPhase;
	RB_ShadowMapBeginTimedPhase( timedPhase, vLight, passKind, SHADOWMAP_TIMING_RECEIVER_FALLBACK );
	RB_ShadowMapStencilFallbackFiltered( primaryShadowSurfs, secondaryShadowSurfs, interactions, RB_SurfaceNeedsShadowMapReceiverFallback );
	RB_ShadowMapEndTimedPhase( timedPhase );
}

static int RB_ShadowMapReceiverMaskForPass(
		const shadowMapPassKind_t passKind ) {
	return passKind == SHADOWMAP_PASS_LOCAL
		? SHADOWMAP_RECEIVER_MASK_LOCAL : SHADOWMAP_RECEIVER_MASK_GLOBAL;
}

static bool RB_ShadowMapHybridAvailableForPass( const viewLight_t *vLight,
		const shadowMapPassKind_t passKind ) {
	if ( vLight == NULL ) {
		return false;
	}
	const int receiverMask = RB_ShadowMapReceiverMaskForPass( passKind );
	const int incompleteMapMask = vLight->shadowMapIncompleteMapMask
		| vLight->shadowMapPrelightMapMissingMask;
	if ( ( incompleteMapMask & receiverMask ) == 0 ) {
		return false;
	}
	// A combined prelight volume cannot supplement a partial map without also
	// restamping mapped casters. Ordinary per-surface supplements can.
	if ( ( vLight->shadowMapPrelightMapMissingMask & receiverMask ) != 0
			|| ( vLight->shadowMapHybridIncompleteMask & receiverMask ) != 0 ) {
		return false;
	}
	return vLight->globalShadowMapStencilSupplements != NULL
		|| ( passKind == SHADOWMAP_PASS_GLOBAL
			&& vLight->localShadowMapStencilSupplements != NULL );
}

static bool RB_ShadowMapMapOrHybridAvailableForPass(
		const viewLight_t *vLight, const shadowMapPassKind_t passKind,
		bool *hybridOut ) {
	if ( hybridOut != NULL ) {
		*hybridOut = false;
	}
	if ( vLight == NULL ) {
		return false;
	}
	const int receiverMask = RB_ShadowMapReceiverMaskForPass( passKind );
	const bool mapIncomplete = ( ( vLight->shadowMapIncompleteMapMask
		| vLight->shadowMapPrelightMapMissingMask ) & receiverMask ) != 0;
	if ( !mapIncomplete ) {
		return true;
	}
	const bool hybrid = RB_ShadowMapHybridAvailableForPass(
		vLight, passKind );
	if ( hybridOut != NULL ) {
		*hybridOut = hybrid;
	}
	return hybrid;
}

static bool RB_ShadowMapPrepareMappedReceiverStencil(
		const viewLight_t *vLight, const shadowMapPassKind_t passKind,
		const bool hybrid ) {
	if ( !hybrid ) {
		glStencilFunc( GL_ALWAYS, 128, 255 );
		return true;
	}
	const drawSurf_t *globalSupplements =
		vLight != NULL ? vLight->globalShadowMapStencilSupplements : NULL;
	const drawSurf_t *localSupplements =
		( vLight != NULL && passKind == SHADOWMAP_PASS_GLOBAL )
			? vLight->localShadowMapStencilSupplements : NULL;
	if ( globalSupplements == NULL && localSupplements == NULL ) {
		return false;
	}

	backEnd.currentScissor = vLight->scissorRect;
	if ( r_useScissor.GetBool() ) {
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}
	glClear( GL_STENCIL_BUFFER_BIT );
	const bool useShadowVertexProgram = r_useShadowVertexProgram.GetBool()
		&& R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW,
			"shadow-map hybrid stencil vertex program", true );
	if ( useShadowVertexProgram ) {
		glEnable( GL_VERTEX_PROGRAM_ARB );
	}
	RB_StencilShadowPass( globalSupplements );
	RB_StencilShadowPass( localSupplements );
	if ( useShadowVertexProgram ) {
		glDisable( GL_VERTEX_PROGRAM_ARB );
	}
	glStencilFunc( GL_GEQUAL, 128, 255 );
	return true;
}

static void RB_ShadowMapRunPass( const viewLight_t *vLight, shadowMapPassKind_t passKind, bool pointLight, const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *tertiaryCasters, const drawSurf_t *quaternaryCasters, const drawSurf_t *primaryShadowSurfs, const drawSurf_t *secondaryShadowSurfs, const drawSurf_t *interactions, const bool deferReceiverDraw = false ) {
	if ( interactions == NULL ) {
		return;
	}

	if ( pointLight ) {
		g_pointTranslucentShadowPassReady = false;
	} else {
		g_projectedTranslucentShadowPassReady = false;
	}

	const drawSurf_t *translucentPrimaryCasters = vLight->globalTranslucentShadowMapCasters;
	const drawSurf_t *translucentSecondaryCasters = ( passKind == SHADOWMAP_PASS_GLOBAL ) ? vLight->localTranslucentShadowMapCasters : NULL;
	const bool haveOpaqueCasters = primaryCasters != NULL || secondaryCasters != NULL || tertiaryCasters != NULL || quaternaryCasters != NULL;
	const bool haveTranslucentCasters = translucentPrimaryCasters != NULL || translucentSecondaryCasters != NULL;
	const bool haveStencilShadowSurfs = primaryShadowSurfs != NULL || secondaryShadowSurfs != NULL;

	if ( !haveOpaqueCasters && !haveStencilShadowSurfs && !haveTranslucentCasters ) {
		// The front end may have elided stencil volumes this light could still
		// need (e.g. its only casters are translucent/forceShadows materials the
		// map path cannot draw). Restore volume generation so the next frame can
		// take the stencil-only path instead of rendering unshadowed.
		if ( vLight != NULL && R_ShadowMapLightWillUseShadowMaps( vLight->lightDef ) ) {
			RB_ShadowMapMarkStencilFallbackSticky( vLight );
		}
		if ( passKind == SHADOWMAP_PASS_LOCAL ) {
			g_shadowMapStats.unshadowedLocalPasses++;
		} else {
			g_shadowMapStats.unshadowedGlobalPasses++;
		}
		RB_ShadowMapPassReport( vLight, passKind, pointLight, SHADOWMAP_PASS_RESULT_NO_SHADOW_SURFS, primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		if ( !deferReceiverDraw ) {
			glStencilFunc( GL_ALWAYS, 128, 255 );
			RB_DrawMaterialInteractions( interactions );
		}
		return;
	}

	if ( !haveOpaqueCasters && !haveTranslucentCasters ) {
		const shadowMapPassResult_t passResult = SHADOWMAP_PASS_RESULT_STENCIL_ONLY;
		if ( passKind == SHADOWMAP_PASS_LOCAL ) {
			g_shadowMapStats.fallbackLocalPasses++;
		} else {
			g_shadowMapStats.fallbackGlobalPasses++;
		}
		RB_ShadowMapDebugOverlayCapture( vLight, passKind, pointLight, false, false,
			primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters,
			primaryShadowSurfs, secondaryShadowSurfs, interactions );
		RB_ShadowMapPassReport( vLight, passKind, pointLight, passResult, primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		RB_ShadowMapMarkStencilFallbackSticky( vLight );
		if ( !deferReceiverDraw ) {
			RB_ShadowMapStencilFallback( vLight, passKind, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		}
		return;
	}

	if ( !RB_DrawSurfChainHasFilteredSurface( interactions, RB_SurfaceEligibleForShadowMapReceiver ) ) {
		RB_ShadowMapDrawReceiverFallbacks( vLight, passKind, primaryShadowSurfs, secondaryShadowSurfs, interactions, !deferReceiverDraw );
		RB_ShadowMapPassReport( vLight, passKind, pointLight, SHADOWMAP_PASS_RESULT_RECEIVER_FALLBACK, primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		return;
	}

	bool hybrid = false;
	const bool mapOrHybridAvailable =
		RB_ShadowMapMapOrHybridAvailableForPass(
			vLight, passKind, &hybrid );
	if ( passKind == SHADOWMAP_PASS_GLOBAL ) {
		g_shadowMapGlobalPassHybrid = hybrid;
	}
	if ( !mapOrHybridAvailable ) {
		// This ownership cannot be represented by a map alone, and its exact
		// supplement set is unavailable. Preserve full-stencil behavior rather
		// than silently accepting a partial mapped shadow.
		if ( passKind == SHADOWMAP_PASS_LOCAL ) {
			g_shadowMapStats.fallbackLocalPasses++;
		} else {
			g_shadowMapStats.fallbackGlobalPasses++;
		}
		RB_ShadowMapPassReport( vLight, passKind, pointLight,
			SHADOWMAP_PASS_RESULT_STENCIL_ONLY, primaryCasters,
			secondaryCasters, tertiaryCasters, quaternaryCasters,
			primaryShadowSurfs, secondaryShadowSurfs, interactions );
		RB_ShadowMapMarkStencilFallbackSticky( vLight );
		if ( !deferReceiverDraw ) {
			RB_ShadowMapStencilFallback( vLight, passKind, primaryShadowSurfs,
				secondaryShadowSurfs, interactions );
		}
		return;
	}

	shadowMapSchedule_t schedule = RB_ShadowMapSchedulePass( vLight, passKind, pointLight, haveTranslucentCasters );

	// cached projected lights with dynamic casters compose: blit the cached
	// static tiles over scratch and draw only the dynamics on top
	const bool composeDynamics = !pointLight
		&& ( tertiaryCasters != NULL || quaternaryCasters != NULL )
		&& schedule.projectedEntry != NULL;

	if ( schedule.action == SHADOWMAP_SCHEDULE_REUSE ) {
		if ( composeDynamics ) {
			shadowMapTimedPhase_t composeTimer;
			RB_ShadowMapBeginTimedPhase( composeTimer, vLight, passKind, SHADOWMAP_TIMING_MAP_RENDER );
			RB_ShadowMapSelectProjectedScratchResources();
			const bool composeOk = RB_RenderShadowMap( primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters,
				SHADOWMAP_RENDER_COMPOSE_DYNAMIC, schedule.projectedEntry );
			g_shadowMapStats.cpuRenderMilliseconds += RB_ShadowMapEndTimedPhase( composeTimer );
			if ( !composeOk ) {
				if ( passKind == SHADOWMAP_PASS_LOCAL ) {
					g_shadowMapStats.fallbackLocalPasses++;
					g_shadowMapStats.renderFailLocalPasses++;
				} else {
					g_shadowMapStats.fallbackGlobalPasses++;
					g_shadowMapStats.renderFailGlobalPasses++;
				}
				RB_ShadowMapPassReport( vLight, passKind, pointLight, SHADOWMAP_PASS_RESULT_RENDER_FAIL, primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs, interactions );
				RB_ShadowMapMarkStencilFallbackSticky( vLight );
				if ( !deferReceiverDraw ) {
					RB_ShadowMapStencilFallback( vLight, passKind, primaryShadowSurfs, secondaryShadowSurfs, interactions );
				}
				return;
			}
		}
		if ( pointLight && schedule.pointEntry != NULL ) {
			schedule.pointEntry->lastUsedFrame = tr.frameCount;
		} else if ( schedule.projectedEntry != NULL ) {
			schedule.projectedEntry->lastUsedFrame = tr.frameCount;
		}
		if ( deferReceiverDraw ) {
			g_shadowMapGlobalPassMapped = pointLight
				? SHADOWMAP_GLOBAL_MAPPED_POINT
				: SHADOWMAP_GLOBAL_MAPPED_PROJECTED;
			g_shadowMapDeferredGlobalReportPending = true;
			g_shadowMapDeferredGlobalPassResult =
				SHADOWMAP_PASS_RESULT_CACHE_REUSE;
			return;
		}
		shadowMapTimedPhase_t cacheReuseTimer;
		RB_ShadowMapBeginTimedPhase( cacheReuseTimer, vLight, passKind, SHADOWMAP_TIMING_CACHE_REUSE );
		const bool receiverStencilReady =
			RB_ShadowMapPrepareMappedReceiverStencil(
				vLight, passKind, hybrid );
		const bool maskOk = receiverStencilReady
			&& ( pointLight
				? RB_GLSLPointShadowMap_CreateDrawInteractions( interactions )
				: RB_GLSLShadowMap_CreateDrawInteractions( interactions ) );
		const float cacheReuseMilliseconds = RB_ShadowMapEndTimedPhase( cacheReuseTimer );
		g_shadowMapStats.cpuMaskMilliseconds += cacheReuseMilliseconds;
		if ( maskOk ) {
			if ( passKind == SHADOWMAP_PASS_LOCAL ) {
				g_shadowMapStats.mappedLocalPasses++;
			} else {
				g_shadowMapStats.mappedGlobalPasses++;
			}
			RB_ShadowMapDebugOverlayCapture( vLight, passKind, pointLight, true, true,
				primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters,
				primaryShadowSurfs, secondaryShadowSurfs, interactions );
			RB_ShadowMapTrackWrappedCustomGLSLReceivers( vLight, passKind, interactions );
			RB_ShadowMapDrawReceiverFallbacks( vLight, passKind, primaryShadowSurfs, secondaryShadowSurfs, interactions );
			if ( g_shadowMapMaskPrepareFailures > 0 ) {
				// light only the surfaces the mask pass skipped; everything
				// else was already drawn additively by the mapped pass
				RB_ShadowMapStencilFallbackFiltered( primaryShadowSurfs, secondaryShadowSurfs, interactions, RB_SurfaceShadowMapReceiverPrepareFailed );
			}
			RB_ShadowMapPassReport( vLight, passKind, pointLight, SHADOWMAP_PASS_RESULT_CACHE_REUSE, primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs, interactions );
			return;
		}
		if ( passKind == SHADOWMAP_PASS_LOCAL ) {
			g_shadowMapStats.fallbackLocalPasses++;
			g_shadowMapStats.maskFailLocalPasses++;
		} else {
			g_shadowMapStats.fallbackGlobalPasses++;
			g_shadowMapStats.maskFailGlobalPasses++;
		}
		RB_ShadowMapPassReport( vLight, passKind, pointLight, SHADOWMAP_PASS_RESULT_MASK_FAIL, primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		RB_ShadowMapMarkStencilFallbackSticky( vLight );
		if ( !deferReceiverDraw ) {
			RB_ShadowMapStencilFallback( vLight, passKind, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		}
		return;
	}

	if ( schedule.action == SHADOWMAP_SCHEDULE_FALLBACK ) {
		g_shadowMapStats.scheduledSkipPasses++;
		if ( passKind == SHADOWMAP_PASS_LOCAL ) {
			g_shadowMapStats.fallbackLocalPasses++;
		} else {
			g_shadowMapStats.fallbackGlobalPasses++;
		}
		RB_ShadowMapPassReport( vLight, passKind, pointLight, SHADOWMAP_PASS_RESULT_SCHEDULED_SKIP, primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		RB_ShadowMapMarkStencilFallbackSticky( vLight );
		if ( !deferReceiverDraw ) {
			RB_ShadowMapStencilFallback( vLight, passKind, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		}
		return;
	}

	// when a cacheable projected light has dynamic casters, keep the cached
	// atlas tiles static-only and layer the dynamics onto scratch afterwards
	const bool composePlanned = composeDynamics && schedule.cacheable;

	shadowMapTimedPhase_t renderTimer;
	RB_ShadowMapBeginTimedPhase( renderTimer, vLight, passKind, SHADOWMAP_TIMING_MAP_RENDER );
	bool renderOk = pointLight ? RB_RenderPointShadowMap( primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters ) : RB_RenderShadowMap( primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters,
		composePlanned ? SHADOWMAP_RENDER_STATIC_ONLY : SHADOWMAP_RENDER_FULL );
	const float renderMilliseconds = RB_ShadowMapEndTimedPhase( renderTimer );
	g_shadowMapStats.cpuRenderMilliseconds += renderMilliseconds;
	RB_ShadowMapCompleteCacheUpdate( schedule, vLight, passKind, pointLight, renderOk );
	if ( composePlanned && renderOk ) {
		shadowMapTimedPhase_t composeTimer;
		RB_ShadowMapBeginTimedPhase( composeTimer, vLight, passKind, SHADOWMAP_TIMING_MAP_RENDER );
		RB_ShadowMapSelectProjectedScratchResources();
		renderOk = RB_RenderShadowMap( primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters,
			SHADOWMAP_RENDER_COMPOSE_DYNAMIC, schedule.projectedEntry );
		g_shadowMapStats.cpuRenderMilliseconds += RB_ShadowMapEndTimedPhase( composeTimer );
	}
	if ( renderOk && haveTranslucentCasters && RB_TranslucentShadowMomentsRequested() ) {
		if ( pointLight ) {
			RB_RenderPointTranslucentShadowMap( translucentPrimaryCasters, translucentSecondaryCasters );
		} else {
			RB_RenderTranslucentShadowMap( translucentPrimaryCasters, translucentSecondaryCasters );
		}
	}
	bool maskOk = false;
	if ( renderOk ) {
		if ( deferReceiverDraw ) {
			// Blended receivers stay in their ordered translucent phase. That
			// phase prepares any stencil supplement and consumes this map once.
			maskOk = true;
		} else {
			shadowMapTimedPhase_t maskTimer;
			RB_ShadowMapBeginTimedPhase( maskTimer, vLight, passKind, SHADOWMAP_TIMING_MASK_PASS );
			const bool receiverStencilReady =
				RB_ShadowMapPrepareMappedReceiverStencil(
					vLight, passKind, hybrid );
			maskOk = receiverStencilReady
				&& ( pointLight
					? RB_GLSLPointShadowMap_CreateDrawInteractions( interactions )
					: RB_GLSLShadowMap_CreateDrawInteractions( interactions ) );
			const float maskMilliseconds = RB_ShadowMapEndTimedPhase( maskTimer );
			g_shadowMapStats.cpuMaskMilliseconds += maskMilliseconds;
		}
	}
	shadowMapPassResult_t passResult = SHADOWMAP_PASS_RESULT_MAPPED;
	if ( !renderOk ) {
		passResult = SHADOWMAP_PASS_RESULT_RENDER_FAIL;
	} else if ( !maskOk ) {
		passResult = SHADOWMAP_PASS_RESULT_MASK_FAIL;
	}
	const bool mapped = ( passResult == SHADOWMAP_PASS_RESULT_MAPPED );

	if ( mapped ) {
		if ( deferReceiverDraw ) {
			g_shadowMapGlobalPassMapped = pointLight
				? SHADOWMAP_GLOBAL_MAPPED_POINT
				: SHADOWMAP_GLOBAL_MAPPED_PROJECTED;
			g_shadowMapDeferredGlobalReportPending = true;
			g_shadowMapDeferredGlobalPassResult =
				SHADOWMAP_PASS_RESULT_MAPPED;
			return;
		}
		RB_ShadowMapDebugOverlayCapture( vLight, passKind, pointLight, true,
			renderOk, primaryCasters, secondaryCasters, tertiaryCasters,
			quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs,
			interactions );
		if ( passKind == SHADOWMAP_PASS_LOCAL ) {
			g_shadowMapStats.mappedLocalPasses++;
		} else {
			g_shadowMapStats.mappedGlobalPasses++;
		}
		RB_ShadowMapTrackWrappedCustomGLSLReceivers( vLight, passKind, interactions );
		RB_ShadowMapDrawReceiverFallbacks( vLight, passKind, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		if ( g_shadowMapMaskPrepareFailures > 0 ) {
			// light only the surfaces the mask pass skipped; everything else
			// was already drawn additively by the mapped pass
			RB_ShadowMapStencilFallbackFiltered( primaryShadowSurfs, secondaryShadowSurfs, interactions, RB_SurfaceShadowMapReceiverPrepareFailed );
		}
		RB_ShadowMapPassReport( vLight, passKind, pointLight, passResult, primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		return;
	}

	RB_ShadowMapDebugOverlayCapture( vLight, passKind, pointLight, false,
		renderOk, primaryCasters, secondaryCasters, tertiaryCasters,
		quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs,
		interactions );

	if ( passKind == SHADOWMAP_PASS_LOCAL ) {
		g_shadowMapStats.fallbackLocalPasses++;
		if ( passResult == SHADOWMAP_PASS_RESULT_RENDER_FAIL ) {
			g_shadowMapStats.renderFailLocalPasses++;
		} else if ( passResult == SHADOWMAP_PASS_RESULT_MASK_FAIL ) {
			g_shadowMapStats.maskFailLocalPasses++;
		}
	} else {
		g_shadowMapStats.fallbackGlobalPasses++;
		if ( passResult == SHADOWMAP_PASS_RESULT_RENDER_FAIL ) {
			g_shadowMapStats.renderFailGlobalPasses++;
		} else if ( passResult == SHADOWMAP_PASS_RESULT_MASK_FAIL ) {
			g_shadowMapStats.maskFailGlobalPasses++;
		}
	}
	RB_ShadowMapPassReport( vLight, passKind, pointLight, passResult, primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs, interactions );
	RB_ShadowMapMarkStencilFallbackSticky( vLight );
	if ( !deferReceiverDraw ) {
		RB_ShadowMapStencilFallback( vLight, passKind, primaryShadowSurfs, secondaryShadowSurfs, interactions );
	}
}

static void RB_ShadowMapCompleteDeferredGlobalReceiver(
		const viewLight_t *vLight, const bool pointLight,
		const bool maskOk ) {
	if ( !g_shadowMapDeferredGlobalReportPending ) {
		return;
	}

	const shadowMapPassResult_t result = maskOk
		? g_shadowMapDeferredGlobalPassResult
		: SHADOWMAP_PASS_RESULT_MASK_FAIL;
	if ( maskOk ) {
		g_shadowMapStats.mappedGlobalPasses++;
	} else {
		g_shadowMapStats.fallbackGlobalPasses++;
		g_shadowMapStats.maskFailGlobalPasses++;
	}
	RB_ShadowMapDebugOverlayCapture( vLight, SHADOWMAP_PASS_GLOBAL,
		pointLight, maskOk, true, vLight->globalShadowMapCasters,
		vLight->localShadowMapCasters, vLight->globalShadowMapDynamicCasters,
		vLight->localShadowMapDynamicCasters, vLight->globalShadows,
		vLight->localShadows, vLight->translucentInteractions );
	RB_ShadowMapPassReport( vLight, SHADOWMAP_PASS_GLOBAL, pointLight,
		result, vLight->globalShadowMapCasters,
		vLight->localShadowMapCasters, vLight->globalShadowMapDynamicCasters,
		vLight->localShadowMapDynamicCasters, vLight->globalShadows,
		vLight->localShadows, vLight->translucentInteractions );
	g_shadowMapDeferredGlobalReportPending = false;
}

/*
==================
RB_ARB2_DrawInteraction
==================
*/
void	RB_ARB2_DrawInteraction( const drawInteraction_t *din ) {
	const bool packedInteractionSurface =
		( din != NULL
			&& din->surf == g_packedInteractionSurf
			&& g_packedInteractionVertexFormatIndex >= 0 );
	const int interactionParamBase = packedInteractionSurface ? ARB2_MD5R_INTERACTION_PARAM_BASE : 0;

	if ( packedInteractionSurface ) {
		RB_ARB2_LoadMD5RMVPMatrix( din->surf );
	}

	// load all the vertex program parameters
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_LIGHT_ORIGIN, din->localLightOrigin.ToFloatPtr() );
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_VIEW_ORIGIN, din->localViewOrigin.ToFloatPtr() );
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_LIGHT_PROJECT_S, din->lightProjection[0].ToFloatPtr() );
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_LIGHT_PROJECT_T, din->lightProjection[1].ToFloatPtr() );
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_LIGHT_PROJECT_Q, din->lightProjection[2].ToFloatPtr() );
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_LIGHT_FALLOFF_S, din->lightProjection[3].ToFloatPtr() );
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_BUMP_MATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_BUMP_MATRIX_T, din->bumpMatrix[1].ToFloatPtr() );
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_DIFFUSE_MATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_DIFFUSE_MATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_SPECULAR_MATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_SPECULAR_MATRIX_T, din->specularMatrix[1].ToFloatPtr() );

	// testing fragment based normal mapping
	if ( r_testARBProgram.GetBool() ) {
		RB_ARB2_SetFragmentEnvParm( 2, din->localLightOrigin.ToFloatPtr() );
		RB_ARB2_SetFragmentEnvParm( 3, din->localViewOrigin.ToFloatPtr() );
	}

	static const float zero[4] = { 0, 0, 0, 0 };
	float modulate = 0.0f;
	float add = 1.0f;

	switch ( din->vertexColor ) {
	case SVC_IGNORE:
		modulate = 0.0f;
		add = 1.0f;
		break;
	case SVC_MODULATE:
		modulate = 1.0f;
		add = 0.0f;
		break;
	case SVC_INVERSE_MODULATE:
		modulate = -1.0f;
		add = 1.0f;
		break;
	}

	if ( packedInteractionSurface ) {
		const float *packedColorMode = RB_ARB2_MD5RVertexColorIgnore;
		switch ( din->vertexColor ) {
		case SVC_MODULATE:
			packedColorMode = RB_ARB2_MD5RVertexColorModulate;
			break;
		case SVC_INVERSE_MODULATE:
			packedColorMode = RB_ARB2_MD5RVertexColorInverseModulate;
			break;
		default:
			break;
		}
		RB_ARB2_SetVertexEnvParm( interactionParamBase + PP_COLOR_MODULATE, packedColorMode );
	} else if ( g_interactionVertexProgramColorMode == ICM_PACKED ) {
		// Stock Quake 4 interaction.vfp packs vertex-color mode as env[16].xy.
		const float packed[4] = { modulate, add, 0.0f, 0.0f };
		RB_ARB2_SetVertexEnvParm( PP_COLOR_MODULATE, packed );
		RB_ARB2_SetVertexEnvParm( PP_COLOR_ADD, zero );
	} else {
		float modulateVec[4] = { modulate, modulate, modulate, modulate };
		float addVec[4] = { add, add, add, add };
		RB_ARB2_SetVertexEnvParm( PP_COLOR_MODULATE, modulateVec );
		RB_ARB2_SetVertexEnvParm( PP_COLOR_ADD, addVec );
	}

	// set the constant colors
	const float specularColorX2[4] = {
		din->specularColor[0] * 2.0f,
		din->specularColor[1] * 2.0f,
		din->specularColor[2] * 2.0f,
		din->specularColor[3] * 2.0f
	};
	RB_ARB2_SetFragmentEnvParm( 0, din->diffuseColor.ToFloatPtr() );
	RB_ARB2_SetFragmentEnvParm( 1, specularColorX2 );

	// set the textures

	// texture 1 will be the per-surface bump map
	GL_SelectTextureNoClient( 1 );
	din->bumpImage->Bind();

	// texture 2 will be the light falloff texture
	GL_SelectTextureNoClient( 2 );
	din->lightFalloffImage->Bind();

	// texture 3 will be the light projection texture
	GL_SelectTextureNoClient( 3 );
	din->lightImage->Bind();

	// texture 4 is the per-surface diffuse map
	GL_SelectTextureNoClient( 4 );
	din->diffuseImage->Bind();

	// texture 5 is the per-surface specular map
	GL_SelectTextureNoClient( 5 );
	din->specularImage->Bind();

	// Quake 4 applies decal polygon offset in interaction passes as well.
	const idMaterial *surfaceMaterial = din->surf->material;
	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
	}

	// draw it
	if ( !packedInteractionSurface || !RB_ARB2_DrawPackedMD5RInteractionBatches( din, g_packedInteractionVertexFormatIndex ) ) {
		RB_DrawElementsWithCounters( din->surf->geo );
	}

	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

/*
=============
RB_ARB2_CreateDrawInteractions

=============
*/
void RB_ARB2_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( !surf ) {
		return;
	}
	if ( !g_firstARB2InteractionHandoffBreadcrumb ) {
		g_firstARB2InteractionHandoffBreadcrumb = true;
		R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_FIRST_ARB2_INTERACTION_HANDOFF );
	}
	const drawSurf_t *firstSurf = surf;

	// perform setup here that will be constant for all interactions
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	// other passes write the same env registers directly, so the value cache
	// is only trustworthy within this batch
	RB_ARB2_InvalidateEnvParamCache();

	const GLuint vertexProgram = RB_CurrentInteractionProgramIdent( GL_VERTEX_PROGRAM_ARB );
	const GLuint fragmentProgram = RB_CurrentInteractionProgramIdent( GL_FRAGMENT_PROGRAM_ARB );
	if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, vertexProgram, "interaction vertex program", true ) ||
		!R_BindARBProgram( GL_FRAGMENT_PROGRAM_ARB, fragmentProgram, "interaction fragment program", true ) ) {
		RB_WarnInteractionShaderRescueMode();
		return;
	}

	glEnable(GL_VERTEX_PROGRAM_ARB);
	glEnable(GL_FRAGMENT_PROGRAM_ARB);

	// enable the vertex arrays
	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );

	// texture 0 is the normalization cube map for the vector towards the light
	GL_SelectTextureNoClient( 0 );
	if ( backEnd.vLight->lightShader->IsAmbientLight() ) {
		globalImages->ambientNormalMap->Bind();
	} else {
		globalImages->normalCubeMapImage->Bind();
	}

	// texture 6 is the specular lookup table
	GL_SelectTextureNoClient( 6 );
	globalImages->specularTableImage->Bind();


	// the classic attrib array enables and the classic interaction vertex
	// program only need to be re-issued on the first classic surface and
	// after a packed MD5R surface changed them, not for every surface
	bool classicInteractionStateValid = false;

	for ( ; surf ; surf=surf->nextOnLight ) {
		g_packedInteractionSurf = NULL;
		g_packedInteractionVertexFormatIndex = -1;

		// perform setup here that will not change over multiple interaction passes
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		bool useClassicInteraction = true;
		const rvMD5RVertexBufferDesc *packedDrawVertexBuffer = NULL;
		// Packed MD5R programs are part of the full interaction family. The
		// Apple compatibility corridor must stay on the validated simple ARB
		// pair for every fallback surface, including generated characters.
		if ( !RB_UseSimpleInteractionShader() && surf->geo != NULL && surf->geo->primBatchMesh != NULL ) {
			packedDrawVertexBuffer = R_MD5R_GetDrawVertexBufferForTri( surf->geo );
		}
		if ( packedDrawVertexBuffer != NULL ) {
			// the packed path (even a partially failed bind attempt) can change
			// the bound vertex program, the attrib arrays, and the low env
			// registers (joint palettes), so the classic fast path must re-issue
			classicInteractionStateValid = false;
			RB_ARB2_InvalidateEnvParamCache();
			int packedVertexFormatIndex = -1;
			const program_t packedVertexProgram = RB_ARB2_GetPackedMD5RInteractionVertexProgram( surf, &packedVertexFormatIndex );
			if ( packedVertexProgram != PROG_INVALID
				&& R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, packedVertexProgram, "packed interaction vertex program", false )
				&& RB_ARB2_BindPackedMD5RInteractionVertexData( *packedDrawVertexBuffer, packedVertexFormatIndex ) ) {
				g_packedInteractionSurf = surf;
				g_packedInteractionVertexFormatIndex = packedVertexFormatIndex;
				useClassicInteraction = false;
			}
		}
		if ( useClassicInteraction ) {
#else
		{
#endif
			idDrawVert *ac = NULL;
			if ( !RB_GLSLPrepareInteractionVertexCache( surf, ac ) ) {
				// Invalid or unavailable cache data is a per-surface failure. Never
				// dereference a null cache handle in the compatibility fallback.
				classicInteractionStateValid = false;
				continue;
			}

			if ( !classicInteractionStateValid || !r_useRedundantStateFiltering.GetBool() ) {
				glDisableVertexAttribArrayARB( 1 );
				glDisableVertexAttribArrayARB( 2 );
				glDisableVertexAttribArrayARB( 5 );
				glDisableVertexAttribArrayARB( 6 );
				glDisableVertexAttribArrayARB( 7 );
				glEnableVertexAttribArrayARB( 8 );
				glEnableVertexAttribArrayARB( 9 );
				glEnableVertexAttribArrayARB( 10 );
				glEnableVertexAttribArrayARB( 11 );
				R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, vertexProgram, "interaction vertex program", true );
				classicInteractionStateValid = true;
			}

			// set the vertex pointers
			glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_COLOR_OFFSET ) );
			glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET ) );
			glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET ) );
			glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET ) );
			glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
			glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET ) );
		}

		// this may cause RB_ARB2_DrawInteraction to be exacuted multiple
		// times with different colors and images if the surface or light have multiple layers
		RB_CreateSingleDrawInteractions( surf, RB_ARB2_DrawInteraction );
	}

	RB_ARB2_CreateCustomGLSLDrawInteractions( firstSurf );

	g_packedInteractionSurf = NULL;
	g_packedInteractionVertexFormatIndex = -1;
	RB_ARB2_UnbindPackedMD5RInteractionVertexData();
	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );

	// disable features
	GL_SelectTextureNoClient( 6 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 5 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 4 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 3 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 2 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();

	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );

	glDisable(GL_VERTEX_PROGRAM_ARB);
	glDisable(GL_FRAGMENT_PROGRAM_ARB);
}

/*
==================
RB_ARB2_DrawInteractions
==================
*/
static void RB_ARB2_DisableInteractionVertexAttribArrays( void ) {
	if ( glDisableVertexAttribArrayARB == NULL ) {
		return;
	}

	static const int attribs[] = { 1, 2, 5, 6, 7, 8, 9, 10, 11 };
	const int numAttribs = static_cast<int>( sizeof( attribs ) / sizeof( attribs[0] ) );
	for ( int i = 0; i < numAttribs; i++ ) {
		glDisableVertexAttribArrayARB( attribs[i] );
	}
}

static void RB_ARB2_UnbindInteractionPrograms( void ) {
	if ( glBindProgramARB == NULL ) {
		return;
	}
	if ( glConfig.ARBVertexProgramAvailable ) {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
	}
	if ( glConfig.ARBFragmentProgramAvailable ) {
		glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, 0 );
	}
}

static void RB_ARB2_ClearInteractionTextureState( void ) {
	const int maxStateUnits = Max( 0, Min( MAX_MULTITEXTURE_UNITS, Min( glConfig.maxTextureUnits, glConfig.maxTextureImageUnits ) ) );
	const int maxInteractionUnit = Min( 6, maxStateUnits - 1 );

	// GL_SelectTextureNoClient can leave the client active texture behind.
	backEnd.glState.currenttmu = -1;
	for ( int unit = maxInteractionUnit; unit >= 1; unit-- ) {
		GL_SelectTexture( unit );
		globalImages->BindNull();
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_R );
		glDisable( GL_TEXTURE_GEN_Q );
		GL_TexEnv( GL_MODULATE );
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	}

	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
}

static void RB_ARB2_RestoreInteractionState( const bool recordBypassBreadcrumb ) {
	RB_ARB2_ClearInteractionTextureState();
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );

	RB_ARB2_DisableInteractionVertexAttribArrays();
	if ( glConfig.ARBVertexProgramAvailable ) {
		glDisable( GL_VERTEX_PROGRAM_ARB );
	}
	if ( glConfig.ARBFragmentProgramAvailable ) {
		glDisable( GL_FRAGMENT_PROGRAM_ARB );
	}
	RB_ARB2_UnbindInteractionPrograms();
	if ( glUseProgramObjectARB != NULL ) {
		glUseProgramObjectARB( 0 );
	}

	glStencilFunc( GL_ALWAYS, 128, 255 );
	backEnd.currentSpace = NULL;
	GL_ClearStateDelta();
	if ( recordBypassBreadcrumb && !g_arb2InteractionBypassStateBreadcrumb ) {
		g_arb2InteractionBypassStateBreadcrumb = true;
		R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_STATE_RESTORED );
	}
}

static void RB_ARB2_RestoreBypassedInteractionState( void ) {
	RB_ARB2_RestoreInteractionState( true );
}

/*
===============================================================================

	Backend-neutral classic interaction consumer

	ClassicInteractionDomain has already interpreted the complete light,
	receiver, and material-stage stream.  This adapter performs every fallible
	OpenGL/resource/cache check for the complete view before it changes the
	framebuffer.  After commit it uses only sealed values plus the explicitly
	retained geometry bridge; the classic interaction walker remains the atomic
	rollback path when any part of preflight is rejected.

===============================================================================
*/

enum rbSharedWorldInteractionGLReject_t {
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_VIEW = 1,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_MUTATED_VIEW,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_CAPACITY,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_PROGRAM,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_LIGHT_RANGE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SURFACE_RANGE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_PRIMITIVE_RANGE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_PRIMITIVE_STATE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_GEOMETRY,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_VERTEX_CACHE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_INDEX_CACHE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_TEXTURE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_INTRINSIC_TEXTURE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MODE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_RANGE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_STATE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_GEOMETRY,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_VERTEX_CACHE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_INDEX_CACHE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_PASS,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_CASTER,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_ALPHA,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_RESOURCE,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_TRANSACTION,
	RB_SHARED_WORLD_INTERACTION_GL_REJECT_COVERAGE
};

typedef struct rbSharedWorldInteractionGLPreparedPrimitive_s {
	const classicInteractionDomainPrimitive_t *primitive;
	unsigned int vertexBuffer;
	const void *vertexPointer;
	unsigned int indexBuffer;
	const void *indexPointer;
	int vertexCount;
	int indexCount;
	bool referencedVertexes;
	bool referencedIndexes;
	bool indexCacheBacked;
	idImage *lightImage;
	idImage *lightFalloffImage;
	idImage *bumpImage;
	idImage *diffuseImage;
	idImage *specularImage;
	idImage *normalImage;
	int stateBits;
	int cullType;
} rbSharedWorldInteractionGLPreparedPrimitive_t;

typedef struct rbSharedWorldInteractionGLPreparedShadowCaster_s {
	const classicInteractionDomainShadowCaster_t *caster;
	unsigned int vertexBuffer;
	const void *vertexPointer;
	unsigned int indexBuffer;
	const void *indexPointer;
	int vertexCount;
	int indexCount;
	bool indexCacheBacked;
} rbSharedWorldInteractionGLPreparedShadowCaster_t;

typedef struct rbSharedWorldInteractionGLPreparedShadowAlphaStage_s {
	const classicInteractionDomainShadowAlphaStage_t *stage;
	idImage *image;
} rbSharedWorldInteractionGLPreparedShadowAlphaStage_t;

typedef struct rbSharedWorldInteractionGLPreparedMapCaster_s {
	const classicInteractionDomainShadowCaster_t *caster;
	unsigned int vertexBuffer;
	const void *vertexPointer;
	unsigned int indexBuffer;
	const void *indexPointer;
	int vertexCount;
	int indexCount;
	int firstAlphaStage;
	int alphaStageCount;
	bool indexCacheBacked;
} rbSharedWorldInteractionGLPreparedMapCaster_t;

typedef struct rbSharedWorldInteractionGLPreparedMapPass_s {
	const classicInteractionDomainShadowMapPass_t *pass;
	projectedShadowMapCacheEntry_t *projectedEntry;
	pointShadowMapCacheEntry_t *pointEntry;
	idImage *projectedDepthImage;
	idImage *pointColorImage;
	idImage *pointDepthImage;
	idRenderTexture *renderTexture;
	std::uint64_t sampleStorageGeneration;
	unsigned int sampleTextureHandle;
	int resourceWidth;
	int resourceHeight;
	int projectedOriginX;
	int projectedOriginY;
	int projectedSpanPixels;
	int cacheLightIndex;
	int cachePassKind;
	int signature;
	bool pointLight;
	bool scratchResource;
	bool cacheUpdated;
	bool prepared;
} rbSharedWorldInteractionGLPreparedMapPass_t;

typedef struct rbSharedWorldInteractionGLPreparedLight_s {
	const classicInteractionDomainLight_t *light;
	int firstPrimitive[ CLASSIC_INTERACTION_RECEIVER_COUNT ];
	int primitiveCount[ CLASSIC_INTERACTION_RECEIVER_COUNT ];
	int firstShadowCaster[ CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT ];
	int shadowCasterCount[ CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT ];
	int firstMapCaster[ CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT ];
	int mapCasterCount[ CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT ];
	int shadowMapPass[ 2 ];
} rbSharedWorldInteractionGLPreparedLight_t;

typedef struct rbSharedWorldInteractionGLPreparedView_s {
	const classicInteractionDomainView_t *view;
	const viewDef_t *viewDef;
	std::uint64_t hash;
	int lightCount;
	int surfaceCount;
	int primitiveCount;
	int drawablePrimitiveCount;
	int noopPrimitiveCount;
	int shadowCasterCount;
	int drawableShadowCasterCount;
	int noopShadowCasterCount;
	int logicalVolumeDrawCount;
	int preloadVolumeDrawCount;
	int mapCasterCount;
	int shadowAlphaStageCount;
	int shadowMapPassCount;
	int hybridShadowPassCount;
	int projectedScratchCount;
	int pointScratchCount;
	bool twoSidedStencil;
	bool mapTransactionPrepared;
	bool mapGLStateCaptured;
	bool mapBlendWasEnabled;
	bool mapDepthWasEnabled;
	bool mapScissorWasEnabled;
	bool mapStencilWasEnabled;
	bool mapCullWasEnabled;
	bool mapDepthWriteWasEnabled;
	bool mapColorWriteWasEnabled[ 4 ];
	int mapSavedFaceCulling;
	float mapSavedClearColor[ 4 ];
	bool ready;
	bool committed;
	bool seenLights[ CLASSIC_INTERACTION_DOMAIN_MAX_LIGHTS ];
	bool seenSurfaces[ CLASSIC_INTERACTION_DOMAIN_MAX_SURFACES ];
	bool seenPrimitives[ CLASSIC_INTERACTION_DOMAIN_MAX_PRIMITIVES ];
	bool seenShadowCasters[ CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS ];
	rbSharedWorldInteractionGLPreparedLight_t lights[
		CLASSIC_INTERACTION_DOMAIN_MAX_LIGHTS ];
	rbSharedWorldInteractionGLPreparedPrimitive_t primitives[
		CLASSIC_INTERACTION_DOMAIN_MAX_PRIMITIVES ];
	rbSharedWorldInteractionGLPreparedShadowCaster_t shadowCasters[
		CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS ];
	rbSharedWorldInteractionGLPreparedMapCaster_t mapCasters[
		CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS ];
	rbSharedWorldInteractionGLPreparedShadowAlphaStage_t shadowAlphaStages[
		CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_ALPHA_STAGES ];
	rbSharedWorldInteractionGLPreparedMapPass_t shadowMapPasses[
		CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_MAP_PASSES ];
} rbSharedWorldInteractionGLPreparedView_t;

static rbSharedWorldInteractionGLPreparedView_t
	rbSharedWorldInteractionGLPreparedView;

static int RB_SharedWorldInteractionGLFailureDetail(
		rbSharedWorldInteractionGLReject_t reason, int lightIndex = -1,
		int surfaceIndex = -1, int primitiveIndex = -1 ) {
	const int lightDetail = lightIndex >= 0 ? Min( lightIndex + 1, 255 ) : 0;
	const int surfaceDetail = surfaceIndex >= 0 ? Min( surfaceIndex + 1, 4095 ) : 0;
	const int primitiveDetail = primitiveIndex >= 0 ? Min( primitiveIndex + 1, 4095 ) : 0;
	return static_cast<int>( reason ) * 100000000
		+ lightDetail * 1000000 + surfaceDetail * 4096 + primitiveDetail;
}

static bool RB_SharedWorldInteractionGLFinite( float value ) {
	return !FLOAT_IS_NAN( value );
}

static bool RB_SharedWorldInteractionGLFiniteArray( const float *values,
		int count ) {
	if ( values == NULL || count < 0 ) {
		return false;
	}
	for ( int i = 0; i < count; ++i ) {
		if ( !RB_SharedWorldInteractionGLFinite( values[i] ) ) {
			return false;
		}
	}
	return true;
}

static bool RB_SharedWorldInteractionGLMapCull( rendererCullMode_t cull,
		int &cullType ) {
	switch ( cull ) {
	case RENDERER_CULL_NONE:
		cullType = CT_TWO_SIDED;
		return true;
	case RENDERER_CULL_FRONT:
		cullType = CT_FRONT_SIDED;
		return true;
	case RENDERER_CULL_BACK:
		cullType = CT_BACK_SIDED;
		return true;
	default:
		return false;
	}
}

static bool RB_SharedWorldInteractionGLBlendValid(
		const rendererBlendState_t &blend ) {
	return blend.enabled
		&& blend.sourceColor == RENDERER_BLEND_ONE
		&& blend.destinationColor == RENDERER_BLEND_ONE
		&& blend.colorOperation == RENDERER_BLEND_OP_ADD
		&& blend.sourceAlpha == RENDERER_BLEND_ONE
		&& blend.destinationAlpha == RENDERER_BLEND_ONE
		&& blend.alphaOperation == RENDERER_BLEND_OP_ADD;
}

static bool RB_SharedWorldInteractionGLIntrinsicImageValid( idImage *image,
		textureType_t expectedType ) {
	return image != NULL && image->IsLoaded() && !image->IsDefaulted()
		&& image->GetDeviceHandle() != 0
		&& image->GetOpts().textureType == expectedType;
}

static bool RB_SharedWorldInteractionGLValidateTexture(
		std::uint64_t resourceId,
		const classicInteractionDomainTexture_t *texture, idImage *&image ) {
	image = NULL;
	if ( resourceId == 0 ) {
		return false;
	}
	if ( texture == NULL || texture->textureResourceId != resourceId
			|| texture->image == NULL || !texture->loaded
			|| texture->defaulted || texture->mutableImage
			|| texture->textureHandle == 0 ) {
		return false;
	}
	idImage *resolved = const_cast<idImage *>( texture->image );
	const textureType_t textureType = resolved->GetOpts().textureType;
	if ( !resolved->IsLoaded() || resolved->IsDefaulted()
			|| resolved->GetDeviceHandle() != texture->textureHandle
			|| resolved->GetStorageGeneration() != texture->storageGeneration
			|| resolved->GetFilter() != texture->filter
			|| resolved->GetRepeat() != texture->repeat
			|| ( textureType != TT_2D && textureType != TT_CUBIC ) ) {
		return false;
	}
	image = resolved;
	return true;
}

static bool RB_SharedWorldInteractionGLCacheValid( const vertCache_t *cache,
		bool indexBuffer, int requiredBytes ) {
	if ( cache == NULL || requiredBytes <= 0 || cache->tag == TAG_FREE
			|| cache->indexBuffer != indexBuffer || cache->offset < 0
			|| cache->size < requiredBytes ) {
		return false;
	}
	if ( cache->vbo != 0 ) {
		return glConfig.ARBVertexBufferObjectAvailable
			&& cache->virtMem == NULL;
	}
	return cache->virtMem != NULL;
}

static const void *RB_SharedWorldInteractionGLCachePointer(
		const vertCache_t &cache ) {
	if ( cache.vbo != 0 ) {
		return reinterpret_cast<const void *>(
			static_cast<uintptr_t>( cache.offset ) );
	}
	return static_cast<const byte *>( cache.virtMem ) + cache.offset;
}

static bool RB_SharedWorldInteractionGLPrimitiveValuesValid(
		const classicInteractionDomainPrimitive_t &primitive ) {
	if ( primitive.lightIndex < 0 || primitive.surfaceIndex < 0
			|| primitive.receiver < CLASSIC_INTERACTION_RECEIVER_LOCAL
			|| primitive.receiver >= CLASSIC_INTERACTION_RECEIVER_COUNT
			|| primitive.disposition < CLASSIC_INTERACTION_PRIMITIVE_DRAW
			|| primitive.disposition >= CLASSIC_INTERACTION_PRIMITIVE_COUNT
			|| primitive.depth < CLASSIC_INTERACTION_DEPTH_EQUAL
			|| primitive.depth >= CLASSIC_INTERACTION_DEPTH_COUNT
			|| primitive.vertexColor < RENDERER_VERTEX_COLOR_IGNORE
			|| primitive.vertexColor > RENDERER_VERTEX_COLOR_INVERSE_MODULATE
			|| primitive.vertexCount <= 0 || primitive.firstIndex != 0
			|| primitive.indexCount <= 0 || primitive.indexCount % 3 != 0
			|| primitive.vertexOffset != 0 ) {
		return false;
	}
	const long long scissorWidth =
		static_cast<long long>( primitive.scissorX2 ) - primitive.scissorX1 + 1;
	const long long scissorHeight =
		static_cast<long long>( primitive.scissorY2 ) - primitive.scissorY1 + 1;
	if ( scissorWidth <= 0 || scissorHeight <= 0
			|| scissorWidth > idMath::INT_MAX
			|| scissorHeight > idMath::INT_MAX
			|| !RB_SharedWorldInteractionGLBlendValid( primitive.blend )
			|| !RB_SharedWorldInteractionGLFinite(
				primitive.polygonOffsetFactor )
			|| !RB_SharedWorldInteractionGLFinite(
				primitive.polygonOffsetUnits )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				primitive.diffuseColor, 4 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				primitive.specularColor, 4 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				primitive.flatDiffuseParams, 4 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				primitive.localLightOrigin, 4 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				primitive.localViewOrigin, 4 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				&primitive.lightProjection[0][0], 16 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				&primitive.bumpMatrix[0][0], 8 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				&primitive.diffuseMatrix[0][0], 8 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				&primitive.specularMatrix[0][0], 8 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				primitive.modelMatrix, 16 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				primitive.modelViewMatrix, 16 ) ) {
		return false;
	}
	return ( primitive.receiver == CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT )
		? primitive.depth == CLASSIC_INTERACTION_DEPTH_LESS_OR_EQUAL
		: primitive.depth == CLASSIC_INTERACTION_DEPTH_EQUAL;
}

static bool RB_SharedWorldInteractionGLShadowCasterValuesValid(
		const classicInteractionDomainShadowCaster_t &caster,
		classicInteractionDomainShadowChain_t expectedChain ) {
	if ( caster.legacyDrawSurf == NULL || caster.legacyViewLight == NULL
			|| caster.legacyCasterGeometry == NULL || caster.lightIndex < 0
			|| caster.sourceOrdinal < 0 || caster.chainOrdinal < 0
			|| caster.chain != expectedChain
			|| caster.disposition < CLASSIC_INTERACTION_SHADOW_CASTER_DRAW
			|| caster.disposition
				>= CLASSIC_INTERACTION_SHADOW_CASTER_DISPOSITION_COUNT
			|| caster.indexSelection < CLASSIC_INTERACTION_SHADOW_INDEX_FULL
			|| caster.indexSelection > CLASSIC_INTERACTION_SHADOW_INDEX_NO_CAPS
			|| caster.alphaStageCount != 0 || caster.firstAlphaStage != -1
			|| caster.vertexCount < 0 || caster.totalIndexCount < 0
			|| caster.selectedIndexCount < 0
			|| caster.selectedIndexCount > caster.totalIndexCount
			|| caster.selectedIndexCount % 3 != 0
			|| caster.ambientGeometry || caster.dynamicCaster
			|| caster.translucentCaster ) {
		return false;
	}
	const long long scissorWidth = static_cast<long long>( caster.scissorX2 )
		- caster.scissorX1 + 1;
	const long long scissorHeight = static_cast<long long>( caster.scissorY2 )
		- caster.scissorY1 + 1;
	if ( scissorWidth <= 0 || scissorHeight <= 0
			|| scissorWidth > idMath::INT_MAX
			|| scissorHeight > idMath::INT_MAX
			|| !RB_SharedWorldInteractionGLFinite( caster.depthMin )
			|| !RB_SharedWorldInteractionGLFinite( caster.depthMax )
			|| caster.depthMin < 0.0f || caster.depthMin > 1.0f
			|| caster.depthMax < caster.depthMin || caster.depthMax > 1.0f
			|| !RB_SharedWorldInteractionGLFiniteArray(
				caster.localLightOrigin, 4 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				caster.modelMatrix, 16 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				caster.modelViewMatrix, 16 )
			|| !RB_SharedWorldInteractionGLFiniteArray( caster.boundsMin, 3 )
			|| !RB_SharedWorldInteractionGLFiniteArray( caster.boundsMax, 3 ) ) {
		return false;
	}
	if ( caster.disposition == CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY ) {
		return !caster.preload
			&& ( caster.vertexCount == 0 || caster.selectedIndexCount == 0 );
	}
	return caster.vertexCount > 0 && caster.totalIndexCount > 0
		&& caster.selectedIndexCount > 0
		&& caster.preload != caster.external;
}

static bool RB_SharedWorldInteractionGLIsMapCasterChain(
		classicInteractionDomainShadowChain_t chain ) {
	return chain >= CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC
		&& chain <= CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_TRANSLUCENT;
}

static bool RB_SharedWorldInteractionGLIsVolumeChain(
		classicInteractionDomainShadowChain_t chain ) {
	return chain == CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL
		|| chain == CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_LOCAL
		|| chain == CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL
		|| chain == CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL;
}

static bool RB_SharedWorldInteractionGLMapCasterValuesValid(
		const classicInteractionDomainShadowCaster_t &caster,
		classicInteractionDomainShadowChain_t expectedChain ) {
	if ( caster.legacyDrawSurf == NULL || caster.legacyViewLight == NULL
			|| caster.legacyCasterGeometry == NULL || caster.lightIndex < 0
			|| caster.sourceOrdinal < 0 || caster.chainOrdinal < 0
			|| caster.chain != expectedChain
			|| !RB_SharedWorldInteractionGLIsMapCasterChain( expectedChain )
			|| caster.disposition < CLASSIC_INTERACTION_SHADOW_CASTER_DRAW
			|| caster.disposition
				>= CLASSIC_INTERACTION_SHADOW_CASTER_DISPOSITION_COUNT
			|| caster.indexSelection
				!= CLASSIC_INTERACTION_SHADOW_INDEX_AMBIENT
			|| !caster.ambientGeometry || caster.external || caster.preload
			|| caster.vertexCount < 0 || caster.totalIndexCount < 0
			|| caster.selectedIndexCount < 0
			|| caster.selectedIndexCount % 3 != 0
			|| ( caster.materialCoverage != MC_OPAQUE
				&& caster.materialCoverage != MC_PERFORATED )
			|| caster.alphaStageCount < 0
			|| ( caster.alphaStageCount == 0
				&& caster.firstAlphaStage != -1 )
			|| ( caster.alphaStageCount > 0
				&& caster.firstAlphaStage < 0 )
			|| caster.translucentCaster
			|| caster.dynamicCaster != (
				expectedChain
					== CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC
				|| expectedChain
					== CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC ) ) {
		return false;
	}
	if ( caster.cull < RENDERER_CULL_NONE
			|| caster.cull > RENDERER_CULL_BACK
			|| !RB_SharedWorldInteractionGLFiniteArray(
				caster.modelMatrix, 16 )
			|| !RB_SharedWorldInteractionGLFiniteArray(
				caster.modelViewMatrix, 16 )
			|| !RB_SharedWorldInteractionGLFiniteArray( caster.boundsMin, 3 )
			|| !RB_SharedWorldInteractionGLFiniteArray( caster.boundsMax, 3 ) ) {
		return false;
	}
	if ( caster.disposition == CLASSIC_INTERACTION_SHADOW_CASTER_NOOP_EMPTY ) {
		return R_ClassicInteractionDomain_ShadowCasterNoopValid( caster );
	}
	// A perforated material with no active alpha-test stage follows the classic
	// opaque-depth fallback; active zero-alpha stages are normalized above to a
	// no-op, while drawable stages carry explicit sealed alpha records.
	return caster.vertexCount > 0 && caster.selectedIndexCount > 0
		&& caster.selectedIndexCount == caster.totalIndexCount;
}

static bool RB_SharedWorldInteractionGLShadowAlphaStageValuesValid(
		const classicInteractionDomainShadowAlphaStage_t &stage,
		int expectedCasterIndex ) {
	return stage.casterIndex == expectedCasterIndex && stage.stageIndex >= 0
		&& ( stage.alphaTestMode == GL_LESS
			|| stage.alphaTestMode == GL_EQUAL
			|| stage.alphaTestMode == GL_GREATER )
		&& RB_SharedWorldInteractionGLFinite( stage.alphaTestValue )
		&& RB_SharedWorldInteractionGLFinite( stage.alphaScale )
		&& stage.alphaScale > 0.0f
		&& RB_SharedWorldInteractionGLFinite( stage.alphaHashMode )
		&& RB_SharedWorldInteractionGLFiniteArray(
			&stage.textureMatrix[0][0], 8 )
		&& stage.textureResourceId != 0;
}

static bool RB_SharedWorldInteractionGLMapPassValuesValid(
		const classicInteractionDomainLight_t &light,
		const classicInteractionDomainShadowMapPass_t &pass,
		classicInteractionDomainReceiver_t receiver, int domainLightIndex ) {
	const bool pointLight = light.pointLight;
	const bool hybrid = pass.disposition
		== CLASSIC_INTERACTION_SHADOW_MAP_PASS_HYBRID;
	if ( pass.legacyViewLight != light.legacyViewLight
			|| pass.lightIndex != domainLightIndex
			|| pass.receiver != receiver
			|| pass.resourceOwner < CLASSIC_INTERACTION_RECEIVER_LOCAL
			|| pass.resourceOwner > CLASSIC_INTERACTION_RECEIVER_GLOBAL
			|| pass.disposition
				< CLASSIC_INTERACTION_SHADOW_MAP_PASS_MAPPED
			|| pass.disposition
				>= CLASSIC_INTERACTION_SHADOW_MAP_PASS_DISPOSITION_COUNT
			|| pass.mode != ( hybrid ? CLASSIC_INTERACTION_SHADOW_HYBRID
				: ( pointLight ? CLASSIC_INTERACTION_SHADOW_POINT
					: CLASSIC_INTERACTION_SHADOW_PROJECTED ) )
			|| pass.receiverMask != ( receiver
				== CLASSIC_INTERACTION_RECEIVER_LOCAL
					? SHADOWMAP_RECEIVER_MASK_LOCAL
					: SHADOWMAP_RECEIVER_MASK_GLOBAL )
			|| pass.mappedCasterCount <= 0
			|| pass.supplementCasterCount < 0
			|| pass.drawableMappedCasters < 0
			|| pass.noopMappedCasters < 0
			|| pass.drawableMappedCasters + pass.noopMappedCasters
				!= pass.mappedCasterCount
			|| pass.drawableSupplementCasters < 0
			|| pass.noopSupplementCasters < 0
			|| pass.drawableSupplementCasters + pass.noopSupplementCasters
				!= pass.supplementCasterCount
			|| pass.resourcePlanId == 0 || pass.resourceGeneration == 0
			|| pass.hash == 0
			|| !pass.mapRequired || !pass.hybridComplete
			|| ( hybrid && ( pass.mapComplete
				|| pass.supplementCasterCount <= 0 ) )
			|| ( !hybrid && !pass.mapComplete )
			|| ( !pass.hasStaticCasters && !pass.hasDynamicCasters )
			|| pass.hasTranslucentCasters
			|| ( ( pass.hasDynamicCasters || pass.hasAlphaCasters )
				&& pass.allowCacheReuse )
			|| ( !pass.allowCacheReuse && !pass.allowScratch )
			|| ( !pass.allowCacheReuse && !pass.allowCacheUpdate )
			|| pass.casterSignature
				!= light.legacyViewLight->shadowMapCasterSignature
			|| pass.casterCullMode
				!= idMath::ClampInt( 0, 2,
					r_shadowMapCasterCulling.GetInteger() )
			|| idMath::Fabs( pass.polygonFactor
				- RB_ShadowMapPolygonFactor() ) > 0.00001f
			|| idMath::Fabs( pass.polygonOffset
				- RB_ShadowMapPolygonOffset() ) > 0.00001f
			|| pass.hashedAlpha != RB_ShadowMapHashedAlphaEnabled()
			|| pass.stableAlphaHash
				!= r_shadowMapStableAlphaHash.GetBool() ) {
		return false;
	}
	if ( pointLight ) {
		return pass.point.valid && pass.point.faceCount == 6
			&& pass.point.faceSize == RB_ShadowMapPointSizeValue()
			&& pass.point.filterTaps >= 1 && pass.point.filterTaps <= 13
			&& pass.point.filterMode >= 0 && pass.point.filterMode <= 1
			&& pass.point.depthCompare
				== RB_PointShadowMapDepthCompareEnabled()
			&& pass.point.highPrecision
				== RB_PointShadowMapHighPrecisionEnabled()
			&& idMath::Fabs( pass.point.farDistance
				- R_ShadowMapPointFarDistance( light.legacyViewLight ) )
					<= 0.0001f
			&& RB_SharedWorldInteractionGLFiniteArray(
				pass.point.lightOrigin, 4 );
	}
	const char *projectedReason = NULL;
	return pass.projected.state.valid
		&& RB_ShadowMapValidateProjectedStateContract(
			light.legacyViewLight, pass.projected.state, &projectedReason )
		&& pass.projected.depthCompare == RB_ShadowMapDepthCompareEnabled();
}

static void RB_SharedWorldInteractionGLAbortMapTransaction(
		const viewDef_t *viewDef ) {
	const rbSharedWorldInteractionGLPreparedView_t &prepared =
		rbSharedWorldInteractionGLPreparedView;
	if ( backEnd.renderTexture != NULL ) {
		backEnd.renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
		glDrawBuffer( GL_BACK );
		glReadBuffer( GL_BACK );
	}
	if ( viewDef != NULL ) {
		glViewport( viewDef->viewport.x1, viewDef->viewport.y1,
			viewDef->viewport.x2 + 1 - viewDef->viewport.x1,
			viewDef->viewport.y2 + 1 - viewDef->viewport.y1 );
		glScissor( viewDef->viewport.x1, viewDef->viewport.y1,
			viewDef->viewport.x2 + 1 - viewDef->viewport.x1,
			viewDef->viewport.y2 + 1 - viewDef->viewport.y1 );
		glMatrixMode( GL_PROJECTION );
		glLoadMatrixf( viewDef->projectionMatrix );
		glMatrixMode( GL_MODELVIEW );
	}
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glDepthMask( GL_TRUE );
	glUseProgramObjectARB( 0 );
	if ( prepared.mapGLStateCaptured ) {
		prepared.mapBlendWasEnabled
			? glEnable( GL_BLEND ) : glDisable( GL_BLEND );
		prepared.mapDepthWasEnabled
			? glEnable( GL_DEPTH_TEST ) : glDisable( GL_DEPTH_TEST );
		prepared.mapScissorWasEnabled
			? glEnable( GL_SCISSOR_TEST ) : glDisable( GL_SCISSOR_TEST );
		prepared.mapStencilWasEnabled
			? glEnable( GL_STENCIL_TEST ) : glDisable( GL_STENCIL_TEST );
		prepared.mapCullWasEnabled
			? glEnable( GL_CULL_FACE ) : glDisable( GL_CULL_FACE );
		glDepthMask( prepared.mapDepthWriteWasEnabled ? GL_TRUE : GL_FALSE );
		glColorMask( prepared.mapColorWriteWasEnabled[0] ? GL_TRUE : GL_FALSE,
			prepared.mapColorWriteWasEnabled[1] ? GL_TRUE : GL_FALSE,
			prepared.mapColorWriteWasEnabled[2] ? GL_TRUE : GL_FALSE,
			prepared.mapColorWriteWasEnabled[3] ? GL_TRUE : GL_FALSE );
		glClearColor( prepared.mapSavedClearColor[0],
			prepared.mapSavedClearColor[1], prepared.mapSavedClearColor[2],
			prepared.mapSavedClearColor[3] );
	}
	backEnd.currentSpace = NULL;
	backEnd.currentScissor.Clear();
	backEnd.glState.faceCulling = -1;
	if ( prepared.mapGLStateCaptured && prepared.mapCullWasEnabled
			&& prepared.mapSavedFaceCulling >= CT_FRONT_SIDED
			&& prepared.mapSavedFaceCulling <= CT_TWO_SIDED ) {
		GL_Cull( prepared.mapSavedFaceCulling );
	}
	backEnd.glState.currenttmu = -1;
	GL_ClearStateDelta();
	GL_SelectTexture( 0 );
}

static void RB_SharedWorldInteractionGLActivateMapResource(
		const rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass ) {
	if ( preparedPass.pointLight ) {
		g_activePointShadowMapCache = preparedPass.pointEntry;
		g_pointShadowMapColorImage = preparedPass.pointColorImage;
		g_pointShadowMapDepthImage = preparedPass.pointDepthImage;
		g_pointShadowMapRenderTexture = preparedPass.renderTexture;
		return;
	}
	g_activeProjectedShadowMapCache = preparedPass.projectedEntry;
	g_shadowMapDepthImage = preparedPass.projectedDepthImage;
	g_shadowMapRenderTexture = preparedPass.renderTexture;
	g_shadowMapActiveSlotOriginX = preparedPass.projectedOriginX;
	g_shadowMapActiveSlotOriginY = preparedPass.projectedOriginY;
}

static bool RB_SharedWorldInteractionGLCaptureMapResource(
		rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass ) {
	const classicInteractionDomainShadowMapPass_t &pass = *preparedPass.pass;
	idImage *sampleImage = preparedPass.pointLight
		? ( pass.point.depthCompare ? preparedPass.pointDepthImage
			: preparedPass.pointColorImage )
		: preparedPass.projectedDepthImage;
	const textureType_t expectedType = preparedPass.pointLight
		? TT_CUBIC : TT_2D;
	if ( sampleImage == NULL || !sampleImage->IsLoaded()
			|| sampleImage->IsDefaulted()
			|| sampleImage->GetDeviceHandle() == 0
			|| sampleImage->GetOpts().textureType != expectedType
			|| preparedPass.renderTexture == NULL
			|| preparedPass.renderTexture->GetWidth() <= 0
			|| preparedPass.renderTexture->GetHeight() <= 0 ) {
		return false;
	}
	preparedPass.sampleTextureHandle = sampleImage->GetDeviceHandle();
	preparedPass.sampleStorageGeneration =
		sampleImage->GetStorageGeneration();
	preparedPass.resourceWidth = preparedPass.renderTexture->GetWidth();
	preparedPass.resourceHeight = preparedPass.renderTexture->GetHeight();
	return true;
}

static bool RB_SharedWorldInteractionGLAssignProjectedCacheResource(
		rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass,
		projectedShadowMapCacheEntry_t *entry ) {
	if ( entry == NULL || g_shadowMapAtlasDepthImage == NULL
			|| g_shadowMapAtlasRenderTexture == NULL ) {
		return false;
	}
	preparedPass.projectedEntry = entry;
	preparedPass.pointEntry = NULL;
	preparedPass.pointLight = false;
	preparedPass.scratchResource = false;
	preparedPass.projectedDepthImage = g_shadowMapAtlasDepthImage;
	preparedPass.pointColorImage = NULL;
	preparedPass.pointDepthImage = NULL;
	preparedPass.renderTexture = g_shadowMapAtlasRenderTexture;
	preparedPass.projectedOriginX = entry->atlasCellX
		* g_shadowMapAtlasCellSize;
	preparedPass.projectedOriginY = entry->atlasCellY
		* g_shadowMapAtlasCellSize;
	preparedPass.projectedSpanPixels = preparedPass.pass->projected.state.tileSize
		* preparedPass.pass->projected.state.atlasDiv;
	return RB_SharedWorldInteractionGLCaptureMapResource( preparedPass );
}

static bool RB_SharedWorldInteractionGLAssignPointCacheResource(
		rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass,
		pointShadowMapCacheEntry_t *entry ) {
	if ( entry == NULL || entry->colorImage == NULL
			|| entry->depthImage == NULL || entry->renderTexture == NULL ) {
		return false;
	}
	preparedPass.projectedEntry = NULL;
	preparedPass.pointEntry = entry;
	preparedPass.pointLight = true;
	preparedPass.scratchResource = false;
	preparedPass.projectedDepthImage = NULL;
	preparedPass.pointColorImage = entry->colorImage;
	preparedPass.pointDepthImage = entry->depthImage;
	preparedPass.renderTexture = entry->renderTexture;
	preparedPass.projectedOriginX = 0;
	preparedPass.projectedOriginY = 0;
	preparedPass.projectedSpanPixels = 0;
	return RB_SharedWorldInteractionGLCaptureMapResource( preparedPass );
}

static bool RB_SharedWorldInteractionGLReserveProjectedScratch(
		rbSharedWorldInteractionGLPreparedView_t &prepared,
		rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass ) {
	if ( !preparedPass.pass->allowScratch
			|| prepared.projectedScratchCount >= SHADOWMAP_CACHE_MAX_SLOTS ) {
		return false;
	}
	const int slotIndex = prepared.projectedScratchCount++;
	sharedInteractionProjectedScratch_t &slot =
		g_sharedInteractionProjectedScratch[slotIndex];
	const int size = preparedPass.pass->projected.state.tileSize
		* preparedPass.pass->projected.state.atlasDiv;
	if ( size <= 0 || size > glConfig.maxTextureSize ) {
		return false;
	}
	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_DEPTH;
	opts.width = size;
	opts.height = size;
	opts.numLevels = 1;
	opts.isPersistant = true;
	slot.depthImage = globalImages->ScratchImage(
		va( "_sharedInteractionShadowMapDepth%d", slotIndex ), &opts,
		TF_NEAREST, TR_CLAMP, TD_DEPTH );
	if ( slot.depthImage == NULL ) {
		return false;
	}
	if ( slot.renderTexture == NULL ) {
		slot.renderTexture = tr.CreateRenderTexture( NULL, slot.depthImage );
		if ( slot.renderTexture != NULL ) {
			slot.renderTexture->SetAllowIncomplete();
		}
	} else if ( slot.renderTexture->GetWidth() != size
			|| slot.renderTexture->GetHeight() != size ) {
		tr.ResizeRenderTexture( slot.renderTexture, size, size );
	}
	preparedPass.projectedEntry = NULL;
	preparedPass.pointEntry = NULL;
	preparedPass.pointLight = false;
	preparedPass.scratchResource = true;
	preparedPass.projectedDepthImage = slot.depthImage;
	preparedPass.pointColorImage = NULL;
	preparedPass.pointDepthImage = NULL;
	preparedPass.renderTexture = slot.renderTexture;
	preparedPass.projectedOriginX = 0;
	preparedPass.projectedOriginY = 0;
	preparedPass.projectedSpanPixels = size;
	return RB_SharedWorldInteractionGLCaptureMapResource( preparedPass );
}

static bool RB_SharedWorldInteractionGLReservePointScratch(
		rbSharedWorldInteractionGLPreparedView_t &prepared,
		rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass ) {
	if ( !preparedPass.pass->allowScratch
			|| prepared.pointScratchCount >= SHADOWMAP_CACHE_MAX_SLOTS ) {
		return false;
	}
	const int slotIndex = prepared.pointScratchCount++;
	sharedInteractionPointScratch_t &slot =
		g_sharedInteractionPointScratch[slotIndex];
	const classicInteractionDomainShadowPointState_t &point =
		preparedPass.pass->point;
	if ( point.faceSize <= 0 || point.faceSize > glConfig.maxTextureSize ) {
		return false;
	}
	idImageOpts colorOpts;
	colorOpts.textureType = TT_CUBIC;
	colorOpts.format = point.highPrecision ? FMT_RGBA16F : FMT_RGBA8;
	colorOpts.width = point.faceSize;
	colorOpts.height = point.faceSize;
	colorOpts.numLevels = 1;
	colorOpts.isPersistant = true;
	slot.colorImage = globalImages->ScratchImage(
		va( "_sharedInteractionPointShadowColor%d", slotIndex ),
		&colorOpts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	idImageOpts depthOpts;
	depthOpts.textureType = TT_CUBIC;
	depthOpts.format = FMT_DEPTH;
	depthOpts.width = point.faceSize;
	depthOpts.height = point.faceSize;
	depthOpts.numLevels = 1;
	depthOpts.isPersistant = true;
	slot.depthImage = globalImages->ScratchImage(
		va( "_sharedInteractionPointShadowDepth%d", slotIndex ),
		&depthOpts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
	if ( slot.colorImage == NULL || slot.depthImage == NULL ) {
		return false;
	}
	if ( slot.renderTexture == NULL ) {
		slot.renderTexture = tr.CreateRenderTexture(
			slot.colorImage, slot.depthImage );
		if ( slot.renderTexture != NULL ) {
			slot.renderTexture->SetAllowIncomplete();
		}
	} else if ( slot.renderTexture->GetWidth() != point.faceSize
			|| slot.renderTexture->GetHeight() != point.faceSize ) {
		tr.ResizeRenderTexture( slot.renderTexture,
			point.faceSize, point.faceSize );
	}
	preparedPass.projectedEntry = NULL;
	preparedPass.pointEntry = NULL;
	preparedPass.pointLight = true;
	preparedPass.scratchResource = true;
	preparedPass.projectedDepthImage = NULL;
	preparedPass.pointColorImage = slot.colorImage;
	preparedPass.pointDepthImage = slot.depthImage;
	preparedPass.renderTexture = slot.renderTexture;
	preparedPass.projectedOriginX = 0;
	preparedPass.projectedOriginY = 0;
	preparedPass.projectedSpanPixels = 0;
	return RB_SharedWorldInteractionGLCaptureMapResource( preparedPass );
}

static bool RB_SharedWorldInteractionGLMapUpdateAdmitted( void ) {
	const int updateBudget = r_shadowMapMaxUpdatesPerView.GetInteger();
	if ( updateBudget > 0
			&& g_shadowMapStats.shadowMapUpdates >= updateBudget ) {
		return false;
	}
	if ( updateBudget > 0 && g_shadowMapAdmissionsActive
			&& !RB_ShadowMapUpdateAdmitted(
				RB_ShadowMapLightIndex( backEnd.vLight ) ) ) {
		return false;
	}
	return true;
}

static bool RB_SharedWorldInteractionGLEnsurePointCacheResource(
		pointShadowMapCacheEntry_t *entry,
		const classicInteractionDomainShadowMapPass_t &pass ) {
	if ( entry == NULL || !pass.point.valid ) {
		return false;
	}
	const int slotIndex = static_cast<int>( entry - g_pointShadowMapCache );
	if ( slotIndex < 0 || slotIndex >= SHADOWMAP_CACHE_MAX_SLOTS ) {
		return false;
	}
	idImageOpts colorOpts;
	colorOpts.textureType = TT_CUBIC;
	colorOpts.format = pass.point.highPrecision ? FMT_RGBA16F : FMT_RGBA8;
	colorOpts.width = pass.point.faceSize;
	colorOpts.height = pass.point.faceSize;
	colorOpts.numLevels = 1;
	colorOpts.isPersistant = true;
	entry->colorImage = globalImages->ScratchImage(
		va( "_pointShadowMapColorCache%d", slotIndex ), &colorOpts,
		TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	idImageOpts depthOpts;
	depthOpts.textureType = TT_CUBIC;
	depthOpts.format = FMT_DEPTH;
	depthOpts.width = pass.point.faceSize;
	depthOpts.height = pass.point.faceSize;
	depthOpts.numLevels = 1;
	depthOpts.isPersistant = true;
	entry->depthImage = globalImages->ScratchImage(
		va( "_pointShadowMapDepthCache%d", slotIndex ), &depthOpts,
		TF_NEAREST, TR_CLAMP, TD_DEPTH );
	if ( entry->colorImage == NULL || entry->depthImage == NULL ) {
		return false;
	}
	if ( entry->renderTexture == NULL ) {
		entry->renderTexture = tr.CreateRenderTexture(
			entry->colorImage, entry->depthImage );
		if ( entry->renderTexture != NULL ) {
			entry->renderTexture->SetAllowIncomplete();
		}
	} else if ( entry->renderTexture->GetWidth() != pass.point.faceSize
			|| entry->renderTexture->GetHeight() != pass.point.faceSize ) {
		tr.ResizeRenderTexture( entry->renderTexture,
			pass.point.faceSize, pass.point.faceSize );
	}
	return entry->renderTexture != NULL;
}

static bool RB_SharedWorldInteractionGLScheduleSealedMapPass(
		const classicInteractionDomainShadowMapPass_t &pass,
		const viewDef_t *viewDef,
		int cacheLightIndex, shadowMapPassKind_t passKind,
		shadowMapSchedule_t &schedule ) {
	memset( &schedule, 0, sizeof( schedule ) );
	schedule.action = SHADOWMAP_SCHEDULE_UPDATE;
	schedule.signature = RB_ShadowMapBuildPassSignatureForView(
		pass.legacyViewLight, viewDef, passKind, pass.point.valid );
	if ( !pass.allowCacheReuse || cacheLightIndex < 0 ) {
		return RB_SharedWorldInteractionGLMapUpdateAdmitted();
	}

	schedule.cacheable = true;
	if ( pass.point.valid ) {
		schedule.pointEntry = RB_ShadowMapFindPointCacheEntry(
			cacheLightIndex, passKind, schedule.signature );
		if ( schedule.pointEntry != NULL ) {
			schedule.cacheHit = true;
			schedule.action = SHADOWMAP_SCHEDULE_REUSE;
			schedule.budgetReuse = r_shadowMapMaxUpdatesPerView.GetInteger() > 0
				&& g_shadowMapStats.shadowMapUpdates
					>= r_shadowMapMaxUpdatesPerView.GetInteger();
			g_shadowMapStats.cacheHitPasses++;
			g_shadowMapStats.cacheReusePasses++;
			g_shadowMapStats.cacheBudgetReusePasses +=
				schedule.budgetReuse ? 1 : 0;
			return true;
		}
	} else {
		schedule.projectedEntry = RB_ShadowMapFindProjectedCacheEntry(
			cacheLightIndex, passKind, schedule.signature );
		if ( schedule.projectedEntry != NULL ) {
			schedule.cacheHit = true;
			schedule.action = SHADOWMAP_SCHEDULE_REUSE;
			schedule.budgetReuse = r_shadowMapMaxUpdatesPerView.GetInteger() > 0
				&& g_shadowMapStats.shadowMapUpdates
					>= r_shadowMapMaxUpdatesPerView.GetInteger();
			g_shadowMapStats.cacheHitPasses++;
			g_shadowMapStats.cacheReusePasses++;
			g_shadowMapStats.cacheBudgetReusePasses +=
				schedule.budgetReuse ? 1 : 0;
			return true;
		}
	}
	g_shadowMapStats.cacheMissPasses++;
	if ( !RB_SharedWorldInteractionGLMapUpdateAdmitted() ) {
		schedule.action = SHADOWMAP_SCHEDULE_FALLBACK;
		return false;
	}
	if ( pass.point.valid ) {
		schedule.pointEntry = RB_ShadowMapAllocPointCacheEntry( false );
		if ( schedule.pointEntry != NULL
				&& !RB_SharedWorldInteractionGLEnsurePointCacheResource(
					schedule.pointEntry, pass ) ) {
			schedule.pointEntry = NULL;
		}
	} else {
		schedule.projectedEntry = RB_ShadowMapAllocProjectedCacheEntry(
			pass.projected.state.atlasDiv, false );
	}
	if ( pass.point.valid ? schedule.pointEntry == NULL
			: schedule.projectedEntry == NULL ) {
		// Cache capacity is an optimization limit.  The sealed scratch plan can
		// still own the pass without changing its rendered semantics.
		schedule.cacheable = false;
	}
	return true;
}

static void RB_SharedWorldInteractionGLCompleteSealedCacheUpdate(
		const rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass,
		const shadowMapSchedule_t &schedule, bool renderOk ) {
	if ( !schedule.cacheable ) {
		return;
	}
	const classicInteractionDomainShadowMapPass_t &pass = *preparedPass.pass;
	if ( preparedPass.pointLight ) {
		pointShadowMapCacheEntry_t *entry = schedule.pointEntry;
		if ( entry == NULL ) {
			return;
		}
		if ( !renderOk ) {
			entry->valid = false;
			return;
		}
		entry->valid = true;
		entry->lightIndex = preparedPass.cacheLightIndex;
		entry->passKind = preparedPass.cachePassKind;
		entry->lightClass = static_cast<int>( pass.lightClass );
		entry->signature = schedule.signature;
		entry->size = pass.point.faceSize;
		entry->highPrecision = pass.point.highPrecision;
		entry->depthCompare = pass.point.depthCompare;
		entry->lightOrigin[0] = pass.point.lightOrigin[0];
		entry->lightOrigin[1] = pass.point.lightOrigin[1];
		entry->lightOrigin[2] = pass.point.lightOrigin[2];
		entry->farDistance = pass.point.farDistance;
		entry->lastUsedFrame = tr.frameCount;
		entry->lastUpdatedFrame = tr.frameCount;
		entry->colorImage = preparedPass.pointColorImage;
		entry->depthImage = preparedPass.pointDepthImage;
		entry->renderTexture = preparedPass.renderTexture;
		entry->colorStorageGeneration = entry->colorImage != NULL
			? entry->colorImage->GetStorageGeneration() : 0;
		entry->depthStorageGeneration = entry->depthImage != NULL
			? entry->depthImage->GetStorageGeneration() : 0;
		return;
	}
	projectedShadowMapCacheEntry_t *entry = schedule.projectedEntry;
	if ( entry == NULL ) {
		return;
	}
	if ( !renderOk ) {
		entry->valid = false;
		return;
	}
	entry->valid = true;
	entry->lightIndex = preparedPass.cacheLightIndex;
	entry->passKind = preparedPass.cachePassKind;
	entry->lightClass = static_cast<int>( pass.lightClass );
	entry->signature = schedule.signature;
	entry->tileSize = pass.projected.state.tileSize;
	entry->atlasDiv = pass.projected.state.atlasDiv;
	entry->cascadeCount = pass.projected.state.cascadeCount;
	entry->lastUsedFrame = tr.frameCount;
	entry->lastUpdatedFrame = tr.frameCount;
	entry->atlasStorageGeneration = preparedPass.projectedDepthImage != NULL
		? preparedPass.projectedDepthImage->GetStorageGeneration() : 0;
	entry->state = pass.projected.state;
}

static bool RB_SharedWorldInteractionGLProjectedEntryMatches(
		const rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass ) {
	const classicInteractionDomainShadowMapPass_t &pass = *preparedPass.pass;
	const projectedShadowMapCacheEntry_t *entry = preparedPass.projectedEntry;
	if ( preparedPass.pointLight || preparedPass.projectedDepthImage == NULL
			|| !preparedPass.projectedDepthImage->IsLoaded()
			|| preparedPass.projectedDepthImage->IsDefaulted()
			|| preparedPass.projectedDepthImage->GetDeviceHandle()
				!= preparedPass.sampleTextureHandle
			|| preparedPass.projectedDepthImage->GetStorageGeneration()
				!= preparedPass.sampleStorageGeneration
			|| preparedPass.projectedDepthImage->GetOpts().textureType != TT_2D
			|| preparedPass.projectedDepthImage->GetOpts().format != FMT_DEPTH
			|| preparedPass.renderTexture == NULL
			|| preparedPass.renderTexture->GetWidth()
				!= preparedPass.resourceWidth
			|| preparedPass.renderTexture->GetHeight()
				!= preparedPass.resourceHeight
			|| preparedPass.projectedSpanPixels
				!= pass.projected.state.tileSize
					* pass.projected.state.atlasDiv ) {
		return false;
	}
	if ( preparedPass.scratchResource ) {
		return entry == NULL && preparedPass.projectedOriginX == 0
			&& preparedPass.projectedOriginY == 0
			&& preparedPass.resourceWidth
				== preparedPass.projectedSpanPixels
			&& preparedPass.resourceHeight
				== preparedPass.projectedSpanPixels;
	}
	return entry != NULL && entry->valid
		&& entry->lightIndex == preparedPass.cacheLightIndex
		&& entry->passKind == preparedPass.cachePassKind
		&& entry->lightClass == static_cast<int>( pass.lightClass )
		&& entry->signature == preparedPass.signature
		&& entry->atlasStorageGeneration != 0
		&& entry->atlasStorageGeneration
			== preparedPass.sampleStorageGeneration
		&& entry->state.valid
		&& entry->tileSize == pass.projected.state.tileSize
		&& entry->atlasDiv == pass.projected.state.atlasDiv
		&& entry->cascadeCount == pass.projected.state.cascadeCount
		&& std::memcmp( &entry->state, &pass.projected.state,
			sizeof( entry->state ) ) == 0
		&& preparedPass.projectedDepthImage == g_shadowMapAtlasDepthImage
		&& preparedPass.renderTexture == g_shadowMapAtlasRenderTexture
		&& entry->atlasCellSpan == pass.projected.state.atlasDiv
		&& preparedPass.projectedOriginX
			== entry->atlasCellX * g_shadowMapAtlasCellSize
		&& preparedPass.projectedOriginY
			== entry->atlasCellY * g_shadowMapAtlasCellSize;
}

static bool RB_SharedWorldInteractionGLPointEntryMatches(
		const rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass ) {
	const classicInteractionDomainShadowMapPass_t &pass = *preparedPass.pass;
	const pointShadowMapCacheEntry_t *entry = preparedPass.pointEntry;
	idImage *sampleImage = pass.point.depthCompare
		? preparedPass.pointDepthImage : preparedPass.pointColorImage;
	if ( !preparedPass.pointLight || sampleImage == NULL
			|| preparedPass.pointColorImage == NULL
			|| !preparedPass.pointColorImage->IsLoaded()
			|| preparedPass.pointColorImage->IsDefaulted()
			|| preparedPass.pointColorImage->GetDeviceHandle() == 0
			|| preparedPass.pointColorImage->GetOpts().textureType != TT_CUBIC
			|| preparedPass.pointColorImage->GetOpts().format
				!= ( pass.point.highPrecision ? FMT_RGBA16F : FMT_RGBA8 )
			|| preparedPass.pointDepthImage == NULL
			|| !preparedPass.pointDepthImage->IsLoaded()
			|| preparedPass.pointDepthImage->IsDefaulted()
			|| preparedPass.pointDepthImage->GetDeviceHandle() == 0
			|| preparedPass.pointDepthImage->GetOpts().textureType != TT_CUBIC
			|| preparedPass.pointDepthImage->GetOpts().format != FMT_DEPTH
			|| !sampleImage->IsLoaded() || sampleImage->IsDefaulted()
			|| sampleImage->GetDeviceHandle()
				!= preparedPass.sampleTextureHandle
			|| sampleImage->GetStorageGeneration()
				!= preparedPass.sampleStorageGeneration
			|| sampleImage->GetOpts().textureType != TT_CUBIC
			|| preparedPass.renderTexture == NULL
			|| preparedPass.renderTexture->GetWidth()
				!= pass.point.faceSize
			|| preparedPass.renderTexture->GetHeight()
				!= pass.point.faceSize
			|| preparedPass.resourceWidth != pass.point.faceSize
			|| preparedPass.resourceHeight != pass.point.faceSize ) {
		return false;
	}
	if ( preparedPass.scratchResource ) {
		return entry == NULL && preparedPass.pointColorImage != NULL
			&& preparedPass.pointDepthImage != NULL;
	}
	return entry != NULL && entry->valid
		&& entry->lightIndex == preparedPass.cacheLightIndex
		&& entry->passKind == preparedPass.cachePassKind
		&& entry->lightClass == static_cast<int>( pass.lightClass )
		&& entry->signature == preparedPass.signature
		&& entry->size == pass.point.faceSize
		&& entry->highPrecision == pass.point.highPrecision
		&& entry->depthCompare == pass.point.depthCompare
		&& entry->lightOrigin[0] == pass.point.lightOrigin[0]
		&& entry->lightOrigin[1] == pass.point.lightOrigin[1]
		&& entry->lightOrigin[2] == pass.point.lightOrigin[2]
		&& entry->farDistance == pass.point.farDistance
		&& entry->colorImage == preparedPass.pointColorImage
		&& entry->depthImage == preparedPass.pointDepthImage
		&& entry->colorStorageGeneration != 0
		&& entry->depthStorageGeneration != 0
		&& entry->colorStorageGeneration
			== preparedPass.pointColorImage->GetStorageGeneration()
		&& entry->depthStorageGeneration
			== preparedPass.pointDepthImage->GetStorageGeneration()
		&& entry->renderTexture == preparedPass.renderTexture;
}

static void RB_SharedWorldInteractionGLApplyMapCasterCull(
		const classicInteractionDomainShadowMapPass_t &pass,
		const classicInteractionDomainShadowCaster_t &caster ) {
	int desired = 0;
	const bool automaticTwoSided = pass.casterCullMode == 2
		&& ( !caster.perfectHull
			|| R_ShadowMapLightOriginInsideCasterBounds(
				pass.legacyViewLight, caster.modelMatrix,
				caster.boundsMin, caster.boundsMax ) );
	if ( pass.casterCullMode != 0 && !automaticTwoSided && caster.cull != RENDERER_CULL_NONE ) {
		GLenum face = GL_FRONT;
		if ( caster.cull == RENDERER_CULL_BACK ) {
			face = face == GL_FRONT ? GL_BACK : GL_FRONT;
		}
		desired = static_cast<int>( face );
	}
	if ( desired == 0 ) {
		glDisable( GL_CULL_FACE );
	} else {
		glEnable( GL_CULL_FACE );
		glCullFace( static_cast<GLenum>( desired ) );
	}
}

static void RB_SharedWorldInteractionGLSubmitMapCaster(
		const rbSharedWorldInteractionGLPreparedMapCaster_t &caster ) {
	idVertexCache::BindArrayBuffer( caster.vertexBuffer );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( caster.vertexPointer,
			DRAWVERT_XYZ_OFFSET ) );
	idVertexCache::BindIndexBuffer( caster.indexBuffer );
	backEnd.pc.c_drawElements++;
	backEnd.pc.c_drawIndexes += caster.indexCount;
	backEnd.pc.c_drawVertexes += caster.vertexCount;
	if ( caster.indexCacheBacked ) {
		backEnd.pc.c_vboIndexes += caster.indexCount;
	}
	glDrawElements( GL_TRIANGLES, caster.indexCount, GL_INDEX_TYPE,
		caster.indexPointer );
}

static void RB_SharedWorldInteractionGLDrawProjectedMapCaster(
		const rbSharedWorldInteractionGLPreparedView_t &prepared,
		const rbSharedWorldInteractionGLPreparedMapCaster_t &preparedCaster,
		const classicInteractionDomainShadowMapPass_t &pass,
		int cascadeIndex ) {
	const classicInteractionDomainShadowCaster_t &caster =
		*preparedCaster.caster;
	glLoadMatrixf( caster.modelMatrix );
	float row0[4];
	float row1[4];
	float row2[4];
	RB_ShadowMapModelMatrixRows( caster.modelMatrix, row0, row1, row2 );
	if ( g_shadowMapCasterProgram.modelMatrixRow0 >= 0 ) {
		glUniform4fvARB( g_shadowMapCasterProgram.modelMatrixRow0, 1, row0 );
		glUniform4fvARB( g_shadowMapCasterProgram.modelMatrixRow1, 1, row1 );
		glUniform4fvARB( g_shadowMapCasterProgram.modelMatrixRow2, 1, row2 );
	}
	if ( g_shadowMapCasterProgram.shadowDepthRow >= 0 ) {
		idPlane localDepth;
		R_GlobalPlaneToLocal( caster.modelMatrix,
			pass.projected.state.clipPlanes[cascadeIndex][2], localDepth );
		glUniform4fvARB( g_shadowMapCasterProgram.shadowDepthRow, 1,
			localDepth.ToFloatPtr() );
	}
	RB_SharedWorldInteractionGLApplyMapCasterCull( pass, caster );
	if ( preparedCaster.alphaStageCount <= 0 ) {
		RB_ShadowMapDisableCasterAlphaTest();
		RB_SharedWorldInteractionGLSubmitMapCaster( preparedCaster );
		return;
	}
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	idVertexCache::BindArrayBuffer( preparedCaster.vertexBuffer );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( preparedCaster.vertexPointer,
			DRAWVERT_ST_OFFSET ) );
	if ( g_shadowMapCasterProgram.alphaTestEnabled >= 0 ) {
		glUniform1fARB( g_shadowMapCasterProgram.alphaTestEnabled, 1.0f );
	}
	if ( g_shadowMapCasterProgram.alphaHashEnabled >= 0 ) {
		glUniform1fARB( g_shadowMapCasterProgram.alphaHashEnabled,
			pass.hashedAlpha ? 1.0f : 0.0f );
	}
	if ( g_shadowMapCasterProgram.alphaHashStable >= 0 ) {
		glUniform1fARB( g_shadowMapCasterProgram.alphaHashStable,
			pass.stableAlphaHash ? 1.0f : 0.0f );
	}
	for ( int i = 0; i < preparedCaster.alphaStageCount; ++i ) {
		const rbSharedWorldInteractionGLPreparedShadowAlphaStage_t &preparedStage =
			prepared.shadowAlphaStages[preparedCaster.firstAlphaStage + i];
		const classicInteractionDomainShadowAlphaStage_t &stage =
			*preparedStage.stage;
		if ( g_shadowMapCasterProgram.alphaRef >= 0 ) {
			glUniform1fARB( g_shadowMapCasterProgram.alphaRef,
				stage.alphaTestValue );
			glUniform1fARB( g_shadowMapCasterProgram.alphaTestMode,
				RB_ShadowMapAlphaTestModeValue( stage.alphaTestMode ) );
			glUniform1fARB( g_shadowMapCasterProgram.alphaScale,
				stage.alphaScale );
		}
		glUniform4fvARB( g_shadowMapCasterProgram.alphaTexCoordS, 1,
			stage.textureMatrix[0] );
		glUniform4fvARB( g_shadowMapCasterProgram.alphaTexCoordT, 1,
			stage.textureMatrix[1] );
		GL_SelectTexture( 0 );
		preparedStage.image->Bind();
		RB_SharedWorldInteractionGLSubmitMapCaster( preparedCaster );
	}
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	RB_ShadowMapDisableCasterAlphaTest();
}

static void RB_SharedWorldInteractionGLDrawPointMapCaster(
		const rbSharedWorldInteractionGLPreparedView_t &prepared,
		const rbSharedWorldInteractionGLPreparedMapCaster_t &preparedCaster,
		const classicInteractionDomainShadowMapPass_t &pass,
		const float lightModelViewMatrix[16] ) {
	const classicInteractionDomainShadowCaster_t &caster =
		*preparedCaster.caster;
	float modelViewMatrix[16];
	myGlMultMatrix( caster.modelMatrix, lightModelViewMatrix,
		modelViewMatrix );
	glLoadMatrixf( modelViewMatrix );
	float row0[4];
	float row1[4];
	float row2[4];
	RB_ShadowMapModelMatrixRows( caster.modelMatrix, row0, row1, row2 );
	glUniform4fvARB( g_pointShadowCasterProgram.modelMatrixRow0, 1, row0 );
	glUniform4fvARB( g_pointShadowCasterProgram.modelMatrixRow1, 1, row1 );
	glUniform4fvARB( g_pointShadowCasterProgram.modelMatrixRow2, 1, row2 );
	RB_SharedWorldInteractionGLApplyMapCasterCull( pass, caster );
	if ( preparedCaster.alphaStageCount <= 0 ) {
		if ( g_pointShadowCasterProgram.alphaTestEnabled >= 0 ) {
			glUniform1fARB( g_pointShadowCasterProgram.alphaTestEnabled, 0.0f );
		}
		RB_SharedWorldInteractionGLSubmitMapCaster( preparedCaster );
		return;
	}
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	idVertexCache::BindArrayBuffer( preparedCaster.vertexBuffer );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( preparedCaster.vertexPointer,
			DRAWVERT_ST_OFFSET ) );
	glUniform1fARB( g_pointShadowCasterProgram.alphaTestEnabled, 1.0f );
	glUniform1fARB( g_pointShadowCasterProgram.alphaHashEnabled,
		pass.hashedAlpha ? 1.0f : 0.0f );
	glUniform1fARB( g_pointShadowCasterProgram.alphaHashStable,
		pass.stableAlphaHash ? 1.0f : 0.0f );
	for ( int i = 0; i < preparedCaster.alphaStageCount; ++i ) {
		const rbSharedWorldInteractionGLPreparedShadowAlphaStage_t &preparedStage =
			prepared.shadowAlphaStages[preparedCaster.firstAlphaStage + i];
		const classicInteractionDomainShadowAlphaStage_t &stage =
			*preparedStage.stage;
		glUniform1fARB( g_pointShadowCasterProgram.alphaRef,
			stage.alphaTestValue );
		glUniform1fARB( g_pointShadowCasterProgram.alphaTestMode,
			RB_ShadowMapAlphaTestModeValue( stage.alphaTestMode ) );
		glUniform1fARB( g_pointShadowCasterProgram.alphaScale,
			stage.alphaScale );
		glUniform4fvARB( g_pointShadowCasterProgram.alphaTexCoordS, 1,
			stage.textureMatrix[0] );
		glUniform4fvARB( g_pointShadowCasterProgram.alphaTexCoordT, 1,
			stage.textureMatrix[1] );
		GL_SelectTexture( 0 );
		preparedStage.image->Bind();
		RB_SharedWorldInteractionGLSubmitMapCaster( preparedCaster );
	}
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glUniform1fARB( g_pointShadowCasterProgram.alphaTestEnabled, 0.0f );
}

static int RB_SharedWorldInteractionGLDrawMapCasterChain(
		const rbSharedWorldInteractionGLPreparedView_t &prepared,
		const rbSharedWorldInteractionGLPreparedLight_t &light,
		classicInteractionDomainShadowChain_t chain,
		const classicInteractionDomainShadowMapPass_t &pass,
		int cascadeIndex, const float *pointLightModelView ) {
	const int first = light.firstMapCaster[chain];
	const int count = light.mapCasterCount[chain];
	for ( int i = 0; i < count; ++i ) {
		const rbSharedWorldInteractionGLPreparedMapCaster_t &caster =
			prepared.mapCasters[first + i];
		if ( pointLightModelView != NULL ) {
			RB_SharedWorldInteractionGLDrawPointMapCaster(
				prepared, caster, pass, pointLightModelView );
		} else {
			RB_SharedWorldInteractionGLDrawProjectedMapCaster(
				prepared, caster, pass, cascadeIndex );
		}
	}
	return count;
}

static bool RB_SharedWorldInteractionGLRenderProjectedMap(
		const rbSharedWorldInteractionGLPreparedView_t &prepared,
		const rbSharedWorldInteractionGLPreparedLight_t &light,
		rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass ) {
	const classicInteractionDomainShadowMapPass_t &pass = *preparedPass.pass;
	RB_SharedWorldInteractionGLActivateMapResource( preparedPass );
	if ( preparedPass.renderTexture == NULL
			|| preparedPass.projectedDepthImage == NULL ) {
		return false;
	}
	g_projectedShadowMapState = pass.projected.state;
	preparedPass.renderTexture->MakeCurrent();
	if ( preparedPass.renderTexture->IsKnownIncomplete() ) {
		RB_SharedWorldInteractionGLAbortMapTransaction( prepared.viewDef );
		return false;
	}
	glUseProgramObjectARB( g_shadowMapCasterProgram.programObject );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	const float oneDepthStep = 1.0f / 16777216.0f;
	glUniform2fARB( g_shadowMapCasterProgram.casterDepthOffset,
		pass.polygonFactor, pass.polygonOffset * oneDepthStep );
	glUniform1iARB( g_shadowMapCasterProgram.alphaMap, 0 );
	RB_ShadowMapDisableCasterAlphaTest();
	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glDisable( GL_BLEND );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_SCISSOR_TEST );
	int drawn = 0;
	for ( int cascade = 0; cascade < pass.projected.state.cascadeCount;
			++cascade ) {
		const int tileX = g_shadowMapActiveSlotOriginX
			+ ( pass.projected.state.atlasDiv > 1
				? cascade % pass.projected.state.atlasDiv
					* pass.projected.state.tileSize : 0 );
		const int tileY = g_shadowMapActiveSlotOriginY
			+ ( pass.projected.state.atlasDiv > 1
				? cascade / pass.projected.state.atlasDiv
					* pass.projected.state.tileSize : 0 );
		float clipMatrix[16];
		R_ShadowMapClipPlanesToGLMatrix(
			pass.projected.state.clipPlanes[cascade], clipMatrix );
		glViewport( tileX, tileY, pass.projected.state.tileSize,
			pass.projected.state.tileSize );
		glScissor( tileX, tileY, pass.projected.state.tileSize,
			pass.projected.state.tileSize );
		glClear( GL_DEPTH_BUFFER_BIT );
		glMatrixMode( GL_PROJECTION );
		glLoadMatrixf( clipMatrix );
		glMatrixMode( GL_MODELVIEW );
		drawn += RB_SharedWorldInteractionGLDrawMapCasterChain(
			prepared, light,
			CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC,
			pass, cascade, NULL );
		drawn += RB_SharedWorldInteractionGLDrawMapCasterChain(
			prepared, light,
			CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC,
			pass, cascade, NULL );
		if ( pass.receiver == CLASSIC_INTERACTION_RECEIVER_GLOBAL ) {
			drawn += RB_SharedWorldInteractionGLDrawMapCasterChain(
				prepared, light,
				CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC,
				pass, cascade, NULL );
			drawn += RB_SharedWorldInteractionGLDrawMapCasterChain(
				prepared, light,
				CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC,
				pass, cascade, NULL );
		}
	}
	glEnable( GL_CULL_FACE );
	glUseProgramObjectARB( 0 );
	g_shadowMapStats.shadowMapUpdates++;
	g_shadowMapStats.projectedAtlasTilesRendered +=
		pass.projected.state.cascadeCount;
	RB_SharedWorldInteractionGLAbortMapTransaction( prepared.viewDef );
	return drawn == pass.drawableMappedCasters
		* pass.projected.state.cascadeCount;
}

static bool RB_SharedWorldInteractionGLRenderPointMap(
		const rbSharedWorldInteractionGLPreparedView_t &prepared,
		const rbSharedWorldInteractionGLPreparedLight_t &light,
		rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass ) {
	const classicInteractionDomainShadowMapPass_t &pass = *preparedPass.pass;
	RB_SharedWorldInteractionGLActivateMapResource( preparedPass );
	if ( preparedPass.renderTexture == NULL
			|| preparedPass.pointColorImage == NULL
			|| preparedPass.pointDepthImage == NULL ) {
		return false;
	}
	preparedPass.renderTexture->MakeCurrent( 0 );
	if ( preparedPass.renderTexture->IsKnownIncomplete() ) {
		RB_SharedWorldInteractionGLAbortMapTransaction( prepared.viewDef );
		return false;
	}
	glUseProgramObjectARB( g_pointShadowCasterProgram.programObject );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUniform4fvARB( g_pointShadowCasterProgram.globalLightOrigin, 1,
		pass.point.lightOrigin );
	glUniform1fARB( g_pointShadowCasterProgram.pointShadowFar,
		pass.point.farDistance );
	glUniform1fARB( g_pointShadowCasterProgram.pointShadowDepthMode,
		pass.point.highPrecision ? 1.0f : 0.0f );
	const float oneDepthStep = 1.0f / 16777216.0f;
	glUniform2fARB( g_pointShadowCasterProgram.casterDepthOffset,
		pass.polygonFactor, pass.polygonOffset * oneDepthStep );
	glUniform1iARB( g_pointShadowCasterProgram.alphaMap, 0 );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glDisable( GL_BLEND );
	glDisable( GL_STENCIL_TEST );
	const bool depthClamp = RB_ShadowMapDepthClampAvailable();
	if ( depthClamp ) {
		glEnable( GL_DEPTH_CLAMP_NV );
	}
	glClearColor( 1.0f, 1.0f, 1.0f, 1.0f );
	const float nearClip = idMath::ClampFloat( 0.5f,
		depthClamp ? 16.0f : 4.0f,
		pass.point.farDistance * 0.01f );
	float projectionMatrix[16];
	RB_PointShadowMapBuildProjectionMatrix( nearClip,
		pass.point.farDistance, projectionMatrix );
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
	int drawn = 0;
	const idVec3 sealedLightOrigin( pass.point.lightOrigin[0],
		pass.point.lightOrigin[1], pass.point.lightOrigin[2] );
	for ( int face = 0; face < pass.point.faceCount; ++face ) {
		float lightModelView[16];
		RB_PointShadowMapBuildModelViewMatrix(
			sealedLightOrigin, face, lightModelView );
		preparedPass.renderTexture->MakeCurrent( face );
		glViewport( 0, 0, pass.point.faceSize, pass.point.faceSize );
		glScissor( 0, 0, pass.point.faceSize, pass.point.faceSize );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		drawn += RB_SharedWorldInteractionGLDrawMapCasterChain(
			prepared, light,
			CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC,
			pass, 0, lightModelView );
		drawn += RB_SharedWorldInteractionGLDrawMapCasterChain(
			prepared, light,
			CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC,
			pass, 0, lightModelView );
		if ( pass.receiver == CLASSIC_INTERACTION_RECEIVER_GLOBAL ) {
			drawn += RB_SharedWorldInteractionGLDrawMapCasterChain(
				prepared, light,
				CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC,
				pass, 0, lightModelView );
			drawn += RB_SharedWorldInteractionGLDrawMapCasterChain(
				prepared, light,
				CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC,
				pass, 0, lightModelView );
		}
	}
	if ( depthClamp ) {
		glDisable( GL_DEPTH_CLAMP_NV );
	}
	glEnable( GL_CULL_FACE );
	glUseProgramObjectARB( 0 );
	g_shadowMapStats.shadowMapUpdates++;
	g_shadowMapStats.pointFacesRendered += pass.point.faceCount;
	g_shadowMapStats.pointHighPrecisionPasses +=
		pass.point.highPrecision ? 1 : 0;
	g_shadowMapStats.pointDepthComparePasses +=
		pass.point.depthCompare ? 1 : 0;
	RB_SharedWorldInteractionGLAbortMapTransaction( prepared.viewDef );
	return drawn == pass.drawableMappedCasters * pass.point.faceCount;
}

static bool RB_SharedWorldInteractionGLAliasMappedPass(
		const rbSharedWorldInteractionGLPreparedLight_t &preparedLight,
		rbSharedWorldInteractionGLPreparedView_t &prepared,
		rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass ) {
	const classicInteractionDomainShadowMapPass_t &pass = *preparedPass.pass;
	if ( !pass.resourceAlias
			|| pass.resourceOwner < CLASSIC_INTERACTION_RECEIVER_LOCAL
			|| pass.resourceOwner > CLASSIC_INTERACTION_RECEIVER_GLOBAL ) {
		return false;
	}
	const int ownerIndex = preparedLight.shadowMapPass[pass.resourceOwner];
	if ( ownerIndex < 0 || ownerIndex >= prepared.shadowMapPassCount
			|| &prepared.shadowMapPasses[ownerIndex] == &preparedPass ) {
		return false;
	}
	const rbSharedWorldInteractionGLPreparedMapPass_t &owner =
		prepared.shadowMapPasses[ownerIndex];
	if ( !owner.prepared || owner.pass == NULL
			|| owner.pass->legacyViewLight != pass.legacyViewLight
			|| owner.pointLight != pass.point.valid
			|| owner.pass->drawableMappedCasters
				!= pass.drawableMappedCasters
			|| owner.pass->noopMappedCasters != pass.noopMappedCasters
			|| ( owner.pointLight
				? std::memcmp( &owner.pass->point, &pass.point,
					sizeof( pass.point ) ) != 0
				: std::memcmp( &owner.pass->projected, &pass.projected,
					sizeof( pass.projected ) ) != 0 ) ) {
		return false;
	}
	preparedPass.projectedEntry = owner.projectedEntry;
	preparedPass.pointEntry = owner.pointEntry;
	preparedPass.projectedDepthImage = owner.projectedDepthImage;
	preparedPass.pointColorImage = owner.pointColorImage;
	preparedPass.pointDepthImage = owner.pointDepthImage;
	preparedPass.renderTexture = owner.renderTexture;
	preparedPass.sampleStorageGeneration = owner.sampleStorageGeneration;
	preparedPass.sampleTextureHandle = owner.sampleTextureHandle;
	preparedPass.resourceWidth = owner.resourceWidth;
	preparedPass.resourceHeight = owner.resourceHeight;
	preparedPass.projectedOriginX = owner.projectedOriginX;
	preparedPass.projectedOriginY = owner.projectedOriginY;
	preparedPass.projectedSpanPixels = owner.projectedSpanPixels;
	preparedPass.cacheLightIndex = owner.cacheLightIndex;
	preparedPass.cachePassKind = owner.cachePassKind;
	preparedPass.signature = owner.signature;
	preparedPass.pointLight = owner.pointLight;
	preparedPass.scratchResource = owner.scratchResource;
	preparedPass.cacheUpdated = owner.cacheUpdated;
	preparedPass.prepared = owner.pointLight
		? RB_SharedWorldInteractionGLPointEntryMatches( preparedPass )
		: RB_SharedWorldInteractionGLProjectedEntryMatches( preparedPass );
	return preparedPass.prepared;
}

static bool RB_SharedWorldInteractionGLPrepareMappedPass(
		rbSharedWorldInteractionGLPreparedView_t &prepared,
		rbSharedWorldInteractionGLPreparedLight_t &preparedLight,
		classicInteractionDomainReceiver_t receiver ) {
	rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass =
		prepared.shadowMapPasses[preparedLight.shadowMapPass[receiver]];
	const classicInteractionDomainLight_t &light = *preparedLight.light;
	const classicInteractionDomainShadowMapPass_t &pass = *preparedPass.pass;
	viewLight_t *viewLight = const_cast<viewLight_t *>( light.legacyViewLight );
	const bool pointLight = light.pointLight;
	const shadowMapPassKind_t requestedPassKind = receiver
		== CLASSIC_INTERACTION_RECEIVER_LOCAL
			? SHADOWMAP_PASS_LOCAL : SHADOWMAP_PASS_GLOBAL;
	if ( viewLight == NULL || pass.hasTranslucentCasters
			|| RB_ShadowMapLightSupportReason( viewLight )
				!= SHADOWMAP_SUPPORT_OK ) {
		return false;
	}

	backEnd.vLight = viewLight;
	const shadowMapPassKind_t passKind =
		RB_ShadowMapCachePassKind( viewLight, requestedPassKind );
	preparedPass.pointLight = pointLight;
	preparedPass.cacheLightIndex = RB_ShadowMapLightIndex( viewLight );
	preparedPass.cachePassKind = static_cast<int>( passKind );
	if ( pass.resourceAlias ) {
		return RB_SharedWorldInteractionGLAliasMappedPass(
			preparedLight, prepared, preparedPass );
	}
	if ( !pass.allowCacheUpdate ) {
		return false;
	}

	shadowMapSchedule_t schedule;
	if ( !RB_SharedWorldInteractionGLScheduleSealedMapPass( pass,
			prepared.viewDef,
			preparedPass.cacheLightIndex, passKind, schedule ) ) {
		return false;
	}
	if ( schedule.action == SHADOWMAP_SCHEDULE_REUSE ) {
		preparedPass.signature = schedule.signature;
		const bool assigned = pointLight
			? RB_SharedWorldInteractionGLAssignPointCacheResource(
				preparedPass, schedule.pointEntry )
			: RB_SharedWorldInteractionGLAssignProjectedCacheResource(
				preparedPass, schedule.projectedEntry );
		if ( !assigned ) {
			return false;
		}
		preparedPass.prepared = pointLight
			? RB_SharedWorldInteractionGLPointEntryMatches( preparedPass )
			: RB_SharedWorldInteractionGLProjectedEntryMatches(
				preparedPass );
		return preparedPass.prepared;
	}
	const bool useCacheResource = schedule.cacheable
		&& ( pointLight ? schedule.pointEntry != NULL
			: schedule.projectedEntry != NULL );

	preparedPass.signature = schedule.signature;
	const bool resourceReady = useCacheResource
		? ( pointLight
			? RB_SharedWorldInteractionGLAssignPointCacheResource(
				preparedPass, schedule.pointEntry )
			: RB_SharedWorldInteractionGLAssignProjectedCacheResource(
				preparedPass, schedule.projectedEntry ) )
		: ( pointLight
			? RB_SharedWorldInteractionGLReservePointScratch(
				prepared, preparedPass )
			: RB_SharedWorldInteractionGLReserveProjectedScratch(
				prepared, preparedPass ) );
	if ( !resourceReady ) {
		return false;
	}
	const bool renderOk = pointLight
		? RB_SharedWorldInteractionGLRenderPointMap(
			prepared, preparedLight, preparedPass )
		: RB_SharedWorldInteractionGLRenderProjectedMap(
			prepared, preparedLight, preparedPass );
	if ( useCacheResource ) {
		RB_SharedWorldInteractionGLCompleteSealedCacheUpdate(
			preparedPass, schedule, renderOk );
	}
	if ( !renderOk
			|| !RB_SharedWorldInteractionGLCaptureMapResource( preparedPass ) ) {
		return false;
	}
	preparedPass.cacheUpdated = useCacheResource;
	preparedPass.prepared = pointLight
		? RB_SharedWorldInteractionGLPointEntryMatches( preparedPass )
		: RB_SharedWorldInteractionGLProjectedEntryMatches( preparedPass );
	return preparedPass.prepared;
}

static bool RB_SharedWorldInteractionGLPreflightMapTransaction(
		const viewDef_t *viewDef,
		rbSharedWorldInteractionGLPreparedView_t &prepared,
		int &failureDetail ) {
	if ( !prepared.mapTransactionPrepared ) {
		RB_ShadowMapStatsReset();
		RB_ShadowMapDebugOverlayReset();
	}
	if ( prepared.shadowMapPassCount <= 0 ) {
		prepared.mapTransactionPrepared = true;
		return true;
	}
	if ( !prepared.mapGLStateCaptured ) {
		prepared.mapGLStateCaptured = true;
		prepared.mapBlendWasEnabled = glIsEnabled( GL_BLEND ) != 0;
		prepared.mapDepthWasEnabled = glIsEnabled( GL_DEPTH_TEST ) != 0;
		prepared.mapScissorWasEnabled =
			glIsEnabled( GL_SCISSOR_TEST ) != 0;
		prepared.mapStencilWasEnabled =
			glIsEnabled( GL_STENCIL_TEST ) != 0;
		prepared.mapCullWasEnabled = glIsEnabled( GL_CULL_FACE ) != 0;
		GLboolean depthWrite = GL_TRUE;
		GLboolean colorWrite[4] = {
			GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE
		};
		glGetBooleanv( GL_DEPTH_WRITEMASK, &depthWrite );
		glGetBooleanv( GL_COLOR_WRITEMASK, colorWrite );
		prepared.mapDepthWriteWasEnabled = depthWrite != 0;
		for ( int component = 0; component < 4; ++component ) {
			prepared.mapColorWriteWasEnabled[component] =
				colorWrite[component] != 0;
		}
		prepared.mapSavedFaceCulling = backEnd.glState.faceCulling;
		glGetFloatv( GL_COLOR_CLEAR_VALUE, prepared.mapSavedClearColor );
	}
	RB_ShadowMapBuildUpdateAdmissions();
	for ( int lightIndex = 0; lightIndex < prepared.lightCount;
			++lightIndex ) {
		rbSharedWorldInteractionGLPreparedLight_t &preparedLight =
			prepared.lights[lightIndex];
		for ( int receiver = CLASSIC_INTERACTION_RECEIVER_LOCAL;
				receiver <= CLASSIC_INTERACTION_RECEIVER_GLOBAL; ++receiver ) {
			const int mapPassIndex = preparedLight.shadowMapPass[receiver];
			if ( mapPassIndex < 0 ) {
				continue;
			}
			if ( mapPassIndex >= prepared.shadowMapPassCount
					|| !RB_SharedWorldInteractionGLPrepareMappedPass(
						prepared, preparedLight,
						static_cast<classicInteractionDomainReceiver_t>(
							receiver ) ) ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_TRANSACTION,
					lightIndex );
				RB_SharedWorldInteractionGLAbortMapTransaction( viewDef );
				return false;
			}
		}
	}

	// A later allocation may have evicted a pass prepared earlier in the same
	// view. Revalidate every exact signature only after all reservations and
	// renders have completed; no main-framebuffer mutation has happened yet.
	for ( int passIndex = 0; passIndex < prepared.shadowMapPassCount;
			++passIndex ) {
		const rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass =
			prepared.shadowMapPasses[passIndex];
		if ( !preparedPass.prepared
				|| ( preparedPass.pointLight
					? !RB_SharedWorldInteractionGLPointEntryMatches( preparedPass )
					: !RB_SharedWorldInteractionGLProjectedEntryMatches(
						preparedPass ) ) ) {
			failureDetail = RB_SharedWorldInteractionGLFailureDetail(
				RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_RESOURCE );
			RB_SharedWorldInteractionGLAbortMapTransaction( viewDef );
			return false;
		}
		for ( int earlierIndex = 0; earlierIndex < passIndex;
				++earlierIndex ) {
			const rbSharedWorldInteractionGLPreparedMapPass_t &earlier =
				prepared.shadowMapPasses[earlierIndex];
			const bool physicalAlias = preparedPass.pointLight
				? earlier.pointLight
					&& preparedPass.sampleTextureHandle
						== earlier.sampleTextureHandle
				: !earlier.pointLight
					&& preparedPass.projectedDepthImage
						== earlier.projectedDepthImage
					&& preparedPass.projectedOriginX
						< earlier.projectedOriginX
							+ earlier.projectedSpanPixels
					&& earlier.projectedOriginX
						< preparedPass.projectedOriginX
							+ preparedPass.projectedSpanPixels
					&& preparedPass.projectedOriginY
						< earlier.projectedOriginY
							+ earlier.projectedSpanPixels
					&& earlier.projectedOriginY
						< preparedPass.projectedOriginY
							+ preparedPass.projectedSpanPixels;
			const auto exactOwnerAlias = [](
					const rbSharedWorldInteractionGLPreparedMapPass_t &alias,
					const rbSharedWorldInteractionGLPreparedMapPass_t &owner ) {
				return alias.pass->resourceAlias
					&& alias.pass->legacyViewLight
						== owner.pass->legacyViewLight
					&& alias.pass->lightIndex == owner.pass->lightIndex
					&& alias.pass->resourceOwner == owner.pass->receiver
					&& !owner.pass->resourceAlias;
			};
			const bool permittedAlias = exactOwnerAlias(
				preparedPass, earlier ) || exactOwnerAlias(
				earlier, preparedPass );
			if ( physicalAlias && !permittedAlias ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_RESOURCE );
				RB_SharedWorldInteractionGLAbortMapTransaction( viewDef );
				return false;
			}
		}
	}
	RB_SharedWorldInteractionGLAbortMapTransaction( viewDef );
	prepared.mapTransactionPrepared = true;
	return true;
}

static bool RB_ARB2_SharedInteractionPreflight( const viewDef_t *viewDef,
		const classicInteractionDomainView_t &view, int &failureDetail ) {
	rbSharedWorldInteractionGLPreparedView_t &prepared =
		rbSharedWorldInteractionGLPreparedView;
	std::memset( &prepared, 0, sizeof( prepared ) );
	prepared.view = &view;
	prepared.viewDef = viewDef;
	prepared.hash = view.hash;
	const auto resolveTexture = []( std::uint64_t resourceId,
			idImage *&image ) -> bool {
		return RB_SharedWorldInteractionGLValidateTexture( resourceId,
			R_ClassicInteractionDomain_ResolveTexture( resourceId ), image );
	};

	if ( viewDef == NULL || view.viewDef != viewDef || !view.ready
			|| viewDef->viewEntitys == NULL || viewDef->renderWorld == NULL
			|| view.lightCount < 0
			|| view.lightCount > CLASSIC_INTERACTION_DOMAIN_MAX_LIGHTS
			|| view.surfaceCount < 0
			|| view.surfaceCount > CLASSIC_INTERACTION_DOMAIN_MAX_SURFACES
			|| view.primitiveCount < 0
			|| view.primitiveCount > CLASSIC_INTERACTION_DOMAIN_MAX_PRIMITIVES
			|| view.shadowCasterCount < 0
			|| view.shadowCasterCount
				> CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_CASTERS
			|| view.drawablePrimitiveCount < 0
			|| view.drawablePrimitiveCount > view.primitiveCount
			|| view.noopPrimitiveCount
				!= view.primitiveCount - view.drawablePrimitiveCount
			|| view.drawableShadowCasterCount < 0
			|| view.drawableShadowCasterCount > view.shadowCasterCount
			|| view.noopShadowCasterCount != view.shadowCasterCount
				- view.drawableShadowCasterCount
			|| view.logicalVolumeDrawCount < 0
			|| view.preloadVolumeDrawCount < 0
			|| view.preloadVolumeDrawCount > view.logicalVolumeDrawCount
			|| view.shadowMapPassCount < 0
			|| view.shadowMapPassCount
				> CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_MAP_PASSES
			|| view.hybridShadowPassCount < 0
			|| view.hybridShadowPassCount > view.shadowMapPassCount
			|| view.shadowMode < CLASSIC_INTERACTION_SHADOW_NONE
			|| view.shadowMode >= CLASSIC_INTERACTION_SHADOW_MODE_COUNT
			|| view.stencilReference != RB_ShadowMapSafeStencilClearValue() ) {
		failureDetail = RB_SharedWorldInteractionGLFailureDetail(
			RB_SHARED_WORLD_INTERACTION_GL_REJECT_VIEW );
		return false;
	}

	const int allowedRenderFlags = RF_NO_GUI | RF_PENUMBRA_MAP | RF_PRIMARY_VIEW;
	if ( viewDef->isSubview || viewDef->isMirror || viewDef->isXraySubview
			|| viewDef->isEditor || viewDef->superView != NULL
			|| viewDef->subviewSurface != NULL || viewDef->numClipPlanes != 0
			|| viewDef->renderView.viewID < 0
			|| ( viewDef->renderFlags & ~allowedRenderFlags ) != 0
			|| viewDef->numOutlineDrawSurfs != 0
			|| viewDef->renderView.globalMaterial != NULL
			|| backEnd.renderTexture != NULL
			|| backEnd.feedbackRenderTexture != NULL
			|| r_skipRender.GetBool() || r_skipRenderContext.GetBool()
			|| r_skipInteractions.GetBool() || r_skipBump.GetBool()
			|| r_skipDiffuse.GetBool() || r_skipSpecular.GetBool()
			|| r_skipTranslucent.GetBool() || r_singleTriangle.GetBool()
			|| r_showOverDraw.GetInteger() != 0
			|| r_showShadows.GetInteger() != 0
			|| RB_ShadowMapDebugMode() != 0
			|| RB_ShadowMapReceiverDebugMode()
			|| ( view.shadowMapPassCount > 0
				&& !r_useShadowMap.GetBool() )
			|| ( view.shadowCasterCount > 0 && !r_shadows.GetBool() )
			|| r_celShading.GetBool() || r_celShadingWorld.GetBool()
			|| RB_EnhancedMaterialShadingActive()
			|| RB_AppleGL21AutomaticInteractionPath()
			|| RB_UseSimpleInteractionShader() || r_testARBProgram.GetBool()
			|| r_interactionColorMode.IsModified() ) {
		failureDetail = RB_SharedWorldInteractionGLFailureDetail(
			RB_SHARED_WORLD_INTERACTION_GL_REJECT_MUTATED_VIEW );
		return false;
	}

	if ( view.viewportX1 != viewDef->viewport.x1
			|| view.viewportY1 != viewDef->viewport.y1
			|| view.viewportX2 != viewDef->viewport.x2
			|| view.viewportY2 != viewDef->viewport.y2
			|| view.scissorX1 != viewDef->scissor.x1
			|| view.scissorY1 != viewDef->scissor.y1
			|| view.scissorX2 != viewDef->scissor.x2
			|| view.scissorY2 != viewDef->scissor.y2
			|| std::memcmp( view.projectionMatrix, viewDef->projectionMatrix,
				sizeof( view.projectionMatrix ) ) != 0
			|| !RB_SharedWorldInteractionGLFiniteArray(
				view.projectionMatrix, 16 )
			|| !RB_SharedWorldInteractionGLFinite( view.lightScale )
			|| !RB_SharedWorldInteractionGLFinite( view.overBright )
			|| !RB_SharedWorldInteractionGLFinite( view.shadowPolygonFactor )
			|| !RB_SharedWorldInteractionGLFinite( view.shadowPolygonUnits )
			|| idMath::Fabs( view.lightScale - backEnd.lightScale ) > 0.00001f
			|| idMath::Fabs( view.overBright - backEnd.overBright ) > 0.00001f
			|| view.useScissor != r_useScissor.GetBool()
			|| view.useShadowVertexProgram
				!= r_useShadowVertexProgram.GetBool()
			|| view.preferTwoSidedStencil != r_useTwoSidedStencil.GetBool()
			|| view.useDepthBounds != r_useDepthBoundsTest.GetBool()
			|| view.stencilTranslucentShadows
				!= r_stencilTranslucentShadows.GetBool()
			|| idMath::Fabs( view.shadowPolygonFactor
				- r_shadowPolygonFactor.GetFloat() ) > 0.00001f
			|| idMath::Fabs( view.shadowPolygonUnits
				+ r_shadowPolygonOffset.GetFloat() ) > 0.00001f ) {
		failureDetail = RB_SharedWorldInteractionGLFailureDetail(
			RB_SHARED_WORLD_INTERACTION_GL_REJECT_MUTATED_VIEW );
		return false;
	}

	if ( !glConfig.isInitialized || glConfig.disableARB2Interactions
			|| !glConfig.ARBVertexProgramAvailable
			|| !glConfig.ARBFragmentProgramAvailable || globalImages == NULL
			|| glConfig.maxTextureUnits < 7
			|| glConfig.maxTextureImageUnits < 7
			|| glBindProgramARB == NULL || glProgramEnvParameter4fvARB == NULL
			|| glEnableVertexAttribArrayARB == NULL
			|| glDisableVertexAttribArrayARB == NULL
			|| glVertexAttribPointerARB == NULL
			|| ( view.useDepthBounds
				&& ( !glConfig.depthBoundsTestAvailable
					|| glDepthBoundsEXT == NULL ) ) ) {
		failureDetail = RB_SharedWorldInteractionGLFailureDetail(
			RB_SHARED_WORLD_INTERACTION_GL_REJECT_CAPACITY );
		return false;
	}
	const GLuint vertexProgram =
		RB_CurrentInteractionProgramIdent( GL_VERTEX_PROGRAM_ARB );
	const GLuint fragmentProgram =
		RB_CurrentInteractionProgramIdent( GL_FRAGMENT_PROGRAM_ARB );
	if ( vertexProgram != VPROG_INTERACTION
			|| fragmentProgram != FPROG_INTERACTION
			|| !R_IsARBProgramValid( GL_VERTEX_PROGRAM_ARB, vertexProgram )
			|| !R_IsARBProgramValid( GL_FRAGMENT_PROGRAM_ARB,
				fragmentProgram ) ) {
		failureDetail = RB_SharedWorldInteractionGLFailureDetail(
			RB_SHARED_WORLD_INTERACTION_GL_REJECT_PROGRAM );
		return false;
	}
	if ( view.useShadowVertexProgram
			&& ( !tr.backEndRendererHasVertexPrograms
				|| !R_IsARBProgramValid( GL_VERTEX_PROGRAM_ARB,
					VPROG_STENCIL_SHADOW ) ) ) {
		failureDetail = RB_SharedWorldInteractionGLFailureDetail(
			RB_SHARED_WORLD_INTERACTION_GL_REJECT_PROGRAM );
		return false;
	}
	if ( !RB_SharedWorldInteractionGLIntrinsicImageValid(
			globalImages->normalCubeMapImage, TT_CUBIC )
			|| !RB_SharedWorldInteractionGLIntrinsicImageValid(
				globalImages->ambientNormalMap, TT_CUBIC )
			|| !RB_SharedWorldInteractionGLIntrinsicImageValid(
				globalImages->specularTableImage, TT_2D ) ) {
		failureDetail = RB_SharedWorldInteractionGLFailureDetail(
			RB_SHARED_WORLD_INTERACTION_GL_REJECT_INTRINSIC_TEXTURE );
		return false;
	}

	int surfaceCursor = 0;
	int primitiveCursor = 0;
	int drawablePrimitives = 0;
	int noopPrimitives = 0;
	int shadowCasterCursor = 0;
	int drawableShadowCasters = 0;
	int noopShadowCasters = 0;
	int logicalVolumeDraws = 0;
	int preloadVolumeDraws = 0;
	int drawableMapCasters = 0;
	int shadowAlphaStages = 0;
	int shadowMapPasses = 0;
	int hybridShadowPasses = 0;
	int shadowLights = 0;
	int receiverSurfaces[ CLASSIC_INTERACTION_RECEIVER_COUNT ] = { 0, 0, 0 };
	int receiverPrimitives[ CLASSIC_INTERACTION_RECEIVER_COUNT ] = { 0, 0, 0 };
	for ( int lightIndex = 0; lightIndex < view.lightCount; ++lightIndex ) {
		const int domainLightIndex = view.firstLight + lightIndex;
		const classicInteractionDomainLight_t *light =
			R_ClassicInteractionDomain_ViewLight( view, lightIndex );
		if ( light == NULL || prepared.seenLights[lightIndex]
				|| light->legacyViewLight == NULL
				|| light->surfaceCount < 0
				|| light->surfaceCount > view.surfaceCount - surfaceCursor
				|| light->primitiveCount < 0
				|| light->primitiveCount > view.primitiveCount - primitiveCursor ) {
			failureDetail = RB_SharedWorldInteractionGLFailureDetail(
				RB_SHARED_WORLD_INTERACTION_GL_REJECT_LIGHT_RANGE,
				lightIndex );
			return false;
		}
		prepared.seenLights[lightIndex] = true;
		rbSharedWorldInteractionGLPreparedLight_t &preparedLight =
			prepared.lights[lightIndex];
		std::memset( &preparedLight, 0, sizeof( preparedLight ) );
		preparedLight.light = light;
		for ( int receiver = 0;
				receiver < CLASSIC_INTERACTION_RECEIVER_COUNT; ++receiver ) {
			preparedLight.firstPrimitive[receiver] = -1;
		}
		for ( int chain = 0;
				chain < CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT; ++chain ) {
			preparedLight.firstShadowCaster[chain] = -1;
			preparedLight.firstMapCaster[chain] = -1;
		}
		preparedLight.shadowMapPass[0] = -1;
		preparedLight.shadowMapPass[1] = -1;
		int lightDrawablePrimitives = 0;
		int lightNoopPrimitives = 0;
		int lightReceiverSurfaces[ CLASSIC_INTERACTION_RECEIVER_COUNT ] = { 0, 0, 0 };
		int lightReceiverPrimitives[ CLASSIC_INTERACTION_RECEIVER_COUNT ] = { 0, 0, 0 };
		for ( int lightSurfaceIndex = 0;
				lightSurfaceIndex < light->surfaceCount; ++lightSurfaceIndex ) {
			const classicInteractionDomainSurface_t *surface =
				R_ClassicInteractionDomain_LightSurface(
					*light, lightSurfaceIndex );
			const classicInteractionDomainSurface_t *viewSurface =
				R_ClassicInteractionDomain_ViewSurface( view, surfaceCursor );
			if ( surface == NULL || surface != viewSurface
					|| prepared.seenSurfaces[surfaceCursor]
					|| surface->legacyDrawSurf == NULL
					|| surface->legacyViewLight != light->legacyViewLight
					|| surface->lightIndex != domainLightIndex
					|| surface->receiver < CLASSIC_INTERACTION_RECEIVER_LOCAL
					|| surface->receiver >= CLASSIC_INTERACTION_RECEIVER_COUNT
					|| surface->primitiveCount < 0
					|| surface->primitiveCount
						> view.primitiveCount - primitiveCursor
					|| surface->drawablePrimitiveCount < 0
					|| surface->drawablePrimitiveCount > surface->primitiveCount
					|| surface->noopPrimitiveCount != surface->primitiveCount
						- surface->drawablePrimitiveCount ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_SURFACE_RANGE,
					lightIndex, surfaceCursor );
				return false;
			}
			prepared.seenSurfaces[surfaceCursor] = true;
			receiverSurfaces[surface->receiver]++;
			lightReceiverSurfaces[surface->receiver]++;

			const drawSurf_t *surf = surface->legacyDrawSurf;
			const srfTriangles_t *tri = surf->geo;
			if ( tri == NULL || surf->space == NULL
					|| surface->firstIndex != 0 || surface->vertexOffset != 0
					|| surface->vertexCount <= 0 || surface->indexCount <= 0
					|| surface->indexCount % 3 != 0
					|| tri->numVerts != surface->vertexCount
					|| tri->numIndexes != surface->indexCount
					|| surface->vertexCount > idMath::INT_MAX
						/ static_cast<int>( sizeof( idDrawVert ) )
					|| surface->indexCount > idMath::INT_MAX
						/ static_cast<int>( sizeof( glIndex_t ) ) ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_GEOMETRY,
					lightIndex, surfaceCursor );
				return false;
			}

			if ( surface->drawablePrimitiveCount > 0
					&& !RB_SharedWorldInteractionGLCacheValid(
						tri->ambientCache, false,
						surface->vertexCount
							* static_cast<int>( sizeof( idDrawVert ) ) ) ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_VERTEX_CACHE,
					lightIndex, surfaceCursor );
				return false;
			}
			if ( surface->drawablePrimitiveCount > 0 ) {
				R_TouchVertexCache( tri->ambientCache );
			}
			if ( surface->drawablePrimitiveCount > 0
					&& tri->indexCache != NULL
					&& !RB_SharedWorldInteractionGLCacheValid(
						tri->indexCache, true,
						surface->indexCount
							* static_cast<int>( sizeof( glIndex_t ) ) ) ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_INDEX_CACHE,
					lightIndex, surfaceCursor );
				return false;
			}
			if ( surface->drawablePrimitiveCount > 0
					&& tri->indexCache != NULL ) {
				R_TouchVertexCache( tri->indexCache );
			}
			if ( surface->drawablePrimitiveCount > 0
					&& ( ( !r_useIndexBuffers.GetBool()
							|| tri->indexCache == NULL )
						&& tri->indexes == NULL ) ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_INDEX_CACHE,
					lightIndex, surfaceCursor );
				return false;
			}

			int surfaceDrawablePrimitives = 0;
			int surfaceNoopPrimitives = 0;
			for ( int surfacePrimitiveIndex = 0;
					surfacePrimitiveIndex < surface->primitiveCount;
					++surfacePrimitiveIndex ) {
				const classicInteractionDomainPrimitive_t *primitive =
					R_ClassicInteractionDomain_SurfacePrimitive(
						*surface, surfacePrimitiveIndex );
				const classicInteractionDomainPrimitive_t *viewPrimitive =
					R_ClassicInteractionDomain_ViewPrimitive(
						view, primitiveCursor );
				const int domainSurfaceIndex = view.firstSurface + surfaceCursor;
				if ( primitive == NULL || primitive != viewPrimitive
						|| prepared.seenPrimitives[primitiveCursor]
						|| primitive->legacyDrawSurf != surf
						|| primitive->legacyViewLight != light->legacyViewLight
						|| primitive->lightIndex != domainLightIndex
						|| primitive->surfaceIndex != domainSurfaceIndex
						|| primitive->receiver != surface->receiver
						|| primitive->vertexCount != surface->vertexCount
						|| primitive->firstIndex != surface->firstIndex
						|| primitive->indexCount != surface->indexCount
						|| primitive->vertexOffset != surface->vertexOffset
						|| primitive->scissorX1 != surface->scissorX1
						|| primitive->scissorY1 != surface->scissorY1
						|| primitive->scissorX2 != surface->scissorX2
						|| primitive->scissorY2 != surface->scissorY2
						|| std::memcmp( primitive->modelMatrix,
							surf->space->modelMatrix,
							sizeof( primitive->modelMatrix ) ) != 0
						|| std::memcmp( primitive->modelViewMatrix,
							surf->space->modelViewMatrix,
							sizeof( primitive->modelViewMatrix ) ) != 0
						|| !RB_SharedWorldInteractionGLPrimitiveValuesValid(
							*primitive ) ) {
					failureDetail = RB_SharedWorldInteractionGLFailureDetail(
						RB_SHARED_WORLD_INTERACTION_GL_REJECT_PRIMITIVE_STATE,
						lightIndex, surfaceCursor, primitiveCursor );
					return false;
				}
				prepared.seenPrimitives[primitiveCursor] = true;
				receiverPrimitives[primitive->receiver]++;
				lightReceiverPrimitives[primitive->receiver]++;
				if ( primitive->disposition != CLASSIC_INTERACTION_PRIMITIVE_DRAW ) {
					surfaceNoopPrimitives++;
					lightNoopPrimitives++;
					noopPrimitives++;
					primitiveCursor++;
					continue;
				}

				rbSharedWorldInteractionGLPreparedPrimitive_t &preparedPrimitive =
					prepared.primitives[drawablePrimitives];
				std::memset( &preparedPrimitive, 0,
					sizeof( preparedPrimitive ) );
				preparedPrimitive.primitive = primitive;
				preparedPrimitive.vertexBuffer = tri->ambientCache->vbo;
				preparedPrimitive.vertexPointer =
					RB_SharedWorldInteractionGLCachePointer(
						*tri->ambientCache );
				const bool useIndexCache = r_useIndexBuffers.GetBool()
					&& tri->indexCache != NULL;
				preparedPrimitive.indexBuffer = useIndexCache
					? tri->indexCache->vbo : 0;
				preparedPrimitive.indexPointer = useIndexCache
					? RB_SharedWorldInteractionGLCachePointer(
						*tri->indexCache )
					: static_cast<const void *>( tri->indexes );
				preparedPrimitive.vertexCount = surface->vertexCount;
				preparedPrimitive.indexCount = surface->indexCount;
				preparedPrimitive.referencedVertexes = tri->ambientSurface != NULL
					&& tri->verts == tri->ambientSurface->verts;
				preparedPrimitive.referencedIndexes = tri->ambientSurface != NULL
					&& tri->indexes == tri->ambientSurface->indexes;
				preparedPrimitive.indexCacheBacked = useIndexCache;
				preparedPrimitive.stateBits = GLS_SRCBLEND_ONE
					| GLS_DSTBLEND_ONE | GLS_DEPTHMASK
					| ( primitive->depth == CLASSIC_INTERACTION_DEPTH_EQUAL
						? GLS_DEPTHFUNC_EQUAL : GLS_DEPTHFUNC_LESS );
				if ( !RB_SharedWorldInteractionGLMapCull(
						primitive->cull, preparedPrimitive.cullType ) ) {
					failureDetail = RB_SharedWorldInteractionGLFailureDetail(
						RB_SHARED_WORLD_INTERACTION_GL_REJECT_PRIMITIVE_STATE,
						lightIndex, surfaceCursor, primitiveCursor );
					return false;
				}
				if ( !resolveTexture(
						primitive->lightImageResourceId,
						preparedPrimitive.lightImage )
						|| !resolveTexture(
							primitive->lightFalloffImageResourceId,
							preparedPrimitive.lightFalloffImage )
						|| !resolveTexture(
							primitive->bumpImageResourceId,
							preparedPrimitive.bumpImage )
						|| !resolveTexture(
							primitive->diffuseImageResourceId,
							preparedPrimitive.diffuseImage )
						|| !resolveTexture(
							primitive->specularImageResourceId,
							preparedPrimitive.specularImage ) ) {
					failureDetail = RB_SharedWorldInteractionGLFailureDetail(
						RB_SHARED_WORLD_INTERACTION_GL_REJECT_TEXTURE,
						lightIndex, surfaceCursor, primitiveCursor );
					return false;
				}
				preparedPrimitive.normalImage = primitive->ambientLight
					? globalImages->ambientNormalMap
					: globalImages->normalCubeMapImage;
				const int receiver = static_cast<int>( primitive->receiver );
				if ( preparedLight.firstPrimitive[receiver] < 0 ) {
					preparedLight.firstPrimitive[receiver] = drawablePrimitives;
				} else if ( preparedLight.firstPrimitive[receiver]
						+ preparedLight.primitiveCount[receiver]
						!= drawablePrimitives ) {
					failureDetail = RB_SharedWorldInteractionGLFailureDetail(
						RB_SHARED_WORLD_INTERACTION_GL_REJECT_COVERAGE,
						lightIndex, surfaceCursor, primitiveCursor );
					return false;
				}
				preparedLight.primitiveCount[receiver]++;
				surfaceDrawablePrimitives++;
				lightDrawablePrimitives++;
				drawablePrimitives++;
				primitiveCursor++;
			}
			if ( surfaceDrawablePrimitives != surface->drawablePrimitiveCount
					|| surfaceNoopPrimitives != surface->noopPrimitiveCount ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_COVERAGE,
					lightIndex, surfaceCursor );
				return false;
			}
			surfaceCursor++;
		}
		if ( lightDrawablePrimitives != light->drawablePrimitiveCount
				|| lightNoopPrimitives != light->noopPrimitiveCount ) {
			failureDetail = RB_SharedWorldInteractionGLFailureDetail(
				RB_SHARED_WORLD_INTERACTION_GL_REJECT_COVERAGE,
				lightIndex );
			return false;
		}
		for ( int receiver = 0; receiver < CLASSIC_INTERACTION_RECEIVER_COUNT;
				++receiver ) {
			if ( lightReceiverSurfaces[receiver]
						!= light->receiverSurfaceCount[receiver]
					|| lightReceiverPrimitives[receiver]
						!= light->receiverPrimitiveCount[receiver] ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_COVERAGE,
					lightIndex );
				return false;
			}
			if ( light->receiverShadowMode[receiver]
					< CLASSIC_INTERACTION_SHADOW_NONE
					|| light->receiverShadowMode[receiver]
						>= CLASSIC_INTERACTION_SHADOW_MODE_COUNT ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MODE,
					lightIndex );
				return false;
			}
		}

		int lightDrawableShadowCasters = 0;
		int lightNoopShadowCasters = 0;
		int lightLogicalVolumeDraws = 0;
		int lightPreloadVolumeDraws = 0;
		int lightShadowCasterTotal = 0;
		int lightDrawableByChain[
			CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT ] = {};
		int lightPreloadByChain[
			CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT ] = {};
		for ( int chain = 0; chain < CLASSIC_INTERACTION_SHADOW_CHAIN_COUNT;
				++chain ) {
			const int chainCount = light->shadowCasterCount[chain];
			if ( chainCount < 0
					|| chainCount > view.shadowCasterCount - shadowCasterCursor ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_RANGE,
					lightIndex );
				return false;
			}
			lightShadowCasterTotal += chainCount;
			if ( chainCount <= 0 ) {
				continue;
			}
			const classicInteractionDomainShadowChain_t shadowChain =
				static_cast<classicInteractionDomainShadowChain_t>( chain );
			const bool mapCasterChain =
				RB_SharedWorldInteractionGLIsMapCasterChain( shadowChain );
			const bool volumeChain =
				RB_SharedWorldInteractionGLIsVolumeChain( shadowChain );
			if ( !mapCasterChain && !volumeChain ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_RANGE,
					lightIndex );
				return false;
			}
			if ( mapCasterChain ) {
				preparedLight.firstMapCaster[chain] = drawableMapCasters;
			} else {
				preparedLight.firstShadowCaster[chain] =
					drawableShadowCasters;
			}
			for ( int chainCasterIndex = 0; chainCasterIndex < chainCount;
					++chainCasterIndex ) {
				const int viewCasterIndex = shadowCasterCursor;
				const classicInteractionDomainShadowCaster_t *caster =
					R_ClassicInteractionDomain_LightShadowCaster(
						*light, shadowChain, chainCasterIndex );
				const classicInteractionDomainShadowCaster_t *viewCaster =
					R_ClassicInteractionDomain_ViewShadowCaster(
						view, shadowCasterCursor );
				if ( caster == NULL || caster != viewCaster
						|| prepared.seenShadowCasters[shadowCasterCursor]
						|| caster->legacyViewLight != light->legacyViewLight
						|| caster->lightIndex != domainLightIndex
						|| caster->legacyDrawSurf == NULL
						|| caster->legacyDrawSurf->space == NULL
						|| std::memcmp( caster->modelMatrix,
							caster->legacyDrawSurf->space->modelMatrix,
							sizeof( caster->modelMatrix ) ) != 0
						|| std::memcmp( caster->modelViewMatrix,
							caster->legacyDrawSurf->space->modelViewMatrix,
							sizeof( caster->modelViewMatrix ) ) != 0
						|| ( mapCasterChain
							? !RB_SharedWorldInteractionGLMapCasterValuesValid(
								*caster, shadowChain )
							: !RB_SharedWorldInteractionGLShadowCasterValuesValid(
								*caster, shadowChain ) ) ) {
					failureDetail = RB_SharedWorldInteractionGLFailureDetail(
						RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_STATE,
						lightIndex, -1, shadowCasterCursor );
					return false;
				}
				prepared.seenShadowCasters[shadowCasterCursor] = true;
				shadowCasterCursor++;
				if ( caster->disposition
						!= CLASSIC_INTERACTION_SHADOW_CASTER_DRAW ) {
					lightNoopShadowCasters++;
					noopShadowCasters++;
					continue;
				}

				const drawSurf_t *shadowSurf = caster->legacyDrawSurf;
				const srfTriangles_t *tri = mapCasterChain
					? caster->legacyCasterGeometry : shadowSurf->geo;
				if ( mapCasterChain ) {
					const srfTriangles_t *sourceTri = shadowSurf->geo;
					const srfTriangles_t *expectedTri = sourceTri != NULL
						&& sourceTri->ambientSurface != NULL
							? sourceTri->ambientSurface : sourceTri;
					if ( tri == NULL || expectedTri != tri
							|| shadowSurf->space == NULL
							|| R_TriHasPrimBatchMesh( tri )
							|| tri->numVerts != caster->vertexCount
							|| tri->numIndexes != caster->selectedIndexCount
							|| tri->numIndexes != caster->totalIndexCount
							|| caster->vertexCount > idMath::INT_MAX
								/ static_cast<int>( sizeof( idDrawVert ) )
							|| caster->selectedIndexCount > idMath::INT_MAX
								/ static_cast<int>( sizeof( glIndex_t ) )
							|| !RB_SharedWorldInteractionGLCacheValid(
								tri->ambientCache, false,
								caster->vertexCount
									* static_cast<int>( sizeof( idDrawVert ) ) ) ) {
						failureDetail = RB_SharedWorldInteractionGLFailureDetail(
							RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_CASTER,
							lightIndex, -1, viewCasterIndex );
						return false;
					}
					R_TouchVertexCache( tri->ambientCache );
					if ( tri->indexCache != NULL
							&& !RB_SharedWorldInteractionGLCacheValid(
								tri->indexCache, true,
								caster->selectedIndexCount
									* static_cast<int>( sizeof( glIndex_t ) ) ) ) {
						failureDetail = RB_SharedWorldInteractionGLFailureDetail(
							RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_INDEX_CACHE,
							lightIndex, -1, viewCasterIndex );
						return false;
					}
					if ( tri->indexCache != NULL ) {
						R_TouchVertexCache( tri->indexCache );
					}
					const bool useIndexCache = r_useIndexBuffers.GetBool()
						&& tri->indexCache != NULL;
					if ( !useIndexCache && tri->indexes == NULL ) {
						failureDetail = RB_SharedWorldInteractionGLFailureDetail(
							RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_INDEX_CACHE,
							lightIndex, -1, viewCasterIndex );
						return false;
					}

					rbSharedWorldInteractionGLPreparedMapCaster_t &preparedCaster =
						prepared.mapCasters[drawableMapCasters];
					std::memset( &preparedCaster, 0, sizeof( preparedCaster ) );
					preparedCaster.caster = caster;
					preparedCaster.vertexBuffer = tri->ambientCache->vbo;
					preparedCaster.vertexPointer =
						RB_SharedWorldInteractionGLCachePointer( *tri->ambientCache );
					preparedCaster.indexBuffer = useIndexCache
						? tri->indexCache->vbo : 0;
					preparedCaster.indexPointer = useIndexCache
						? RB_SharedWorldInteractionGLCachePointer( *tri->indexCache )
						: static_cast<const void *>( tri->indexes );
					preparedCaster.vertexCount = caster->vertexCount;
					preparedCaster.indexCount = caster->selectedIndexCount;
					preparedCaster.indexCacheBacked = useIndexCache;
					preparedCaster.firstAlphaStage = shadowAlphaStages;
					for ( int alphaIndex = 0;
							alphaIndex < caster->alphaStageCount; ++alphaIndex ) {
						const classicInteractionDomainShadowAlphaStage_t *alpha =
							R_ClassicInteractionDomain_ShadowAlphaStage(
								*caster, alphaIndex );
						idImage *alphaImage = NULL;
						if ( alpha == NULL
								|| shadowAlphaStages
									>= CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_ALPHA_STAGES
								|| !RB_SharedWorldInteractionGLShadowAlphaStageValuesValid(
									*alpha, view.firstShadowCaster + viewCasterIndex )
								|| !resolveTexture( alpha->textureResourceId,
									alphaImage ) ) {
							failureDetail = RB_SharedWorldInteractionGLFailureDetail(
								RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_ALPHA,
								lightIndex, -1, viewCasterIndex );
							return false;
						}
						prepared.shadowAlphaStages[shadowAlphaStages].stage = alpha;
						prepared.shadowAlphaStages[shadowAlphaStages].image = alphaImage;
						shadowAlphaStages++;
						preparedCaster.alphaStageCount++;
					}
					preparedLight.mapCasterCount[chain]++;
					drawableMapCasters++;
					lightDrawableShadowCasters++;
					lightDrawableByChain[chain]++;
					drawableShadowCasters++;
					continue;
				}

				int sealedIndexCount = -1;
				if ( caster->indexSelection
						== CLASSIC_INTERACTION_SHADOW_INDEX_FULL ) {
					sealedIndexCount = tri != NULL ? tri->numIndexes : -1;
				} else if ( caster->indexSelection
						== CLASSIC_INTERACTION_SHADOW_INDEX_NO_FRONT_CAPS ) {
					sealedIndexCount = tri != NULL
						? tri->numShadowIndexesNoFrontCaps : -1;
				} else if ( caster->indexSelection
						== CLASSIC_INTERACTION_SHADOW_INDEX_NO_CAPS ) {
					sealedIndexCount = tri != NULL
						? tri->numShadowIndexesNoCaps : -1;
				}
				if ( tri == NULL || shadowSurf->space == NULL
						|| caster->legacyCasterGeometry != tri
						|| R_TriHasPrimBatchMesh( tri )
						|| tri->numVerts != caster->vertexCount
						|| tri->numIndexes != caster->totalIndexCount
						|| sealedIndexCount != caster->selectedIndexCount
						|| caster->vertexCount > idMath::INT_MAX
							/ static_cast<int>( sizeof( shadowCache_t ) )
						|| caster->selectedIndexCount > idMath::INT_MAX
							/ static_cast<int>( sizeof( glIndex_t ) ) ) {
					failureDetail = RB_SharedWorldInteractionGLFailureDetail(
						RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_GEOMETRY,
						lightIndex, -1, shadowCasterCursor - 1 );
					return false;
				}
				// Vertex-program turbo volumes seal numVerts as the paired
				// front/back stream already, while private projected volumes seal
				// their explicit stream. In both cases numVerts is the exact cache
				// record count consumed by the index buffer.
				if ( !RB_SharedWorldInteractionGLCacheValid( tri->shadowCache,
						false, caster->vertexCount
							* static_cast<int>( sizeof( shadowCache_t ) ) ) ) {
					failureDetail = RB_SharedWorldInteractionGLFailureDetail(
						RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_VERTEX_CACHE,
						lightIndex, -1, shadowCasterCursor - 1 );
					return false;
				}
				R_TouchVertexCache( tri->shadowCache );
				if ( tri->indexCache != NULL
						&& !RB_SharedWorldInteractionGLCacheValid( tri->indexCache,
							true, caster->selectedIndexCount
								* static_cast<int>( sizeof( glIndex_t ) ) ) ) {
					failureDetail = RB_SharedWorldInteractionGLFailureDetail(
						RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_INDEX_CACHE,
						lightIndex, -1, shadowCasterCursor - 1 );
					return false;
				}
				if ( tri->indexCache != NULL ) {
					R_TouchVertexCache( tri->indexCache );
				}
				const bool useIndexCache = r_useIndexBuffers.GetBool()
					&& tri->indexCache != NULL;
				if ( !useIndexCache && tri->indexes == NULL ) {
					failureDetail = RB_SharedWorldInteractionGLFailureDetail(
						RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_INDEX_CACHE,
						lightIndex, -1, shadowCasterCursor - 1 );
					return false;
				}

				rbSharedWorldInteractionGLPreparedShadowCaster_t &preparedCaster =
					prepared.shadowCasters[drawableShadowCasters];
				std::memset( &preparedCaster, 0, sizeof( preparedCaster ) );
				preparedCaster.caster = caster;
				preparedCaster.vertexBuffer = tri->shadowCache->vbo;
				preparedCaster.vertexPointer =
					RB_SharedWorldInteractionGLCachePointer( *tri->shadowCache );
				preparedCaster.indexBuffer = useIndexCache
					? tri->indexCache->vbo : 0;
				preparedCaster.indexPointer = useIndexCache
					? RB_SharedWorldInteractionGLCachePointer( *tri->indexCache )
					: static_cast<const void *>( tri->indexes );
				preparedCaster.vertexCount = caster->vertexCount;
				preparedCaster.indexCount = caster->selectedIndexCount;
				preparedCaster.indexCacheBacked = useIndexCache;
				preparedLight.shadowCasterCount[chain]++;
				lightDrawableShadowCasters++;
				lightDrawableByChain[chain]++;
				drawableShadowCasters++;
				if ( caster->preload ) {
					lightPreloadByChain[chain]++;
				}
			}
		}

		for ( int receiver = CLASSIC_INTERACTION_RECEIVER_LOCAL;
				receiver <= CLASSIC_INTERACTION_RECEIVER_GLOBAL; ++receiver ) {
			const classicInteractionDomainShadowMapPass_t *mapPass =
				R_ClassicInteractionDomain_LightShadowMapPass(
					*light,
					static_cast<classicInteractionDomainReceiver_t>( receiver ) );
			const classicInteractionDomainShadowMode_t receiverMode =
				light->receiverShadowMode[receiver];
			const bool mappedReceiver =
				receiverMode == CLASSIC_INTERACTION_SHADOW_PROJECTED
				|| receiverMode == CLASSIC_INTERACTION_SHADOW_POINT
				|| receiverMode == CLASSIC_INTERACTION_SHADOW_HYBRID;
			if ( mappedReceiver != ( mapPass != NULL ) ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_PASS,
					lightIndex );
				return false;
			}
			if ( mapPass == NULL ) {
				continue;
			}
			if ( shadowMapPasses
					>= CLASSIC_INTERACTION_DOMAIN_MAX_SHADOW_MAP_PASSES
					|| !RB_SharedWorldInteractionGLMapPassValuesValid(
						*light, *mapPass,
						static_cast<classicInteractionDomainReceiver_t>( receiver ),
						domainLightIndex ) ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_SHADOW_MAP_PASS,
					lightIndex );
				return false;
			}
			const bool globalReceiver = receiver
				== CLASSIC_INTERACTION_RECEIVER_GLOBAL;
			const bool expectedHasStatic =
				light->shadowCasterCount[
					CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC ]
					+ ( globalReceiver ? light->shadowCasterCount[
						CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC ]
						: 0 ) > 0;
			const bool expectedHasDynamic =
				light->shadowCasterCount[
					CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC ]
					+ ( globalReceiver ? light->shadowCasterCount[
						CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC ]
						: 0 ) > 0;
			bool expectedHasAlpha = false;
			for ( int chainIndex =
					CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC;
					chainIndex
						<= CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC;
					++chainIndex ) {
				const classicInteractionDomainShadowChain_t mapChain =
					static_cast<classicInteractionDomainShadowChain_t>(
						chainIndex );
				if ( !globalReceiver
						&& ( mapChain
							== CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC
							|| mapChain
								== CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC ) ) {
					continue;
				}
				for ( int casterIndex = 0;
						casterIndex < light->shadowCasterCount[mapChain];
						++casterIndex ) {
					const classicInteractionDomainShadowCaster_t *mapCaster =
						R_ClassicInteractionDomain_LightShadowCaster(
							*light, mapChain, casterIndex );
					expectedHasAlpha = expectedHasAlpha
						|| ( mapCaster != NULL
							&& mapCaster->alphaStageCount > 0 );
				}
			}
			const int expectedMapped =
				light->shadowCasterCount[
					CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC ]
				+ light->shadowCasterCount[
					CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC ]
				+ ( globalReceiver
					? light->shadowCasterCount[
						CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC ]
						+ light->shadowCasterCount[
							CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC ]
					: 0 );
			const int expectedDrawableMapped =
				lightDrawableByChain[
					CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_STATIC ]
				+ lightDrawableByChain[
					CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_GLOBAL_DYNAMIC ]
				+ ( globalReceiver
					? lightDrawableByChain[
						CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_STATIC ]
						+ lightDrawableByChain[
							CLASSIC_INTERACTION_SHADOW_CHAIN_MAP_LOCAL_DYNAMIC ]
					: 0 );
			const int expectedSupplement =
				light->shadowCasterCount[
					CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL ]
				+ ( globalReceiver
					? light->shadowCasterCount[
						CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL ]
					: 0 );
			const int expectedDrawableSupplement =
				lightDrawableByChain[
					CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL ]
				+ ( globalReceiver
					? lightDrawableByChain[
						CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL ]
					: 0 );
			if ( mapPass->hasStaticCasters != expectedHasStatic
					|| mapPass->hasDynamicCasters != expectedHasDynamic
					|| mapPass->hasAlphaCasters != expectedHasAlpha
					|| mapPass->mappedCasterCount != expectedMapped
					|| mapPass->drawableMappedCasters
						!= expectedDrawableMapped
					|| mapPass->noopMappedCasters
						!= expectedMapped - expectedDrawableMapped
					|| mapPass->supplementCasterCount
						!= expectedSupplement
					|| mapPass->drawableSupplementCasters
						!= expectedDrawableSupplement
					|| mapPass->noopSupplementCasters
						!= expectedSupplement
							- expectedDrawableSupplement ) {
				failureDetail = RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_COVERAGE,
					lightIndex );
				return false;
			}
			preparedLight.shadowMapPass[receiver] = shadowMapPasses;
			prepared.shadowMapPasses[shadowMapPasses].pass = mapPass;
			if ( mapPass->disposition
					== CLASSIC_INTERACTION_SHADOW_MAP_PASS_HYBRID ) {
				hybridShadowPasses++;
			}
			shadowMapPasses++;
		}
		classicInteractionDomainShadowMode_t plannedVolumeMode =
			CLASSIC_INTERACTION_SHADOW_NONE;
		bool plannedVolumeIncludesLocal = false;
		bool plannedVolumeSelected = false;
		for ( int receiver = CLASSIC_INTERACTION_RECEIVER_LOCAL;
				receiver < CLASSIC_INTERACTION_RECEIVER_COUNT; ++receiver ) {
			const classicInteractionDomainShadowMode_t mode =
				light->receiverShadowMode[receiver];
			const bool hybrid = mode == CLASSIC_INTERACTION_SHADOW_HYBRID;
			if ( mode != CLASSIC_INTERACTION_SHADOW_STENCIL && !hybrid ) {
				continue;
			}
			plannedVolumeSelected = true;
			const int globalChain = hybrid
				? CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL
				: CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL;
			const int localChain = hybrid
				? CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL
				: CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_LOCAL;
			if ( plannedVolumeMode != mode ) {
				lightLogicalVolumeDraws += lightDrawableByChain[globalChain];
				lightPreloadVolumeDraws += lightPreloadByChain[globalChain];
				plannedVolumeMode = mode;
				plannedVolumeIncludesLocal = false;
			}
			if ( receiver != CLASSIC_INTERACTION_RECEIVER_LOCAL
					&& !plannedVolumeIncludesLocal ) {
				lightLogicalVolumeDraws += lightDrawableByChain[localChain];
				lightPreloadVolumeDraws += lightPreloadByChain[localChain];
				plannedVolumeIncludesLocal = true;
			}
		}
		logicalVolumeDraws += lightLogicalVolumeDraws;
		preloadVolumeDraws += lightPreloadVolumeDraws;
		if ( lightShadowCasterTotal != light->shadowCasterTotal
				|| lightDrawableShadowCasters != light->drawableShadowCasters
				|| lightNoopShadowCasters != light->noopShadowCasters
				|| lightLogicalVolumeDraws != light->logicalVolumeDraws
				|| lightPreloadVolumeDraws != light->preloadVolumeDraws
				|| light->clearStencil != plannedVolumeSelected ) {
			failureDetail = RB_SharedWorldInteractionGLFailureDetail(
				RB_SHARED_WORLD_INTERACTION_GL_REJECT_COVERAGE, lightIndex );
			return false;
		}
		if ( lightShadowCasterTotal > 0 ) {
			shadowLights++;
		}
	}

	if ( surfaceCursor != view.surfaceCount
			|| primitiveCursor != view.primitiveCount
			|| drawablePrimitives != view.drawablePrimitiveCount
			|| noopPrimitives != view.noopPrimitiveCount
			|| shadowCasterCursor != view.shadowCasterCount
			|| drawableShadowCasters != view.drawableShadowCasterCount
			|| noopShadowCasters != view.noopShadowCasterCount
			|| logicalVolumeDraws != view.logicalVolumeDrawCount
			|| preloadVolumeDraws != view.preloadVolumeDrawCount
			|| shadowLights != view.shadowLightCount
			|| shadowMapPasses != view.shadowMapPassCount
			|| hybridShadowPasses != view.hybridShadowPassCount ) {
		failureDetail = RB_SharedWorldInteractionGLFailureDetail(
			RB_SHARED_WORLD_INTERACTION_GL_REJECT_COVERAGE );
		return false;
	}
	for ( int receiver = 0; receiver < CLASSIC_INTERACTION_RECEIVER_COUNT;
			++receiver ) {
		if ( receiverSurfaces[receiver] != view.receiverSurfaceCount[receiver]
				|| receiverPrimitives[receiver]
					!= view.receiverPrimitiveCount[receiver] ) {
			failureDetail = RB_SharedWorldInteractionGLFailureDetail(
				RB_SHARED_WORLD_INTERACTION_GL_REJECT_COVERAGE );
			return false;
		}
	}

	prepared.lightCount = view.lightCount;
	prepared.surfaceCount = view.surfaceCount;
	prepared.primitiveCount = view.primitiveCount;
	prepared.drawablePrimitiveCount = drawablePrimitives;
	prepared.noopPrimitiveCount = noopPrimitives;
	prepared.shadowCasterCount = view.shadowCasterCount;
	prepared.drawableShadowCasterCount = drawableShadowCasters;
	prepared.noopShadowCasterCount = noopShadowCasters;
	prepared.logicalVolumeDrawCount = logicalVolumeDraws;
	prepared.preloadVolumeDrawCount = preloadVolumeDraws;
	prepared.mapCasterCount = drawableMapCasters;
	prepared.shadowAlphaStageCount = shadowAlphaStages;
	prepared.shadowMapPassCount = shadowMapPasses;
	prepared.hybridShadowPassCount = hybridShadowPasses;
	prepared.twoSidedStencil = view.preferTwoSidedStencil
		&& glStencilOpSeparate != NULL && tr.stencilIncr == GL_INCR_WRAP_EXT;
	if ( !RB_SharedWorldInteractionGLPreflightMapTransaction(
			viewDef, prepared, failureDetail ) ) {
		return false;
	}
	prepared.ready = true;
	return true;
}

static void RB_SharedWorldInteractionGLLoadPrimitiveParameters(
		const classicInteractionDomainPrimitive_t &primitive ) {
	RB_ARB2_SetVertexEnvParm( PP_LIGHT_ORIGIN,
		primitive.localLightOrigin );
	RB_ARB2_SetVertexEnvParm( PP_VIEW_ORIGIN,
		primitive.localViewOrigin );
	RB_ARB2_SetVertexEnvParm( PP_LIGHT_PROJECT_S,
		primitive.lightProjection[0] );
	RB_ARB2_SetVertexEnvParm( PP_LIGHT_PROJECT_T,
		primitive.lightProjection[1] );
	RB_ARB2_SetVertexEnvParm( PP_LIGHT_PROJECT_Q,
		primitive.lightProjection[2] );
	RB_ARB2_SetVertexEnvParm( PP_LIGHT_FALLOFF_S,
		primitive.lightProjection[3] );
	RB_ARB2_SetVertexEnvParm( PP_BUMP_MATRIX_S, primitive.bumpMatrix[0] );
	RB_ARB2_SetVertexEnvParm( PP_BUMP_MATRIX_T, primitive.bumpMatrix[1] );
	RB_ARB2_SetVertexEnvParm( PP_DIFFUSE_MATRIX_S,
		primitive.diffuseMatrix[0] );
	RB_ARB2_SetVertexEnvParm( PP_DIFFUSE_MATRIX_T,
		primitive.diffuseMatrix[1] );
	RB_ARB2_SetVertexEnvParm( PP_SPECULAR_MATRIX_S,
		primitive.specularMatrix[0] );
	RB_ARB2_SetVertexEnvParm( PP_SPECULAR_MATRIX_T,
		primitive.specularMatrix[1] );

	float modulate = 0.0f;
	float add = 1.0f;
	switch ( primitive.vertexColor ) {
	case RENDERER_VERTEX_COLOR_MODULATE:
		modulate = 1.0f;
		add = 0.0f;
		break;
	case RENDERER_VERTEX_COLOR_INVERSE_MODULATE:
		modulate = -1.0f;
		add = 1.0f;
		break;
	default:
		break;
	}
	static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	if ( g_interactionVertexProgramColorMode == ICM_PACKED ) {
		const float packed[4] = { modulate, add, 0.0f, 0.0f };
		RB_ARB2_SetVertexEnvParm( PP_COLOR_MODULATE, packed );
		RB_ARB2_SetVertexEnvParm( PP_COLOR_ADD, zero );
	} else {
		const float modulateVector[4] = {
			modulate, modulate, modulate, modulate };
		const float addVector[4] = { add, add, add, add };
		RB_ARB2_SetVertexEnvParm( PP_COLOR_MODULATE, modulateVector );
		RB_ARB2_SetVertexEnvParm( PP_COLOR_ADD, addVector );
	}

	const float specularColorX2[4] = {
		primitive.specularColor[0] * 2.0f,
		primitive.specularColor[1] * 2.0f,
		primitive.specularColor[2] * 2.0f,
		primitive.specularColor[3] * 2.0f
	};
	RB_ARB2_SetFragmentEnvParm( 0, primitive.diffuseColor );
	RB_ARB2_SetFragmentEnvParm( 1, specularColorX2 );
}

static void RB_SharedWorldInteractionGLBeginInteractionState( void ) {
	RB_ARB2_InvalidateEnvParamCache();
	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION );
	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION );
	glEnable( GL_VERTEX_PROGRAM_ARB );
	glEnable( GL_FRAGMENT_PROGRAM_ARB );
	glDisableVertexAttribArrayARB( 1 );
	glDisableVertexAttribArrayARB( 2 );
	glDisableVertexAttribArrayARB( 5 );
	glDisableVertexAttribArrayARB( 6 );
	glDisableVertexAttribArrayARB( 7 );
	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );
	GL_SelectTextureNoClient( 6 );
	globalImages->specularTableImage->Bind();
}

static void RB_SharedWorldInteractionGLDrawPrimitive(
		const rbSharedWorldInteractionGLPreparedPrimitive_t &preparedPrimitive,
		const classicInteractionDomainView_t &view,
		const viewDef_t *viewDef ) {
	const classicInteractionDomainPrimitive_t &primitive =
		*preparedPrimitive.primitive;
	backEnd.currentSpace = NULL;
	glLoadMatrixf( primitive.modelViewMatrix );
	if ( view.useScissor ) {
		backEnd.currentScissor.x1 = primitive.scissorX1;
		backEnd.currentScissor.y1 = primitive.scissorY1;
		backEnd.currentScissor.x2 = primitive.scissorX2;
		backEnd.currentScissor.y2 = primitive.scissorY2;
		glScissor( viewDef->viewport.x1 + primitive.scissorX1,
			viewDef->viewport.y1 + primitive.scissorY1,
			primitive.scissorX2 + 1 - primitive.scissorX1,
			primitive.scissorY2 + 1 - primitive.scissorY1 );
	}
	GL_State( preparedPrimitive.stateBits );
	GL_Cull( preparedPrimitive.cullType );
	idVertexCache::BindArrayBuffer( preparedPrimitive.vertexBuffer );
	const void *ac = preparedPrimitive.vertexPointer;
	glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_COLOR_OFFSET ) );
	glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET ) );
	glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET ) );
	glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET ) );
	glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET ) );

	RB_SharedWorldInteractionGLLoadPrimitiveParameters( primitive );
	GL_SelectTextureNoClient( 0 );
	preparedPrimitive.normalImage->Bind();
	GL_SelectTextureNoClient( 1 );
	preparedPrimitive.bumpImage->Bind();
	GL_SelectTextureNoClient( 2 );
	preparedPrimitive.lightFalloffImage->Bind();
	GL_SelectTextureNoClient( 3 );
	preparedPrimitive.lightImage->Bind();
	GL_SelectTextureNoClient( 4 );
	preparedPrimitive.diffuseImage->Bind();
	GL_SelectTextureNoClient( 5 );
	preparedPrimitive.specularImage->Bind();

	if ( primitive.polygonOffsetEnabled ) {
		glPolygonOffset( primitive.polygonOffsetFactor,
			primitive.polygonOffsetUnits );
		glEnable( GL_POLYGON_OFFSET_FILL );
	}
	idVertexCache::BindIndexBuffer( preparedPrimitive.indexBuffer );
	backEnd.pc.c_drawElements++;
	backEnd.pc.c_drawIndexes += preparedPrimitive.indexCount;
	backEnd.pc.c_drawVertexes += preparedPrimitive.vertexCount;
	if ( preparedPrimitive.referencedIndexes ) {
		backEnd.pc.c_drawRefIndexes += preparedPrimitive.indexCount;
	}
	if ( preparedPrimitive.referencedVertexes ) {
		backEnd.pc.c_drawRefVertexes += preparedPrimitive.vertexCount;
	}
	if ( preparedPrimitive.indexCacheBacked ) {
		backEnd.pc.c_vboIndexes += preparedPrimitive.indexCount;
	}
	glDrawElements( GL_TRIANGLES, preparedPrimitive.indexCount,
		GL_INDEX_TYPE, preparedPrimitive.indexPointer );
	if ( primitive.polygonOffsetEnabled ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

static int RB_SharedWorldInteractionGLDrawPrimitiveRange(
		const rbSharedWorldInteractionGLPreparedView_t &prepared,
		const rbSharedWorldInteractionGLPreparedLight_t &light,
		classicInteractionDomainReceiver_t receiver,
		const classicInteractionDomainView_t &view,
		const viewDef_t *viewDef ) {
	const int first = light.firstPrimitive[receiver];
	const int count = light.primitiveCount[receiver];
	for ( int i = 0; i < count; ++i ) {
		RB_SharedWorldInteractionGLDrawPrimitive(
			prepared.primitives[first + i], view, viewDef );
	}
	return count;
}

static void RB_SharedWorldInteractionGLSelectMapPass(
		const rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass ) {
	const classicInteractionDomainShadowMapPass_t &pass = *preparedPass.pass;
	RB_SharedWorldInteractionGLActivateMapResource( preparedPass );
	if ( !preparedPass.pointLight ) {
		g_projectedShadowMapState = pass.projected.state;
	}
}

static void RB_SharedWorldInteractionGLBeginMappedReceiver(
		const rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass ) {
	const classicInteractionDomainShadowMapPass_t &pass = *preparedPass.pass;
	const bool pointLight = preparedPass.pointLight;
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( pointLight
		? g_pointShadowMapProgram.programObject
		: g_shadowMapProgram.programObject );
	const GLint bumpMap = pointLight ? g_pointShadowMapProgram.bumpMap
		: g_shadowMapProgram.bumpMap;
	const GLint falloffMap = pointLight
		? g_pointShadowMapProgram.lightFalloffMap
		: g_shadowMapProgram.lightFalloffMap;
	const GLint projectionMap = pointLight
		? g_pointShadowMapProgram.lightProjectionMap
		: g_shadowMapProgram.lightProjectionMap;
	const GLint diffuseMap = pointLight ? g_pointShadowMapProgram.diffuseMap
		: g_shadowMapProgram.diffuseMap;
	const GLint specularMap = pointLight
		? g_pointShadowMapProgram.specularMap
		: g_shadowMapProgram.specularMap;
	glUniform1iARB( bumpMap, 0 );
	glUniform1iARB( falloffMap, 1 );
	glUniform1iARB( projectionMap, 2 );
	glUniform1iARB( diffuseMap, 3 );
	glUniform1iARB( specularMap, 4 );
	// The shared domain intentionally rejects translucent moment casters, but
	// both mapped receiver programs still declare their three non-comparison
	// moment samplers.  Keep them on a neutral unit distinct from the depth
	// comparison sampler; leaving their default binding at unit zero makes the
	// point program's sampler types conflict once unit five enables comparison.
	for ( int i = 0; i < 3; ++i ) {
		const GLint momentMap = pointLight
			? g_pointShadowMapProgram.translucentShadowMap[i]
			: g_shadowMapProgram.translucentShadowMap[i];
		if ( momentMap >= 0 ) {
			glUniform1iARB( momentMap, 6 );
		}
	}

	if ( pointLight ) {
		glUniform1iARB( g_pointShadowMapProgram.pointShadowMap, 5 );
		shadowMapPointReceiverSettings_t sealedReceiverSettings;
		sealedReceiverSettings.constantBias = pass.point.constantBias;
		sealedReceiverSettings.normalBias = pass.point.normalBias;
		sealedReceiverSettings.texelBiasScale = pass.point.texelBiasScale;
		sealedReceiverSettings.normalOffsetScale =
			pass.point.normalOffsetScale;
		sealedReceiverSettings.worldBiasScale = 1.0f;
		const shadowMapPointReceiverSettings_t receiverSettings =
			R_ShadowMapPointStorageAdjustedReceiverSettings(
				sealedReceiverSettings, pass.point.farDistance,
				pass.point.faceSize, pass.point.depthCompare,
				pass.point.highPrecision );
		glUniform1fARB( g_pointShadowMapProgram.shadowBias,
			receiverSettings.constantBias );
		glUniform1fARB( g_pointShadowMapProgram.shadowNormalBias,
			receiverSettings.normalBias );
		glUniform1fARB(
			g_pointShadowMapProgram.pointShadowTexelDepthBias,
			receiverSettings.texelBiasScale
				/ Max( 1, pass.point.faceSize ) );
		glUniform1fARB(
			g_pointShadowMapProgram.pointShadowNormalOffsetWorld,
			2.0f * receiverSettings.normalOffsetScale
				/ Max( 1, pass.point.faceSize ) );
		glUniform1fARB( g_pointShadowMapProgram.shadowFilterRadius,
			pass.point.filterRadius );
		glUniform1fARB( g_pointShadowMapProgram.shadowFilterTaps,
			static_cast<float>( pass.point.filterTaps ) );
		glUniform1fARB( g_pointShadowMapProgram.shadowFilterMode,
			static_cast<float>( pass.point.filterMode ) );
		glUniform1fARB( g_pointShadowMapProgram.shadowDebugMode, 0.0f );
		glUniform1fARB(
			g_pointShadowMapProgram.shadowReceiverDebugReason, 0.0f );
		glUniform4fvARB( g_pointShadowMapProgram.globalLightOrigin, 1,
			pass.point.lightOrigin );
		glUniform1fARB( g_pointShadowMapProgram.pointShadowFar,
			pass.point.farDistance );
		glUniform1fARB( g_pointShadowMapProgram.pointShadowTexelScale,
			2.0f / Max( 1, pass.point.faceSize ) );
		glUniform1fARB( g_pointShadowMapProgram.pointShadowDepthMode,
			pass.point.highPrecision ? 1.0f : 0.0f );
		glUniform1fARB( g_pointShadowMapProgram.translucentShadowEnabled,
			0.0f );
		GL_SelectTextureNoClient( 5 );
		idImage *sample = pass.point.depthCompare
			? preparedPass.pointDepthImage
			: preparedPass.pointColorImage;
		sample->Bind();
		glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE,
			pass.point.depthCompare ? GL_COMPARE_R_TO_TEXTURE : GL_NONE );
		if ( pass.point.depthCompare ) {
			glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC,
				GL_LEQUAL );
			glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
				GL_LINEAR );
			glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER,
				GL_LINEAR );
		}
	} else {
		glUniform1iARB( g_shadowMapProgram.shadowMap, 5 );
		const int atlasWidth = preparedPass.resourceWidth;
		const int atlasHeight = preparedPass.resourceHeight;
		const float texelSize[2] = {
			1.0f / Max( 1, atlasWidth ),
			1.0f / Max( 1, atlasHeight )
		};
		glUniform2fvARB( g_shadowMapProgram.shadowTexelSize, 1,
			texelSize );
		glUniform1fARB( g_shadowMapProgram.shadowBias,
			pass.projected.constantBias );
		glUniform1fARB( g_shadowMapProgram.shadowNormalBias,
			pass.projected.normalBias );
		glUniform1fvARB( g_shadowMapProgram.shadowTexelDepthBias,
			SHADOWMAP_MAX_CASCADES,
			pass.projected.state.texelDepthBias );
		float normalOffsets[SHADOWMAP_MAX_CASCADES] = {};
		for ( int i = 0; i < SHADOWMAP_MAX_CASCADES; ++i ) {
			normalOffsets[i] = pass.projected.state.worldTexelSize[i]
				* pass.projected.normalOffsetScale;
		}
		glUniform1fvARB( g_shadowMapProgram.shadowNormalOffsetWorld,
			SHADOWMAP_MAX_CASCADES, normalOffsets );
		glUniform1fARB( g_shadowMapProgram.shadowReceiverPlaneBias,
			pass.projected.receiverPlaneBias ? 1.0f : 0.0f );
		glUniform1fARB( g_shadowMapProgram.shadowFilterRadius,
			pass.projected.filter.filterRadius );
		glUniform1fARB( g_shadowMapProgram.shadowFilterTaps,
			static_cast<float>( pass.projected.filter.filterTaps ) );
		glUniform1fARB( g_shadowMapProgram.shadowFilterMode,
			static_cast<float>( pass.projected.filter.filterMode ) );
		glUniform1fARB( g_shadowMapProgram.shadowPCSSLightRadius,
			pass.projected.filter.pcssLightRadius );
		glUniform1fARB( g_shadowMapProgram.shadowPCSSMaxRadius,
			pass.projected.filter.pcssMaxRadius );
		const float slotU0 = preparedPass.projectedOriginX
			/ static_cast<float>( atlasWidth );
		const float slotV0 = preparedPass.projectedOriginY
			/ static_cast<float>( atlasHeight );
		const float slotSpanU = preparedPass.projectedSpanPixels
			/ static_cast<float>( atlasWidth );
		const float slotSpanV = preparedPass.projectedSpanPixels
			/ static_cast<float>( atlasHeight );
		float composedRects[SHADOWMAP_MAX_CASCADES][4] = {};
		for ( int i = 0; i < SHADOWMAP_MAX_CASCADES; ++i ) {
			const idVec4 &rect = pass.projected.state.atlasRect[i];
			composedRects[i][0] = slotU0 + rect.x * slotSpanU;
			composedRects[i][1] = slotV0 + rect.y * slotSpanV;
			composedRects[i][2] = slotU0 + rect.z * slotSpanU;
			composedRects[i][3] = slotV0 + rect.w * slotSpanV;
		}
		glUniform4fvARB( g_shadowMapProgram.shadowAtlasRect,
			SHADOWMAP_MAX_CASCADES, composedRects[0] );
		glUniform1fvARB( g_shadowMapProgram.shadowSplitDepths,
			SHADOWMAP_MAX_CASCADES, pass.projected.state.splitDepths );
		glUniform1fvARB( g_shadowMapProgram.shadowCascadeBiasScale,
			SHADOWMAP_MAX_CASCADES, pass.projected.state.biasScale );
		glUniform1iARB( g_shadowMapProgram.shadowCascadeCount,
			pass.projected.state.cascadeCount );
		glUniform1fARB( g_shadowMapProgram.shadowCascadeBlend,
			pass.projected.cascadeBlend );
		glUniform1fARB( g_shadowMapProgram.shadowDebugMode, 0.0f );
		glUniform1fARB( g_shadowMapProgram.shadowReceiverDebugReason, 0.0f );
		glUniform1fARB( g_shadowMapProgram.translucentShadowEnabled,
			0.0f );
		GL_SelectTextureNoClient( 5 );
		preparedPass.projectedDepthImage->Bind();
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
			pass.projected.depthCompare
				? GL_COMPARE_R_TO_TEXTURE : GL_NONE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			pass.projected.depthCompare ? GL_LINEAR : GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
			pass.projected.depthCompare ? GL_LINEAR : GL_NEAREST );
	}

	for ( int unit = 6; unit <= 8 && unit < glConfig.maxTextureUnits;
			++unit ) {
		GL_SelectTextureNoClient( unit );
		globalImages->BindNull();
	}
	const float neutralCel[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
	if ( pointLight ) {
		glUniform1fARB( g_pointShadowMapProgram.materialEnhanced, 0.0f );
		glUniform1fARB( g_pointShadowMapProgram.materialNormalScale, 1.0f );
		glUniform1fARB( g_pointShadowMapProgram.materialSpecularBoost, 1.0f );
		glUniform1fARB( g_pointShadowMapProgram.materialFresnel, 0.0f );
		glUniform4fvARB( g_pointShadowMapProgram.celParams, 1, neutralCel );
	} else {
		glUniform1fARB( g_shadowMapProgram.materialEnhanced, 0.0f );
		glUniform1fARB( g_shadowMapProgram.materialNormalScale, 1.0f );
		glUniform1fARB( g_shadowMapProgram.materialSpecularBoost, 1.0f );
		glUniform1fARB( g_shadowMapProgram.materialFresnel, 0.0f );
		glUniform4fvARB( g_shadowMapProgram.celParams, 1, neutralCel );
	}
	glStencilMask( 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );
}

static void RB_SharedWorldInteractionGLDrawMappedPrimitive(
		const rbSharedWorldInteractionGLPreparedPrimitive_t &preparedPrimitive,
		const classicInteractionDomainShadowMapPass_t &pass,
		const classicInteractionDomainView_t &view,
		const viewDef_t *viewDef ) {
	const classicInteractionDomainPrimitive_t &primitive =
		*preparedPrimitive.primitive;
	const bool pointLight = pass.point.valid;
	glLoadMatrixf( primitive.modelViewMatrix );
	if ( view.useScissor ) {
		glScissor( viewDef->viewport.x1 + primitive.scissorX1,
			viewDef->viewport.y1 + primitive.scissorY1,
			primitive.scissorX2 + 1 - primitive.scissorX1,
			primitive.scissorY2 + 1 - primitive.scissorY1 );
	}
	GL_State( preparedPrimitive.stateBits );
	GL_Cull( preparedPrimitive.cullType );
	idVertexCache::BindArrayBuffer( preparedPrimitive.vertexBuffer );
	const void *ac = preparedPrimitive.vertexPointer;
	glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_COLOR_OFFSET ) );
	glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET ) );
	glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET ) );
	glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET ) );
	glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET ) );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ),
		RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET ) );

#define RB_SHARED_MAP_UNIFORM4( member, value ) \
	glUniform4fvARB( pointLight ? g_pointShadowMapProgram.member \
		: g_shadowMapProgram.member, 1, value )
	RB_SHARED_MAP_UNIFORM4( localLightOrigin, primitive.localLightOrigin );
	RB_SHARED_MAP_UNIFORM4( localViewOrigin, primitive.localViewOrigin );
	RB_SHARED_MAP_UNIFORM4( lightProjectionS, primitive.lightProjection[0] );
	RB_SHARED_MAP_UNIFORM4( lightProjectionT, primitive.lightProjection[1] );
	RB_SHARED_MAP_UNIFORM4( lightProjectionQ, primitive.lightProjection[2] );
	RB_SHARED_MAP_UNIFORM4( lightFalloffS, primitive.lightProjection[3] );
	RB_SHARED_MAP_UNIFORM4( bumpMatrixS, primitive.bumpMatrix[0] );
	RB_SHARED_MAP_UNIFORM4( bumpMatrixT, primitive.bumpMatrix[1] );
	RB_SHARED_MAP_UNIFORM4( diffuseMatrixS, primitive.diffuseMatrix[0] );
	RB_SHARED_MAP_UNIFORM4( diffuseMatrixT, primitive.diffuseMatrix[1] );
	RB_SHARED_MAP_UNIFORM4( specularMatrixS, primitive.specularMatrix[0] );
	RB_SHARED_MAP_UNIFORM4( specularMatrixT, primitive.specularMatrix[1] );
	RB_SHARED_MAP_UNIFORM4( diffuseColor, primitive.diffuseColor );
	RB_SHARED_MAP_UNIFORM4( specularColor, primitive.specularColor );
	RB_SHARED_MAP_UNIFORM4( flatDiffuseParams, primitive.flatDiffuseParams );
#undef RB_SHARED_MAP_UNIFORM4
	float modulate = 0.0f;
	float add = 1.0f;
	if ( primitive.vertexColor == RENDERER_VERTEX_COLOR_MODULATE ) {
		modulate = 1.0f;
		add = 0.0f;
	} else if ( primitive.vertexColor
			== RENDERER_VERTEX_COLOR_INVERSE_MODULATE ) {
		modulate = -1.0f;
	}
	const float vertexColorParams[2] = { modulate, add };
	glUniform2fvARB( pointLight
		? g_pointShadowMapProgram.vertexColorParams
		: g_shadowMapProgram.vertexColorParams, 1, vertexColorParams );
	if ( pointLight ) {
		float row0[4];
		float row1[4];
		float row2[4];
		RB_ShadowMapModelMatrixRows( primitive.modelMatrix,
			row0, row1, row2 );
		glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow0, 1, row0 );
		glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow1, 1, row1 );
		glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow2, 1, row2 );
	} else {
		float shadowRows[4][SHADOWMAP_MAX_CASCADES * 4] = {};
		for ( int cascade = 0;
				cascade < pass.projected.state.cascadeCount; ++cascade ) {
			for ( int plane = 0; plane < 4; ++plane ) {
				idPlane localPlane;
				R_GlobalPlaneToLocal( primitive.modelMatrix,
					pass.projected.state.clipPlanes[cascade][plane],
					localPlane );
				std::memcpy( &shadowRows[plane][cascade * 4],
					localPlane.ToFloatPtr(), sizeof( float ) * 4 );
			}
		}
		for ( int row = 0; row < 4; ++row ) {
			glUniform4fvARB( g_shadowMapProgram.shadowRow[row],
				pass.projected.state.cascadeCount, shadowRows[row] );
		}
	}
	GL_SelectTextureNoClient( 0 );
	preparedPrimitive.bumpImage->Bind();
	GL_SelectTextureNoClient( 1 );
	preparedPrimitive.lightFalloffImage->Bind();
	GL_SelectTextureNoClient( 2 );
	preparedPrimitive.lightImage->Bind();
	GL_SelectTextureNoClient( 3 );
	preparedPrimitive.diffuseImage->Bind();
	GL_SelectTextureNoClient( 4 );
	preparedPrimitive.specularImage->Bind();
	if ( primitive.polygonOffsetEnabled ) {
		glPolygonOffset( primitive.polygonOffsetFactor,
			primitive.polygonOffsetUnits );
		glEnable( GL_POLYGON_OFFSET_FILL );
	}
	idVertexCache::BindIndexBuffer( preparedPrimitive.indexBuffer );
	backEnd.pc.c_drawElements++;
	backEnd.pc.c_drawIndexes += preparedPrimitive.indexCount;
	backEnd.pc.c_drawVertexes += preparedPrimitive.vertexCount;
	if ( preparedPrimitive.referencedIndexes ) {
		backEnd.pc.c_drawRefIndexes += preparedPrimitive.indexCount;
	}
	if ( preparedPrimitive.referencedVertexes ) {
		backEnd.pc.c_drawRefVertexes += preparedPrimitive.vertexCount;
	}
	if ( preparedPrimitive.indexCacheBacked ) {
		backEnd.pc.c_vboIndexes += preparedPrimitive.indexCount;
	}
	glDrawElements( GL_TRIANGLES, preparedPrimitive.indexCount,
		GL_INDEX_TYPE, preparedPrimitive.indexPointer );
	if ( primitive.polygonOffsetEnabled ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

static int RB_SharedWorldInteractionGLCommitMappedPass(
		const rbSharedWorldInteractionGLPreparedView_t &prepared,
		const rbSharedWorldInteractionGLPreparedLight_t &light,
		classicInteractionDomainReceiver_t receiver,
		const classicInteractionDomainView_t &view,
		const viewDef_t *viewDef ) {
	const int mapPassIndex = light.shadowMapPass[receiver ==
		CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT
			? CLASSIC_INTERACTION_RECEIVER_GLOBAL : receiver];
	const rbSharedWorldInteractionGLPreparedMapPass_t &preparedPass =
		prepared.shadowMapPasses[mapPassIndex];
	RB_SharedWorldInteractionGLSelectMapPass( preparedPass );
	RB_SharedWorldInteractionGLBeginMappedReceiver( preparedPass );
	const int first = light.firstPrimitive[receiver];
	const int count = light.primitiveCount[receiver];
	for ( int i = 0; i < count; ++i ) {
		RB_SharedWorldInteractionGLDrawMappedPrimitive(
			prepared.primitives[first + i], *preparedPass.pass,
			view, viewDef );
	}
	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );
	glUseProgramObjectARB( 0 );
	// Exact shared-domain cache hits must count as live presentation just like
	// the established ARB2 path.  Refresh residency only after the mapped pass
	// has committed so a later whole-view preflight rejection cannot pin an
	// entry which was never consumed.
	if ( preparedPass.pointEntry != NULL ) {
		preparedPass.pointEntry->lastUsedFrame = tr.frameCount;
	} else if ( preparedPass.projectedEntry != NULL ) {
		preparedPass.projectedEntry->lastUsedFrame = tr.frameCount;
	}
	return count;
}

static void RB_SharedWorldInteractionGLBeginShadowState(
		const classicInteractionDomainView_t &view ) {
	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	GL_SelectTextureNoClient( 6 );
	globalImages->BindNull();
	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_State( GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK
		| GLS_DEPTHFUNC_LESS );
	if ( view.shadowPolygonFactor != 0.0f
			|| view.shadowPolygonUnits != 0.0f ) {
		glPolygonOffset( view.shadowPolygonFactor, view.shadowPolygonUnits );
		glEnable( GL_POLYGON_OFFSET_FILL );
	}
	glStencilMask( 255 );
	glStencilFunc( GL_ALWAYS, 1, 255 );
	if ( view.useDepthBounds ) {
		glEnable( GL_DEPTH_BOUNDS_TEST_EXT );
	}
	if ( view.useShadowVertexProgram ) {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
		glEnable( GL_VERTEX_PROGRAM_ARB );
	} else {
		glDisable( GL_VERTEX_PROGRAM_ARB );
	}
}

static void RB_SharedWorldInteractionGLSubmitShadowElements(
		const rbSharedWorldInteractionGLPreparedShadowCaster_t &preparedCaster ) {
	backEnd.pc.c_shadowElements++;
	backEnd.pc.c_shadowIndexes += preparedCaster.indexCount;
	backEnd.pc.c_shadowVertexes += preparedCaster.vertexCount;
	if ( preparedCaster.indexCacheBacked ) {
		backEnd.pc.c_vboIndexes += preparedCaster.indexCount;
	}
	glDrawElements( GL_TRIANGLES, preparedCaster.indexCount, GL_INDEX_TYPE,
		preparedCaster.indexPointer );
}

static void RB_SharedWorldInteractionGLDrawShadowCaster(
		const rbSharedWorldInteractionGLPreparedShadowCaster_t &preparedCaster,
		const rbSharedWorldInteractionGLPreparedView_t &prepared,
		const classicInteractionDomainView_t &view,
		const viewDef_t *viewDef ) {
	const classicInteractionDomainShadowCaster_t &caster =
		*preparedCaster.caster;
	backEnd.currentSpace = NULL;
	glLoadMatrixf( caster.modelViewMatrix );
	if ( view.useScissor ) {
		backEnd.currentScissor.x1 = caster.scissorX1;
		backEnd.currentScissor.y1 = caster.scissorY1;
		backEnd.currentScissor.x2 = caster.scissorX2;
		backEnd.currentScissor.y2 = caster.scissorY2;
		glScissor( viewDef->viewport.x1 + caster.scissorX1,
			viewDef->viewport.y1 + caster.scissorY1,
			caster.scissorX2 + 1 - caster.scissorX1,
			caster.scissorY2 + 1 - caster.scissorY1 );
	}
	if ( view.useDepthBounds ) {
		glDepthBoundsEXT( caster.depthMin, caster.depthMax );
	}
	if ( view.useShadowVertexProgram ) {
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_ORIGIN,
			caster.localLightOrigin );
	}
	idVertexCache::BindArrayBuffer( preparedCaster.vertexBuffer );
	glVertexPointer( 4, GL_FLOAT, sizeof( shadowCache_t ),
		preparedCaster.vertexPointer );
	idVertexCache::BindIndexBuffer( preparedCaster.indexBuffer );

	if ( prepared.twoSidedStencil ) {
		const GLenum frontSidedFace = viewDef->isMirror ? GL_FRONT : GL_BACK;
		const GLenum backSidedFace = viewDef->isMirror ? GL_BACK : GL_FRONT;
		GL_Cull( CT_TWO_SIDED );
		if ( caster.preload ) {
			glStencilOpSeparate( frontSidedFace, GL_KEEP,
				tr.stencilDecr, tr.stencilDecr );
			glStencilOpSeparate( backSidedFace, GL_KEEP,
				tr.stencilIncr, tr.stencilIncr );
			RB_SharedWorldInteractionGLSubmitShadowElements( preparedCaster );
		}
		glStencilOpSeparate( frontSidedFace, GL_KEEP, GL_KEEP,
			tr.stencilIncr );
		glStencilOpSeparate( backSidedFace, GL_KEEP, GL_KEEP,
			tr.stencilDecr );
		RB_SharedWorldInteractionGLSubmitShadowElements( preparedCaster );
		return;
	}

	if ( caster.preload ) {
		glStencilOp( GL_KEEP, tr.stencilDecr, tr.stencilDecr );
		GL_Cull( CT_FRONT_SIDED );
		RB_SharedWorldInteractionGLSubmitShadowElements( preparedCaster );
		glStencilOp( GL_KEEP, tr.stencilIncr, tr.stencilIncr );
		GL_Cull( CT_BACK_SIDED );
		RB_SharedWorldInteractionGLSubmitShadowElements( preparedCaster );
	}
	glStencilOp( GL_KEEP, GL_KEEP, tr.stencilIncr );
	GL_Cull( CT_FRONT_SIDED );
	RB_SharedWorldInteractionGLSubmitShadowElements( preparedCaster );
	glStencilOp( GL_KEEP, GL_KEEP, tr.stencilDecr );
	GL_Cull( CT_BACK_SIDED );
	RB_SharedWorldInteractionGLSubmitShadowElements( preparedCaster );
}

static void RB_SharedWorldInteractionGLEndShadowState(
		const classicInteractionDomainView_t &view ) {
	GL_Cull( CT_FRONT_SIDED );
	if ( view.shadowPolygonFactor != 0.0f
			|| view.shadowPolygonUnits != 0.0f ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	if ( view.useDepthBounds ) {
		glDisable( GL_DEPTH_BOUNDS_TEST_EXT );
	}
	glStencilFunc( GL_GEQUAL, view.stencilReference, 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
}

static void RB_SharedWorldInteractionGLDrawShadowRange(
		const rbSharedWorldInteractionGLPreparedView_t &prepared,
		const rbSharedWorldInteractionGLPreparedLight_t &light,
		int chain, const classicInteractionDomainView_t &view,
		const viewDef_t *viewDef, int &logicalVolumeDraws,
		int &preloadVolumeDraws ) {
	const int count = light.shadowCasterCount[chain];
	if ( count <= 0 ) {
		return;
	}
	RB_SharedWorldInteractionGLBeginShadowState( view );
	const int first = light.firstShadowCaster[chain];
	for ( int i = 0; i < count; ++i ) {
		const rbSharedWorldInteractionGLPreparedShadowCaster_t &caster =
			prepared.shadowCasters[first + i];
		RB_SharedWorldInteractionGLDrawShadowCaster(
			caster, prepared, view, viewDef );
		logicalVolumeDraws++;
		preloadVolumeDraws += caster.caster->preload ? 1 : 0;
	}
	RB_SharedWorldInteractionGLEndShadowState( view );
}

static void RB_SharedWorldInteractionGLRestoreState( void ) {
	glDisable( GL_POLYGON_OFFSET_FILL );
	if ( glConfig.depthBoundsTestAvailable ) {
		glDisable( GL_DEPTH_BOUNDS_TEST_EXT );
	}
	const int maxUnit = Min( 8, Min( glConfig.maxTextureUnits,
		glConfig.maxTextureImageUnits ) - 1 );
	for ( int unit = maxUnit; unit >= 1; --unit ) {
		GL_SelectTextureNoClient( unit );
		globalImages->BindNull();
	}
	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( 0 );
	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );
	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glStencilFunc( GL_ALWAYS, 128, 255 );
	glStencilMask( 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	GL_Cull( CT_FRONT_SIDED );
	backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
	backEnd.currentSpace = NULL;
}

static bool RB_ARB2_DrawSharedInteractionView( const viewDef_t *viewDef,
		const classicInteractionDomainView_t &view ) {
	rbSharedWorldInteractionGLPreparedView_t &prepared =
		rbSharedWorldInteractionGLPreparedView;
	if ( !prepared.ready || prepared.view != &view
			|| prepared.viewDef != viewDef || prepared.hash != view.hash ) {
		return false;
	}

	// Ownership commits here.  Every operation which can reject the view has
	// completed, so no code below this point returns to the classic walker.
	prepared.committed = true;
	if ( !g_firstARB2InteractionHandoffBreadcrumb ) {
		g_firstARB2InteractionHandoffBreadcrumb = true;
		R_RecordRendererStartupPhase(
			RENDERER_STARTUP_PHASE_FIRST_ARB2_INTERACTION_HANDOFF );
	}
	RB_LogComment( "---------- RB_ARB2_DrawSharedInteractionView ----------\n" );
	RB_ARB2_InvalidateEnvParamCache();
	GL_SelectTexture( 0 );
	glEnableClientState( GL_VERTEX_ARRAY );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glClearStencil( view.stencilReference );
	glStencilMask( 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	glStencilFunc( GL_ALWAYS, view.stencilReference, 255 );

	int drawnPrimitives = 0;
	const int submittedShadowCasters = prepared.drawableShadowCasterCount;
	int logicalVolumeDraws = 0;
	int preloadVolumeDraws = 0;
	for ( int lightIndex = 0; lightIndex < prepared.lightCount; ++lightIndex ) {
		const rbSharedWorldInteractionGLPreparedLight_t &preparedLight =
			prepared.lights[lightIndex];
		const classicInteractionDomainLight_t &light = *preparedLight.light;
		backEnd.vLight = const_cast<viewLight_t *>( light.legacyViewLight );
		classicInteractionDomainShadowMode_t preparedVolumeMode =
			CLASSIC_INTERACTION_SHADOW_NONE;
		bool preparedVolumeIncludesLocal = false;
		const auto clearLightStencil = [&]() {
			backEnd.currentScissor.x1 = light.scissorX1;
			backEnd.currentScissor.y1 = light.scissorY1;
			backEnd.currentScissor.x2 = light.scissorX2;
			backEnd.currentScissor.y2 = light.scissorY2;
			if ( view.useScissor ) {
				glScissor( viewDef->viewport.x1 + light.scissorX1,
					viewDef->viewport.y1 + light.scissorY1,
					light.scissorX2 + 1 - light.scissorX1,
					light.scissorY2 + 1 - light.scissorY1 );
			}
			glClear( GL_STENCIL_BUFFER_BIT );
		};
		const auto drawOrdinaryReceiver = [&](
				classicInteractionDomainReceiver_t receiver ) {
			if ( preparedLight.primitiveCount[receiver] <= 0 ) {
				return;
			}
			RB_SharedWorldInteractionGLBeginInteractionState();
			drawnPrimitives += RB_SharedWorldInteractionGLDrawPrimitiveRange(
				prepared, preparedLight, receiver, view, viewDef );
		};
		const auto mappedMode = []( classicInteractionDomainShadowMode_t mode ) {
			return mode == CLASSIC_INTERACTION_SHADOW_PROJECTED
				|| mode == CLASSIC_INTERACTION_SHADOW_POINT
				|| mode == CLASSIC_INTERACTION_SHADOW_HYBRID;
		};
		const auto prepareVolumeState = [&] (
				classicInteractionDomainShadowMode_t mode,
				bool includeLocal ) {
			const bool hybrid = mode == CLASSIC_INTERACTION_SHADOW_HYBRID;
			if ( mode != CLASSIC_INTERACTION_SHADOW_STENCIL && !hybrid ) {
				return;
			}
			if ( preparedVolumeMode != mode ) {
				clearLightStencil();
				RB_SharedWorldInteractionGLDrawShadowRange( prepared,
					preparedLight, hybrid
						? CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_GLOBAL
						: CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_GLOBAL,
					view, viewDef, logicalVolumeDraws,
					preloadVolumeDraws );
				preparedVolumeMode = mode;
				preparedVolumeIncludesLocal = false;
			}
			if ( includeLocal && !preparedVolumeIncludesLocal ) {
				RB_SharedWorldInteractionGLDrawShadowRange( prepared,
					preparedLight, hybrid
						? CLASSIC_INTERACTION_SHADOW_CHAIN_SUPPLEMENT_LOCAL
						: CLASSIC_INTERACTION_SHADOW_CHAIN_STENCIL_LOCAL,
					view, viewDef, logicalVolumeDraws,
					preloadVolumeDraws );
				preparedVolumeIncludesLocal = true;
			}
		};

		const classicInteractionDomainShadowMode_t localMode =
			light.receiverShadowMode[CLASSIC_INTERACTION_RECEIVER_LOCAL];
		if ( localMode == CLASSIC_INTERACTION_SHADOW_STENCIL ) {
			prepareVolumeState( localMode, false );
			glStencilFunc( GL_GEQUAL, view.stencilReference, 255 );
			drawOrdinaryReceiver( CLASSIC_INTERACTION_RECEIVER_LOCAL );
		} else if ( mappedMode( localMode ) ) {
			if ( localMode == CLASSIC_INTERACTION_SHADOW_HYBRID ) {
				prepareVolumeState( localMode, false );
				glStencilFunc( GL_GEQUAL, view.stencilReference, 255 );
			} else {
				glStencilFunc( GL_ALWAYS, view.stencilReference, 255 );
			}
			drawnPrimitives += RB_SharedWorldInteractionGLCommitMappedPass(
				prepared, preparedLight,
				CLASSIC_INTERACTION_RECEIVER_LOCAL, view, viewDef );
		} else {
			glStencilFunc( GL_ALWAYS, view.stencilReference, 255 );
			drawOrdinaryReceiver( CLASSIC_INTERACTION_RECEIVER_LOCAL );
		}

		const classicInteractionDomainShadowMode_t globalMode =
			light.receiverShadowMode[CLASSIC_INTERACTION_RECEIVER_GLOBAL];
		if ( globalMode == CLASSIC_INTERACTION_SHADOW_STENCIL ) {
			prepareVolumeState( globalMode, true );
			glStencilFunc( GL_GEQUAL, view.stencilReference, 255 );
			drawOrdinaryReceiver( CLASSIC_INTERACTION_RECEIVER_GLOBAL );
		} else if ( mappedMode( globalMode ) ) {
			if ( globalMode == CLASSIC_INTERACTION_SHADOW_HYBRID ) {
				prepareVolumeState( globalMode, true );
				glStencilFunc( GL_GEQUAL, view.stencilReference, 255 );
			} else {
				glStencilFunc( GL_ALWAYS, view.stencilReference, 255 );
			}
			drawnPrimitives += RB_SharedWorldInteractionGLCommitMappedPass(
				prepared, preparedLight,
				CLASSIC_INTERACTION_RECEIVER_GLOBAL, view, viewDef );
		} else {
			glStencilFunc( GL_ALWAYS, view.stencilReference, 255 );
			drawOrdinaryReceiver( CLASSIC_INTERACTION_RECEIVER_GLOBAL );
		}

		const classicInteractionDomainShadowMode_t translucentMode =
			light.receiverShadowMode[
				CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT];
		if ( mappedMode( translucentMode ) ) {
			if ( translucentMode == CLASSIC_INTERACTION_SHADOW_HYBRID ) {
				prepareVolumeState( translucentMode, true );
			}
			glStencilFunc( translucentMode == CLASSIC_INTERACTION_SHADOW_HYBRID
					? GL_GEQUAL : GL_ALWAYS,
				view.stencilReference, 255 );
			drawnPrimitives += RB_SharedWorldInteractionGLCommitMappedPass(
				prepared, preparedLight,
				CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT, view, viewDef );
		} else if ( translucentMode == CLASSIC_INTERACTION_SHADOW_STENCIL ) {
			prepareVolumeState( translucentMode, true );
			glStencilFunc( GL_GEQUAL, view.stencilReference, 255 );
			drawOrdinaryReceiver(
				CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT );
		} else {
			glStencilFunc( GL_ALWAYS, view.stencilReference, 255 );
			drawOrdinaryReceiver(
				CLASSIC_INTERACTION_RECEIVER_TRANSLUCENT );
		}
	}

	RB_SharedWorldInteractionGLRestoreState();
	RB_ShadowMapStatsReport();
	RB_ShadowMapDebugOverlayDraw();
	const bool coverageRecorded = R_ClassicInteractionDomain_RecordOwned(
		viewDef, CLASSIC_INTERACTION_BACKEND_GL, drawnPrimitives,
		prepared.noopPrimitiveCount, submittedShadowCasters,
		prepared.noopShadowCasterCount, logicalVolumeDraws,
		preloadVolumeDraws, prepared.shadowMapPassCount,
		prepared.hybridShadowPassCount );
	if ( !coverageRecorded ) {
		// The framebuffer is already committed.  Report the contract violation,
		// but never render the classic stream too.
		common->Warning( "RB_ARB2_DrawSharedInteractionView: GL ownership coverage rejected after committed draw (lights=%d surfaces=%d primitives=%d drawn=%d noop=%d shadowCasters=%d shadowNoop=%d volumes=%d preloads=%d hash=%llu)",
			prepared.lightCount, prepared.surfaceCount, prepared.primitiveCount,
			drawnPrimitives, prepared.noopPrimitiveCount,
			submittedShadowCasters, prepared.noopShadowCasterCount,
			logicalVolumeDraws, preloadVolumeDraws,
			static_cast<unsigned long long>( prepared.hash ) );
	}
	return true;
}

void RB_ARB2_DrawInteractions( void ) {
	viewLight_t		*vLight;

	if ( r_rendererSharedWorldInteraction.GetBool() ) {
		std::memset( &rbSharedWorldInteractionGLPreparedView, 0,
			sizeof( rbSharedWorldInteractionGLPreparedView ) );
		const classicInteractionDomainView_t *sharedView =
			R_ClassicInteractionDomain_FindView( backEnd.viewDef );
		if ( sharedView == NULL ) {
			R_ClassicInteractionDomain_RecordBackendFallback( backEnd.viewDef,
				CLASSIC_INTERACTION_BACKEND_GL,
				CLASSIC_INTERACTION_FAILURE_BACKEND_NOT_READY,
				RB_SharedWorldInteractionGLFailureDetail(
					RB_SHARED_WORLD_INTERACTION_GL_REJECT_VIEW ) );
		} else if ( sharedView->backendOutcome[CLASSIC_INTERACTION_BACKEND_GL]
				!= CLASSIC_INTERACTION_BACKEND_FALLBACK ) {
			if ( !sharedView->ready ) {
				R_ClassicInteractionDomain_RecordBackendFallback(
					backEnd.viewDef, CLASSIC_INTERACTION_BACKEND_GL,
					sharedView->failure != CLASSIC_INTERACTION_FAILURE_NONE
						? sharedView->failure
						: CLASSIC_INTERACTION_FAILURE_BACKEND_NOT_READY,
					sharedView->failureDetail );
			} else {
				int sharedFailureDetail = 0;
				if ( RB_ARB2_SharedInteractionPreflight( backEnd.viewDef,
						*sharedView, sharedFailureDetail ) ) {
					if ( RB_ARB2_DrawSharedInteractionView(
							backEnd.viewDef, *sharedView ) ) {
						return;
					}
					// The draw function can reject only before its commit point.
					sharedFailureDetail =
						RB_SharedWorldInteractionGLFailureDetail(
							RB_SHARED_WORLD_INTERACTION_GL_REJECT_COVERAGE );
				}
				R_ClassicInteractionDomain_RecordBackendFallback(
					backEnd.viewDef, CLASSIC_INTERACTION_BACKEND_GL,
					CLASSIC_INTERACTION_FAILURE_BACKEND_REJECTED,
					sharedFailureDetail );
			}
		}
	}

	if ( glConfig.disableARB2Interactions ) {
		if ( r_interactionColorMode.IsModified() ) {
			r_interactionColorMode.ClearModified();
		}
		RB_ShadowMapStatsReset();
		RB_ShadowMapDebugOverlayReset();
		if ( !g_arb2InteractionDriverBypassWarned ) {
			g_arb2InteractionDriverBypassWarned = true;
			R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_DRIVER_BYPASS );
			common->Warning( "Apple OpenGL 2.1 compatibility path bypassing ARB2 light interactions to avoid issue #73 startup crash; scene lighting is degraded" );
		}
		RB_ARB2_RestoreBypassedInteractionState();
		return;
	}

	if ( r_interactionColorMode.IsModified() ) {
		r_interactionColorMode.ClearModified();
		RB_UpdateInteractionColorMode( true );
	}

	GL_SelectTexture( 0 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	RB_ShadowMapStatsReset();
	RB_ShadowMapDebugOverlayReset();
	RB_ShadowMapBuildUpdateAdmissions();

	//
	// for each light, perform adding and shadowing
	//
	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		// do fogging later
		if ( vLight->lightShader->IsFogLight() ) {
			continue;
		}
		if ( vLight->lightShader->IsBlendLight() ) {
			continue;
		}

		if ( !vLight->localInteractions && !vLight->globalInteractions
			&& !vLight->translucentInteractions ) {
			continue;
		}

		g_shadowMapStats.totalLights++;
		RB_ShadowMapStatsAccumulateCasterPolicy( vLight );

		const shadowMapLightSupportReason_t supportReason = RB_ShadowMapLightSupportReason( vLight );
		if ( supportReason == SHADOWMAP_SUPPORT_OK ) {
			g_shadowMapStats.supportedLights++;
			const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
			switch ( classification.lightClass ) {
			case SHADOWMAP_LIGHT_POINT:
				g_shadowMapStats.pointLights++;
				break;
			case SHADOWMAP_LIGHT_PARALLEL:
				g_shadowMapStats.parallelLights++;
				break;
			case SHADOWMAP_LIGHT_GLOBAL:
				g_shadowMapStats.globalLights++;
				break;
			default:
				g_shadowMapStats.projectedLights++;
				break;
			}
			if ( classification.csmEnabled ) {
				g_shadowMapStats.cascadeLights++;
				g_shadowMapStats.cascadeCount += classification.cascadeCount;
			}
			const bool projectedCsmEligible = classification.ordinaryProjectedLight
				&& g_shadowMapStats.csmRequested
				&& g_shadowMapStats.requestedCascadeCount > 1;
			if ( classification.ordinaryProjectedLight && projectedCsmEligible ) {
				g_shadowMapStats.projectedCsmEligibleLights++;
				if ( classification.csmEnabled ) {
					g_shadowMapStats.projectedCsmCascadeLights++;
				} else {
					g_shadowMapStats.projectedCsmSuppressedLights++;
				}
			} else if ( !classification.ordinaryProjectedLight && classification.csmEnabled ) {
				g_shadowMapStats.nonProjectedCsmLights++;
			}
			RB_ShadowMapLightReport( vLight, supportReason );

			glStencilFunc( GL_ALWAYS, 128, 255 );
			g_shadowMapGlobalPassMapped = SHADOWMAP_GLOBAL_MAPPED_NONE;
			g_shadowMapGlobalPassHybrid = false;
			g_shadowMapDeferredGlobalReportPending = false;
			bool deferGlobalReceiverDraw = false;
			const drawSurf_t *globalReceiverInteractions =
				RB_ShadowMapGlobalReceiverPlanningChain(
					vLight, deferGlobalReceiverDraw );

			if ( classification.pointLight ) {
				// Point-light shadow maps use the same ownership split as the retail
				// stencil path: local receivers see global casters, while global
				// receivers see both global and noSelfShadow/local casters. The
				// shadow-map pass must only draw dedicated ambient-geometry casters:
				// legacy stencil shadow chains resolve back to the same ambient
				// surfaces and can double-submit overlapping collision/helper meshes.
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_LOCAL, true, vLight->globalShadowMapCasters, NULL, vLight->globalShadowMapDynamicCasters, NULL, vLight->globalShadows, NULL, vLight->localInteractions );
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_GLOBAL, true, vLight->globalShadowMapCasters, vLight->localShadowMapCasters, vLight->globalShadowMapDynamicCasters, vLight->localShadowMapDynamicCasters, vLight->globalShadows, vLight->localShadows, globalReceiverInteractions, deferGlobalReceiverDraw );
			} else {
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_LOCAL, false, vLight->globalShadowMapCasters, NULL, vLight->globalShadowMapDynamicCasters, NULL, vLight->globalShadows, NULL, vLight->localInteractions );
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_GLOBAL, false, vLight->globalShadowMapCasters, vLight->localShadowMapCasters, vLight->globalShadowMapDynamicCasters, vLight->localShadowMapDynamicCasters, vLight->globalShadows, vLight->localShadows, globalReceiverInteractions, deferGlobalReceiverDraw );
			}
			if ( deferGlobalReceiverDraw
				&& vLight->globalInteractions != NULL ) {
				// The translucent chain drove GLOBAL map preparation because the
				// opaque chain has no map-capable receiver. Draw that opaque fallback
				// subset now, before the ordered translucent phase below.
				RB_ShadowMapDrawReceiverFallbacks( vLight,
					SHADOWMAP_PASS_GLOBAL, vLight->globalShadows,
					vLight->localShadows, vLight->globalInteractions );
			}

			if ( !r_skipTranslucent.GetBool() ) {
				backEnd.depthFunc = GLS_DEPTHFUNC_LESS;
				if ( r_shadowMapTranslucentReceivers.GetBool()
					&& g_shadowMapGlobalPassMapped != SHADOWMAP_GLOBAL_MAPPED_NONE
					&& vLight->translucentInteractions != NULL ) {
					// stencil parity (C3): the GLOBAL pass just mapped, so its
					// receiver state (projected state, bound depth map/cube) is
					// still current - translucent receivers sample the same map
					// the opaque receivers used. Surfaces the receiver path
					// cannot draw (custom GLSL, generated geometry, prepare
					// failures) take the filtered stencil fallback below.
					shadowMapTimedPhase_t translucentMaskTimer;
					RB_ShadowMapBeginTimedPhase( translucentMaskTimer, vLight,
						SHADOWMAP_PASS_GLOBAL, SHADOWMAP_TIMING_MASK_PASS );
					const bool receiverStencilReady =
						RB_ShadowMapPrepareMappedReceiverStencil( vLight,
							SHADOWMAP_PASS_GLOBAL,
							g_shadowMapGlobalPassHybrid );
					const bool translucentMaskOk = receiverStencilReady
						&& ( g_shadowMapGlobalPassMapped == SHADOWMAP_GLOBAL_MAPPED_POINT
							? RB_GLSLPointShadowMap_CreateDrawInteractions(
								vLight->translucentInteractions )
							: RB_GLSLShadowMap_CreateDrawInteractions(
								vLight->translucentInteractions ) );
					g_shadowMapStats.cpuMaskMilliseconds +=
						RB_ShadowMapEndTimedPhase( translucentMaskTimer );
					if ( translucentMaskOk ) {
						g_shadowMapStats.translucentReceiverPasses++;
						RB_ShadowMapCompleteDeferredGlobalReceiver( vLight,
							classification.pointLight, true );
						RB_ShadowMapTrackWrappedCustomGLSLReceivers(
							vLight, SHADOWMAP_PASS_GLOBAL,
							vLight->translucentInteractions );
						RB_ShadowMapDrawReceiverFallbacks( vLight,
							SHADOWMAP_PASS_GLOBAL, vLight->globalShadows,
							vLight->localShadows,
							vLight->translucentInteractions );
						if ( g_shadowMapMaskPrepareFailures > 0 ) {
							RB_ShadowMapStencilFallbackFiltered(
								vLight->globalShadows,
								vLight->localShadows,
								vLight->translucentInteractions,
								RB_SurfaceShadowMapReceiverPrepareFailed );
						}
					} else {
						RB_ShadowMapCompleteDeferredGlobalReceiver( vLight,
							classification.pointLight, false );
						RB_ShadowMapMarkStencilFallbackSticky( vLight );
						RB_ShadowMapStencilFallback( vLight,
							SHADOWMAP_PASS_GLOBAL, vLight->globalShadows,
							vLight->localShadows,
							vLight->translucentInteractions );
					}
				} else if ( vLight->translucentInteractions != NULL
					&& r_shadows.GetBool()
					&& ( r_shadowMapTranslucentReceivers.GetBool()
						|| r_stencilTranslucentShadows.GetBool() )
					&& ( vLight->globalShadows != NULL
						|| vLight->localShadows != NULL ) ) {
					// A deferred GLOBAL map that could not be prepared must not
					// degrade translucent receivers to an unshadowed draw. Restamp
					// the complete ownership and draw it once at translucent depth.
					RB_ShadowMapStencilFallback( vLight,
						SHADOWMAP_PASS_GLOBAL, vLight->globalShadows,
						vLight->localShadows,
						vLight->translucentInteractions );
				} else {
					RB_DrawMaterialInteractions( vLight->translucentInteractions );
				}
				backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
			}

			continue;
		}

		if ( supportReason >= 0 && supportReason < SHADOWMAP_SUPPORT_COUNT ) {
			g_shadowMapStats.unsupportedLights[supportReason]++;
		}
		RB_ShadowMapLightReport( vLight, supportReason );

		// clear the stencil buffer if needed
		if ( vLight->globalShadows || vLight->localShadows ) {
			backEnd.currentScissor = vLight->scissorRect;
			if ( r_useScissor.GetBool() ) {
				glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
					backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
					backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
					backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
			}
			glClear( GL_STENCIL_BUFFER_BIT );
		} else {
			// no shadows, so no need to read or write the stencil buffer
			// we might in theory want to use GL_ALWAYS instead of disabling
			// completely, to satisfy the invarience rules
			glStencilFunc( GL_ALWAYS, 128, 255 );
		}

		if ( r_useShadowVertexProgram.GetBool() && R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW, "stencil shadow vertex program", true ) ) {
			glEnable( GL_VERTEX_PROGRAM_ARB );
			RB_StencilShadowPass( vLight->globalShadows );
			RB_DrawMaterialInteractions( vLight->localInteractions );
			glEnable( GL_VERTEX_PROGRAM_ARB );
			R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW, "stencil shadow vertex program", true );
			RB_StencilShadowPass( vLight->localShadows );
			RB_DrawMaterialInteractions( vLight->globalInteractions );
			glDisable( GL_VERTEX_PROGRAM_ARB );	// if there weren't any globalInteractions, it would have stayed on
		} else {
			RB_StencilShadowPass( vLight->globalShadows );
			RB_DrawMaterialInteractions( vLight->localInteractions );
			RB_StencilShadowPass( vLight->localShadows );
			RB_DrawMaterialInteractions( vLight->globalInteractions );
		}

		if ( r_skipTranslucent.GetBool() ) {
			continue;
		}

		const bool shadowTranslucentInteractions =
			r_stencilTranslucentShadows.GetBool() &&
			r_shadows.GetBool() &&
			( vLight->globalShadows != NULL || vLight->localShadows != NULL );

		// Keep the legacy unshadowed path available, but default translucent receivers
		// to the same stencil test as opaque interactions when a stencil shadow exists.
		glStencilFunc( shadowTranslucentInteractions ? GL_GEQUAL : GL_ALWAYS, 128, 255 );

		backEnd.depthFunc = GLS_DEPTHFUNC_LESS;
		RB_DrawMaterialInteractions( vLight->translucentInteractions );

		backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
		continue;
	}

	// disable stencil shadow test
	glStencilFunc( GL_ALWAYS, 128, 255 );
	RB_ShadowMapStatsReport();
	RB_ShadowMapDebugOverlayDraw();

	if ( ( RendererDriverQuirks_LastReport().flags & RENDERER_DRIVER_QUIRK_DISABLE_ARB2_INTERACTIONS ) != 0 ) {
		// Apple interaction routing switches between GLSL and ARB programs on a
		// per-surface basis. Restore every touched compatibility-state family at
		// the end of the pass so the following ambient/fog pass starts clean.
		RB_ARB2_RestoreInteractionState( false );
	} else {
		GL_SelectTexture( 0 );
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	}
}

//===================================================================================


typedef struct {
	GLenum			target;
	GLuint			ident;
	char			name[64];
	bool			valid;
	bool			warnedOnUse;
	bool			requiredForLighting;
	int				errorPosition;
	char			failureReason[256];
	char			normalizedPath[96];
} progDef_t;

static	const int	MAX_GLPROGS = 200;

// a single file can have both a vertex program and a fragment program
static progDef_t	progs[MAX_GLPROGS] = {
	{ GL_VERTEX_PROGRAM_ARB, VPROG_TEST, "test.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_TEST, "test.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION, "interaction.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION, "interaction.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_AMBIENT, "ambientLight.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_AMBIENT, "ambientLight.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW, "shadow.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_R200_INTERACTION, "R200_interaction.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_BUMP_AND_LIGHT, "nv20_bumpAndLight.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_DIFFUSE_COLOR, "nv20_diffuseColor.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_SPECULAR_COLOR, "nv20_specularColor.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_DIFFUSE_AND_SPECULAR_COLOR, "nv20_diffuseAndSpecularColor.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_ENVIRONMENT, "environment.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_ENVIRONMENT, "environment.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_GLASSWARP, "arbVP_glasswarp.txt" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_GLASSWARP, "arbFP_glasswarp.txt" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_SIMPLE_INTERACTION, "SimpleInteraction.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_SIMPLE_INTERACTION, "SimpleInteraction.vfp" },
	// Retail Quake 4 reserves fixed vertex-program ids for the packed MD5R
	// families; register the stock glprogs here so dormant bind paths can
	// resolve those ids without falling back to dynamic PROG_USER entries.
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_INTERACTION_VPROG_BASE, "md5rinteraction.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_INTERACTION_VPROG_BASE + 1, "md5rinteraction1.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_INTERACTION_VPROG_BASE + 2, "md5rinteraction4.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_DEPTH_VPROG_BASE, "md5rsimple.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_DEPTH_VPROG_BASE + 1, "md5rsimple1.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_DEPTH_VPROG_BASE + 2, "md5rsimple4.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_STAGE_VPROG_BASE, "md5rstdtex.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_STAGE_VPROG_BASE + 1, "md5rstdtex1.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_STAGE_VPROG_BASE + 2, "md5rstdtex4.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_SKYBOX_VPROG_BASE, "md5rskybox.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_SKYBOX_VPROG_BASE + 1, "md5rskybox1.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_SKYBOX_VPROG_BASE + 2, "md5rskybox4.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_DIFFUSE_CUBE_VPROG_BASE, "md5renvnormal.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_DIFFUSE_CUBE_VPROG_BASE + 1, "md5renvnormal1.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_DIFFUSE_CUBE_VPROG_BASE + 2, "md5renvnormal4.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_REFLECT_CUBE_VPROG_BASE, "md5renvreflect.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_REFLECT_CUBE_VPROG_BASE + 1, "md5renvreflect1.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_REFLECT_CUBE_VPROG_BASE + 2, "md5renvreflect4.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_BUMPY_REFLECT_CUBE_VPROG_BASE, "md5renvbump.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_BUMPY_REFLECT_CUBE_VPROG_BASE + 1, "md5renvbump1.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_BUMPY_REFLECT_CUBE_VPROG_BASE + 2, "md5renvbump4.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_SHADOW_VOLUME_VPROG_BASE, "md5rshadow.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_SHADOW_VOLUME_VPROG_BASE + 1, "md5rshadow1.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_SHADOW_VOLUME_VPROG_BASE + 2, "md5rshadow4.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_BASIC_FOG_VPROG_BASE, "md5rbasicfog.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_BASIC_FOG_VPROG_BASE + 1, "md5rbasicfog1.vp" },
	{ GL_VERTEX_PROGRAM_ARB, ARB2_MD5R_BASIC_FOG_VPROG_BASE + 2, "md5rbasicfog4.vp" },

	// additional programs can be dynamically specified in materials
};

static const char *RB_ARBProgramTargetName( GLenum target ) {
	switch ( target ) {
	case GL_VERTEX_PROGRAM_ARB:
		return "vertex";
	case GL_FRAGMENT_PROGRAM_ARB:
		return "fragment";
	default:
		return "unknown";
	}
}

static bool RB_IsRequiredLightingARBProgram( const progDef_t &prog ) {
	return prog.ident == VPROG_INTERACTION ||
		prog.ident == FPROG_INTERACTION ||
		prog.ident == VPROG_SIMPLE_INTERACTION ||
		prog.ident == FPROG_SIMPLE_INTERACTION ||
		idStr::Icmp( prog.name, "interaction.vfp" ) == 0 ||
		idStr::Icmp( prog.name, "SimpleInteraction.vfp" ) == 0;
}

static void RB_SetARBProgramPath( progDef_t &prog ) {
	idStr fullPath = "glprogs/";
	fullPath += prog.name;
	fullPath.BackSlashesToSlashes();
	idStr::Copynz( prog.normalizedPath, fullPath.c_str(), sizeof( prog.normalizedPath ) );
}

static void RB_ResetARBProgramStatus( progDef_t &prog ) {
	prog.valid = false;
	prog.warnedOnUse = false;
	prog.requiredForLighting = RB_IsRequiredLightingARBProgram( prog );
	prog.errorPosition = -1;
	prog.failureReason[0] = '\0';
	RB_SetARBProgramPath( prog );
}

static void RB_SetARBProgramFailure( progDef_t &prog, const char *reason, int errorPosition = -1 ) {
	prog.valid = false;
	prog.errorPosition = errorPosition;
	idStr::Copynz( prog.failureReason, reason ? reason : "unknown failure", sizeof( prog.failureReason ) );
}

static progDef_t *RB_FindARBProgramRecord( GLenum target, GLuint ident ) {
	if ( ident == 0 ) {
		return NULL;
	}

	for ( int i = 0; i < MAX_GLPROGS && progs[i].name[0]; i++ ) {
		if ( progs[i].target == target && progs[i].ident == ident ) {
			return &progs[i];
		}
	}

	return NULL;
}

static bool RB_DriverPrefersSimpleInteraction( void ) {
	// Once R_ARB2_Init has run, glConfig.preferSimpleInteraction is the
	// single source of truth: it is derived there from the driver quirk
	// report and the r_appleARB2Interactions policy (mode 2 clears it so the
	// full diagnostic path is actually exercised). The raw quirk flag
	// only backstops calls made before R_ARB2_Init.
	if ( glConfig.allowARB2Path ) {
		return glConfig.preferSimpleInteraction;
	}
	return glConfig.preferSimpleInteraction ||
		( ( RendererDriverQuirks_LastReport().flags & RENDERER_DRIVER_QUIRK_PREFER_SIMPLE_INTERACTION ) != 0 );
}

static bool RB_AppleGL21CorridorActive( void ) {
	return ( RendererDriverQuirks_LastReport().flags & RENDERER_DRIVER_QUIRK_DISABLE_ARB2_INTERACTIONS ) != 0;
}

static bool RB_UseSimpleInteractionShader( void ) {
	// Inside the Apple GL 2.1 corridor r_appleARB2Interactions is authoritative.
	// r_useSimpleInteraction is archived, so ORing it in here silently defeated
	// mode 2 -- the one mode whose whole purpose is to run the full ARB2 path --
	// for anyone who had ever set it.
	if ( RB_AppleGL21CorridorActive() ) {
		return RB_DriverPrefersSimpleInteraction();
	}
	return r_useSimpleInteraction.GetBool() || RB_DriverPrefersSimpleInteraction();
}

static bool RB_ShouldSkipFullInteractionUpload( const progDef_t &prog ) {
	return RB_DriverPrefersSimpleInteraction() &&
		( prog.ident == VPROG_INTERACTION ||
		  prog.ident == FPROG_INTERACTION ||
		  idStr::Icmp( prog.name, "interaction.vfp" ) == 0 );
}

static bool RB_IsFullInteractionProgram( const progDef_t &prog ) {
	return prog.ident == VPROG_INTERACTION ||
		prog.ident == FPROG_INTERACTION ||
		idStr::Icmp( prog.name, "interaction.vfp" ) == 0;
}

static bool RB_IsSimpleInteractionProgram( const progDef_t &prog ) {
	return prog.ident == VPROG_SIMPLE_INTERACTION ||
		prog.ident == FPROG_SIMPLE_INTERACTION ||
		idStr::Icmp( prog.name, "SimpleInteraction.vfp" ) == 0;
}

static GLuint RB_CurrentInteractionProgramIdent( GLenum target ) {
	if ( r_testARBProgram.GetBool() ) {
		return ( target == GL_VERTEX_PROGRAM_ARB ) ? VPROG_TEST : FPROG_TEST;
	}

	if ( RB_UseSimpleInteractionShader() ) {
		return ( target == GL_VERTEX_PROGRAM_ARB ) ? VPROG_SIMPLE_INTERACTION : FPROG_SIMPLE_INTERACTION;
	}

	return ( target == GL_VERTEX_PROGRAM_ARB ) ? VPROG_INTERACTION : FPROG_INTERACTION;
}

static const char *RB_CurrentInteractionProgramFamilyName( void ) {
	if ( r_testARBProgram.GetBool() ) {
		return "test";
	}

	if ( RB_UseSimpleInteractionShader() ) {
		return "simple";
	}

	return "interaction";
}

static void RB_RecordCurrentInteractionSelectionBreadcrumb( void ) {
	if ( RB_UseSimpleInteractionShader() ) {
		R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_SIMPLE_SELECTION );
	} else {
		R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_FULL_SELECTION );
	}
	common->Printf( "renderer startup ARB interaction selection: family=%s vertex=%u fragment=%u colorMode=%s auto=%s override=%s\n",
		RB_CurrentInteractionProgramFamilyName(),
		RB_CurrentInteractionProgramIdent( GL_VERTEX_PROGRAM_ARB ),
		RB_CurrentInteractionProgramIdent( GL_FRAGMENT_PROGRAM_ARB ),
		RB_InteractionColorModeName( g_interactionVertexProgramColorMode ),
		RB_InteractionColorModeName( g_interactionVertexProgramAutoColorMode ),
		RB_InteractionColorOverrideName( g_interactionVertexProgramOverride ) );
}

static void RB_WarnInvalidARBProgramUse( progDef_t *prog, GLenum target, GLuint ident, const char *usage, bool required ) {
	if ( !required && r_shaderReport.GetInteger() < 2 ) {
		return;
	}

	if ( prog != NULL ) {
		if ( prog->warnedOnUse ) {
			return;
		}
		prog->warnedOnUse = true;
		common->Warning( "Skipping %s: ARB %s program '%s' is invalid (%s%s%s)",
			usage ? usage : "ARB program use",
			RB_ARBProgramTargetName( target ),
			prog->normalizedPath[0] ? prog->normalizedPath : prog->name,
			prog->failureReason[0] ? prog->failureReason : "unknown failure",
			( prog->errorPosition >= 0 ) ? ", errorPosition=" : "",
			( prog->errorPosition >= 0 ) ? va( "%d", prog->errorPosition ) : "" );
		return;
	}

	common->Warning( "Skipping %s: ARB %s program id %u is not registered", usage ? usage : "ARB program use", RB_ARBProgramTargetName( target ), ident );
}

static bool RB_CurrentInteractionProgramsValid( void ) {
	const GLuint vertexProgram = RB_CurrentInteractionProgramIdent( GL_VERTEX_PROGRAM_ARB );
	const GLuint fragmentProgram = RB_CurrentInteractionProgramIdent( GL_FRAGMENT_PROGRAM_ARB );
	return R_IsARBProgramValid( GL_VERTEX_PROGRAM_ARB, vertexProgram ) &&
		R_IsARBProgramValid( GL_FRAGMENT_PROGRAM_ARB, fragmentProgram );
}

static void RB_ErrorIfDriverRequiredSimpleInteractionFailed( void ) {
	if ( !RB_DriverPrefersSimpleInteraction() ) {
		return;
	}

	const progDef_t *vertexRecord = RB_FindARBProgramRecord( GL_VERTEX_PROGRAM_ARB, VPROG_SIMPLE_INTERACTION );
	const progDef_t *fragmentRecord = RB_FindARBProgramRecord( GL_FRAGMENT_PROGRAM_ARB, FPROG_SIMPLE_INTERACTION );
	if ( vertexRecord != NULL && vertexRecord->valid && fragmentRecord != NULL && fragmentRecord->valid ) {
		return;
	}

	// Absent is not the same as broken. SimpleInteraction.vfp is stock Quake 4
	// content, so a startup with no game assets installed - the renderer
	// validation harnesses, the packaged-app smoke, a first run before the
	// retail data is pointed at - reaches here with nothing to load rather than
	// with something wrong. Refusing to start there turns "you have not
	// installed the game data yet" into a fatal driver diagnostic, and the
	// interaction rescue path below already exists to render without these.
	// A program that IS present and fails to compile or validate still ends the
	// run: that is a real driver or content fault worth refusing.
	const bool vertexAbsent = vertexRecord == NULL ||
		idStr::Icmp( vertexRecord->failureReason, "file not found" ) == 0;
	const bool fragmentAbsent = fragmentRecord == NULL ||
		idStr::Icmp( fragmentRecord->failureReason, "file not found" ) == 0;
	if ( vertexAbsent && fragmentAbsent ) {
		common->Warning(
			"Apple OpenGL 2.1 compatibility path: SimpleInteraction.vfp is not installed, so interaction "
			"rendering is unavailable. This is expected without game assets; install the game data for "
			"lit rendering on this driver path." );
		return;
	}

	common->Error(
		"Unsupported Apple OpenGL 2.1 compatibility path: required SimpleInteraction.vfp ARB programs failed to load "
		"(vertex: %s, fragment: %s). The ARB2 interaction renderer cannot safely continue on this driver path.",
		( vertexRecord != NULL && vertexRecord->failureReason[0] ) ? vertexRecord->failureReason : "unavailable",
		( fragmentRecord != NULL && fragmentRecord->failureReason[0] ) ? fragmentRecord->failureReason : "unavailable" );
}

static void RB_WarnInteractionShaderRescueMode( void ) {
	if ( g_interactionShaderRescueWarned ) {
		return;
	}

	const GLuint vertexProgram = RB_CurrentInteractionProgramIdent( GL_VERTEX_PROGRAM_ARB );
	const GLuint fragmentProgram = RB_CurrentInteractionProgramIdent( GL_FRAGMENT_PROGRAM_ARB );
	const progDef_t *vertexRecord = RB_FindARBProgramRecord( GL_VERTEX_PROGRAM_ARB, vertexProgram );
	const progDef_t *fragmentRecord = RB_FindARBProgramRecord( GL_FRAGMENT_PROGRAM_ARB, fragmentProgram );

	common->Warning( "ARB interaction rescue mode active: skipping interaction rendering because required programs are invalid (%s: %s, %s: %s)",
		vertexRecord != NULL ? vertexRecord->name : "vertex",
		( vertexRecord != NULL && vertexRecord->failureReason[0] ) ? vertexRecord->failureReason : "unavailable",
		fragmentRecord != NULL ? fragmentRecord->name : "fragment",
		( fragmentRecord != NULL && fragmentRecord->failureReason[0] ) ? fragmentRecord->failureReason : "unavailable" );
	g_interactionShaderRescueWarned = true;
}

/*
=================
R_LoadARBProgram
=================
*/
bool R_IsARBProgramValid( GLenum target, GLuint ident ) {
	const progDef_t *prog = RB_FindARBProgramRecord( target, ident );
	return prog != NULL && prog->valid;
}

bool R_BindARBProgram( GLenum target, GLuint ident, const char *usage, bool required ) {
	progDef_t *prog = RB_FindARBProgramRecord( target, ident );
	if ( prog == NULL || !prog->valid ) {
		RB_WarnInvalidARBProgramUse( prog, target, ident, usage, required );
		return false;
	}

	glBindProgramARB( target, ident );
	return true;
}

void R_LoadARBProgram( int progIndex ) {
	int		ofs;
	int		err;
	progDef_t &prog = progs[progIndex];
	idStr	fullPath = "glprogs/";
	fullPath += prog.name;
	fullPath.BackSlashesToSlashes();
	char	*fileBuffer;
	char	*buffer;
	idList<char> programBuffer;
	char	*start = NULL, *end;

	RB_ResetARBProgramStatus( prog );

	if ( RB_IsFullInteractionProgram( prog ) ) {
		R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_FULL_UPLOAD );
	} else if ( RB_IsSimpleInteractionProgram( prog ) ) {
		R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_SIMPLE_UPLOAD );
	}

	common->Printf( "%s", fullPath.c_str() );

	if ( RB_ShouldSkipFullInteractionUpload( prog ) ) {
		R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_SKIP_FULL_UPLOAD );
		common->Printf( ": skipped by renderer driver quirk; using SimpleInteraction.vfp\n" );
		RB_SetARBProgramFailure( prog, "skipped by renderer driver quirk; using SimpleInteraction.vfp" );
		return;
	}

	// load the program even if we don't support it, so
	// fs_copyfiles can generate cross-platform data dumps
	fileSystem->ReadFile( fullPath.c_str(), (void **)&fileBuffer, NULL );
	if ( !fileBuffer ) {
		common->Printf( ": File not found\n" );
		RB_SetARBProgramFailure( prog, "file not found" );
		return;
	}

	// copy to resizable memory and free; this buffer is edited while selecting the program section
	const int programLength = idStr::Length( fileBuffer );
	programBuffer.SetNum( programLength + 1 );
	memcpy( programBuffer.Ptr(), fileBuffer, programLength + 1 );
	buffer = programBuffer.Ptr();
	fileSystem->FreeFile( fileBuffer );

	if ( !glConfig.isInitialized ) {
		RB_SetARBProgramFailure( prog, "pending GL initialization" );
		return;
	}

	//
	// submit the program string at start to GL
	//
	if ( prog.ident == 0 ) {
		// allocate a new identifier for this program
		prog.ident = PROG_USER + progIndex;
		prog.requiredForLighting = RB_IsRequiredLightingARBProgram( prog );
	}

	// vertex and fragment programs can both be present in a single file, so
	// scan for the proper header to be the start point, and stamp a 0 in after the end

	if ( prog.target == GL_VERTEX_PROGRAM_ARB ) {
		if ( !glConfig.ARBVertexProgramAvailable ) {
			common->Printf( ": GL_VERTEX_PROGRAM_ARB not available\n" );
			RB_SetARBProgramFailure( prog, "GL_VERTEX_PROGRAM_ARB not available" );
			return;
		}
		start = strstr( (char *)buffer, "!!ARBvp" );
	}
	if ( prog.target == GL_FRAGMENT_PROGRAM_ARB ) {
		if ( !glConfig.ARBFragmentProgramAvailable ) {
			common->Printf( ": GL_FRAGMENT_PROGRAM_ARB not available\n" );
			RB_SetARBProgramFailure( prog, "GL_FRAGMENT_PROGRAM_ARB not available" );
			return;
		}
		start = strstr( (char *)buffer, "!!ARBfp" );
	}
	if ( !start ) {
		common->Printf( ": !!ARB not found\n" );
		RB_SetARBProgramFailure( prog, ( prog.target == GL_VERTEX_PROGRAM_ARB ) ? "missing !!ARBvp header" : "missing !!ARBfp header" );
		return;
	}
	end = strstr( start, "END" );

	if ( !end ) {
		common->Printf( ": END not found\n" );
		RB_SetARBProgramFailure( prog, "missing END terminator" );
		return;
	}
	end[3] = 0;

	if ( prog.ident == VPROG_INTERACTION ) {
		interactionColorMode_t detectedMode = ICM_PACKED;
		if ( !RB_DetectInteractionColorMode( start, detectedMode ) ) {
			common->Warning( "R_LoadARBProgram: failed to infer interaction color mode from %s, defaulting auto mode to %s",
				fullPath.c_str(), RB_InteractionColorModeName( ICM_PACKED ) );
			detectedMode = ICM_PACKED;
		}
		g_interactionVertexProgramAutoColorMode = detectedMode;
		R_SetRendererStartupPhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_COLOR_MODE );
		RB_UpdateInteractionColorMode( true );
	}

	glBindProgramARB( prog.target, prog.ident );
	glGetError();

	glProgramStringARB( prog.target, GL_PROGRAM_FORMAT_ASCII_ARB,
		idLib::SizeToInt( strlen( start ), "R_LoadARBProgram" ), (unsigned char *)start );

	err = glGetError();
	glGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, (GLint *)&ofs );
	if ( err == GL_INVALID_OPERATION ) {
		const GLubyte *str = glGetString( GL_PROGRAM_ERROR_STRING_ARB );
		idStr failure = "GL_PROGRAM_ERROR_STRING_ARB: ";
		failure += ( str != NULL ) ? (const char *)str : "unknown";
		common->Printf( "\nGL_PROGRAM_ERROR_STRING_ARB: %s\n", ( str != NULL ) ? (const char *)str : "unknown" );
		if ( ofs < 0 ) {
			common->Printf( "GL_PROGRAM_ERROR_POSITION_ARB < 0 with error\n" );
		} else if ( ofs >= (int)strlen( (char *)start ) ) {
			common->Printf( "error at end of program\n" );
		} else {
			common->Printf( "error at %i:\n%s", ofs, start + ofs );
		}
		RB_SetARBProgramFailure( prog, failure.c_str(), ofs );
		return;
	}
	if ( ofs != -1 ) {
		common->Printf( "\nGL_PROGRAM_ERROR_POSITION_ARB != -1 without error\n" );
		RB_SetARBProgramFailure( prog, "driver reported GL_PROGRAM_ERROR_POSITION_ARB without GL error", ofs );
		return;
	}

	prog.valid = true;
	common->Printf( "\n" );
}

/*
==================
R_FindARBProgram

Returns a GL identifier that can be bound to the given target, parsing
a text file if it hasn't already been loaded.
==================
*/
int R_FindARBProgram( GLenum target, const char *program ) {
	int		i;
	idStr	stripped = program;

	stripped.StripFileExtension();

	// see if it is already loaded. Bound the scan by MAX_GLPROGS as the sibling
	// RB_FindARBProgramRecord does: without it, a full table reads progs[MAX_GLPROGS]
	// out of bounds before the cap check below can fire.
	for ( i = 0 ; i < MAX_GLPROGS && progs[i].name[0] ; i++ ) {
		if ( progs[i].target != target ) {
			continue;
		}

		idStr	compare = progs[i].name;
		compare.StripFileExtension();

		if ( !idStr::Icmp( stripped.c_str(), compare.c_str() ) ) {
			return progs[i].ident;
		}
	}

	if ( i == MAX_GLPROGS ) {
		common->Error( "R_FindARBProgram: MAX_GLPROGS" );
	}

	// add it to the list and load it
	progs[i].ident = (program_t)0;	// will be gen'd by R_LoadARBProgram
	progs[i].target = target;
	idStr::Copynz( progs[i].name, program, sizeof( progs[i].name ) );
	RB_ResetARBProgramStatus( progs[i] );

	R_LoadARBProgram( i );

	return progs[i].ident;
}

/*
==================
R_ReloadARBPrograms_f
==================
*/
void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
	int		i;

	g_interactionShaderRescueWarned = false;
	R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_RELOAD_ARB_PROGRAMS );
	common->Printf( "----- R_ReloadARBPrograms -----\n" );
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
		R_LoadARBProgram( i );
	}
	RB_RecordCurrentInteractionSelectionBreadcrumb();
	RB_ErrorIfDriverRequiredSimpleInteractionFailed();
	common->Printf( "-------------------------------\n" );
	if ( r_shaderReport.GetInteger() >= 1 ) {
		R_ReportShaderPrograms_f( idCmdArgs() );
	}
}

static const char *RB_ShadowProgramStatusName( GLhandleARB programObject, bool programValid, int programGeneration ) {
	if ( programObject == 0 && programGeneration != tr.videoRestartCount ) {
		return "unloaded";
	}

	return programValid ? "valid" : "invalid";
}

void R_ReportShaderPrograms_f( const idCmdArgs &args ) {
	int validCount = 0;
	int invalidCount = 0;

	common->Printf( "----- R_ReportShaderPrograms -----\n" );
	for ( int i = 0; i < MAX_GLPROGS && progs[i].name[0]; i++ ) {
		const progDef_t &prog = progs[i];
		if ( prog.valid ) {
			validCount++;
		} else {
			invalidCount++;
		}

		common->Printf( "ARB %-8s id=%3u required=%s status=%s path=%s",
			RB_ARBProgramTargetName( prog.target ),
			prog.ident,
			prog.requiredForLighting ? "yes" : "no",
			prog.valid ? "valid" : "invalid",
			prog.normalizedPath[0] ? prog.normalizedPath : prog.name );
		if ( !prog.valid ) {
			common->Printf( " reason=%s", prog.failureReason[0] ? prog.failureReason : "unknown failure" );
			if ( prog.errorPosition >= 0 ) {
				common->Printf( " errorPosition=%d", prog.errorPosition );
			}
		}
		common->Printf( "\n" );
	}

	common->Printf( "ARB summary: valid=%d invalid=%d rescueMode=%s\n",
		validCount,
		invalidCount,
		RB_CurrentInteractionProgramsValid() ? "off" : "on" );
	common->Printf( "ARB interaction family: %s\n", RB_CurrentInteractionProgramFamilyName() );
	common->Printf( "GLSL shadow projected: %s\n",
		RB_ShadowProgramStatusName( g_shadowMapProgram.programObject, g_shadowMapProgram.programValid, g_shadowMapProgram.programGeneration ) );
	common->Printf( "GLSL material interaction: %s\n",
		RB_ShadowProgramStatusName( g_materialInteractionProgram.programObject, g_materialInteractionProgram.programValid, g_materialInteractionProgram.programGeneration ) );
	common->Printf( "GLSL shadow projected caster: %s\n",
		RB_ShadowProgramStatusName( g_shadowMapCasterProgram.programObject, g_shadowMapCasterProgram.programValid, g_shadowMapCasterProgram.programGeneration ) );
	common->Printf( "GLSL shadow projected translucent caster: %s\n",
		RB_ShadowProgramStatusName( g_translucentShadowCasterProgram.programObject, g_translucentShadowCasterProgram.programValid, g_translucentShadowCasterProgram.programGeneration ) );
	common->Printf( "GLSL shadow point: %s\n",
		RB_ShadowProgramStatusName( g_pointShadowMapProgram.programObject, g_pointShadowMapProgram.programValid, g_pointShadowMapProgram.programGeneration ) );
	common->Printf( "GLSL shadow point caster: %s\n",
		RB_ShadowProgramStatusName( g_pointShadowCasterProgram.programObject, g_pointShadowCasterProgram.programValid, g_pointShadowCasterProgram.programGeneration ) );
	common->Printf( "GLSL shadow point translucent caster: %s\n",
		RB_ShadowProgramStatusName( g_pointTranslucentShadowCasterProgram.programObject, g_pointTranslucentShadowCasterProgram.programValid, g_pointTranslucentShadowCasterProgram.programGeneration ) );
	common->Printf( "GLSL shadow debug overlay: %s\n",
		RB_ShadowProgramStatusName( g_shadowDebugOverlayProgram.programObject, g_shadowDebugOverlayProgram.programValid, g_shadowDebugOverlayProgram.programGeneration ) );
	common->Printf( "----------------------------------\n" );
}

/*
==================
R_ARB2_Init

==================
*/
void R_ARB2_Init( void ) {
	R_RecordRendererStartupPhase( RENDERER_STARTUP_PHASE_R_ARB2_INIT );
	g_appleGL21AutomaticInteractionPath = false;
	glConfig.allowARB2Path = false;
	glConfig.preferNV20Path = false;
	glConfig.preferSimpleLighting = false;
	glConfig.preferSimpleInteraction = false;
	glConfig.disableARB2Interactions = false;

	common->Printf( "---------- R_ARB2_Init ----------\n" );

	if ( !glConfig.backendCaps.hasFixedFunctionCompatibility ) {
		common->Printf( "Not available: OpenGL core profile does not expose the compatibility state required by the ARB2 bridge.\n" );
		return;
	}

	if ( !glConfig.ARBVertexProgramAvailable || !glConfig.ARBFragmentProgramAvailable ) {
		common->Printf( "Not available.\n" );
		return;
	}

	common->Printf( "Available.\n" );

	const idStr renderer = ( glConfig.renderer_string != NULL ) ? glConfig.renderer_string : "";
	const bool legacyGeForceFX =
		idStr::FindText( renderer.c_str(), "GeForce", false ) != -1 &&
		( idStr::FindText( renderer.c_str(), "5200", false ) != -1 ||
		  idStr::FindText( renderer.c_str(), "5600", false ) != -1 );
	const bool legacyRadeonSimpleLighting =
		idStr::FindText( renderer.c_str(), "RADEON", false ) != -1 &&
		( idStr::FindText( renderer.c_str(), "9700", false ) != -1 ||
		  idStr::FindText( renderer.c_str(), "9600", false ) != -1 );

	if ( legacyGeForceFX ) {
		if ( glConfig.allowNV20Path ) {
			glConfig.preferNV20Path = true;
			common->Printf( "%s: prefers NV20 compatibility path\n", renderer.c_str() );
		} else {
			common->Printf(
				"%s: retail would prefer the NV20 compatibility path, but openQ4 keeps ARB2 because the legacy NV20 backend is not shipped\n",
				renderer.c_str() );
		}
	}

	if ( legacyRadeonSimpleLighting ) {
		glConfig.preferSimpleLighting = true;
		common->Printf( "%s: prefers simple lighting\n", renderer.c_str() );
	}

	if ( ( RendererDriverQuirks_LastReport().flags & RENDERER_DRIVER_QUIRK_PREFER_SIMPLE_INTERACTION ) != 0 ) {
		glConfig.preferSimpleInteraction = true;
		common->Printf( "%s: prefers simple ARB interaction shader for compatibility\n", renderer.c_str() );
	}

	if ( ( RendererDriverQuirks_LastReport().flags & RENDERER_DRIVER_QUIRK_DISABLE_ARB2_INTERACTIONS ) != 0 ) {
		// Route stock surfaces around the Apple 2.1 ARB interaction crash while
		// retaining the validated simple ARB pair for per-surface fallback.
		// Modes 1/2 are diagnostics and mode 3 preserves the old blanket bypass
		// as an emergency recovery option for real-hardware testing.
		const int interactionOverride = r_appleARB2Interactions.GetInteger();
		if ( interactionOverride == 0 ) {
			glConfig.disableARB2Interactions = false;
			glConfig.preferSimpleInteraction = true;
			g_appleGL21AutomaticInteractionPath = true;
			common->Printf( "%s: r_appleARB2Interactions 0 uses automatic stock GLSL interactions with simple ARB per-surface fallback\n", renderer.c_str() );
		} else if ( interactionOverride == 1 ) {
			glConfig.disableARB2Interactions = false;
			glConfig.preferSimpleInteraction = true;
			common->Printf( "%s: r_appleARB2Interactions 1 forces the simple ARB interaction diagnostic path\n", renderer.c_str() );
		} else if ( interactionOverride == 2 ) {
			glConfig.disableARB2Interactions = false;
			// The PREFER_SIMPLE_INTERACTION quirk branch above set this; the
			// full path cannot be exercised while it remains preferred.
			glConfig.preferSimpleInteraction = false;
			common->Printf( "%s: r_appleARB2Interactions 2 forces the full ARB2 interaction diagnostic path\n", renderer.c_str() );
		} else {
			glConfig.disableARB2Interactions = true;
			common->Printf( "%s: r_appleARB2Interactions 3 activates the emergency ARB2 light-interaction bypass\n", renderer.c_str() );
		}
	}

	common->Printf( "---------------------------------\n" );

	glConfig.allowARB2Path = true;
}

void RB_ResetARB2InteractionHandoffBreadcrumb( void ) {
	g_firstARB2InteractionHandoffBreadcrumb = false;
	g_arb2InteractionDriverBypassWarned = false;
	g_arb2InteractionBypassStateBreadcrumb = false;
}
