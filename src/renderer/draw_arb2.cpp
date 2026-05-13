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

#include "cg_explicit.h"
#include <ctype.h>

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
	// Retail md5r*.vp programs read programmable stage vertex-color controls
	// from c[90]/c[91] and leave c[92] as the preserved reserved slot.
	ARB2_MD5R_STAGE_VERTEX_COLOR_MODULATE,
	ARB2_MD5R_STAGE_VERTEX_COLOR_ADD,
	ARB2_MD5R_STAGE_RESERVED,
	ARB2_MD5R_FOG_DISTANCE_PLANE,
	ARB2_MD5R_FOG_DISTANCE_BIAS,
	ARB2_MD5R_FOG_ENTER_PLANE_T,
	ARB2_MD5R_FOG_ENTER_PLANE_S
} arb2ProgramParameter_t;

static_assert(
	ARB2_MD5R_STAGE_VERTEX_COLOR_MODULATE == 90 &&
	ARB2_MD5R_STAGE_VERTEX_COLOR_ADD == 91 &&
	ARB2_MD5R_STAGE_RESERVED == 92 &&
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

static GLuint RB_CurrentInteractionProgramIdent( GLenum target );
static const char *RB_CurrentInteractionProgramFamilyName( void );
static void RB_WarnInteractionShaderRescueMode( void );

static const float RB_ARB2_MD5RIdentityTextureMatrixRows[2][4] = {
	{ 1.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 1.0f, 0.0f, 0.0f }
};
static const float RB_ARB2_MD5RVertexColorIgnore[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
static const float RB_ARB2_MD5RVertexColorModulate[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
static const float RB_ARB2_MD5RVertexColorInverseModulate[4] = { -1.0f, 1.0f, 0.0f, 0.0f };
static const int ARB2_MD5R_INTERACTION_PARAM_BASE = ARB2_MD5R_MVP_ROW_0;

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

	if ( vertexFormatIndex == 1 && vertexBuffer.binormals.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( vertexFormatIndex >= 1 && vertexBuffer.blendIndices.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	if ( vertexFormatIndex >= 2 && vertexBuffer.blendWeights.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
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

	glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
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

	glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
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

	glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
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
	glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
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
	glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
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

void RB_ARB2_LoadMD5RMVPMatrix( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL || backEnd.viewDef == NULL ) {
		return;
	}

	float modelViewProjection[16];
	myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, modelViewProjection );
	RB_ARB2_LoadVertexProgramMatrixRows( ARB2_MD5R_MVP_ROW_0, modelViewProjection, 4 );
}

void RB_ARB2_LoadMD5RProjectionMatrix( void ) {
	if ( backEnd.viewDef == NULL ) {
		return;
	}

	RB_ARB2_LoadVertexProgramMatrixRows( ARB2_MD5R_PROJECTION_ROW_0, backEnd.viewDef->projectionMatrix, 4 );
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
		glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
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
	glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
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
	glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
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

static void cg_error_callback( void ) {
	CGerror i = cgGetError();
	common->Printf( "Cg error (%d): %s\n", i, cgGetErrorString(i) );
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
	GLint			materialNormalScale;
	GLint			materialSpecularBoost;
	GLint			materialFresnel;
	GLint			shadowTexelSize;
	GLint			shadowBias;
	GLint			shadowNormalBias;
	GLint			shadowFilterRadius;
	GLint			shadowAtlasRect;
	GLint			shadowSplitDepths;
	GLint			shadowCascadeBiasScale;
	GLint			shadowCascadeCount;
	GLint			shadowCascadeBlend;
	GLint			shadowDebugMode;
	GLint			translucentShadowEnabled;
	GLint			translucentShadowDensity;

	GLint			bumpMap;
	GLint			lightFalloffMap;
	GLint			lightProjectionMap;
	GLint			diffuseMap;
	GLint			specularMap;
	GLint			shadowMap;
	GLint			translucentShadowMap[3];
} shadowMapProgram_t;

typedef struct {
	GLhandleARB		programObject;
	GLhandleARB		vertexShaderObject;
	GLhandleARB		fragmentShaderObject;
	int				programGeneration;
	bool			programValid;

	GLint			alphaTexCoordS;
	GLint			alphaTexCoordT;
	GLint			alphaRef;
	GLint			alphaScale;
	GLint			alphaHashEnabled;
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
	GLint			materialNormalScale;
	GLint			materialSpecularBoost;
	GLint			materialFresnel;
	GLint			shadowBias;
	GLint			shadowNormalBias;
	GLint			shadowFilterRadius;
	GLint			pointShadowTexelScale;
	GLint			translucentShadowEnabled;
	GLint			translucentShadowDensity;

	GLint			bumpMap;
	GLint			lightFalloffMap;
	GLint			lightProjectionMap;
	GLint			diffuseMap;
	GLint			specularMap;
	GLint			pointShadowMap;
	GLint			translucentShadowMap[3];
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
	GLint			materialNormalScale;
	GLint			materialSpecularBoost;
	GLint			materialFresnel;

	GLint			bumpMap;
	GLint			lightFalloffMap;
	GLint			lightProjectionMap;
	GLint			diffuseMap;
	GLint			specularMap;
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
	GLint			alphaScale;
	GLint			alphaTestEnabled;
	GLint			alphaHashEnabled;
	GLint			alphaMap;
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

	GLint			shadowAtlasMap;
	GLint			pointShadowMap;
} shadowDebugOverlayProgram_t;

typedef enum {
	TRANSLUCENT_SHADOW_STAGE_NONE = 0,
	TRANSLUCENT_SHADOW_STAGE_TEXTURE_ALPHA,
	TRANSLUCENT_SHADOW_STAGE_TEXTURE_ADDITIVE,
	TRANSLUCENT_SHADOW_STAGE_CUBEMAP_ADDITIVE
} translucentShadowStageMode_t;

static const int SHADOWMAP_MAX_CASCADES = 4;

typedef struct {
	bool			valid;
	int				cascadeCount;
	int				atlasDiv;
	int				tileSize;
	float			splitDepths[SHADOWMAP_MAX_CASCADES];
	float			biasScale[SHADOWMAP_MAX_CASCADES];
	float			depthRange[SHADOWMAP_MAX_CASCADES];
	float			clipZExtent[SHADOWMAP_MAX_CASCADES];
	idPlane			clipPlanes[SHADOWMAP_MAX_CASCADES][4];
	idVec4			atlasRect[SHADOWMAP_MAX_CASCADES];
} projectedShadowMapState_t;

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
static idImage *			g_translucentShadowMomentImages[3] = { NULL, NULL, NULL };
static idRenderTexture *	g_translucentShadowMapRenderTexture = NULL;
static idImage *			g_pointShadowMapColorImage = NULL;
static idImage *			g_pointShadowMapDepthImage = NULL;
static idRenderTexture *	g_pointShadowMapRenderTexture = NULL;
static idImage *			g_pointTranslucentShadowMomentImages[3] = { NULL, NULL, NULL };
static idRenderTexture *	g_pointTranslucentShadowMapRenderTexture = NULL;
static projectedShadowMapState_t	g_projectedShadowMapState = { 0 };
static bool				g_projectedTranslucentShadowPassReady = false;
static bool				g_pointTranslucentShadowPassReady = false;

typedef enum {
	SHADOWMAP_SUPPORT_OK = 0,
	SHADOWMAP_SUPPORT_DISABLED,
	SHADOWMAP_SUPPORT_SHADOWS_DISABLED,
	SHADOWMAP_SUPPORT_NULL_LIGHT,
	SHADOWMAP_SUPPORT_NO_SHADOWS_FLAG,
	SHADOWMAP_SUPPORT_NO_DYNAMIC_SHADOWS_FLAG,
	SHADOWMAP_SUPPORT_AMBIENT_LIGHT,
	SHADOWMAP_SUPPORT_LIGHT_SHADER_NO_SHADOWS,
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

typedef struct {
	int					totalLights;
	int					supportedLights;
	int					pointLights;
	int					projectedLights;
	int					parallelLights;
	int					mappedLocalPasses;
	int					mappedGlobalPasses;
	int					unshadowedLocalPasses;
	int					unshadowedGlobalPasses;
	int					fallbackLocalPasses;
	int					fallbackGlobalPasses;
	int					renderFailLocalPasses;
	int					renderFailGlobalPasses;
	int					maskFailLocalPasses;
	int					maskFailGlobalPasses;
	int					unsupportedLights[SHADOWMAP_SUPPORT_COUNT];
} shadowMapStats_t;

static shadowMapStats_t	g_shadowMapStats;
static int				g_shadowMapLastReportFrame = -0x3fffffff;
static bool				g_shadowMapReportThisFrame = false;

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
	int					shadowSurfCount;
	int					interactionCount;
} shadowMapDebugOverlayState_t;

static shadowMapDebugOverlayState_t	g_shadowMapDebugOverlayState;

typedef enum {
	SHADOWMAP_PASS_RESULT_MAPPED = 0,
	SHADOWMAP_PASS_RESULT_NO_SHADOW_SURFS,
	SHADOWMAP_PASS_RESULT_RENDER_FAIL,
	SHADOWMAP_PASS_RESULT_MASK_FAIL
} shadowMapPassResult_t;

void RB_ARB2_CreateDrawInteractions( const drawSurf_t *surf );

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
	case SHADOWMAP_PASS_RESULT_NO_SHADOW_SURFS:
		return "no-shadow-surfs";
	case SHADOWMAP_PASS_RESULT_RENDER_FAIL:
		return "render-fail";
	case SHADOWMAP_PASS_RESULT_MASK_FAIL:
		return "mask-fail";
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

static bool RB_ProjectedTranslucentShadowEnabled( void ) {
	return RB_TranslucentShadowMomentsSupported() && g_projectedTranslucentShadowPassReady && RB_TranslucentShadowMomentImagesReady( g_translucentShadowMomentImages );
}

static bool RB_PointTranslucentShadowEnabled( void ) {
	return RB_TranslucentShadowMomentsSupported() && g_pointTranslucentShadowPassReady && RB_TranslucentShadowMomentImagesReady( g_pointTranslucentShadowMomentImages );
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
	default:
		return "unknown";
	}
}

static int RB_CountDrawSurfChain( const drawSurf_t *surf ) {
	int count = 0;
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		count++;
	}
	return count;
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

/*
=============
RB_ShadowMapResetProjectedState
=============
*/
static void RB_ShadowMapResetProjectedState( void ) {
	memset( &g_projectedShadowMapState, 0, sizeof( g_projectedShadowMapState ) );
	g_projectedShadowMapState.cascadeCount = 1;
	g_projectedShadowMapState.atlasDiv = 1;
	g_projectedShadowMapState.atlasRect[0].Set( 0.0f, 0.0f, 1.0f, 1.0f );
	for ( int i = 0; i < SHADOWMAP_MAX_CASCADES; i++ ) {
		g_projectedShadowMapState.splitDepths[i] = 1.0e30f;
		g_projectedShadowMapState.biasScale[i] = 1.0f;
		g_projectedShadowMapState.depthRange[i] = 0.0f;
		g_projectedShadowMapState.clipZExtent[i] = 0.0f;
	}
}

static idVec4 RB_ShadowMapBuildAtlasRect( const int cascadeIndex, const int atlasDiv );

static void RB_ShadowMapInitializeProjectedState( const idPlane baseClipPlanes[4], const int cascadeCount, const int tileSize ) {
	g_projectedShadowMapState.valid = true;
	g_projectedShadowMapState.cascadeCount = cascadeCount;
	g_projectedShadowMapState.atlasDiv = cascadeCount > 1 ? 2 : 1;
	g_projectedShadowMapState.tileSize = tileSize;

	for ( int cascadeIndex = 0; cascadeIndex < SHADOWMAP_MAX_CASCADES; cascadeIndex++ ) {
		for ( int planeIndex = 0; planeIndex < 4; planeIndex++ ) {
			g_projectedShadowMapState.clipPlanes[cascadeIndex][planeIndex] = baseClipPlanes[planeIndex];
		}
		g_projectedShadowMapState.atlasRect[cascadeIndex] = RB_ShadowMapBuildAtlasRect( cascadeIndex, g_projectedShadowMapState.atlasDiv );
		g_projectedShadowMapState.splitDepths[cascadeIndex] = 1.0e30f;
	}
}

static bool RB_ShadowMapUseCSM( const viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->pointLight ) {
		return false;
	}
	return r_shadowMapCSM.GetBool() && idMath::ClampInt( 1, SHADOWMAP_MAX_CASCADES, r_shadowMapCascadeCount.GetInteger() ) > 1;
}

static int RB_ShadowMapCascadeCountForLight( const viewLight_t *vLight ) {
	if ( !RB_ShadowMapUseCSM( vLight ) ) {
		return 1;
	}
	return idMath::ClampInt( 1, SHADOWMAP_MAX_CASCADES, r_shadowMapCascadeCount.GetInteger() );
}

static int RB_ShadowMapAtlasDivForLight( const viewLight_t *vLight ) {
	return RB_ShadowMapCascadeCountForLight( vLight ) > 1 ? 2 : 1;
}

static int RB_ShadowMapTileSizeForLight( const viewLight_t *vLight ) {
	const int atlasDiv = RB_ShadowMapAtlasDivForLight( vLight );
	const int maxTileSize = glConfig.maxTextureSize / atlasDiv;
	if ( maxTileSize < 128 ) {
		return 0;
	}
	return idMath::ClampInt( 128, maxTileSize, r_shadowMapSize.GetInteger() );
}

static float RB_ShadowMapViewNear( const viewDef_t *viewDef ) {
	float zNear = r_znear.GetFloat();
	if ( viewDef != NULL && viewDef->renderView.cramZNear ) {
		zNear *= 0.25f;
	}
	return Max( zNear, 0.25f );
}

static float RB_ShadowMapCascadeDistanceForView( const viewDef_t *viewDef ) {
	const float zNear = RB_ShadowMapViewNear( viewDef );
	return Max( r_shadowMapCascadeDistance.GetFloat(), zNear + 32.0f );
}

static void RB_ShadowMapGetViewExtents( const viewDef_t *viewDef, float &zNear, float &xmin, float &xmax, float &ymin, float &ymax ) {
	const renderView_t &renderView = viewDef->renderView;

	zNear = RB_ShadowMapViewNear( viewDef );
	ymax = zNear * tan( renderView.fov_y * idMath::PI / 360.0f );
	ymin = -ymax;

	xmax = zNear * tan( renderView.fov_x * idMath::PI / 360.0f );
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

static const int SHADOWMAP_CASCADE_SAMPLE_POINT_COUNT = 23;

static void RB_ShadowMapBuildSliceCorners( const viewDef_t *viewDef, const float sliceNear, const float sliceFar, idVec3 corners[8] ) {
	const renderView_t &renderView = viewDef->renderView;
	const idVec3 &origin = renderView.vieworg;
	const idVec3 &forward = renderView.viewaxis[0];
	const idVec3 &left = renderView.viewaxis[1];
	const idVec3 &up = renderView.viewaxis[2];
	float zNear, xmin, xmax, ymin, ymax;

	RB_ShadowMapGetViewExtents( viewDef, zNear, xmin, xmax, ymin, ymax );

	const float depths[2] = { sliceNear, sliceFar };
	for ( int depthIndex = 0; depthIndex < 2; depthIndex++ ) {
		const float depth = depths[depthIndex];
		const idVec3 center = origin + forward * depth;
		const float depthScale = depth / Max( zNear, idMath::FLOAT_EPSILON );
		const float leftExtent = xmax * depthScale;
		const float rightExtent = xmin * depthScale;
		const float topExtent = ymax * depthScale;
		const float bottomExtent = ymin * depthScale;
		const int baseIndex = depthIndex * 4;

		corners[baseIndex + 0] = center + left * leftExtent + up * topExtent;
		corners[baseIndex + 1] = center + left * rightExtent + up * topExtent;
		corners[baseIndex + 2] = center + left * rightExtent + up * bottomExtent;
		corners[baseIndex + 3] = center + left * leftExtent + up * bottomExtent;
	}
}

static int RB_ShadowMapBuildSliceSamplePoints( const viewDef_t *viewDef, const float sliceNear, const float sliceFar, idVec3 samplePoints[SHADOWMAP_CASCADE_SAMPLE_POINT_COUNT] ) {
	idVec3 corners[8];
	RB_ShadowMapBuildSliceCorners( viewDef, sliceNear, sliceFar, corners );

	for ( int i = 0; i < 8; i++ ) {
		samplePoints[i] = corners[i];
	}

	const idVec3 nearCenter = ( corners[0] + corners[1] + corners[2] + corners[3] ) * 0.25f;
	const idVec3 farCenter = ( corners[4] + corners[5] + corners[6] + corners[7] ) * 0.25f;
	int sampleCount = 8;
	samplePoints[sampleCount++] = nearCenter;
	samplePoints[sampleCount++] = farCenter;
	samplePoints[sampleCount++] = ( nearCenter + farCenter ) * 0.5f;

	for ( int i = 0; i < 4; i++ ) {
		const int next = ( i + 1 ) & 3;
		samplePoints[sampleCount++] = ( corners[i] + corners[next] ) * 0.5f;
		samplePoints[sampleCount++] = ( corners[i + 4] + corners[next + 4] ) * 0.5f;
		samplePoints[sampleCount++] = ( corners[i] + corners[i + 4] ) * 0.5f;
	}

	return sampleCount;
}

static void RB_ShadowMapTransformPointToClip( const idVec3 &point, const idPlane clipPlanes[4], idVec4 &clip ) {
	for ( int i = 0; i < 4; i++ ) {
		clip[i] = point[0] * clipPlanes[i][0] + point[1] * clipPlanes[i][1] + point[2] * clipPlanes[i][2] + clipPlanes[i][3];
	}
}

static float RB_ShadowMapProjectedKernelGuardNDC( const int tileSize ) {
	const float texelStep = 2.0f / Max( 1, tileSize );
	const float kernelRadius = Max( 0.5f, r_shadowMapFilterRadius.GetFloat() + 0.75f );
	return texelStep * kernelRadius;
}

static bool RB_ShadowMapBuildCascadeBounds( const idPlane baseClipPlanes[4], const viewDef_t *viewDef, const float sliceNear, const float sliceFar, const int tileSize, int &validPointsOut, int &skippedPointsOut, bool &mixedWSignsOut, idVec3 &ndcMins, idVec3 &ndcMaxs ) {
	idVec3 samplePoints[SHADOWMAP_CASCADE_SAMPLE_POINT_COUNT];
	const int sampleCount = RB_ShadowMapBuildSliceSamplePoints( viewDef, sliceNear, sliceFar, samplePoints );

	validPointsOut = 0;
	skippedPointsOut = 0;
	mixedWSignsOut = false;
	ndcMins.Set( 1.0e30f, 1.0e30f, 1.0e30f );
	ndcMaxs.Set( -1.0e30f, -1.0e30f, -1.0e30f );
	int positiveWPoints = 0;
	int negativeWPoints = 0;

	for ( int i = 0; i < sampleCount; i++ ) {
		idVec4 clip;
		RB_ShadowMapTransformPointToClip( samplePoints[i], baseClipPlanes, clip );
		if ( clip.w == clip.w ) {
			if ( clip.w > 0.0f ) {
				positiveWPoints++;
			} else if ( clip.w < 0.0f ) {
				negativeWPoints++;
			}
		}
		if ( clip.w != clip.w || idMath::Fabs( clip.w ) <= 1.0e-5f ) {
			skippedPointsOut++;
			continue;
		}

		const float invW = 1.0f / clip.w;
		idVec3 ndc( clip.x * invW, clip.y * invW, clip.z * invW );
		if ( ndc.x != ndc.x || ndc.y != ndc.y || ndc.z != ndc.z ) {
			skippedPointsOut++;
			continue;
		}

		for ( int axis = 0; axis < 3; axis++ ) {
			ndcMins[axis] = Min( ndcMins[axis], ndc[axis] );
			ndcMaxs[axis] = Max( ndcMaxs[axis], ndc[axis] );
		}
		validPointsOut++;
	}

	mixedWSignsOut = ( positiveWPoints > 0 && negativeWPoints > 0 );

	if ( validPointsOut < 4 ) {
		return false;
	}

	const float pad = Max( 0.0f, r_shadowMapProjectionPad.GetFloat() * 0.5f );
	const float filterGuard = RB_ShadowMapProjectedKernelGuardNDC( tileSize );
	idVec3 center = ( ndcMins + ndcMaxs ) * 0.5f;
	idVec3 extent = ( ndcMaxs - ndcMins ) * 0.5f;

	extent.x = Max( extent.x * ( 1.0f + pad * 2.0f ) + filterGuard, 2.0f / Max( 1, tileSize ) );
	extent.y = Max( extent.y * ( 1.0f + pad * 2.0f ) + filterGuard, 2.0f / Max( 1, tileSize ) );
	extent.z = Max( extent.z * ( 1.0f + pad ), 0.02f );

	if ( r_shadowMapCascadeStabilize.GetBool() ) {
		const float texelStep = 2.0f / Max( 1, tileSize );
		center.x = floor( center.x / texelStep + 0.5f ) * texelStep;
		center.y = floor( center.y / texelStep + 0.5f ) * texelStep;
		extent.x = Max( texelStep, float( ceil( extent.x / texelStep ) ) * texelStep );
		extent.y = Max( texelStep, float( ceil( extent.y / texelStep ) ) * texelStep );
	}

	ndcMins = center - extent;
	ndcMaxs = center + extent;

	for ( int axis = 0; axis < 3; axis++ ) {
		ndcMins[axis] = idMath::ClampFloat( -1.0f, 1.0f, ndcMins[axis] );
		ndcMaxs[axis] = idMath::ClampFloat( -1.0f, 1.0f, ndcMaxs[axis] );
	}

	if ( ndcMaxs.x - ndcMins.x <= 1.0e-4f || ndcMaxs.y - ndcMins.y <= 1.0e-4f || ndcMaxs.z - ndcMins.z <= 1.0e-4f ) {
		return false;
	}

	return true;
}

static void RB_ShadowMapBuildCascadeClipPlanes( const idPlane baseClipPlanes[4], const idVec3 &ndcMins, const idVec3 &ndcMaxs, idPlane cascadeClipPlanes[4] ) {
	const float scaleX = 2.0f / ( ndcMaxs.x - ndcMins.x );
	const float scaleY = 2.0f / ( ndcMaxs.y - ndcMins.y );
	const float scaleZ = 2.0f / ( ndcMaxs.z - ndcMins.z );
	const float offsetX = -( ndcMaxs.x + ndcMins.x ) / ( ndcMaxs.x - ndcMins.x );
	const float offsetY = -( ndcMaxs.y + ndcMins.y ) / ( ndcMaxs.y - ndcMins.y );
	const float offsetZ = -( ndcMaxs.z + ndcMins.z ) / ( ndcMaxs.z - ndcMins.z );

	for ( int i = 0; i < 4; i++ ) {
		cascadeClipPlanes[0][i] = baseClipPlanes[0][i] * scaleX + baseClipPlanes[3][i] * offsetX;
		cascadeClipPlanes[1][i] = baseClipPlanes[1][i] * scaleY + baseClipPlanes[3][i] * offsetY;
		cascadeClipPlanes[2][i] = baseClipPlanes[2][i] * scaleZ + baseClipPlanes[3][i] * offsetZ;
		cascadeClipPlanes[3][i] = baseClipPlanes[3][i];
	}
}

static idVec4 RB_ShadowMapBuildAtlasRect( const int cascadeIndex, const int atlasDiv ) {
	if ( atlasDiv <= 1 ) {
		return idVec4( 0.0f, 0.0f, 1.0f, 1.0f );
	}

	const float invAtlasDiv = 1.0f / atlasDiv;
	const int tileX = cascadeIndex % atlasDiv;
	const int tileY = cascadeIndex / atlasDiv;
	return idVec4(
		tileX * invAtlasDiv,
		tileY * invAtlasDiv,
		( tileX + 1 ) * invAtlasDiv,
		( tileY + 1 ) * invAtlasDiv );
}

/*
=============
RB_ShadowMapCascadeBiasScale

Derives a receiver-bias multiplier from the fitted clip-space footprint and depth extent of a cascade.
=============
*/
static float RB_ShadowMapCascadeBiasScale( const idVec3 &ndcMins, const idVec3 &ndcMaxs ) {
	const float xyExtent = Max( ndcMaxs.x - ndcMins.x, ndcMaxs.y - ndcMins.y );
	const float zExtent = ndcMaxs.z - ndcMins.z;
	const float footprintScale = xyExtent * 0.5f;
	const float depthScale = zExtent * 0.5f;
	const float combinedScale = Max( footprintScale, depthScale );
	return idMath::ClampFloat( 0.35f, 3.0f, combinedScale );
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
	common->Printf( "SM csm '%s' zNear=%.2f max=%.2f cascades=%d debug=%s splits",
		shaderName, zNear, maxDistance, cascadeCount, RB_ShadowMapDebugModeName( RB_ShadowMapDebugMode() ) );
	for ( int splitIndex = 0; splitIndex < cascadeCount - 1; splitIndex++ ) {
		common->Printf( " [%d]=%.2f", splitIndex, g_projectedShadowMapState.splitDepths[splitIndex] );
	}
	common->Printf( "\n" );
}

static void RB_ShadowMapReportProjectedCascadeFit( const viewLight_t *vLight, const int cascadeIndex, const int cascadeCount, const float sliceNear, const float sliceFar, const int validPoints, const int skippedPoints ) {
	if ( skippedPoints <= 0 || idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) < 2 || !g_shadowMapReportThisFrame || vLight == NULL ) {
		return;
	}

	const char *shaderName = vLight->lightShader != NULL ? vLight->lightShader->GetName() : "<null>";
	common->Printf(
		"SM csm fit '%s' cascade=%d/%d range=[%.2f %.2f] validSamples=%d skippedW=%d\n",
		shaderName,
		cascadeIndex,
		cascadeCount,
		sliceNear,
		sliceFar,
		validPoints,
		skippedPoints );
}

static void RB_ShadowMapReportProjectedCascadeFallback( const viewLight_t *vLight, const int requestedCascadeCount, const int cascadeIndex, const float sliceNear, const float sliceFar, const int validPoints, const int skippedPoints, const bool mixedWSigns ) {
	if ( idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) < 2 || !g_shadowMapReportThisFrame || vLight == NULL ) {
		return;
	}

	const char *shaderName = vLight->lightShader != NULL ? vLight->lightShader->GetName() : "<null>";
	common->Printf(
		"SM csm fallback '%s' requested=%d failedCascade=%d range=[%.2f %.2f] validSamples=%d skippedW=%d mixedW=%d -> single-cascade\n",
		shaderName,
		requestedCascadeCount,
		cascadeIndex,
		sliceNear,
		sliceFar,
		validPoints,
		skippedPoints,
		mixedWSigns ? 1 : 0 );
}

/*
=============
RB_ShadowMapBuildProjectedState

Builds projected-light shadow-map state, including interior cascade split planes and per-cascade clip volumes.
=============
*/
static void RB_ShadowMapBuildProjectedState( const viewLight_t *vLight, const idPlane baseClipPlanes[4], const int tileSize ) {
	RB_ShadowMapResetProjectedState();

	const int requestedCascadeCount = RB_ShadowMapCascadeCountForLight( vLight );
	RB_ShadowMapInitializeProjectedState( baseClipPlanes, requestedCascadeCount, tileSize );

	if ( requestedCascadeCount <= 1 || backEnd.viewDef == NULL ) {
		return;
	}

	const viewDef_t *viewDef = backEnd.viewDef;
	const float zNear = RB_ShadowMapViewNear( viewDef );
	const float maxDistance = RB_ShadowMapCascadeDistanceForView( viewDef );
	const float lambda = idMath::ClampFloat( 0.0f, 1.0f, r_shadowMapCascadeLambda.GetFloat() );
	const float range = maxDistance - zNear;
	const float ratio = maxDistance / zNear;

	for ( int splitIndex = 0; splitIndex < requestedCascadeCount - 1; splitIndex++ ) {
		const float p = float( splitIndex + 1 ) / float( requestedCascadeCount );
		const float uniformSplit = zNear + range * p;
		const float logSplit = zNear * pow( ratio, p );
		g_projectedShadowMapState.splitDepths[splitIndex] = uniformSplit + ( logSplit - uniformSplit ) * lambda;
	}

	float sliceNear = zNear;
	for ( int cascadeIndex = 0; cascadeIndex < requestedCascadeCount; cascadeIndex++ ) {
		const bool finalCascade = cascadeIndex == requestedCascadeCount - 1;
		const float targetFar = finalCascade ? maxDistance : g_projectedShadowMapState.splitDepths[cascadeIndex];
		const float sliceFar = Max( targetFar, sliceNear + 1.0f );
		idVec3 ndcMins;
		idVec3 ndcMaxs;
		int validPoints = 0;
		int skippedPoints = 0;
		bool mixedWSigns = false;
		if ( RB_ShadowMapBuildCascadeBounds( baseClipPlanes, viewDef, sliceNear, sliceFar, tileSize, validPoints, skippedPoints, mixedWSigns, ndcMins, ndcMaxs ) ) {
			if ( mixedWSigns ) {
				RB_ShadowMapReportProjectedCascadeFallback( vLight, requestedCascadeCount, cascadeIndex, sliceNear, sliceFar, validPoints, skippedPoints, mixedWSigns );
				RB_ShadowMapResetProjectedState();
				RB_ShadowMapInitializeProjectedState( baseClipPlanes, 1, tileSize );
				g_projectedShadowMapState.depthRange[0] = Max( maxDistance - zNear, 0.0f );
				g_projectedShadowMapState.clipZExtent[0] = 2.0f;
				return;
			}
			RB_ShadowMapBuildCascadeClipPlanes( baseClipPlanes, ndcMins, ndcMaxs, g_projectedShadowMapState.clipPlanes[cascadeIndex] );
			g_projectedShadowMapState.biasScale[cascadeIndex] = RB_ShadowMapCascadeBiasScale( ndcMins, ndcMaxs );
			g_projectedShadowMapState.clipZExtent[cascadeIndex] = ndcMaxs.z - ndcMins.z;
			RB_ShadowMapReportProjectedCascadeFit( vLight, cascadeIndex, requestedCascadeCount, sliceNear, sliceFar, validPoints, skippedPoints );
		} else {
			RB_ShadowMapReportProjectedCascadeFallback( vLight, requestedCascadeCount, cascadeIndex, sliceNear, sliceFar, validPoints, skippedPoints, mixedWSigns );
			RB_ShadowMapResetProjectedState();
			RB_ShadowMapInitializeProjectedState( baseClipPlanes, 1, tileSize );
			g_projectedShadowMapState.depthRange[0] = Max( maxDistance - zNear, 0.0f );
			g_projectedShadowMapState.clipZExtent[0] = 2.0f;
			return;
		}
		g_projectedShadowMapState.depthRange[cascadeIndex] = sliceFar - sliceNear;
		sliceNear = sliceFar;
	}

	RB_ShadowMapReportProjectedSplits( vLight, zNear, maxDistance, requestedCascadeCount );
	if ( idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) >= 2 && g_shadowMapReportThisFrame && vLight != NULL ) {
		const char *shaderName = vLight->lightShader != NULL ? vLight->lightShader->GetName() : "<null>";
		common->Printf( "SM csm bias '%s'", shaderName );
		for ( int cascadeIndex = 0; cascadeIndex < requestedCascadeCount; cascadeIndex++ ) {
			common->Printf( " [%d]=%.3f depth=%.2f clipZ=%.3f", cascadeIndex,
				g_projectedShadowMapState.biasScale[cascadeIndex],
				g_projectedShadowMapState.depthRange[cascadeIndex],
				g_projectedShadowMapState.clipZExtent[cascadeIndex] );
		}
		common->Printf( "\n" );
	}
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

	memset( &g_materialInteractionProgram, 0, sizeof( g_materialInteractionProgram ) );
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

static void RB_ShadowMapPrintInfoLog( GLhandleARB object, const char *label, const char *name ) {
	GLint logLength = 0;
	glGetObjectParameterivARB( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &logLength );
	if ( logLength <= 1 ) {
		common->Warning( "GLSL %s error in '%s' (no info log)", label, name );
		return;
	}

	char *logBuffer = (char *)_alloca( logLength );
	GLsizei written = 0;
	glGetInfoLogARB( object, logLength, &written, logBuffer );
	common->Warning( "GLSL %s error in '%s':\n%s", label, name, logBuffer );
}

static bool RB_MaterialInteractionLoadProgram( void ) {
	static const char *programBaseName = "glprogs/material_interaction";

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	if ( g_materialInteractionProgram.programObject != 0 && g_materialInteractionProgram.programGeneration == tr.videoRestartCount ) {
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
	g_materialInteractionProgram.materialNormalScale = glGetUniformLocationARB( programObject, "uMaterialNormalScale" );
	g_materialInteractionProgram.materialSpecularBoost = glGetUniformLocationARB( programObject, "uMaterialSpecularBoost" );
	g_materialInteractionProgram.materialFresnel = glGetUniformLocationARB( programObject, "uMaterialFresnel" );
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

	if ( g_shadowMapProgram.programObject != 0 && g_shadowMapProgram.programGeneration == tr.videoRestartCount ) {
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
	g_shadowMapProgram.materialNormalScale = glGetUniformLocationARB( programObject, "uMaterialNormalScale" );
	g_shadowMapProgram.materialSpecularBoost = glGetUniformLocationARB( programObject, "uMaterialSpecularBoost" );
	g_shadowMapProgram.materialFresnel = glGetUniformLocationARB( programObject, "uMaterialFresnel" );
	g_shadowMapProgram.shadowTexelSize = glGetUniformLocationARB( programObject, "uShadowTexelSize" );
	g_shadowMapProgram.shadowBias = glGetUniformLocationARB( programObject, "uShadowBias" );
	g_shadowMapProgram.shadowNormalBias = glGetUniformLocationARB( programObject, "uShadowNormalBias" );
	g_shadowMapProgram.shadowFilterRadius = glGetUniformLocationARB( programObject, "uShadowFilterRadius" );
	g_shadowMapProgram.shadowAtlasRect = glGetUniformLocationARB( programObject, "uShadowAtlasRect[0]" );
	g_shadowMapProgram.shadowSplitDepths = glGetUniformLocationARB( programObject, "uShadowSplitDepths[0]" );
	g_shadowMapProgram.shadowCascadeBiasScale = glGetUniformLocationARB( programObject, "uShadowCascadeBiasScale[0]" );
	g_shadowMapProgram.shadowCascadeCount = glGetUniformLocationARB( programObject, "uShadowCascadeCount" );
	g_shadowMapProgram.shadowCascadeBlend = glGetUniformLocationARB( programObject, "uShadowCascadeBlend" );
	g_shadowMapProgram.shadowDebugMode = glGetUniformLocationARB( programObject, "uShadowDebugMode" );
	g_shadowMapProgram.translucentShadowEnabled = glGetUniformLocationARB( programObject, "uTranslucentShadowEnabled" );
	g_shadowMapProgram.translucentShadowDensity = glGetUniformLocationARB( programObject, "uTranslucentShadowDensity" );
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
	g_shadowMapCasterProgram.alphaRef = glGetUniformLocationARB( programObject, "uAlphaRef" );
	g_shadowMapCasterProgram.alphaScale = glGetUniformLocationARB( programObject, "uAlphaScale" );
	g_shadowMapCasterProgram.alphaHashEnabled = glGetUniformLocationARB( programObject, "uAlphaHashEnabled" );
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

	if ( g_pointShadowMapProgram.programObject != 0 && g_pointShadowMapProgram.programGeneration == tr.videoRestartCount ) {
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
		return false;
	}

	g_pointShadowMapProgram.programObject = programObject;
	g_pointShadowMapProgram.vertexShaderObject = vertexShader;
	g_pointShadowMapProgram.fragmentShaderObject = fragmentShader;
	g_pointShadowMapProgram.programGeneration = tr.videoRestartCount;
	g_pointShadowMapProgram.programValid = true;

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
	g_pointShadowMapProgram.materialNormalScale = glGetUniformLocationARB( programObject, "uMaterialNormalScale" );
	g_pointShadowMapProgram.materialSpecularBoost = glGetUniformLocationARB( programObject, "uMaterialSpecularBoost" );
	g_pointShadowMapProgram.materialFresnel = glGetUniformLocationARB( programObject, "uMaterialFresnel" );
	g_pointShadowMapProgram.shadowBias = glGetUniformLocationARB( programObject, "uShadowBias" );
	g_pointShadowMapProgram.shadowNormalBias = glGetUniformLocationARB( programObject, "uShadowNormalBias" );
	g_pointShadowMapProgram.shadowFilterRadius = glGetUniformLocationARB( programObject, "uShadowFilterRadius" );
	g_pointShadowMapProgram.pointShadowTexelScale = glGetUniformLocationARB( programObject, "uPointShadowTexelScale" );
	g_pointShadowMapProgram.translucentShadowEnabled = glGetUniformLocationARB( programObject, "uTranslucentShadowEnabled" );
	g_pointShadowMapProgram.translucentShadowDensity = glGetUniformLocationARB( programObject, "uTranslucentShadowDensity" );
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

	if ( g_pointShadowCasterProgram.programObject != 0 && g_pointShadowCasterProgram.programGeneration == tr.videoRestartCount ) {
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
	g_pointShadowCasterProgram.alphaScale = glGetUniformLocationARB( programObject, "uAlphaScale" );
	g_pointShadowCasterProgram.alphaTestEnabled = glGetUniformLocationARB( programObject, "uAlphaTestEnabled" );
	g_pointShadowCasterProgram.alphaHashEnabled = glGetUniformLocationARB( programObject, "uAlphaHashEnabled" );
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
	g_shadowDebugOverlayProgram.shadowAtlasMap = glGetUniformLocationARB( programObject, "uShadowAtlasMap" );
	g_shadowDebugOverlayProgram.pointShadowMap = glGetUniformLocationARB( programObject, "uPointShadowMap" );

	common->Printf( "Loaded GLSL program '%s'\n", programBaseName );
	return true;
}

static void RB_ShadowMapBuildClipPlanes( const idPlane lightProject[4], idPlane clipPlanes[4] ) {
	const float projectionPad = Max( 0.0f, r_shadowMapProjectionPad.GetFloat() );
	const float projectionScale = 1.0f / ( 1.0f + projectionPad * 2.0f );

	for ( int i = 0; i < 4; i++ ) {
		clipPlanes[0][i] = ( lightProject[0][i] * 2.0f - lightProject[2][i] ) * projectionScale;
		clipPlanes[1][i] = ( lightProject[1][i] * 2.0f - lightProject[2][i] ) * projectionScale;
		clipPlanes[2][i] = lightProject[3][i] * 2.0f - lightProject[2][i];
		clipPlanes[3][i] = lightProject[2][i];
	}
}

static void RB_ShadowMapClipPlanesToGLMatrix( const idPlane clipPlanes[4], float matrix[16] ) {
	matrix[0] = clipPlanes[0][0];
	matrix[4] = clipPlanes[0][1];
	matrix[8] = clipPlanes[0][2];
	matrix[12] = clipPlanes[0][3];

	matrix[1] = clipPlanes[1][0];
	matrix[5] = clipPlanes[1][1];
	matrix[9] = clipPlanes[1][2];
	matrix[13] = clipPlanes[1][3];

	matrix[2] = clipPlanes[2][0];
	matrix[6] = clipPlanes[2][1];
	matrix[10] = clipPlanes[2][2];
	matrix[14] = clipPlanes[2][3];

	matrix[3] = clipPlanes[3][0];
	matrix[7] = clipPlanes[3][1];
	matrix[11] = clipPlanes[3][2];
	matrix[15] = clipPlanes[3][3];
}

static bool RB_ShadowMapEnsureResources( const viewLight_t *vLight ) {
	if ( !RB_ShadowMapLoadProgram() ) {
		return false;
	}

	const int shadowMapTileSize = RB_ShadowMapTileSizeForLight( vLight );
	if ( shadowMapTileSize <= 0 ) {
		return false;
	}

	const int shadowMapAtlasDiv = RB_ShadowMapAtlasDivForLight( vLight );
	const int shadowMapSize = shadowMapTileSize * shadowMapAtlasDiv;

	if ( g_shadowMapDepthImage == NULL ) {
		idImageOpts opts;
		opts.textureType = TT_2D;
		opts.format = FMT_DEPTH;
		opts.width = shadowMapSize;
		opts.height = shadowMapSize;
		opts.numLevels = 1;
		opts.isPersistant = true;
		g_shadowMapDepthImage = globalImages->ScratchImage( "_shadowMapDepth", &opts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
	}

	if ( g_shadowMapRenderTexture == NULL ) {
		g_shadowMapRenderTexture = tr.CreateRenderTexture( NULL, g_shadowMapDepthImage );
	} else if ( g_shadowMapRenderTexture->GetWidth() != shadowMapSize || g_shadowMapRenderTexture->GetHeight() != shadowMapSize ) {
		tr.ResizeRenderTexture( g_shadowMapRenderTexture, shadowMapSize, shadowMapSize );
	}

	if ( g_shadowMapDepthImage == NULL || g_shadowMapRenderTexture == NULL ) {
		return false;
	}

	GL_SelectTextureNoClient( 5 );
	g_shadowMapDepthImage->Bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
	for ( int i = 0; i < 3; i++ ) {
		if ( glConfig.maxTextureUnits >= 7 + i ) {
			GL_SelectTextureNoClient( 6 + i );
			if ( RB_ProjectedTranslucentShadowEnabled() ) {
				g_translucentShadowMomentImages[i]->Bind();
			} else {
				globalImages->BindNull();
			}
		}
	}
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL );
	backEnd.glState.currenttmu = -1;

	return true;
}

static bool RB_PointShadowMapEnsureResources( void ) {
	if ( !glConfig.cubeMapAvailable ) {
		return false;
	}
	if ( !RB_PointShadowMapLoadProgram() || !RB_PointShadowMapLoadCasterProgram() ) {
		return false;
	}

	const int shadowMapSize = idMath::ClampInt( 128, 2048, r_shadowMapSize.GetInteger() );

	if ( g_pointShadowMapColorImage == NULL ) {
		idImageOpts opts;
		opts.textureType = TT_CUBIC;
		opts.format = FMT_RGBA8;
		opts.width = shadowMapSize;
		opts.height = shadowMapSize;
		opts.numLevels = 1;
		opts.isPersistant = true;
		g_pointShadowMapColorImage = globalImages->ScratchImage( "_pointShadowMapColor", &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	}

	if ( g_pointShadowMapDepthImage == NULL ) {
		idImageOpts opts;
		opts.textureType = TT_CUBIC;
		opts.format = FMT_DEPTH;
		opts.width = shadowMapSize;
		opts.height = shadowMapSize;
		opts.numLevels = 1;
		opts.isPersistant = true;
		g_pointShadowMapDepthImage = globalImages->ScratchImage( "_pointShadowMapDepth", &opts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
	}

	if ( g_pointShadowMapRenderTexture == NULL ) {
		g_pointShadowMapRenderTexture = tr.CreateRenderTexture( g_pointShadowMapColorImage, g_pointShadowMapDepthImage );
	} else if ( g_pointShadowMapRenderTexture->GetWidth() != shadowMapSize || g_pointShadowMapRenderTexture->GetHeight() != shadowMapSize ) {
		tr.ResizeRenderTexture( g_pointShadowMapRenderTexture, shadowMapSize, shadowMapSize );
	}

	return g_pointShadowMapColorImage != NULL && g_pointShadowMapDepthImage != NULL && g_pointShadowMapRenderTexture != NULL;
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
	} else if ( g_translucentShadowMapRenderTexture->GetWidth() != shadowMapSize || g_translucentShadowMapRenderTexture->GetHeight() != shadowMapSize ) {
		tr.ResizeRenderTexture( g_translucentShadowMapRenderTexture, shadowMapSize, shadowMapSize );
	}

	return RB_TranslucentShadowMomentImagesReady( g_translucentShadowMomentImages ) && g_translucentShadowMapRenderTexture != NULL;
}

static bool RB_PointShadowMapEnsureTranslucentResources( void ) {
	if ( !glConfig.cubeMapAvailable || !RB_TranslucentShadowMomentsSupported() || !RB_PointTranslucentShadowMapLoadCasterProgram() ) {
		return false;
	}

	const int shadowMapSize = idMath::ClampInt( 128, 2048, r_shadowMapSize.GetInteger() );

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
	} else if ( g_pointTranslucentShadowMapRenderTexture->GetWidth() != shadowMapSize || g_pointTranslucentShadowMapRenderTexture->GetHeight() != shadowMapSize ) {
		tr.ResizeRenderTexture( g_pointTranslucentShadowMapRenderTexture, shadowMapSize, shadowMapSize );
	}

	return RB_TranslucentShadowMomentImagesReady( g_pointTranslucentShadowMomentImages ) && g_pointTranslucentShadowMapRenderTexture != NULL;
}

// Keep shadow-map support reporting aligned with the interaction shadow admission rules.
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
	if ( vLight->globalInteractions == NULL && vLight->localInteractions == NULL ) {
		return SHADOWMAP_SUPPORT_NO_INTERACTIONS;
	}
	if ( vLight->pointLight ) {
		if ( !glConfig.cubeMapAvailable ) {
			return SHADOWMAP_SUPPORT_CUBEMAP_UNAVAILABLE;
		}
		return RB_PointShadowMapEnsureResources() ? SHADOWMAP_SUPPORT_OK : SHADOWMAP_SUPPORT_RESOURCE_FAILURE;
	}
	return RB_ShadowMapEnsureResources( vLight ) ? SHADOWMAP_SUPPORT_OK : SHADOWMAP_SUPPORT_RESOURCE_FAILURE;
}

static void RB_ShadowMapStatsReset( void ) {
	memset( &g_shadowMapStats, 0, sizeof( g_shadowMapStats ) );
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
}

static void RB_ShadowMapStatsReport( void ) {
	const int reportLevel = idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() );
	if ( reportLevel <= 0 || !g_shadowMapReportThisFrame || g_shadowMapStats.totalLights <= 0 ) {
		return;
	}

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
			"SM summary: lights=%d supported=%d point=%d projected=%d parallel=%d mapped(local=%d global=%d) unshadowed(local=%d global=%d) fallback(local=%d global=%d) debug=%s unsupported[%s]\n",
			g_shadowMapStats.totalLights,
			g_shadowMapStats.supportedLights,
			g_shadowMapStats.pointLights,
			g_shadowMapStats.projectedLights,
			g_shadowMapStats.parallelLights,
			g_shadowMapStats.mappedLocalPasses,
			g_shadowMapStats.mappedGlobalPasses,
			g_shadowMapStats.unshadowedLocalPasses,
			g_shadowMapStats.unshadowedGlobalPasses,
			g_shadowMapStats.fallbackLocalPasses,
			g_shadowMapStats.fallbackGlobalPasses,
			RB_ShadowMapDebugModeName( RB_ShadowMapDebugMode() ),
			unsupportedSummary.c_str() );
		common->Printf(
			"SM detail: failures(render local=%d global=%d, mask local=%d global=%d)\n",
			g_shadowMapStats.renderFailLocalPasses,
			g_shadowMapStats.renderFailGlobalPasses,
			g_shadowMapStats.maskFailLocalPasses,
			g_shadowMapStats.maskFailGlobalPasses );
	} else {
		common->Printf(
			"SM summary: lights=%d supported=%d point=%d projected=%d parallel=%d mapped(local=%d global=%d) unshadowed(local=%d global=%d) fallback(local=%d global=%d) debug=%s\n",
			g_shadowMapStats.totalLights,
			g_shadowMapStats.supportedLights,
			g_shadowMapStats.pointLights,
			g_shadowMapStats.projectedLights,
			g_shadowMapStats.parallelLights,
			g_shadowMapStats.mappedLocalPasses,
			g_shadowMapStats.mappedGlobalPasses,
			g_shadowMapStats.unshadowedLocalPasses,
			g_shadowMapStats.unshadowedGlobalPasses,
			g_shadowMapStats.fallbackLocalPasses,
			g_shadowMapStats.fallbackGlobalPasses,
			RB_ShadowMapDebugModeName( RB_ShadowMapDebugMode() ) );
		common->Printf(
			"SM detail: failures(render local=%d global=%d, mask local=%d global=%d)\n",
			g_shadowMapStats.renderFailLocalPasses,
			g_shadowMapStats.renderFailGlobalPasses,
			g_shadowMapStats.maskFailLocalPasses,
			g_shadowMapStats.maskFailGlobalPasses );
	}

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
	common->Printf(
		"SM light[%d] '%s' origin=(%.1f %.1f %.1f) type=%s%s interactions(local=%d global=%d) shadows(local=%d global=%d) debug=%s support=%s\n",
		( vLight != NULL && vLight->lightDef != NULL ) ? vLight->lightDef->index : -1,
		shaderName,
		origin[0], origin[1], origin[2],
		( vLight != NULL && vLight->pointLight ) ? "point" : "projected",
		( vLight != NULL && vLight->parallel ) ? "/parallel" : "",
		RB_CountDrawSurfChain( vLight != NULL ? vLight->localInteractions : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->globalInteractions : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->localShadows : NULL ),
		RB_CountDrawSurfChain( vLight != NULL ? vLight->globalShadows : NULL ),
		RB_ShadowMapDebugModeName( RB_ShadowMapDebugMode() ),
		RB_ShadowMapSupportReasonName( reason ) );
}

static void RB_ShadowMapPassReport( const viewLight_t *vLight, shadowMapPassKind_t passKind, bool pointLight, shadowMapPassResult_t result, const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *tertiaryCasters, const drawSurf_t *quaternaryCasters, const drawSurf_t *primaryShadowSurfs, const drawSurf_t *secondaryShadowSurfs, const drawSurf_t *interactions ) {
	if ( idMath::ClampInt( 0, 2, r_shadowMapReport.GetInteger() ) < 2 || !g_shadowMapReportThisFrame ) {
		return;
	}

	const char *shaderName = ( vLight != NULL && vLight->lightShader != NULL ) ? vLight->lightShader->GetName() : "<null>";
	common->Printf(
		"SM pass %s[%d] '%s' type=%s debug=%s result=%s casters(a=%d b=%d c=%d d=%d) shadowSurfs(primary=%d secondary=%d) receivers=%d\n",
		RB_ShadowMapPassName( passKind ),
		( vLight != NULL && vLight->lightDef != NULL ) ? vLight->lightDef->index : -1,
		shaderName,
		pointLight ? "point" : "projected",
		RB_ShadowMapDebugModeName( RB_ShadowMapDebugMode() ),
		RB_ShadowMapPassResultName( result ),
		RB_CountDrawSurfChain( primaryCasters ),
		RB_CountDrawSurfChain( secondaryCasters ),
		RB_CountDrawSurfChain( tertiaryCasters ),
		RB_CountDrawSurfChain( quaternaryCasters ),
		RB_CountDrawSurfChain( primaryShadowSurfs ),
		RB_CountDrawSurfChain( secondaryShadowSurfs ),
		RB_CountDrawSurfChain( interactions ) );
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

static void RB_ShadowMapRestorePolygonOffset( void ) {
	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( r_shadowMapPolygonFactor.GetFloat(), r_shadowMapPolygonOffset.GetFloat() );
}

static void RB_ShadowMapModelMatrixRows( const float modelMatrix[16], float row0[4], float row1[4], float row2[4] );

static bool RB_ShadowMapTextureStageHasBindableImage( const textureStage_t *texture ) {
	return texture != NULL && ( texture->image != NULL || texture->cinematic != NULL );
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

static void RB_ShadowMapDrawPerforatedCasterClassic( const drawSurf_t *surf, const srfTriangles_t *casterGeo, idDrawVert *ac ) {
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	bool didDraw = false;

	if ( shader == NULL || regs == NULL ) {
		RB_DrawElementsWithCounters( casterGeo );
		return;
	}

	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), reinterpret_cast<void *>( &ac->st ) );
	glEnable( GL_ALPHA_TEST );

	for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );

		if ( !pStage->hasAlphaTest ) {
			continue;
		}

		if ( regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}

		didDraw = true;
		color[3] = regs[ pStage->color.registers[3] ];
		if ( color[3] <= 0.0f ) {
			continue;
		}

		glColor4fv( color );
		glAlphaFunc( pStage->alphaTestMode, regs[ pStage->alphaTestRegister ] );

		if ( !RB_ShadowMapTextureStageHasBindableImage( &pStage->texture ) ) {
			continue;
		}
		RB_BindVariableStageImage( &pStage->texture, regs );

		if ( !RB_PrepareStageTexturing( pStage, surf, ac ) ) {
			RB_FinishStageTexturing( pStage, surf, ac );
			RB_ShadowMapRestorePolygonOffset();
			continue;
		}

		RB_DrawElementsWithCounters( casterGeo );
		RB_FinishStageTexturing( pStage, surf, ac );
		RB_ShadowMapRestorePolygonOffset();
	}

	glDisable( GL_ALPHA_TEST );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 0 );

	if ( !didDraw ) {
		RB_DrawElementsWithCounters( casterGeo );
	}
}

static bool RB_ShadowMapCanHashPerforatedCaster( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return false;
	}

	bool haveActiveStage = false;
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
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

	for ( int stage = 0; stage < shader->GetNumStages(); ++stage ) {
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

static void RB_ShadowMapDrawPerforatedCasterHashed( const drawSurf_t *surf, const srfTriangles_t *casterGeo, idDrawVert *ac ) {
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	bool didDraw = false;

	if ( shader == NULL || regs == NULL ) {
		RB_DrawElementsWithCounters( casterGeo );
		return;
	}

	glUseProgramObjectARB( g_shadowMapCasterProgram.programObject );
	if ( g_shadowMapCasterProgram.alphaMap >= 0 ) {
		glUniform1iARB( g_shadowMapCasterProgram.alphaMap, 0 );
	}
	if ( g_shadowMapCasterProgram.alphaHashEnabled >= 0 ) {
		glUniform1fARB( g_shadowMapCasterProgram.alphaHashEnabled, RB_ShadowMapHashedAlphaEnabled() ? 1.0f : 0.0f );
	}

	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), reinterpret_cast<void *>( &ac->st ) );

	for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
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
	glUseProgramObjectARB( 0 );

	if ( !didDraw ) {
		RB_DrawElementsWithCounters( casterGeo );
	}
}

static void RB_ShadowMapDrawCasterChain( const drawSurf_t *surf, const bool useHashedAlpha ) {
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		srfTriangles_t *casterGeo = NULL;
		vertCache_s *ambientCache = NULL;
		if ( !RB_ShadowMapResolveCasterDrawData( surf, casterGeo, ambientCache ) ) {
			continue;
		}
		if ( surf->space == NULL ) {
			RB_ShadowMapReportCasterSkip( surf, casterGeo, "no-space" );
			continue;
		}

		RB_ShadowMapTouchCache( ambientCache );
		RB_ShadowMapTouchCache( casterGeo->indexCache );

		if ( surf->space != backEnd.currentSpace ) {
			backEnd.currentSpace = surf->space;
			glLoadMatrixf( surf->space->modelMatrix );
		}

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );

		const idMaterial *shader = surf->material;
		if ( shader != NULL && shader->Coverage() == MC_PERFORATED ) {
			if ( useHashedAlpha && RB_ShadowMapCanHashPerforatedCaster( surf ) ) {
				RB_ShadowMapDrawPerforatedCasterHashed( surf, casterGeo, ac );
			} else {
				RB_ShadowMapDrawPerforatedCasterClassic( surf, casterGeo, ac );
			}
			continue;
		}

		RB_DrawElementsWithCounters( casterGeo );
	}
}

static void RB_ShadowMapDrawTranslucentCasterChain( const drawSurf_t *surf ) {
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
		}

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
		glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), reinterpret_cast<void *>( &ac->st ) );
		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );

		const idMaterial *shader = surf->material;
		const float *regs = surf->shaderRegisters;
		const shaderStage_t *coverageStage = RB_FindTranslucentShadowCoverageStage( shader, regs );
		bool drewStage = false;

		for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
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

static float RB_PointShadowMapLightFar( const viewLight_t *vLight ) {
	idVec3 adjustedRadius = vLight->lightRadius;
	if ( vLight->lightDef != NULL ) {
		const renderLight_t &parms = vLight->lightDef->parms;
		adjustedRadius[0] = parms.lightRadius[0] + idMath::Fabs( parms.lightCenter[0] );
		adjustedRadius[1] = parms.lightRadius[1] + idMath::Fabs( parms.lightCenter[1] );
		adjustedRadius[2] = parms.lightRadius[2] + idMath::Fabs( parms.lightCenter[2] );
	}

	return Max( adjustedRadius.Length() * r_shadowMapPointFarScale.GetFloat(), 1.0f );
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
	for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
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
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), reinterpret_cast<void *>( &ac->st ) );
	RB_PointShadowMapSetAlphaTexCoordMatrix( regs, &pStage->texture );

	if ( g_pointShadowCasterProgram.alphaRef >= 0 ) {
		glUniform1fARB( g_pointShadowCasterProgram.alphaRef, regs[ pStage->alphaTestRegister ] );
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

	for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
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

static void RB_PointShadowMapDrawCasterChain( const drawSurf_t *surf, const float lightModelViewMatrix[16] ) {
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		srfTriangles_t *casterGeo = NULL;
		vertCache_s *ambientCache = NULL;
		if ( !RB_ShadowMapResolveCasterDrawData( surf, casterGeo, ambientCache ) ) {
			continue;
		}
		if ( surf->space == NULL ) {
			RB_ShadowMapReportCasterSkip( surf, casterGeo, "no-space" );
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

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );

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
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
		glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), reinterpret_cast<void *>( &ac->st ) );
		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );

		const idMaterial *shader = surf->material;
		const float *regs = surf->shaderRegisters;
		const shaderStage_t *coverageStage = RB_FindTranslucentShadowCoverageStage( shader, regs );
		bool drewStage = false;

		for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
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

static bool RB_RenderShadowMap( const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *tertiaryCasters, const drawSurf_t *quaternaryCasters ) {
	if ( !RB_ShadowMapEnsureResources( backEnd.vLight ) ) {
		return false;
	}

	const GLboolean blendWasEnabled = glIsEnabled( GL_BLEND );
	const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
	const int savedFaceCulling = backEnd.glState.faceCulling;

	idPlane baseClipPlanes[4];
	RB_ShadowMapBuildClipPlanes( backEnd.vLight->lightProject, baseClipPlanes );
	RB_ShadowMapBuildProjectedState( backEnd.vLight, baseClipPlanes, RB_ShadowMapTileSizeForLight( backEnd.vLight ) );
	if ( !g_projectedShadowMapState.valid ) {
		return false;
	}

	g_shadowMapRenderTexture->MakeCurrent();

	glUseProgramObjectARB( 0 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );

	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glDisable( GL_BLEND );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_SCISSOR_TEST );
	glEnable( GL_CULL_FACE );
	glCullFace( GL_BACK );
	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( r_shadowMapPolygonFactor.GetFloat(), r_shadowMapPolygonOffset.GetFloat() );
	const bool useHashedAlpha = RB_ShadowMapHashedAlphaEnabled() && RB_ShadowMapLoadCasterProgram();

	for ( int cascadeIndex = 0; cascadeIndex < g_projectedShadowMapState.cascadeCount; cascadeIndex++ ) {
		float clipMatrix[16];
		const int tileX = ( g_projectedShadowMapState.atlasDiv > 1 ) ? ( cascadeIndex % g_projectedShadowMapState.atlasDiv ) * g_projectedShadowMapState.tileSize : 0;
		const int tileY = ( g_projectedShadowMapState.atlasDiv > 1 ) ? ( cascadeIndex / g_projectedShadowMapState.atlasDiv ) * g_projectedShadowMapState.tileSize : 0;

		RB_ShadowMapClipPlanesToGLMatrix( g_projectedShadowMapState.clipPlanes[cascadeIndex], clipMatrix );
		glViewport( tileX, tileY, g_projectedShadowMapState.tileSize, g_projectedShadowMapState.tileSize );
		glScissor( tileX, tileY, g_projectedShadowMapState.tileSize, g_projectedShadowMapState.tileSize );
		glClear( GL_DEPTH_BUFFER_BIT );

		glMatrixMode( GL_PROJECTION );
		glLoadMatrixf( clipMatrix );
		glMatrixMode( GL_MODELVIEW );

		backEnd.currentSpace = NULL;
		RB_ShadowMapDrawCasterChain( primaryCasters, useHashedAlpha );
		RB_ShadowMapDrawCasterChain( secondaryCasters, useHashedAlpha );
		RB_ShadowMapDrawCasterChain( tertiaryCasters, useHashedAlpha );
		RB_ShadowMapDrawCasterChain( quaternaryCasters, useHashedAlpha );
	}

	glDisable( GL_POLYGON_OFFSET_FILL );

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
	backEnd.glState.faceCulling = -1;
	if ( savedFaceCulling >= CT_FRONT_SIDED && savedFaceCulling <= CT_TWO_SIDED ) {
		GL_Cull( savedFaceCulling );
	}

	backEnd.currentSpace = NULL;
	backEnd.currentScissor.Clear();
	backEnd.glState.currenttmu = -1;
	GL_ClearStateDelta();
	GL_SelectTexture( 0 );

	return true;
}

static bool RB_RenderPointShadowMap( const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *tertiaryCasters, const drawSurf_t *quaternaryCasters ) {
	if ( !RB_PointShadowMapEnsureResources() ) {
		return false;
	}

	const GLboolean blendWasEnabled = glIsEnabled( GL_BLEND );
	const int savedFaceCulling = backEnd.glState.faceCulling;

	const float farClip = RB_PointShadowMapLightFar( backEnd.vLight );
	const float nearClip = idMath::ClampFloat( 0.5f, 16.0f, farClip * 0.01f );
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

	const int shadowMapWidth = g_pointShadowMapRenderTexture->GetWidth();
	const int shadowMapHeight = g_pointShadowMapRenderTexture->GetHeight();

	glUseProgramObjectARB( g_pointShadowCasterProgram.programObject );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUniform4fvARB( g_pointShadowCasterProgram.globalLightOrigin, 1, globalLightOrigin );
	glUniform1fARB( g_pointShadowCasterProgram.pointShadowFar, farClip );
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
	RB_PointShadowMapSetAlphaTexCoordIdentity();

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glDisable( GL_BLEND );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_CULL_FACE );
	glCullFace( GL_BACK );
	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( r_shadowMapPolygonFactor.GetFloat(), r_shadowMapPolygonOffset.GetFloat() );
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
		RB_PointShadowMapDrawCasterChain( primaryCasters, lightModelViewMatrix );
		RB_PointShadowMapDrawCasterChain( secondaryCasters, lightModelViewMatrix );
		RB_PointShadowMapDrawCasterChain( tertiaryCasters, lightModelViewMatrix );
		RB_PointShadowMapDrawCasterChain( quaternaryCasters, lightModelViewMatrix );
	}

	glDisable( GL_POLYGON_OFFSET_FILL );
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
	backEnd.glState.faceCulling = -1;
	if ( savedFaceCulling >= CT_FRONT_SIDED && savedFaceCulling <= CT_TWO_SIDED ) {
		GL_Cull( savedFaceCulling );
	}

	backEnd.currentSpace = NULL;
	backEnd.currentScissor.Clear();
	backEnd.glState.currenttmu = -1;
	GL_ClearStateDelta();
	GL_SelectTexture( 0 );

	return true;
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
	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( r_shadowMapPolygonFactor.GetFloat(), r_shadowMapPolygonOffset.GetFloat() );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );

	for ( int cascadeIndex = 0; cascadeIndex < g_projectedShadowMapState.cascadeCount; cascadeIndex++ ) {
		float clipMatrix[16];
		const int tileX = ( g_projectedShadowMapState.atlasDiv > 1 ) ? ( cascadeIndex % g_projectedShadowMapState.atlasDiv ) * g_projectedShadowMapState.tileSize : 0;
		const int tileY = ( g_projectedShadowMapState.atlasDiv > 1 ) ? ( cascadeIndex / g_projectedShadowMapState.atlasDiv ) * g_projectedShadowMapState.tileSize : 0;

		RB_ShadowMapClipPlanesToGLMatrix( g_projectedShadowMapState.clipPlanes[cascadeIndex], clipMatrix );
		glViewport( tileX, tileY, g_projectedShadowMapState.tileSize, g_projectedShadowMapState.tileSize );
		glScissor( tileX, tileY, g_projectedShadowMapState.tileSize, g_projectedShadowMapState.tileSize );
		glClear( GL_COLOR_BUFFER_BIT );

		glMatrixMode( GL_PROJECTION );
		glLoadMatrixf( clipMatrix );
		glMatrixMode( GL_MODELVIEW );

		backEnd.currentSpace = NULL;
		RB_ShadowMapDrawTranslucentCasterChain( primaryCasters );
		RB_ShadowMapDrawTranslucentCasterChain( secondaryCasters );
	}

	glDisable( GL_POLYGON_OFFSET_FILL );
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
	const int savedFaceCulling = backEnd.glState.faceCulling;
	const float farClip = RB_PointShadowMapLightFar( backEnd.vLight );
	const float nearClip = idMath::ClampFloat( 0.5f, 16.0f, farClip * 0.01f );
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
	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( r_shadowMapPolygonFactor.GetFloat(), r_shadowMapPolygonOffset.GetFloat() );
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

	glDisable( GL_POLYGON_OFFSET_FILL );
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

static bool RB_EnhancedMaterialShadingActive( void ) {
	return glConfig.GLSLProgramAvailable && r_enhancedMaterials.GetBool();
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

static void RB_MaterialInteractionSetEnhancementUniforms( const GLint normalScaleLocation, const GLint specularBoostLocation, const GLint fresnelLocation ) {
	if ( normalScaleLocation >= 0 ) {
		glUniform1fARB( normalScaleLocation, RB_EnhancedMaterialNormalScaleValue() );
	}
	if ( specularBoostLocation >= 0 ) {
		glUniform1fARB( specularBoostLocation, RB_EnhancedMaterialSpecularBoostValue() );
	}
	if ( fresnelLocation >= 0 ) {
		glUniform1fARB( fresnelLocation, RB_EnhancedMaterialFresnelValue() );
	}
}

static void RB_GLSLMaterial_DrawInteraction( const drawInteraction_t *din ) {
	glUniform4fvARB( g_materialInteractionProgram.localLightOrigin, 1, din->localLightOrigin.ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.localViewOrigin, 1, din->localViewOrigin.ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.lightProjectionS, 1, din->lightProjection[0].ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.lightProjectionT, 1, din->lightProjection[1].ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.lightProjectionQ, 1, din->lightProjection[2].ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.lightFalloffS, 1, din->lightProjection[3].ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.bumpMatrixS, 1, din->bumpMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.bumpMatrixT, 1, din->bumpMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.diffuseMatrixS, 1, din->diffuseMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.diffuseMatrixT, 1, din->diffuseMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.specularMatrixS, 1, din->specularMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.specularMatrixT, 1, din->specularMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.diffuseColor, 1, din->diffuseColor.ToFloatPtr() );
	glUniform4fvARB( g_materialInteractionProgram.specularColor, 1, din->specularColor.ToFloatPtr() );

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
	glUniform2fvARB( g_materialInteractionProgram.vertexColorParams, 1, vertexColorParams );

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

static void RB_GLSLShadowMap_DrawInteraction( const drawInteraction_t *din ) {
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

	glUniform4fvARB( g_shadowMapProgram.localLightOrigin, 1, din->localLightOrigin.ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.localViewOrigin, 1, din->localViewOrigin.ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.lightProjectionS, 1, din->lightProjection[0].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.lightProjectionT, 1, din->lightProjection[1].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.lightProjectionQ, 1, din->lightProjection[2].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.lightFalloffS, 1, din->lightProjection[3].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.bumpMatrixS, 1, din->bumpMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.bumpMatrixT, 1, din->bumpMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.diffuseMatrixS, 1, din->diffuseMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.diffuseMatrixT, 1, din->diffuseMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.specularMatrixS, 1, din->specularMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.specularMatrixT, 1, din->specularMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.shadowRow[0], cascadeCount, shadowRow0 );
	glUniform4fvARB( g_shadowMapProgram.shadowRow[1], cascadeCount, shadowRow1 );
	glUniform4fvARB( g_shadowMapProgram.shadowRow[2], cascadeCount, shadowRow2 );
	glUniform4fvARB( g_shadowMapProgram.shadowRow[3], cascadeCount, shadowRow3 );
	glUniform4fvARB( g_shadowMapProgram.diffuseColor, 1, din->diffuseColor.ToFloatPtr() );
	glUniform4fvARB( g_shadowMapProgram.specularColor, 1, din->specularColor.ToFloatPtr() );

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
	glUniform2fvARB( g_shadowMapProgram.vertexColorParams, 1, vertexColorParams );

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
	g_shadowMapDepthImage->Bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );

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

	glUniform4fvARB( g_pointShadowMapProgram.localLightOrigin, 1, din->localLightOrigin.ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.localViewOrigin, 1, din->localViewOrigin.ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.lightProjectionS, 1, din->lightProjection[0].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.lightProjectionT, 1, din->lightProjection[1].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.lightProjectionQ, 1, din->lightProjection[2].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.lightFalloffS, 1, din->lightProjection[3].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.bumpMatrixS, 1, din->bumpMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.bumpMatrixT, 1, din->bumpMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.diffuseMatrixS, 1, din->diffuseMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.diffuseMatrixT, 1, din->diffuseMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.specularMatrixS, 1, din->specularMatrix[0].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.specularMatrixT, 1, din->specularMatrix[1].ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow0, 1, row0 );
	glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow1, 1, row1 );
	glUniform4fvARB( g_pointShadowMapProgram.modelMatrixRow2, 1, row2 );
	glUniform4fvARB( g_pointShadowMapProgram.globalLightOrigin, 1, globalLightOrigin );
	glUniform1fARB( g_pointShadowMapProgram.pointShadowFar, RB_PointShadowMapLightFar( backEnd.vLight ) );
	glUniform4fvARB( g_pointShadowMapProgram.diffuseColor, 1, din->diffuseColor.ToFloatPtr() );
	glUniform4fvARB( g_pointShadowMapProgram.specularColor, 1, din->specularColor.ToFloatPtr() );

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
	glUniform2fvARB( g_pointShadowMapProgram.vertexColorParams, 1, vertexColorParams );

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
	g_pointShadowMapColorImage->Bind();
	for ( int i = 0; i < 3; i++ ) {
		if ( glConfig.maxTextureUnits >= 7 + i ) {
			GL_SelectTextureNoClient( 6 + i );
			if ( RB_PointTranslucentShadowEnabled() ) {
				g_pointTranslucentShadowMomentImages[i]->Bind();
			} else {
				globalImages->BindNull();
			}
		}
	}

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

static idDrawVert *RB_GLSLPrepareInteractionVertexCache( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->geo == NULL ) {
		return NULL;
	}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	const srfTriangles_t *tri = surf->geo;
	if ( tri->primBatchMesh != NULL ) {
		srfTriangles_t *mutableTri = const_cast<srfTriangles_t *>( tri );
		srfTriangles_t *ambientTri =
			( tri->ambientSurface != NULL ) ? tri->ambientSurface : mutableTri;
		const bool needsLighting =
			( surf->material != NULL ) ? surf->material->ReceivesLighting() : false;

		if ( ambientTri != NULL && ambientTri->ambientCache == NULL ) {
			if ( ambientTri->primBatchMesh != NULL ) {
				if ( !R_CreatePackedSurfaceFrameCaches( ambientTri, needsLighting, false ) ) {
					return NULL;
				}
			} else if ( ambientTri->verts != NULL ) {
				if ( !R_CreateAmbientCache( ambientTri, needsLighting ) ) {
					return NULL;
				}
			}
		}

		if ( mutableTri->ambientCache == NULL && ambientTri != NULL && ambientTri->ambientCache != NULL ) {
			mutableTri->ambientCache = ambientTri->ambientCache;
			mutableTri->tempAmbientCache = ambientTri->tempAmbientCache;
		}

		if ( mutableTri->ambientCache == NULL ) {
			return NULL;
		}

		if ( mutableTri->indexCache == NULL
			&& r_useIndexBuffers.GetBool()
			&& mutableTri->indexes != NULL
			&& mutableTri->numIndexes > 0 ) {
			mutableTri->indexCache = vertexCache.AllocFrameTemp(
				mutableTri->indexes,
				mutableTri->numIndexes * sizeof( mutableTri->indexes[0] ),
				true );
		}

		R_TouchVertexCache( mutableTri->ambientCache );
		if ( mutableTri->indexCache != NULL ) {
			R_TouchVertexCache( mutableTri->indexCache );
		}
	}
#endif

	if ( surf->geo->ambientCache == NULL ) {
		return NULL;
	}

	return (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );
}

static bool RB_SurfaceHasActiveCustomGLSLLighting( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return false;
	}

	for ( int stageIndex = 0; stageIndex < surf->material->GetNumStages(); stageIndex++ ) {
		const shaderStage_t *stage = surf->material->GetStage( stageIndex );
		if ( stage == NULL || stage->newStage == NULL ) {
			continue;
		}
		if ( !stage->newStage->customLighting || !stage->newStage->glslProgram ) {
			continue;
		}
		if ( surf->shaderRegisters[ stage->conditionRegister ] == 0.0f ) {
			continue;
		}
		return true;
	}

	return false;
}

static bool RB_DrawSurfChainHasCustomGLSLLighting( const drawSurf_t *surf ) {
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		if ( RB_SurfaceHasActiveCustomGLSLLighting( surf ) ) {
			return true;
		}
	}
	return false;
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

	for ( int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++ ) {
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

		for ( int surfaceStageNum = 0; surfaceStageNum < surfaceShader->GetNumStages(); surfaceStageNum++ ) {
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

		idDrawVert *ac = RB_GLSLPrepareInteractionVertexCache( surf );
		if ( ac == NULL ) {
			continue;
		}

		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );
		glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
		glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );

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

static bool RB_GLSLMaterial_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( surf == NULL || !RB_MaterialInteractionLoadProgram() ) {
		return false;
	}

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( g_materialInteractionProgram.programObject );

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
	RB_MaterialInteractionSetEnhancementUniforms(
		g_materialInteractionProgram.materialNormalScale,
		g_materialInteractionProgram.materialSpecularBoost,
		g_materialInteractionProgram.materialFresnel );

	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );

	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		idDrawVert *ac = RB_GLSLPrepareInteractionVertexCache( surf );
		if ( ac == NULL ) {
			continue;
		}

		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );

		RB_CreateSingleDrawInteractions( surf, RB_GLSLMaterial_DrawInteraction );
	}

	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );

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
	return true;
}

static void RB_DrawMaterialInteractions( const drawSurf_t *surf ) {
	if ( surf == NULL ) {
		return;
	}

	if ( !RB_DrawSurfChainHasCustomGLSLLighting( surf ) && RB_EnhancedMaterialShadingActive() && RB_GLSLMaterial_CreateDrawInteractions( surf ) ) {
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
	if ( g_shadowMapProgram.shadowFilterRadius >= 0 ) {
		glUniform1fARB( g_shadowMapProgram.shadowFilterRadius, r_shadowMapFilterRadius.GetFloat() );
	}
	if ( g_shadowMapProgram.shadowAtlasRect >= 0 ) {
		glUniform4fvARB( g_shadowMapProgram.shadowAtlasRect, SHADOWMAP_MAX_CASCADES, g_projectedShadowMapState.atlasRect[0].ToFloatPtr() );
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
	RB_MaterialInteractionSetEnhancementUniforms(
		g_shadowMapProgram.materialNormalScale,
		g_shadowMapProgram.materialSpecularBoost,
		g_shadowMapProgram.materialFresnel );

	glStencilMask( 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	glStencilFunc( GL_ALWAYS, 128, 255 );

	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );

	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		idDrawVert *ac = RB_GLSLPrepareInteractionVertexCache( surf );
		if ( ac == NULL ) {
			continue;
		}

		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );

		RB_CreateSingleDrawInteractions( surf, RB_GLSLShadowMap_DrawInteraction );
	}

	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );

	for ( int i = 0; i < 3; i++ ) {
		if ( glConfig.maxTextureUnits >= 7 + i ) {
			GL_SelectTextureNoClient( 6 + i );
			globalImages->BindNull();
		}
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
	return true;
}

static bool RB_GLSLPointShadowMap_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( surf == NULL || !RB_PointShadowMapLoadProgram() ) {
		return false;
	}

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glUseProgramObjectARB( g_pointShadowMapProgram.programObject );

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
		glUniform1fARB( g_pointShadowMapProgram.shadowBias, r_shadowMapPointBias.GetFloat() );
	}
	if ( g_pointShadowMapProgram.shadowNormalBias >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.shadowNormalBias, r_shadowMapPointNormalBias.GetFloat() );
	}
	if ( g_pointShadowMapProgram.shadowFilterRadius >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.shadowFilterRadius, r_shadowMapPointFilterRadius.GetFloat() );
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
		glUniform1fARB( g_pointShadowMapProgram.pointShadowFar, RB_PointShadowMapLightFar( backEnd.vLight ) );
	}
	if ( g_pointShadowMapProgram.pointShadowTexelScale >= 0 ) {
		const float texelScale = 2.0f / Max( 1, g_pointShadowMapRenderTexture->GetWidth() );
		glUniform1fARB( g_pointShadowMapProgram.pointShadowTexelScale, texelScale );
	}
	if ( g_pointShadowMapProgram.translucentShadowEnabled >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.translucentShadowEnabled, RB_PointTranslucentShadowEnabled() ? 1.0f : 0.0f );
	}
	if ( g_pointShadowMapProgram.translucentShadowDensity >= 0 ) {
		glUniform1fARB( g_pointShadowMapProgram.translucentShadowDensity, r_shadowMapTranslucentDensity.GetFloat() );
	}
	RB_MaterialInteractionSetEnhancementUniforms(
		g_pointShadowMapProgram.materialNormalScale,
		g_pointShadowMapProgram.materialSpecularBoost,
		g_pointShadowMapProgram.materialFresnel );

	glStencilMask( 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	glStencilFunc( GL_ALWAYS, 128, 255 );

	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );

	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		idDrawVert *ac = RB_GLSLPrepareInteractionVertexCache( surf );
		if ( ac == NULL ) {
			continue;
		}

		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );

		RB_CreateSingleDrawInteractions( surf, RB_GLSLPointShadowMap_DrawInteraction );
	}

	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );

	for ( int i = 0; i < 3; i++ ) {
		if ( glConfig.maxTextureUnits >= 7 + i ) {
			GL_SelectTextureNoClient( 6 + i );
			globalImages->BindNull();
		}
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
	return true;
}

static void RB_ShadowMapStencilFallback( const drawSurf_t *primaryShadowSurfs, const drawSurf_t *secondaryShadowSurfs, const drawSurf_t *interactions ) {
	if ( interactions == NULL ) {
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

		if ( r_useShadowVertexProgram.GetBool() && R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW, "stencil shadow fallback vertex program", false ) ) {
			glEnable( GL_VERTEX_PROGRAM_ARB );
			RB_StencilShadowPass( primaryShadowSurfs );
			RB_StencilShadowPass( secondaryShadowSurfs );
			RB_DrawMaterialInteractions( interactions );
			glDisable( GL_VERTEX_PROGRAM_ARB );
		} else {
			RB_StencilShadowPass( primaryShadowSurfs );
			RB_StencilShadowPass( secondaryShadowSurfs );
			RB_DrawMaterialInteractions( interactions );
		}
	} else {
		glStencilFunc( GL_ALWAYS, 128, 255 );
		RB_DrawMaterialInteractions( interactions );
	}
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
	g_shadowMapDebugOverlayState.casterCount =
		RB_CountDrawSurfChain( primaryCasters ) +
		RB_CountDrawSurfChain( secondaryCasters ) +
		RB_CountDrawSurfChain( tertiaryCasters ) +
		RB_CountDrawSurfChain( quaternaryCasters );
	g_shadowMapDebugOverlayState.shadowSurfCount =
		RB_CountDrawSurfChain( primaryShadowSurfs ) +
		RB_CountDrawSurfChain( secondaryShadowSurfs );
	g_shadowMapDebugOverlayState.interactionCount = RB_CountDrawSurfChain( interactions );
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

	GL_SelectTextureNoClient( 0 );
	if ( g_shadowMapDepthImage != NULL ) {
		g_shadowMapDepthImage->Bind();
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
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
	const float statsH = 40.0f;
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
		RB_ShadowMapDebugOverlaySubmitQuad( panelX, panelY, panelW, panelH, 0.0f, 0.0f, 1.0f, 1.0f );
		RB_ShadowMapDebugOverlayDrawLabels( panelX, panelY, panelW, panelH );
	}

	char line1[96];
	char line2[96];
	char line3[96];
	if ( g_shadowMapDebugOverlayState.valid ) {
		const char *lightLabel = ( g_shadowMapDebugOverlayState.lightDefIndex >= 0 ) ?
			va( "L%d", g_shadowMapDebugOverlayState.lightDefIndex ) : "L?";
		idStr::snPrintf( line1, sizeof( line1 ), "%s %c %s %c%d %s",
			g_shadowMapDebugOverlayState.pointLight ? "POINT" : "PROJ",
			g_shadowMapDebugOverlayState.globalPass ? 'G' : 'L',
			lightLabel,
			g_shadowMapDebugOverlayState.pointLight ? 'F' : 'C',
			g_shadowMapDebugOverlayState.pointLight ? 6 : g_shadowMapDebugOverlayState.cascadeCount,
			g_shadowMapDebugOverlayState.mapped ? "MAP" : "FB" );
		idStr::snPrintf( line2, sizeof( line2 ), "CAST %s SURF %s INTR %s",
			( g_shadowMapDebugOverlayState.casterCount >= 0 ) ? va( "%d", g_shadowMapDebugOverlayState.casterCount ) : "?",
			( g_shadowMapDebugOverlayState.shadowSurfCount >= 0 ) ? va( "%d", g_shadowMapDebugOverlayState.shadowSurfCount ) : "?",
			( g_shadowMapDebugOverlayState.interactionCount >= 0 ) ? va( "%d", g_shadowMapDebugOverlayState.interactionCount ) : "?" );
	} else {
		idStr::snPrintf( line1, sizeof( line1 ), "NO MAP" );
		idStr::snPrintf( line2, sizeof( line2 ), "SUP %d/%d",
			g_shadowMapStats.supportedLights,
			g_shadowMapStats.totalLights );
	}
	idStr::snPrintf( line3, sizeof( line3 ), "SUP %d/%d MAP %d/%d FB %d/%d",
		g_shadowMapStats.supportedLights,
		g_shadowMapStats.totalLights,
		g_shadowMapStats.mappedLocalPasses,
		g_shadowMapStats.mappedGlobalPasses,
		g_shadowMapStats.fallbackLocalPasses,
		g_shadowMapStats.fallbackGlobalPasses );

	RB_ShadowMapDebugOverlayDrawString( panelX, statsY, 0.9f, textColor, line1 );
	RB_ShadowMapDebugOverlayDrawString( panelX, statsY + 10.0f, 0.8f, textColor, line2 );
	RB_ShadowMapDebugOverlayDrawString( panelX, statsY + 20.0f, 0.8f, textColor, line3 );

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

static void RB_ShadowMapRunPass( const viewLight_t *vLight, shadowMapPassKind_t passKind, bool pointLight, const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters, const drawSurf_t *tertiaryCasters, const drawSurf_t *quaternaryCasters, const drawSurf_t *primaryShadowSurfs, const drawSurf_t *secondaryShadowSurfs, const drawSurf_t *interactions ) {
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
	const bool haveTranslucentCasters = translucentPrimaryCasters != NULL || translucentSecondaryCasters != NULL;

	if ( primaryShadowSurfs == NULL && secondaryShadowSurfs == NULL && primaryCasters == NULL && secondaryCasters == NULL && tertiaryCasters == NULL && quaternaryCasters == NULL && !haveTranslucentCasters ) {
		if ( passKind == SHADOWMAP_PASS_LOCAL ) {
			g_shadowMapStats.unshadowedLocalPasses++;
		} else {
			g_shadowMapStats.unshadowedGlobalPasses++;
		}
		RB_ShadowMapPassReport( vLight, passKind, pointLight, SHADOWMAP_PASS_RESULT_NO_SHADOW_SURFS, primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		glStencilFunc( GL_ALWAYS, 128, 255 );
		RB_DrawMaterialInteractions( interactions );
		return;
	}

	const bool renderOk = pointLight ? RB_RenderPointShadowMap( primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters ) : RB_RenderShadowMap( primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters );
	if ( renderOk && haveTranslucentCasters && RB_TranslucentShadowMomentsRequested() ) {
		if ( pointLight ) {
			RB_RenderPointTranslucentShadowMap( translucentPrimaryCasters, translucentSecondaryCasters );
		} else {
			RB_RenderTranslucentShadowMap( translucentPrimaryCasters, translucentSecondaryCasters );
		}
	}
	const bool customGLSLLighting = RB_DrawSurfChainHasCustomGLSLLighting( interactions );
	const bool maskOk = renderOk && !customGLSLLighting && ( pointLight ? RB_GLSLPointShadowMap_CreateDrawInteractions( interactions ) : RB_GLSLShadowMap_CreateDrawInteractions( interactions ) );
	shadowMapPassResult_t passResult = SHADOWMAP_PASS_RESULT_MAPPED;
	if ( !renderOk ) {
		passResult = SHADOWMAP_PASS_RESULT_RENDER_FAIL;
	} else if ( !maskOk ) {
		passResult = SHADOWMAP_PASS_RESULT_MASK_FAIL;
	}
	const bool mapped = ( passResult == SHADOWMAP_PASS_RESULT_MAPPED );

	RB_ShadowMapDebugOverlayCapture( vLight, passKind, pointLight, mapped, renderOk,
		primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters,
		primaryShadowSurfs, secondaryShadowSurfs, interactions );

	if ( mapped ) {
		if ( passKind == SHADOWMAP_PASS_LOCAL ) {
			g_shadowMapStats.mappedLocalPasses++;
		} else {
			g_shadowMapStats.mappedGlobalPasses++;
		}
		RB_ShadowMapPassReport( vLight, passKind, pointLight, passResult, primaryCasters, secondaryCasters, tertiaryCasters, quaternaryCasters, primaryShadowSurfs, secondaryShadowSurfs, interactions );
		return;
	}

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
	RB_ShadowMapStencilFallback( primaryShadowSurfs, secondaryShadowSurfs, interactions );
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
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_LIGHT_ORIGIN, din->localLightOrigin.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_VIEW_ORIGIN, din->localViewOrigin.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_LIGHT_PROJECT_S, din->lightProjection[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_LIGHT_PROJECT_T, din->lightProjection[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_LIGHT_PROJECT_Q, din->lightProjection[2].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_LIGHT_FALLOFF_S, din->lightProjection[3].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_BUMP_MATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_BUMP_MATRIX_T, din->bumpMatrix[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_DIFFUSE_MATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_DIFFUSE_MATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_SPECULAR_MATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_SPECULAR_MATRIX_T, din->specularMatrix[1].ToFloatPtr() );

	// testing fragment based normal mapping
	if ( r_testARBProgram.GetBool() ) {
		glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 2, din->localLightOrigin.ToFloatPtr() );
		glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 3, din->localViewOrigin.ToFloatPtr() );
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
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, interactionParamBase + PP_COLOR_MODULATE, packedColorMode );
	} else if ( g_interactionVertexProgramColorMode == ICM_PACKED ) {
		// Stock Quake 4 interaction.vfp packs vertex-color mode as env[16].xy.
		const float packed[4] = { modulate, add, 0.0f, 0.0f };
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_MODULATE, packed );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_ADD, zero );
	} else {
		float modulateVec[4] = { modulate, modulate, modulate, modulate };
		float addVec[4] = { add, add, add, add };
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_MODULATE, modulateVec );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_ADD, addVec );
	}

	// set the constant colors
	const float specularColorX2[4] = {
		din->specularColor[0] * 2.0f,
		din->specularColor[1] * 2.0f,
		din->specularColor[2] * 2.0f,
		din->specularColor[3] * 2.0f
	};
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, din->diffuseColor.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, specularColorX2 );

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
	const drawSurf_t *firstSurf = surf;

	// perform setup here that will be constant for all interactions
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

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


	for ( ; surf ; surf=surf->nextOnLight ) {
		g_packedInteractionSurf = NULL;
		g_packedInteractionVertexFormatIndex = -1;

		// perform setup here that will not change over multiple interaction passes
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		bool useClassicInteraction = true;
		const rvMD5RVertexBufferDesc *packedDrawVertexBuffer =
			( surf->geo != NULL && surf->geo->primBatchMesh != NULL ) ? R_MD5R_GetDrawVertexBufferForTri( surf->geo ) : NULL;
		if ( packedDrawVertexBuffer != NULL ) {
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
			glDisableVertexAttribArrayARB( 1 );
			glDisableVertexAttribArrayARB( 2 );
			glDisableVertexAttribArrayARB( 5 );
			glDisableVertexAttribArrayARB( 6 );
			glDisableVertexAttribArrayARB( 7 );
			glEnableVertexAttribArrayARB( 8 );
			glEnableVertexAttribArrayARB( 9 );
			glEnableVertexAttribArrayARB( 10 );
			glEnableVertexAttribArrayARB( 11 );

			// set the vertex pointers
			idDrawVert	*ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );
			glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );
			glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
			glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
			glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
			glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
			glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
			R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, vertexProgram, "interaction vertex program", true );
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
void RB_ARB2_DrawInteractions( void ) {
	viewLight_t		*vLight;

	if ( r_interactionColorMode.IsModified() ) {
		r_interactionColorMode.ClearModified();
		RB_UpdateInteractionColorMode( true );
	}

	GL_SelectTexture( 0 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	RB_ShadowMapStatsReset();
	RB_ShadowMapDebugOverlayReset();

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

		const shadowMapLightSupportReason_t supportReason = RB_ShadowMapLightSupportReason( vLight );
		if ( supportReason == SHADOWMAP_SUPPORT_OK ) {
			g_shadowMapStats.supportedLights++;
			if ( vLight->pointLight ) {
				g_shadowMapStats.pointLights++;
			} else {
				g_shadowMapStats.projectedLights++;
			}
			if ( vLight->parallel ) {
				g_shadowMapStats.parallelLights++;
			}
			RB_ShadowMapLightReport( vLight, supportReason );

			glStencilFunc( GL_ALWAYS, 128, 255 );

			if ( vLight->pointLight ) {
				const drawSurf_t *pointLocalPrimaryCasters = vLight->globalShadowMapCasters;
				const drawSurf_t *pointLocalSecondaryCasters = ( pointLocalPrimaryCasters == NULL ) ? vLight->globalShadows : NULL;
				const bool pointHaveDedicatedGlobalCasters = ( vLight->globalShadowMapCasters != NULL || vLight->localShadowMapCasters != NULL );
				const drawSurf_t *pointGlobalPrimaryCasters = vLight->globalShadowMapCasters;
				const drawSurf_t *pointGlobalSecondaryCasters = vLight->localShadowMapCasters;
				const drawSurf_t *pointGlobalTertiaryCasters = pointHaveDedicatedGlobalCasters ? NULL : vLight->globalShadows;
				const drawSurf_t *pointGlobalQuaternaryCasters = pointHaveDedicatedGlobalCasters ? NULL : vLight->localShadows;

				// Prefer the dedicated ambient-geometry shadow-map caster lists. The legacy
				// shadow volume chains are a recovery path only when no dedicated point-light
				// caster geometry survived interaction building.
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_LOCAL, true, pointLocalPrimaryCasters, pointLocalSecondaryCasters, NULL, NULL, vLight->globalShadows, NULL, vLight->localInteractions );
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_GLOBAL, true, pointGlobalPrimaryCasters, pointGlobalSecondaryCasters, pointGlobalTertiaryCasters, pointGlobalQuaternaryCasters, vLight->globalShadows, vLight->localShadows, vLight->globalInteractions );
			} else {
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_LOCAL, false, vLight->globalShadowMapCasters, vLight->globalShadows, NULL, NULL, vLight->globalShadows, NULL, vLight->localInteractions );
				RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_GLOBAL, false, vLight->globalShadowMapCasters, vLight->localShadowMapCasters, vLight->globalShadows, vLight->localShadows, vLight->globalShadows, vLight->localShadows, vLight->globalInteractions );
			}

			if ( !r_skipTranslucent.GetBool() ) {
				glStencilFunc( GL_ALWAYS, 128, 255 );
				backEnd.depthFunc = GLS_DEPTHFUNC_LESS;
				RB_DrawMaterialInteractions( vLight->translucentInteractions );
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

		if ( r_useShadowVertexProgram.GetBool() && R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW, "stencil shadow vertex program", false ) ) {
			glEnable( GL_VERTEX_PROGRAM_ARB );
			RB_StencilShadowPass( vLight->globalShadows );
			RB_DrawMaterialInteractions( vLight->localInteractions );
			glEnable( GL_VERTEX_PROGRAM_ARB );
			R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW, "stencil shadow vertex program", false );
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

	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
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
		idStr::Icmp( prog.name, "interaction.vfp" ) == 0;
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

static GLuint RB_CurrentInteractionProgramIdent( GLenum target ) {
	if ( r_testARBProgram.GetBool() ) {
		return ( target == GL_VERTEX_PROGRAM_ARB ) ? VPROG_TEST : FPROG_TEST;
	}

	if ( r_useSimpleInteraction.GetBool() ) {
		return ( target == GL_VERTEX_PROGRAM_ARB ) ? VPROG_SIMPLE_INTERACTION : FPROG_SIMPLE_INTERACTION;
	}

	return ( target == GL_VERTEX_PROGRAM_ARB ) ? VPROG_INTERACTION : FPROG_INTERACTION;
}

static const char *RB_CurrentInteractionProgramFamilyName( void ) {
	if ( r_testARBProgram.GetBool() ) {
		return "test";
	}

	if ( r_useSimpleInteraction.GetBool() ) {
		return "simple";
	}

	return "interaction";
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
	char	*start, *end;

	RB_ResetARBProgramStatus( prog );

	common->Printf( "%s", fullPath.c_str() );

	// load the program even if we don't support it, so
	// fs_copyfiles can generate cross-platform data dumps
	fileSystem->ReadFile( fullPath.c_str(), (void **)&fileBuffer, NULL );
	if ( !fileBuffer ) {
		common->Printf( ": File not found\n" );
		RB_SetARBProgramFailure( prog, "file not found" );
		return;
	}

	// copy to stack memory and free
	buffer = (char *)_alloca( strlen( fileBuffer ) + 1 );
	strcpy( buffer, fileBuffer );
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
		RB_UpdateInteractionColorMode( true );
	}

	glBindProgramARB( prog.target, prog.ident );
	glGetError();

	glProgramStringARB( prog.target, GL_PROGRAM_FORMAT_ASCII_ARB,
		strlen( start ), (unsigned char *)start );

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

	// see if it is already loaded
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
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
	common->Printf( "----- R_ReloadARBPrograms -----\n" );
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
		R_LoadARBProgram( i );
	}
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
	glConfig.allowARB2Path = false;
	glConfig.preferNV20Path = false;
	glConfig.preferSimpleLighting = false;

	common->Printf( "---------- R_ARB2_Init ----------\n" );

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
				"%s: retail would prefer the NV20 compatibility path, but OpenQ4 keeps ARB2 because the legacy NV20 backend is not shipped\n",
				renderer.c_str() );
		}
	}

	if ( legacyRadeonSimpleLighting ) {
		glConfig.preferSimpleLighting = true;
		common->Printf( "%s: prefers simple lighting\n", renderer.c_str() );
	}

	common->Printf( "---------------------------------\n" );

	glConfig.allowARB2Path = true;
}
