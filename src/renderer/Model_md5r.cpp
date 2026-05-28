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
#include "../idlib/LexerFactory.h"
#include "../idlib/geometry/rvVertex.h"

rvRenderModelMD5R *rvRenderModelMD5R::modelList = NULL;

static const char *MD5R_SnapshotName = "_MD5R_Snapshot_";
static const int MD5R_BackSideSurfaceIdOffset = 1000;
// Retail MD5R synthesizes omitted LOD ranges from this table, doubling after the third entry.
static const float MD5R_DefaultLODRanges[] = { 100.0f, 250.0f, 500.0f };

/*
========================
R_MD5R_ReadBinaryAwareTimestamp
========================
*/
static ID_TIME_T R_MD5R_ReadBinaryAwareTimestamp( const idStr &filename, idStr *resolvedFilename = NULL ) {
	if ( resolvedFilename != NULL ) {
		resolvedFilename->Clear();
	}

	if ( cvarSystem->GetCVarBool( "com_binaryRead" ) ) {
		idStr compiledFilename = filename;
		compiledFilename += Lexer::sCompiledFileSuffix;

		ID_TIME_T compiledTimeStamp;
		fileSystem->ReadFile( compiledFilename, NULL, &compiledTimeStamp );
		if ( compiledTimeStamp != FILE_NOT_FOUND_TIMESTAMP ) {
			if ( resolvedFilename != NULL ) {
				*resolvedFilename = compiledFilename;
			}
			return compiledTimeStamp;
		}
	}

	ID_TIME_T sourceTimeStamp;
	fileSystem->ReadFile( filename, NULL, &sourceTimeStamp );
	if ( resolvedFilename != NULL && sourceTimeStamp != FILE_NOT_FOUND_TIMESTAMP ) {
		*resolvedFilename = filename;
	}

	return sourceTimeStamp;
}

/*
========================
R_MD5R_GetDefaultLODRange
========================
*/
static float R_MD5R_GetDefaultLODRange( int lodIndex ) {
	const int numDefaultRanges = static_cast<int>( sizeof( MD5R_DefaultLODRanges ) / sizeof( MD5R_DefaultLODRanges[ 0 ] ) );
	if ( lodIndex < 0 ) {
		return 0.0f;
	}
	if ( lodIndex < numDefaultRanges ) {
		return MD5R_DefaultLODRanges[ lodIndex ];
	}

	float rangeEnd = MD5R_DefaultLODRanges[ numDefaultRanges - 1 ];
	for ( int i = numDefaultRanges; i <= lodIndex; ++i ) {
		rangeEnd *= 2.0f;
	}

	return rangeEnd;
}

/*
========================
R_MD5R_CopyAndReverseTriangles
========================
*/
static void R_MD5R_CopyAndReverseTriangles( const srfTriangles_t *src, srfTriangles_t **dst ) {
	if ( src == NULL ) {
		return;
	}

	if ( *dst != NULL ) {
		if ( ( *dst )->numVerts == src->numVerts && ( *dst )->numIndexes == src->numIndexes ) {
			R_FreeStaticTriSurfVertexCaches( *dst );
		} else {
			R_FreeStaticTriSurf( *dst );
			*dst = NULL;
		}
	}

	if ( *dst == NULL ) {
		*dst = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts( *dst, src->numVerts );
		R_AllocStaticTriSurfIndexes( *dst, src->numIndexes );
		( *dst )->numVerts = src->numVerts;
		( *dst )->numIndexes = src->numIndexes;
	}

	srfTriangles_t *tri = *dst;
	tri->bounds = src->bounds;
	tri->deformedSurface = false;
	tri->tangentsCalculated = false;
	tri->facePlanesCalculated = false;
	tri->generateNormals = src->generateNormals;

	memcpy( tri->verts, src->verts, src->numVerts * sizeof( tri->verts[ 0 ] ) );

	for ( int i = 0; i < tri->numVerts; ++i ) {
		tri->verts[ i ].normal = vec3_origin - tri->verts[ i ].normal;
	}

	for ( int i = 0; i < tri->numIndexes; i += 3 ) {
		tri->indexes[ i + 0 ] = src->indexes[ i + 1 ];
		tri->indexes[ i + 1 ] = src->indexes[ i + 0 ];
		tri->indexes[ i + 2 ] = src->indexes[ i + 2 ];
	}
}

/*
===========================
R_MD5R_ParseFlexibleVec3

Retail MD5R files use plain numeric rows in several places, but some community
tools emit parenthesized vectors. Accept either form so the metadata loader
stays tolerant while the full packed-mesh runtime is still in progress.
===========================
*/
static void R_MD5R_ParseFlexibleVec3( Lexer &parser, idVec3 &value ) {
	if ( parser.PeekTokenString( "(" ) ) {
		parser.Parse1DMatrix( 3, value.ToFloatPtr() );
		return;
	}

	value.x = parser.ParseFloat();
	value.y = parser.ParseFloat();
	value.z = parser.ParseFloat();
}

/*
===========================
R_MD5R_ParseFlexibleBounds
===========================
*/
static void R_MD5R_ParseFlexibleBounds( Lexer &parser, idBounds &bounds ) {
	idVec3 mins;
	idVec3 maxs;

	R_MD5R_ParseFlexibleVec3( parser, mins );
	R_MD5R_ParseFlexibleVec3( parser, maxs );
	bounds[ 0 ] = mins;
	bounds[ 1 ] = maxs;
}

/*
========================
R_MD5R_ModelHasSky
========================
*/
static bool R_MD5R_ModelHasSky( const idRenderModelStatic &model ) {
	if ( model.HasProcSky() ) {
		return true;
	}

	for ( int surfaceIndex = 0; surfaceIndex < model.surfaces.Num(); ++surfaceIndex ) {
		const modelSurface_t &surface = model.surfaces[ surfaceIndex ];
		if ( surface.shader == NULL ) {
			continue;
		}
		const texgen_t texgen = surface.shader->Texgen();
		if ( surface.shader->IsPortalSky()
			|| texgen == TG_SKYBOX_CUBE
			|| texgen == TG_WOBBLESKY_CUBE
			|| idStr::Icmp( surface.shader->GetName(), "textures/smf/portal_sky" ) == 0 ) {
			return true;
		}
	}

	return false;
}

/*
========================
R_MD5R_StaticModelHasRenderableSurfaces
========================
*/
static bool R_MD5R_StaticModelHasRenderableSurfaces( const idRenderModelStatic &model ) {
	for ( int surfaceIndex = 0; surfaceIndex < model.surfaces.Num(); ++surfaceIndex ) {
		const modelSurface_t &surface = model.surfaces[ surfaceIndex ];
		if ( surface.geometry != NULL
			&& surface.shader != NULL
			&& ( surface.shader->GetSurfaceFlags() & SURF_COLLISION ) == 0 ) {
			return true;
		}
	}

	return false;
}

/*
========================
R_MD5R_ShouldKeepStaticSurface
========================
*/
static bool R_MD5R_ShouldKeepStaticSurface( const modelSurface_t &surface, bool dropCollisionHelperSurfaces ) {
	if ( surface.geometry == NULL || surface.shader == NULL ) {
		return false;
	}
	if ( dropCollisionHelperSurfaces && ( surface.shader->GetSurfaceFlags() & SURF_COLLISION ) != 0 ) {
		return false;
	}

	return true;
}

/*
========================
R_MD5R_PackBlendIndices
========================
*/
static dword R_MD5R_PackBlendIndices( const int blendIndices[ 4 ] ) {
	return static_cast<dword>( blendIndices[ 0 ] & 0xFF )
		| ( static_cast<dword>( blendIndices[ 1 ] & 0xFF ) << 8 )
		| ( static_cast<dword>( blendIndices[ 2 ] & 0xFF ) << 16 )
		| ( static_cast<dword>( blendIndices[ 3 ] & 0xFF ) << 24 );
}

static ID_INLINE int R_MD5R_GetBlendIndex( dword packedBlendIndices, int component );

/*
========================
R_MD5R_InitStaticVertexFormat
========================
*/
static void R_MD5R_InitStaticVertexFormat( rvMD5RVertexFormatDesc &vertexFormat ) {
	vertexFormat = rvMD5RVertexFormatDesc();
	vertexFormat.hasPosition = true;
	vertexFormat.positionDim = 3;
	vertexFormat.positionTokenType = TT_FLOAT;
	vertexFormat.hasNormal = true;
	vertexFormat.normalTokenType = TT_FLOAT;
	vertexFormat.hasTangent = true;
	vertexFormat.tangentTokenType = TT_FLOAT;
	vertexFormat.hasBinormal = true;
	vertexFormat.binormalTokenType = TT_FLOAT;
	vertexFormat.hasDiffuseColor = true;
	vertexFormat.diffuseColorTokenType = 0;
	vertexFormat.hasTexCoord[ 0 ] = true;
	vertexFormat.texCoordDim[ 0 ] = 2;
	vertexFormat.texCoordTokenType[ 0 ] = TT_FLOAT;
}

/*
========================
R_MD5R_InitSkinnedVertexFormat
========================
*/
static void R_MD5R_InitSkinnedVertexFormat( rvMD5RVertexFormatDesc &vertexFormat ) {
	vertexFormat = rvMD5RVertexFormatDesc();
	vertexFormat.hasPosition = true;
	vertexFormat.positionDim = 3;
	vertexFormat.positionTokenType = TT_FLOAT;
	vertexFormat.hasBlendIndex = true;
	vertexFormat.blendIndexTokenType = 0;
	vertexFormat.hasBlendWeight = true;
	vertexFormat.blendWeightDim = 4;
	vertexFormat.blendWeightTransformCount = 4;
	vertexFormat.blendWeightTokenType = TT_FLOAT;
	vertexFormat.hasTexCoord[ 0 ] = true;
	vertexFormat.texCoordDim[ 0 ] = 2;
	vertexFormat.texCoordTokenType[ 0 ] = TT_FLOAT;
}

/*
========================
R_MD5R_InitVertexBufferDesc
========================
*/
static void R_MD5R_InitVertexBufferDesc( rvMD5RVertexBufferDesc &vertexBuffer, const rvMD5RVertexFormatDesc &vertexFormat, int numVertices ) {
	vertexBuffer = rvMD5RVertexBufferDesc();
	vertexBuffer.numVertices = numVertices;
	vertexBuffer.systemMemory = true;
	vertexBuffer.videoMemory = true;
	vertexBuffer.soA = false;
	vertexBuffer.hasVertexFormat = true;
	vertexBuffer.hasLoadVertexFormat = true;
	vertexBuffer.vertexFormat = vertexFormat;
	vertexBuffer.loadVertexFormat = vertexFormat;
	vertexBuffer.positions.SetNum( numVertices );

	if ( vertexFormat.hasBlendIndex ) {
		vertexBuffer.blendIndices.SetNum( numVertices );
	}
	if ( vertexFormat.hasBlendWeight ) {
		vertexBuffer.blendWeights.SetNum( numVertices );
	}
	if ( vertexFormat.hasNormal ) {
		vertexBuffer.normals.SetNum( numVertices );
	}
	if ( vertexFormat.hasTangent ) {
		vertexBuffer.tangents.SetNum( numVertices );
	}
	if ( vertexFormat.hasBinormal ) {
		vertexBuffer.binormals.SetNum( numVertices );
	}
	if ( vertexFormat.hasDiffuseColor ) {
		vertexBuffer.diffuseColors.SetNum( numVertices );
	}
	if ( vertexFormat.hasSpecularColor ) {
		vertexBuffer.specularColors.SetNum( numVertices );
	}
	if ( vertexFormat.hasPointSize ) {
		vertexBuffer.pointSizes.SetNum( numVertices );
	}

	for ( int texCoordSet = 0; texCoordSet < 7; ++texCoordSet ) {
		if ( vertexFormat.hasTexCoord[ texCoordSet ] ) {
			vertexBuffer.texCoords[ texCoordSet ].SetNum( numVertices );
		}
	}
}

/*
========================
R_MD5R_InitIndexBufferDesc
========================
*/
static void R_MD5R_InitIndexBufferDesc( rvMD5RIndexBufferDesc &indexBuffer, int numIndices ) {
	indexBuffer = rvMD5RIndexBufferDesc();
	indexBuffer.numIndices = numIndices;
	indexBuffer.bitDepth = 32;
	indexBuffer.systemMemory = true;
	indexBuffer.videoMemory = true;
	indexBuffer.indices.SetNum( numIndices );
}

/*
========================
R_MD5R_FillStaticVertexBuffer
========================
*/
static void R_MD5R_FillStaticVertexBuffer( rvMD5RVertexBufferDesc &vertexBuffer, const srfTriangles_t &tri ) {
	for ( int vertexIndex = 0; vertexIndex < tri.numVerts; ++vertexIndex ) {
		const idDrawVert &sourceVert = tri.verts[ vertexIndex ];

		vertexBuffer.positions[ vertexIndex ].Set( sourceVert.xyz.x, sourceVert.xyz.y, sourceVert.xyz.z, 1.0f );
		vertexBuffer.normals[ vertexIndex ] = sourceVert.normal;
		vertexBuffer.tangents[ vertexIndex ] = sourceVert.tangents[ 0 ];
		vertexBuffer.binormals[ vertexIndex ] = sourceVert.tangents[ 1 ];
		vertexBuffer.diffuseColors[ vertexIndex ] = sourceVert.GetColor();
		vertexBuffer.texCoords[ 0 ][ vertexIndex ].Set( sourceVert.st.x, sourceVert.st.y, 0.0f, 0.0f );
	}
}

/*
========================
R_MD5R_CopyIndexes
========================
*/
static void R_MD5R_CopyIndexes( rvMD5RIndexBufferDesc &indexBuffer, const glIndex_t *indices, int numIndices ) {
	for ( int index = 0; index < numIndices; ++index ) {
		indexBuffer.indices[ index ] = indices[ index ];
	}
}

/*
===========================
R_MD5R_CalcMeshGeometryProfile

Retail rvMesh tracks aggregate geometry counts across prim batches. OpenQ4 uses
the same accounting now so later rvMesh porting can plug into already-populated
metadata instead of reparsing the MD5R text file.
===========================
*/
static void R_MD5R_CalcMeshGeometryProfile( rvMD5RMesh &mesh ) {
	mesh.numSilTraceVertices = 0;
	mesh.numSilTraceIndices = 0;
	mesh.numSilTracePrimitives = 0;
	mesh.numSilEdges = 0;
	mesh.numDrawVertices = 0;
	mesh.numDrawIndices = 0;
	mesh.numDrawPrimitives = 0;
	mesh.numTransforms = 0;

	for ( int i = 0; i < mesh.primBatches.Num(); ++i ) {
		const rvMD5RPrimBatch &primBatch = mesh.primBatches[ i ];

		if ( primBatch.hasSilTraceGeoSpec ) {
			mesh.numSilTraceVertices += primBatch.silTraceGeoSpec.vertexCount;
			mesh.numSilTraceIndices += primBatch.silTraceGeoSpec.primitiveCount * 3;
			mesh.numSilTracePrimitives += primBatch.silTraceGeoSpec.primitiveCount;
		}

		mesh.numSilEdges += primBatch.silEdgeCount;

		if ( primBatch.hasDrawGeoSpec ) {
			mesh.numDrawVertices += primBatch.drawGeoSpec.vertexCount;
			mesh.numDrawIndices += primBatch.drawGeoSpec.primitiveCount * 3;
			mesh.numDrawPrimitives += primBatch.drawGeoSpec.primitiveCount;
		}

		mesh.numTransforms += primBatch.numTransforms;
	}
}

/*
===========================
R_MD5R_TokenTypeIsFloat
===========================
*/
static bool R_MD5R_TokenTypeIsFloat( int tokenType ) {
	return ( tokenType & TT_FLOAT ) != 0;
}

/*
===========================
R_MD5R_ParseNumericAsFloat
===========================
*/
static float R_MD5R_ParseNumericAsFloat( Lexer &parser, int tokenType ) {
	if ( R_MD5R_TokenTypeIsFloat( tokenType ) ) {
		return parser.ParseFloat();
	}

	return static_cast<float>( parser.ParseInt() );
}

/*
===========================
R_MD5R_ParseNumericAsInt
===========================
*/
static int R_MD5R_ParseNumericAsInt( Lexer &parser, int tokenType ) {
	if ( R_MD5R_TokenTypeIsFloat( tokenType ) ) {
		return static_cast<int>( parser.ParseFloat() );
	}

	return parser.ParseInt();
}

/*
===========================
R_MD5R_SkipNumericValues
===========================
*/
static void R_MD5R_SkipNumericValues( Lexer &parser, int tokenType, int count ) {
	for ( int i = 0; i < count; ++i ) {
		if ( R_MD5R_TokenTypeIsFloat( tokenType ) ) {
			parser.ParseFloat();
		} else {
			parser.ParseInt();
		}
	}
}

/*
===========================
R_MD5R_ParseVertexDataType
===========================
*/
static void R_MD5R_ParseVertexDataType( Lexer &parser, int defaultTokenType, int &tokenType ) {
	idToken token;

	if ( !parser.ReadToken( &token ) ) {
		parser.Error( "Unexpected end of token stream while parsing MD5R vertex datatype" );
	}

	if ( token.Icmp( "Float" ) == 0 || token.Icmp( "Float16" ) == 0 ) {
		tokenType = TT_FLOAT;
		return;
	}

	if ( token.Icmp( "Byte" ) == 0
		|| token.Icmp( "ByteN" ) == 0
		|| token.Icmp( "Color" ) == 0
		|| token.Icmp( "DEC_10_10_10" ) == 0
		|| token.Icmp( "DEC_10_10_10N" ) == 0
		|| token.Icmp( "DEC_10_11_11" ) == 0
		|| token.Icmp( "DEC_10_11_11N" ) == 0
		|| token.Icmp( "DEC_11_11_10" ) == 0
		|| token.Icmp( "DEC_11_11_10N" ) == 0
		|| token.Icmp( "Int" ) == 0
		|| token.Icmp( "IntN" ) == 0
		|| token.Icmp( "Short" ) == 0
		|| token.Icmp( "ShortN" ) == 0
		|| token.Icmp( "UInt" ) == 0
		|| token.Icmp( "UIntN" ) == 0
		|| token.Icmp( "UByte" ) == 0
		|| token.Icmp( "UByteN" ) == 0
		|| token.Icmp( "UDec_10_10_10" ) == 0
		|| token.Icmp( "UDec_10_10_10N" ) == 0
		|| token.Icmp( "UDec_10_11_11" ) == 0
		|| token.Icmp( "UDec_10_11_11N" ) == 0
		|| token.Icmp( "UDec_11_11_10" ) == 0
		|| token.Icmp( "UDec_11_11_10N" ) == 0
		|| token.Icmp( "UShort" ) == 0
		|| token.Icmp( "UShortN" ) == 0 ) {
		tokenType = 0;
		return;
	}

	parser.UnreadToken( &token );
	tokenType = defaultTokenType;
}

/*
===========================
R_MD5R_SetPosition
===========================
*/
static void R_MD5R_SetPosition( idVec4 &value, Lexer &parser, int tokenType, int dimension ) {
	value.Zero();
	value.w = 1.0f;
	for ( int i = 0; i < dimension; ++i ) {
		value[ i ] = R_MD5R_ParseNumericAsFloat( parser, tokenType );
	}
}

/*
===========================
R_MD5R_SetSwizzledPositions
===========================
*/
static void R_MD5R_SetSwizzledPositions( idList<idVec4> &positions, Lexer &parser, int tokenType ) {
	const int numVertices = positions.Num();
	const int alignedVertexCount = ( numVertices + 3 ) & ~3;

	for ( int vertexBase = 0; vertexBase < alignedVertexCount; vertexBase += 4 ) {
		float swizzledX[ 4 ];
		float swizzledY[ 4 ];
		float swizzledZ[ 4 ];

		for ( int lane = 0; lane < 4; ++lane ) {
			swizzledX[ lane ] = R_MD5R_ParseNumericAsFloat( parser, tokenType );
		}
		for ( int lane = 0; lane < 4; ++lane ) {
			swizzledY[ lane ] = R_MD5R_ParseNumericAsFloat( parser, tokenType );
		}
		for ( int lane = 0; lane < 4; ++lane ) {
			swizzledZ[ lane ] = R_MD5R_ParseNumericAsFloat( parser, tokenType );
		}

		for ( int lane = 0; lane < 4; ++lane ) {
			const int vertexIndex = vertexBase + lane;
			if ( vertexIndex >= numVertices ) {
				break;
			}

			idVec4 &position = positions[ vertexIndex ];
			position.Zero();
			position.x = swizzledX[ lane ];
			position.y = swizzledY[ lane ];
			position.z = swizzledZ[ lane ];
			position.w = 1.0f;
		}
	}
}

/*
===========================
R_MD5R_SetVec3
===========================
*/
static void R_MD5R_SetVec3( idVec3 &value, Lexer &parser, int tokenType ) {
	value.x = R_MD5R_ParseNumericAsFloat( parser, tokenType );
	value.y = R_MD5R_ParseNumericAsFloat( parser, tokenType );
	value.z = R_MD5R_ParseNumericAsFloat( parser, tokenType );
}

/*
===========================
R_MD5R_SetTexCoord
===========================
*/
static void R_MD5R_SetTexCoord( idVec4 &value, Lexer &parser, int tokenType, int dimension ) {
	value.Zero();
	for ( int i = 0; i < dimension; ++i ) {
		value[ i ] = R_MD5R_ParseNumericAsFloat( parser, tokenType );
	}
}

/*
========================
R_MD5R_SetBlendIndex

MD5R encodes blend indices as a single packed numeric token containing up to
four byte-sized local transform indexes.
========================
*/
static void R_MD5R_SetBlendIndex( dword &value, Lexer &parser, int tokenType ) {
	value = static_cast<dword>( R_MD5R_ParseNumericAsInt( parser, tokenType ) );
}

/*
========================
R_MD5R_SetBlendWeights
========================
*/
static void R_MD5R_SetBlendWeights( idVec4 &value, Lexer &parser, int tokenType, int dimension, int numTransforms ) {
	value.Zero();

	float explicitWeightSum = 0.0f;
	for ( int i = 0; i < dimension && i < 4; ++i ) {
		value[ i ] = R_MD5R_ParseNumericAsFloat( parser, tokenType );
		explicitWeightSum += idMath::Fabs( value[ i ] );
	}

	if ( numTransforms == dimension + 1 && dimension >= 0 && dimension < 4 ) {
		value[ dimension ] = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - explicitWeightSum );
	}
}

/*
===========================
R_MD5R_ParseInterleavedVertexData
===========================
*/
static void R_MD5R_ParseInterleavedVertexData( Lexer &parser, rvMD5RVertexBufferDesc &vertexBuffer ) {
	const rvMD5RVertexFormatDesc &format = vertexBuffer.loadVertexFormat;

	for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
		if ( format.hasPosition ) {
			R_MD5R_SetPosition( vertexBuffer.positions[ vertexIndex ], parser, format.positionTokenType, format.positionDim );
		}

		if ( format.hasBlendIndex ) {
			R_MD5R_SetBlendIndex( vertexBuffer.blendIndices[ vertexIndex ], parser, format.blendIndexTokenType );
		}

		if ( format.hasBlendWeight ) {
			R_MD5R_SetBlendWeights(
				vertexBuffer.blendWeights[ vertexIndex ],
				parser,
				format.blendWeightTokenType,
				format.blendWeightDim,
				format.blendWeightTransformCount );
		}

		if ( format.hasNormal ) {
			R_MD5R_SetVec3( vertexBuffer.normals[ vertexIndex ], parser, format.normalTokenType );
		}

		if ( format.hasTangent ) {
			R_MD5R_SetVec3( vertexBuffer.tangents[ vertexIndex ], parser, format.tangentTokenType );
		}

		if ( format.hasBinormal ) {
			R_MD5R_SetVec3( vertexBuffer.binormals[ vertexIndex ], parser, format.binormalTokenType );
		}

		if ( format.hasDiffuseColor ) {
			vertexBuffer.diffuseColors[ vertexIndex ] = static_cast<dword>( R_MD5R_ParseNumericAsInt( parser, format.diffuseColorTokenType ) );
		}

		if ( format.hasSpecularColor ) {
			vertexBuffer.specularColors[ vertexIndex ] = static_cast<dword>( R_MD5R_ParseNumericAsInt( parser, format.specularColorTokenType ) );
		}

		if ( format.hasPointSize ) {
			vertexBuffer.pointSizes[ vertexIndex ] = R_MD5R_ParseNumericAsFloat( parser, format.pointSizeTokenType );
		}

		for ( int texCoordSet = 0; texCoordSet < 7; ++texCoordSet ) {
			if ( !format.hasTexCoord[ texCoordSet ] ) {
				continue;
			}

			if ( vertexBuffer.texCoords[ texCoordSet ].Num() > 0 ) {
				R_MD5R_SetTexCoord(
					vertexBuffer.texCoords[ texCoordSet ][ vertexIndex ],
					parser,
					format.texCoordTokenType[ texCoordSet ],
					format.texCoordDim[ texCoordSet ] );
			} else {
				R_MD5R_SkipNumericValues( parser, format.texCoordTokenType[ texCoordSet ], format.texCoordDim[ texCoordSet ] );
			}
		}
	}
}

/*
===========================
R_MD5R_ParseSoAVertexData
===========================
*/
static void R_MD5R_ParseSoAVertexData( Lexer &parser, rvMD5RVertexBufferDesc &vertexBuffer ) {
	const rvMD5RVertexFormatDesc &format = vertexBuffer.loadVertexFormat;

	if ( format.hasPosition ) {
		if ( format.positionSwizzled ) {
			R_MD5R_SetSwizzledPositions( vertexBuffer.positions, parser, format.positionTokenType );
		} else {
			for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
				R_MD5R_SetPosition( vertexBuffer.positions[ vertexIndex ], parser, format.positionTokenType, format.positionDim );
			}
		}
	}

	if ( format.hasBlendIndex ) {
		for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
			R_MD5R_SetBlendIndex( vertexBuffer.blendIndices[ vertexIndex ], parser, format.blendIndexTokenType );
		}
	}

	if ( format.hasBlendWeight ) {
		for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
			R_MD5R_SetBlendWeights(
				vertexBuffer.blendWeights[ vertexIndex ],
				parser,
				format.blendWeightTokenType,
				format.blendWeightDim,
				format.blendWeightTransformCount );
		}
	}

	if ( format.hasNormal ) {
		for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
			R_MD5R_SetVec3( vertexBuffer.normals[ vertexIndex ], parser, format.normalTokenType );
		}
	}

	if ( format.hasTangent ) {
		for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
			R_MD5R_SetVec3( vertexBuffer.tangents[ vertexIndex ], parser, format.tangentTokenType );
		}
	}

	if ( format.hasBinormal ) {
		for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
			R_MD5R_SetVec3( vertexBuffer.binormals[ vertexIndex ], parser, format.binormalTokenType );
		}
	}

	if ( format.hasDiffuseColor ) {
		for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
			vertexBuffer.diffuseColors[ vertexIndex ] = static_cast<dword>( R_MD5R_ParseNumericAsInt( parser, format.diffuseColorTokenType ) );
		}
	}

	if ( format.hasSpecularColor ) {
		for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
			vertexBuffer.specularColors[ vertexIndex ] = static_cast<dword>( R_MD5R_ParseNumericAsInt( parser, format.specularColorTokenType ) );
		}
	}

	if ( format.hasPointSize ) {
		for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
			vertexBuffer.pointSizes[ vertexIndex ] = R_MD5R_ParseNumericAsFloat( parser, format.pointSizeTokenType );
		}
	}

	for ( int texCoordSet = 0; texCoordSet < 7; ++texCoordSet ) {
		if ( !format.hasTexCoord[ texCoordSet ] ) {
			continue;
		}

		if ( vertexBuffer.texCoords[ texCoordSet ].Num() > 0 ) {
			for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
				R_MD5R_SetTexCoord(
					vertexBuffer.texCoords[ texCoordSet ][ vertexIndex ],
					parser,
					format.texCoordTokenType[ texCoordSet ],
					format.texCoordDim[ texCoordSet ] );
			}
		} else {
			R_MD5R_SkipNumericValues( parser, format.texCoordTokenType[ texCoordSet ], vertexBuffer.numVertices * format.texCoordDim[ texCoordSet ] );
		}
	}
}

/*
===========================
R_MD5R_CopyDrawVertices
===========================
*/
static bool R_MD5R_CopyDrawVertices( const rvMD5RVertexBufferDesc &drawVertexBuffer, const rvMD5RGeometrySpec &drawGeoSpec, idDrawVert *destDrawVerts ) {
	if ( drawGeoSpec.vertexStart < 0 || drawGeoSpec.vertexCount < 0 ) {
		return false;
	}

	if ( drawGeoSpec.vertexStart + drawGeoSpec.vertexCount > drawVertexBuffer.numVertices ) {
		return false;
	}

	if ( drawVertexBuffer.positions.Num() < drawVertexBuffer.numVertices ) {
		return false;
	}

	const bool hasNormals = ( drawVertexBuffer.normals.Num() == drawVertexBuffer.numVertices );
	const bool hasTangents = ( drawVertexBuffer.tangents.Num() == drawVertexBuffer.numVertices );
	const bool hasBinormals = ( drawVertexBuffer.binormals.Num() == drawVertexBuffer.numVertices );
	const bool hasDiffuseColors = ( drawVertexBuffer.diffuseColors.Num() == drawVertexBuffer.numVertices );
	const bool hasTexCoords = ( drawVertexBuffer.texCoords[ 0 ].Num() == drawVertexBuffer.numVertices );

	for ( int localVertexIndex = 0; localVertexIndex < drawGeoSpec.vertexCount; ++localVertexIndex ) {
		const int sourceVertexIndex = drawGeoSpec.vertexStart + localVertexIndex;
		idDrawVert &destVert = destDrawVerts[ localVertexIndex ];
		const idVec4 &sourcePosition = drawVertexBuffer.positions[ sourceVertexIndex ];

		destVert.Clear();
		destVert.xyz.Set( sourcePosition.x, sourcePosition.y, sourcePosition.z );

		if ( hasNormals ) {
			destVert.normal = drawVertexBuffer.normals[ sourceVertexIndex ];
		}
		if ( hasTangents ) {
			destVert.tangents[ 0 ] = drawVertexBuffer.tangents[ sourceVertexIndex ];
		}
		if ( hasBinormals ) {
			destVert.tangents[ 1 ] = drawVertexBuffer.binormals[ sourceVertexIndex ];
		}
		if ( hasTexCoords ) {
			destVert.st.Set(
				drawVertexBuffer.texCoords[ 0 ][ sourceVertexIndex ].x,
				drawVertexBuffer.texCoords[ 0 ][ sourceVertexIndex ].y );
		}
		if ( hasDiffuseColors ) {
			destVert.SetColor( drawVertexBuffer.diffuseColors[ sourceVertexIndex ] );
		} else {
			destVert.SetColor( 0xFFFFFFFFu );
		}
	}

	return true;
}

/*
===========================
R_MD5R_CopyDrawIndices
===========================
*/
static bool R_MD5R_CopyDrawIndices( const rvMD5RIndexBufferDesc &drawIndexBuffer, const rvMD5RGeometrySpec &drawGeoSpec, int destBase, glIndex_t *destIndices ) {
	const int numIndices = drawGeoSpec.primitiveCount * 3;

	if ( drawGeoSpec.indexStart < 0 || drawGeoSpec.primitiveCount < 0 ) {
		return false;
	}

	if ( drawGeoSpec.indexStart + numIndices > drawIndexBuffer.numIndices || drawIndexBuffer.indices.Num() < drawIndexBuffer.numIndices ) {
		return false;
	}

	for ( int localIndex = 0; localIndex < numIndices; ++localIndex ) {
		const int sourceIndex = drawIndexBuffer.indices[ drawGeoSpec.indexStart + localIndex ];
		const int remappedIndex = sourceIndex + destBase - drawGeoSpec.vertexStart;

		if ( remappedIndex < destBase || remappedIndex >= destBase + drawGeoSpec.vertexCount ) {
			return false;
		}

		destIndices[ localIndex ] = static_cast<glIndex_t>( remappedIndex );
	}

	return true;
}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
static ID_INLINE idVec3 R_MD5R_TransformPackedPosition( const float *transform, const idVec4 &position ) {
	return idVec3(
		transform[0] * position.x + transform[1] * position.y + transform[2] * position.z + transform[3] * position.w,
		transform[4] * position.x + transform[5] * position.y + transform[6] * position.z + transform[7] * position.w,
		transform[8] * position.x + transform[9] * position.y + transform[10] * position.z + transform[11] * position.w );
}

static bool R_MD5R_SkinVertexPositionFromPackedTransforms(
	const rvMD5RVertexBufferDesc &vertexBuffer,
	int sourceVertexIndex,
	const rvMD5RPrimBatch &primBatch,
	const float *skinToModelTransforms,
	int numSkinToModelTransforms,
	int transformBase,
	idVec3 &skinnedPosition ) {
	if ( skinToModelTransforms == NULL
		|| sourceVertexIndex < 0
		|| sourceVertexIndex >= vertexBuffer.numVertices
		|| vertexBuffer.positions.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	const bool hasBlendIndices = ( vertexBuffer.blendIndices.Num() == vertexBuffer.numVertices );
	const bool hasBlendWeights = ( vertexBuffer.blendWeights.Num() == vertexBuffer.numVertices );
	const int primBatchTransformCount = Max( primBatch.numTransforms, 1 );
	const idVec4 &sourcePosition = vertexBuffer.positions[ sourceVertexIndex ];
	const dword packedBlendIndices = hasBlendIndices ? vertexBuffer.blendIndices[ sourceVertexIndex ] : 0u;

	idVec4 blendWeights;
	blendWeights.Zero();
	blendWeights.x = 1.0f;
	if ( hasBlendWeights ) {
		blendWeights = vertexBuffer.blendWeights[ sourceVertexIndex ];
	}

	skinnedPosition.Zero();
	float totalWeight = 0.0f;

	for ( int influenceIndex = 0; influenceIndex < 4; ++influenceIndex ) {
		const float weight = idMath::Fabs( blendWeights[ influenceIndex ] );
		if ( weight <= 0.0f ) {
			continue;
		}

		const int localTransformIndex = hasBlendIndices ? R_MD5R_GetBlendIndex( packedBlendIndices, influenceIndex ) : 0;
		if ( localTransformIndex < 0 || localTransformIndex >= primBatchTransformCount ) {
			return false;
		}

		const int transformIndex = transformBase + localTransformIndex;
		if ( transformIndex < 0 || transformIndex >= numSkinToModelTransforms ) {
			return false;
		}

		skinnedPosition += weight * R_MD5R_TransformPackedPosition(
			skinToModelTransforms + transformIndex * 16,
			sourcePosition );
		totalWeight += weight;
	}

	if ( totalWeight <= 0.0f ) {
		const int localTransformIndex = hasBlendIndices ? R_MD5R_GetBlendIndex( packedBlendIndices, 0 ) : 0;
		if ( localTransformIndex < 0 || localTransformIndex >= primBatchTransformCount ) {
			return false;
		}

		const int transformIndex = transformBase + localTransformIndex;
		if ( transformIndex < 0 || transformIndex >= numSkinToModelTransforms ) {
			return false;
		}

		skinnedPosition = R_MD5R_TransformPackedPosition(
			skinToModelTransforms + transformIndex * 16,
			sourcePosition );
	}

	return true;
}

bool R_MD5R_CreateDecalTriangles( idRenderModelDecal *decalModel, const srfTriangles_t &sourceTri, const decalProjectionInfo_t &localInfo ) {
	if ( decalModel == NULL || sourceTri.primBatchMesh == NULL ) {
		return false;
	}

	const rvMD5RMesh *mesh = reinterpret_cast<const rvMD5RMesh *>( sourceTri.primBatchMesh );
	if ( mesh == NULL || mesh->renderModel == NULL || mesh->primBatches.Num() <= 0 ) {
		return false;
	}

	const rvRenderModelMD5R *renderModel = mesh->renderModel;
	const idList<rvMD5RVertexBufferDesc> &vertexBuffers = renderModel->GetVertexBuffers();
	const idList<rvMD5RIndexBufferDesc> &indexBuffers = renderModel->GetIndexBuffers();

	if ( mesh->silTraceIndexBuffer < 0 || mesh->silTraceIndexBuffer >= indexBuffers.Num() ) {
		return false;
	}

	const rvMD5RIndexBufferDesc &silTraceIndexBuffer = indexBuffers[ mesh->silTraceIndexBuffer ];
	const rvMD5RVertexBufferDesc *silTraceVertexBuffer = NULL;
	if ( mesh->silTraceVertexBuffer >= 0 && mesh->silTraceVertexBuffer < vertexBuffers.Num() ) {
		const rvMD5RVertexBufferDesc &decodedSilTraceVertexBuffer = vertexBuffers[ mesh->silTraceVertexBuffer ];
		if ( decodedSilTraceVertexBuffer.positions.Num() == decodedSilTraceVertexBuffer.numVertices ) {
			silTraceVertexBuffer = &decodedSilTraceVertexBuffer;
		}
	}

	const bool usePackedSkinning =
		( sourceTri.skinToModelTransforms != NULL
			&& sourceTri.numSkinToModelTransforms > 0
			&& silTraceVertexBuffer != NULL );

	// Retail rvMesh::CreateDecalTriangles walks the compact sil-trace topology.
	// OpenQ4's tri->silTraceVerts on packed surfaces mirrors the materialized
	// draw-vertex order instead, so only use the true packed sil-trace buffers
	// here and let the classic fallback handle anything else.
	if ( silTraceVertexBuffer == NULL ) {
		return false;
	}

	const idPlane *facePlanes = ( sourceTri.facePlanes != NULL && sourceTri.facePlanesCalculated ) ? sourceTri.facePlanes : NULL;
	const int totalFacePlanes = sourceTri.numIndexes / 3;
	int facePlaneBase = 0;
	int transformBase = 0;

	for ( int primBatchIndex = 0; primBatchIndex < mesh->primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
		if ( !primBatch.hasSilTraceGeoSpec ) {
			return false;
		}

		const int primBatchVertCount = primBatch.silTraceGeoSpec.vertexCount;
		const int primBatchIndexCount = primBatch.silTraceGeoSpec.primitiveCount * 3;
		const int primBatchTransformCount = Max( primBatch.numTransforms, 1 );

		if ( primBatchVertCount <= 0 || primBatchIndexCount <= 0
			|| primBatch.silTraceGeoSpec.indexStart < 0
			|| primBatch.silTraceGeoSpec.indexStart + primBatchIndexCount > silTraceIndexBuffer.numIndices
			|| silTraceIndexBuffer.indices.Num() < silTraceIndexBuffer.numIndices ) {
			return false;
		}

		const idPlane *batchFacePlanes = NULL;
		if ( facePlanes != NULL ) {
			if ( facePlaneBase + primBatch.silTraceGeoSpec.primitiveCount > totalFacePlanes ) {
				return false;
			}
			batchFacePlanes = facePlanes + facePlaneBase;
		}

		rvSilTraceVertT *batchSilTraceVerts = (rvSilTraceVertT *)_alloca16( primBatchVertCount * sizeof( batchSilTraceVerts[0] ) );
		if ( usePackedSkinning ) {
			if ( transformBase + primBatchTransformCount > sourceTri.numSkinToModelTransforms
				|| primBatch.silTraceGeoSpec.vertexStart < 0
				|| primBatch.silTraceGeoSpec.vertexStart + primBatchVertCount > silTraceVertexBuffer->numVertices ) {
				return false;
			}

			for ( int localVertexIndex = 0; localVertexIndex < primBatchVertCount; ++localVertexIndex ) {
				const int sourceVertexIndex = primBatch.silTraceGeoSpec.vertexStart + localVertexIndex;
				idVec3 skinnedPosition;
				if ( !R_MD5R_SkinVertexPositionFromPackedTransforms(
						*silTraceVertexBuffer,
						sourceVertexIndex,
						primBatch,
						sourceTri.skinToModelTransforms,
						sourceTri.numSkinToModelTransforms,
						transformBase,
						skinnedPosition ) ) {
					return false;
				}

				batchSilTraceVerts[ localVertexIndex ].xyzw.Set(
					skinnedPosition.x,
					skinnedPosition.y,
					skinnedPosition.z,
					1.0f );
			}
		} else {
			if ( primBatch.silTraceGeoSpec.vertexStart < 0
				|| primBatch.silTraceGeoSpec.vertexStart + primBatchVertCount > silTraceVertexBuffer->numVertices ) {
				return false;
			}

			for ( int localVertexIndex = 0; localVertexIndex < primBatchVertCount; ++localVertexIndex ) {
				const idVec4 &position = silTraceVertexBuffer->positions[ primBatch.silTraceGeoSpec.vertexStart + localVertexIndex ];
				batchSilTraceVerts[ localVertexIndex ].xyzw = position;
			}
		}

		const int cullByteCount = ( primBatchVertCount + 3 ) & ~3;
		byte *cullBits = (byte *)_alloca16( cullByteCount );
		SIMDProcessor->DecalPointCull( cullBits, localInfo.boundingPlanes, batchSilTraceVerts, primBatchVertCount );

		for ( int indexBase = 0, triNum = 0; indexBase < primBatchIndexCount; indexBase += 3, ++triNum ) {
			const int v1 = silTraceIndexBuffer.indices[ primBatch.silTraceGeoSpec.indexStart + indexBase + 0 ];
			const int v2 = silTraceIndexBuffer.indices[ primBatch.silTraceGeoSpec.indexStart + indexBase + 1 ];
			const int v3 = silTraceIndexBuffer.indices[ primBatch.silTraceGeoSpec.indexStart + indexBase + 2 ];

			if ( v1 < 0 || v2 < 0 || v3 < 0
				|| v1 >= primBatchVertCount || v2 >= primBatchVertCount || v3 >= primBatchVertCount ) {
				return false;
			}

			if ( cullBits[v1] & cullBits[v2] & cullBits[v3] ) {
				continue;
			}

			if ( batchFacePlanes != NULL ) {
				const float facing = batchFacePlanes[triNum].Normal() * localInfo.boundingPlanes[NUM_DECAL_BOUNDING_PLANES - 2].Normal();
				if ( facing < localInfo.maxAngle ) {
					continue;
				}
			}

			idFixedWinding fw;
			fw.SetNumPoints( 3 );
			const int localIndices[3] = { v1, v2, v3 };
			for ( int pointNum = 0; pointNum < 3; ++pointNum ) {
				const idVec3 position = batchSilTraceVerts[ localIndices[pointNum] ].xyzw.ToVec3();
				fw[pointNum] = position;

				if ( localInfo.parallel ) {
					fw[pointNum].s = localInfo.textureAxis[0].Distance( position );
					fw[pointNum].t = localInfo.textureAxis[1].Distance( position );
				} else {
					const idVec3 dir = position - localInfo.projectionOrigin;
					float scale = 0.0f;
					localInfo.boundingPlanes[NUM_DECAL_BOUNDING_PLANES - 1].RayIntersection( position, dir, scale );
					const idVec3 projectedPoint = position + scale * dir;
					fw[pointNum].s = localInfo.textureAxis[0].Distance( projectedPoint );
					fw[pointNum].t = localInfo.textureAxis[1].Distance( projectedPoint );
				}
			}

			const int orBits = cullBits[v1] | cullBits[v2] | cullBits[v3];
			for ( int planeNum = 0; planeNum < NUM_DECAL_BOUNDING_PLANES; ++planeNum ) {
				if ( orBits & ( 1 << planeNum ) ) {
					if ( !fw.ClipInPlace( -localInfo.boundingPlanes[planeNum] ) ) {
						break;
					}
				}
			}

			if ( fw.GetNumPoints() != 0 ) {
				decalModel->AddDepthFadedWinding( fw, localInfo.material, localInfo.fadePlanes, localInfo.fadeDepth, localInfo.startTime );
			}
		}

		facePlaneBase += primBatch.silTraceGeoSpec.primitiveCount;
		transformBase += primBatchTransformCount;
	}

	return true;
}
#endif

/*
===========================
R_MD5R_DrawVertsEquivalent
===========================
*/
static bool R_MD5R_DrawVertsEquivalent( const idDrawVert &lhs, const idDrawVert &rhs ) {
	static const float epsilon = 1.0e-5f;

	return lhs.xyz.Compare( rhs.xyz, epsilon )
		&& lhs.normal.Compare( rhs.normal, epsilon )
		&& lhs.tangents[ 0 ].Compare( rhs.tangents[ 0 ], epsilon )
		&& lhs.tangents[ 1 ].Compare( rhs.tangents[ 1 ], epsilon )
		&& lhs.st.Compare( rhs.st, epsilon )
		&& memcmp( lhs.color, rhs.color, sizeof( lhs.color ) ) == 0;
}

/*
===========================
R_MD5R_CopyPrimBatchTrianglesFromMesh
===========================
*/
bool rvRenderModelMD5R::CopyPrimBatchTriangles( const rvMD5RMesh &mesh, idDrawVert *destDrawVerts, glIndex_t *destIndices, const rvSilTraceVertT *silTraceVerts ) const {
	if ( mesh.renderModel == NULL || destDrawVerts == NULL || destIndices == NULL ) {
		return false;
	}

	if ( mesh.renderModel != this ) {
		return false;
	}

	const idList<rvMD5RVertexBufferDesc> &vertexBuffers = GetVertexBuffers();
	const idList<rvMD5RIndexBufferDesc> &indexBuffers = GetIndexBuffers();

	if ( mesh.drawVertexBuffer < 0 || mesh.drawVertexBuffer >= vertexBuffers.Num()
		|| mesh.drawIndexBuffer < 0 || mesh.drawIndexBuffer >= indexBuffers.Num()
		|| mesh.silTraceIndexBuffer < 0 || mesh.silTraceIndexBuffer >= indexBuffers.Num() ) {
		return false;
	}

	const rvMD5RVertexBufferDesc &drawVertexBuffer = vertexBuffers[ mesh.drawVertexBuffer ];
	const rvMD5RIndexBufferDesc &drawIndexBuffer = indexBuffers[ mesh.drawIndexBuffer ];
	const rvMD5RIndexBufferDesc &silTraceIndexBuffer = indexBuffers[ mesh.silTraceIndexBuffer ];
	const rvMD5RVertexBufferDesc *silTraceVertexBuffer = NULL;
	if ( mesh.silTraceVertexBuffer >= 0 && mesh.silTraceVertexBuffer < vertexBuffers.Num() ) {
		const rvMD5RVertexBufferDesc &decodedSilTraceVertexBuffer = vertexBuffers[ mesh.silTraceVertexBuffer ];
		if ( decodedSilTraceVertexBuffer.positions.Num() == decodedSilTraceVertexBuffer.numVertices ) {
			silTraceVertexBuffer = &decodedSilTraceVertexBuffer;
		}
	}
	if ( silTraceVertexBuffer == NULL && silTraceVerts == NULL ) {
		return false;
	}

	int destVertexBase = 0;
	int destIndexBase = 0;
	const rvSilTraceVertT *currentSilTraceVerts = silTraceVerts;

	for ( int primBatchIndex = 0; primBatchIndex < mesh.primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh.primBatches[ primBatchIndex ];
		if ( !primBatch.hasDrawGeoSpec || !primBatch.hasSilTraceGeoSpec ) {
			return false;
		}

		const int numDrawIndices = primBatch.drawGeoSpec.primitiveCount * 3;
		const int numSilTraceIndices = primBatch.silTraceGeoSpec.primitiveCount * 3;
		if ( numDrawIndices != numSilTraceIndices ) {
			return false;
		}

		if ( primBatch.drawGeoSpec.vertexStart < 0 || primBatch.drawGeoSpec.vertexCount < 0
			|| primBatch.drawGeoSpec.vertexStart + primBatch.drawGeoSpec.vertexCount > drawVertexBuffer.numVertices
			|| primBatch.drawGeoSpec.indexStart < 0 || primBatch.drawGeoSpec.indexStart + numDrawIndices > drawIndexBuffer.numIndices
			|| primBatch.silTraceGeoSpec.indexStart < 0 || primBatch.silTraceGeoSpec.indexStart + numSilTraceIndices > silTraceIndexBuffer.numIndices ) {
			return false;
		}

		if ( !R_MD5R_CopyDrawVertices( drawVertexBuffer, primBatch.drawGeoSpec, destDrawVerts + destVertexBase )
			|| !R_MD5R_CopyDrawIndices( drawIndexBuffer, primBatch.drawGeoSpec, destVertexBase, destIndices + destIndexBase ) ) {
			return false;
		}

		for ( int localIndex = 0; localIndex < numDrawIndices; ++localIndex ) {
			const int drawVertexIndex = drawIndexBuffer.indices[ primBatch.drawGeoSpec.indexStart + localIndex ] - primBatch.drawGeoSpec.vertexStart;
			const int silTraceVertexIndex = silTraceIndexBuffer.indices[ primBatch.silTraceGeoSpec.indexStart + localIndex ];
			if ( drawVertexIndex < 0 || drawVertexIndex >= primBatch.drawGeoSpec.vertexCount
				|| silTraceVertexIndex < 0 || silTraceVertexIndex >= primBatch.silTraceGeoSpec.vertexCount ) {
				return false;
			}

			const int destVertexIndex = destVertexBase + drawVertexIndex;
			idDrawVert &destVert = destDrawVerts[ destVertexIndex ];
			if ( silTraceVertexBuffer != NULL ) {
				const int sourceSilTraceVertexIndex = primBatch.silTraceGeoSpec.vertexStart + silTraceVertexIndex;
				if ( sourceSilTraceVertexIndex < 0 || sourceSilTraceVertexIndex >= silTraceVertexBuffer->numVertices ) {
					return false;
				}

				const idVec4 &silTracePosition = silTraceVertexBuffer->positions[ sourceSilTraceVertexIndex ];
				destVert.xyz.Set( silTracePosition.x, silTracePosition.y, silTracePosition.z );
			} else {
				destVert.xyz = currentSilTraceVerts[ silTraceVertexIndex ].xyzw.ToVec3();
			}
		}

		destVertexBase += primBatch.drawGeoSpec.vertexCount;
		destIndexBase += numDrawIndices;
		if ( silTraceVertexBuffer == NULL ) {
			currentSilTraceVerts += primBatch.silTraceGeoSpec.vertexCount;
		}
	}

	return destVertexBase == mesh.numDrawVertices && destIndexBase == mesh.numDrawIndices;
}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
/*
===========================
R_MD5R_GetMeshForTri
===========================
*/
const rvMD5RMesh *R_MD5R_GetMeshForTri( const srfTriangles_t *tri ) {
	if ( tri == NULL || tri->primBatchMesh == NULL ) {
		return NULL;
	}

	const rvMD5RMesh *mesh = reinterpret_cast<const rvMD5RMesh *>( tri->primBatchMesh );
	if ( mesh->renderModel == NULL ) {
		return NULL;
	}

	return mesh;
}

/*
===========================
R_MD5R_GetDrawVertexBufferForTri
===========================
*/
const rvMD5RVertexBufferDesc *R_MD5R_GetDrawVertexBufferForTri( const srfTriangles_t *tri ) {
	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( tri );
	if ( mesh == NULL || mesh->drawVertexBuffer < 0 ) {
		return NULL;
	}

	const rvRenderModelMD5R *renderModel = mesh->renderModel;
	const idList<rvMD5RVertexBufferDesc> &vertexBuffers = renderModel->GetVertexBuffers();
	if ( mesh->drawVertexBuffer >= vertexBuffers.Num() ) {
		return NULL;
	}

	return &vertexBuffers[ mesh->drawVertexBuffer ];
}

/*
===========================
R_MD5R_GetShadowVertexBufferForTri
===========================
*/
const rvMD5RVertexBufferDesc *R_MD5R_GetShadowVertexBufferForTri( const srfTriangles_t *tri ) {
	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( tri );
	if ( mesh == NULL || mesh->shadowVolVertexBuffer < 0 ) {
		return NULL;
	}

	const rvRenderModelMD5R *renderModel = mesh->renderModel;
	const idList<rvMD5RVertexBufferDesc> &vertexBuffers = renderModel->GetVertexBuffers();
	if ( mesh->shadowVolVertexBuffer >= vertexBuffers.Num() ) {
		return NULL;
	}

	return &vertexBuffers[ mesh->shadowVolVertexBuffer ];
}

/*
===========================
R_MD5R_GetSilhouetteEdgeListForTri
===========================
*/
const idList<silEdge_t> *R_MD5R_GetSilhouetteEdgeListForTri( const srfTriangles_t *tri ) {
	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( tri );
	if ( mesh == NULL || mesh->renderModel == NULL ) {
		return NULL;
	}

	return &mesh->renderModel->GetSilhouetteEdges();
}

/*
===========================
R_MD5R_GetSilTraceIndexBufferForTri
===========================
*/
const rvMD5RIndexBufferDesc *R_MD5R_GetSilTraceIndexBufferForTri( const srfTriangles_t *tri ) {
	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( tri );
	if ( mesh == NULL || mesh->silTraceIndexBuffer < 0 ) {
		return NULL;
	}

	const rvRenderModelMD5R *renderModel = mesh->renderModel;
	const idList<rvMD5RIndexBufferDesc> &indexBuffers = renderModel->GetIndexBuffers();
	if ( mesh->silTraceIndexBuffer >= indexBuffers.Num() ) {
		return NULL;
	}

	return &indexBuffers[ mesh->silTraceIndexBuffer ];
}

/*
===========================
R_MD5R_GetDrawIndexBufferForTri
===========================
*/
const rvMD5RIndexBufferDesc *R_MD5R_GetDrawIndexBufferForTri( const srfTriangles_t *tri ) {
	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( tri );
	if ( mesh == NULL || mesh->drawIndexBuffer < 0 ) {
		return NULL;
	}

	const rvRenderModelMD5R *renderModel = mesh->renderModel;
	const idList<rvMD5RIndexBufferDesc> &indexBuffers = renderModel->GetIndexBuffers();
	if ( mesh->drawIndexBuffer >= indexBuffers.Num() ) {
		return NULL;
	}

	return &indexBuffers[ mesh->drawIndexBuffer ];
}

/*
===========================
R_MD5R_GetShadowIndexBufferForTri
===========================
*/
const rvMD5RIndexBufferDesc *R_MD5R_GetShadowIndexBufferForTri( const srfTriangles_t *tri ) {
	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( tri );
	if ( mesh == NULL || mesh->shadowVolIndexBuffer < 0 ) {
		return NULL;
	}

	const rvRenderModelMD5R *renderModel = mesh->renderModel;
	const idList<rvMD5RIndexBufferDesc> &indexBuffers = renderModel->GetIndexBuffers();
	if ( mesh->shadowVolIndexBuffer >= indexBuffers.Num() ) {
		return NULL;
	}

	return &indexBuffers[ mesh->shadowVolIndexBuffer ];
}

/*
========================
R_MD5R_CreateLightTris
========================
*/
bool R_MD5R_CreateLightTris(
	const srfTriangles_t &sourceTri,
	srfTriangles_t *destTri,
	int &c_backfaced,
	int &c_distance,
	const byte *facing,
	const byte *cullBits,
	bool includeBackFaces ) {
	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( &sourceTri );
	if ( mesh == NULL || destTri == NULL || sourceTri.silTraceVerts == NULL || mesh->primBatches.Num() <= 0 ) {
		return false;
	}

	const rvMD5RIndexBufferDesc *silTraceIndexBuffer = R_MD5R_GetSilTraceIndexBufferForTri( &sourceTri );
	const rvMD5RIndexBufferDesc *drawIndexBuffer = R_MD5R_GetDrawIndexBufferForTri( &sourceTri );
	if ( silTraceIndexBuffer == NULL || drawIndexBuffer == NULL ) {
		return false;
	}

	if ( silTraceIndexBuffer->numIndices <= 0
		|| drawIndexBuffer->numIndices <= 0
		|| silTraceIndexBuffer->indices.Num() != silTraceIndexBuffer->numIndices
		|| drawIndexBuffer->indices.Num() != drawIndexBuffer->numIndices ) {
		return false;
	}

	for ( int primBatchIndex = 0; primBatchIndex < mesh->primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
		if ( !primBatch.hasSilTraceGeoSpec || !primBatch.hasDrawGeoSpec ) {
			return false;
		}

		const int batchSilTraceIndexCount = primBatch.silTraceGeoSpec.primitiveCount * 3;
		const int batchDrawIndexCount = primBatch.drawGeoSpec.primitiveCount * 3;
		if ( primBatch.silTraceGeoSpec.vertexCount < 0
			|| primBatch.drawGeoSpec.vertexCount < 0
			|| batchSilTraceIndexCount < 0
			|| batchDrawIndexCount < 0
			|| primBatch.silTraceGeoSpec.vertexStart < 0
			|| primBatch.drawGeoSpec.vertexStart < 0
			|| primBatch.silTraceGeoSpec.indexStart < 0
			|| primBatch.drawGeoSpec.indexStart < 0
			|| primBatch.silTraceGeoSpec.indexStart + batchSilTraceIndexCount > silTraceIndexBuffer->numIndices
			|| primBatch.drawGeoSpec.indexStart + batchDrawIndexCount > drawIndexBuffer->numIndices ) {
			return false;
		}
	}

	if ( !includeBackFaces && facing == NULL ) {
		return false;
	}

	const int numPrimBatches = mesh->primBatches.Num();
	const int maxIndexCount = Max( mesh->numSilTraceIndices, mesh->numDrawIndices );
	R_AllocStaticTriSurfIndexes( destTri, maxIndexCount + numPrimBatches );
	destTri->bounds.Clear();

	glIndex_t *batchHeader = destTri->indexes;
	glIndex_t *batchIndices = destTri->indexes + numPrimBatches;
	const rvSilTraceVertT *batchSilTraceVerts = reinterpret_cast<const rvSilTraceVertT *>( sourceTri.silTraceVerts );
	int totalIndexCount = 0;

	for ( int primBatchIndex = 0; primBatchIndex < numPrimBatches; ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
		const int batchTriangleCount = primBatch.silTraceGeoSpec.primitiveCount;
		const int batchSilTraceIndexCount = batchTriangleCount * 3;
		const int batchDrawIndexCount = primBatch.drawGeoSpec.primitiveCount * 3;
		const glIndex_t *batchSilTraceSource = silTraceIndexBuffer->indices.Ptr() + primBatch.silTraceGeoSpec.indexStart;
		const glIndex_t *batchDrawSource = drawIndexBuffer->indices.Ptr() + primBatch.drawGeoSpec.indexStart;
		glIndex_t *destSilTraceIndices = (glIndex_t *)_alloca16( Max( batchSilTraceIndexCount, 1 ) * sizeof( destSilTraceIndices[0] ) );
		int emittedIndexCount = 0;

		if ( batchSilTraceIndexCount != batchDrawIndexCount ) {
			R_ResizeStaticTriSurfIndexes( destTri, 0 );
			destTri->numIndexes = 0;
			return false;
		}

		for ( int triNum = 0; triNum < batchTriangleCount; ++triNum ) {
			if ( !includeBackFaces && !facing[ triNum ] ) {
				++c_backfaced;
				continue;
			}

			const int silTraceIndex0 = batchSilTraceSource[ triNum * 3 + 0 ];
			const int silTraceIndex1 = batchSilTraceSource[ triNum * 3 + 1 ];
			const int silTraceIndex2 = batchSilTraceSource[ triNum * 3 + 2 ];
			if ( silTraceIndex0 < 0 || silTraceIndex1 < 0 || silTraceIndex2 < 0
				|| silTraceIndex0 >= primBatch.silTraceGeoSpec.vertexCount
				|| silTraceIndex1 >= primBatch.silTraceGeoSpec.vertexCount
				|| silTraceIndex2 >= primBatch.silTraceGeoSpec.vertexCount ) {
				R_ResizeStaticTriSurfIndexes( destTri, 0 );
				destTri->numIndexes = 0;
				return false;
			}

			if ( cullBits != NULL && ( cullBits[ silTraceIndex0 ] & cullBits[ silTraceIndex1 ] & cullBits[ silTraceIndex2 ] ) != 0 ) {
				++c_distance;
				continue;
			}

			const int drawIndex0 = batchDrawSource[ triNum * 3 + 0 ];
			const int drawIndex1 = batchDrawSource[ triNum * 3 + 1 ];
			const int drawIndex2 = batchDrawSource[ triNum * 3 + 2 ];
			if ( drawIndex0 < 0 || drawIndex1 < 0 || drawIndex2 < 0
				|| drawIndex0 >= primBatch.drawGeoSpec.vertexCount
				|| drawIndex1 >= primBatch.drawGeoSpec.vertexCount
				|| drawIndex2 >= primBatch.drawGeoSpec.vertexCount ) {
				R_ResizeStaticTriSurfIndexes( destTri, 0 );
				destTri->numIndexes = 0;
				return false;
			}

			destSilTraceIndices[ emittedIndexCount + 0 ] = silTraceIndex0;
			destSilTraceIndices[ emittedIndexCount + 1 ] = silTraceIndex1;
			destSilTraceIndices[ emittedIndexCount + 2 ] = silTraceIndex2;
			batchIndices[ emittedIndexCount + 0 ] = drawIndex0 + primBatch.drawGeoSpec.vertexStart;
			batchIndices[ emittedIndexCount + 1 ] = drawIndex1 + primBatch.drawGeoSpec.vertexStart;
			batchIndices[ emittedIndexCount + 2 ] = drawIndex2 + primBatch.drawGeoSpec.vertexStart;
			emittedIndexCount += 3;
		}

		batchHeader[ primBatchIndex ] = emittedIndexCount;
		if ( emittedIndexCount > 0 ) {
			idBounds batchBounds;
			batchBounds.Clear();
			SIMDProcessor->MinMax( batchBounds[0], batchBounds[1], batchSilTraceVerts, destSilTraceIndices, emittedIndexCount );
			destTri->bounds.AddBounds( batchBounds );
		}

		totalIndexCount += emittedIndexCount;
		batchIndices += emittedIndexCount;
		if ( !includeBackFaces ) {
			facing += batchTriangleCount;
		}
		if ( cullBits != NULL ) {
			cullBits += primBatch.silTraceGeoSpec.vertexCount;
		}
		batchSilTraceVerts += primBatch.silTraceGeoSpec.vertexCount;
	}

	if ( totalIndexCount > 0 ) {
		R_ResizeStaticTriSurfIndexes( destTri, totalIndexCount + numPrimBatches );
	} else {
		R_ResizeStaticTriSurfIndexes( destTri, 0 );
	}
	destTri->numIndexes = totalIndexCount;
	return true;
}

/*
===========================
R_MD5R_CopyPrimBatchTriangles
===========================
*/
bool R_MD5R_CopyPrimBatchTriangles( idDrawVert *destDrawVerts, glIndex_t *destIndices, const rvMesh *primBatchMesh, const rvSilTraceVertT *silTraceVerts ) {
	if ( primBatchMesh == NULL ) {
		return false;
	}

	const rvMD5RMesh *mesh = reinterpret_cast<const rvMD5RMesh *>( primBatchMesh );
	if ( mesh->renderModel == NULL ) {
		return false;
	}

	return mesh->renderModel->CopyPrimBatchTriangles( *mesh, destDrawVerts, destIndices, silTraceVerts );
}
#endif

/*
============================
rvRenderModelMD5R::RemoveFromList
============================
*/
void rvRenderModelMD5R::RemoveFromList( rvRenderModelMD5R &model ) {
	rvRenderModelMD5R *previous = NULL;
	for ( rvRenderModelMD5R *current = modelList; current != NULL; current = current->next ) {
		if ( current != &model ) {
			previous = current;
			continue;
		}

		if ( previous != NULL ) {
			previous->next = model.next;
		} else {
			modelList = model.next;
		}
		model.next = NULL;
		return;
	}
}

/*
========================
rvRenderModelMD5R::rvRenderModelMD5R
========================
*/
rvRenderModelMD5R::rvRenderModelMD5R() :
	md5rVersion( 0 ),
	metadataOnly( false ),
	geometrySectionsSkipped( false ),
	hasSky( false ),
	source( MD5R_SOURCE_FILE ),
	next( modelList ),
	sharedVertexBuffers( NULL ),
	sharedIndexBuffers( NULL ),
	sharedSilEdges( NULL ) {
	modelList = this;
}

/*
========================
rvRenderModelMD5R::~rvRenderModelMD5R
========================
*/
rvRenderModelMD5R::~rvRenderModelMD5R() {
	RemoveFromList( *this );
}

/*
========================
rvRenderModelMD5R::GetVertexBuffers
========================
*/
const idList<rvMD5RVertexBufferDesc> &rvRenderModelMD5R::GetVertexBuffers() const {
	return ( sharedVertexBuffers != NULL ) ? *sharedVertexBuffers : vertexBuffers;
}

/*
========================
rvRenderModelMD5R::GetIndexBuffers
========================
*/
const idList<rvMD5RIndexBufferDesc> &rvRenderModelMD5R::GetIndexBuffers() const {
	return ( sharedIndexBuffers != NULL ) ? *sharedIndexBuffers : indexBuffers;
}

/*
========================
rvRenderModelMD5R::GetSilhouetteEdges
========================
*/
const idList<silEdge_t> &rvRenderModelMD5R::GetSilhouetteEdges() const {
	return ( sharedSilEdges != NULL ) ? *sharedSilEdges : silEdges;
}

/*
========================
rvRenderModelMD5R::InitFromFile
========================
*/
void rvRenderModelMD5R::InitFromFile( const char *fileName ) {
	idRenderModelStatic::InitEmpty( fileName );
	source = MD5R_SOURCE_FILE;
	reloadable = true;
	LoadModel();
}

/*
========================
rvRenderModelMD5R::InitFromMD5Model
========================
*/
bool rvRenderModelMD5R::InitFromMD5Model( const idRenderModelMD5 &sourceModel ) {
	PurgeModel();
	idRenderModelStatic::InitEmpty( sourceModel.Name() );
	source = MD5R_SOURCE_MD5;
	reloadable = true;
	purged = false;
	md5rVersion = MD5R_VERSION;

	if ( sourceModel.IsDefaultModel()
		|| sourceModel.meshes.Num() <= 0
		|| sourceModel.joints.Num() <= 0 ) {
		return false;
	}

	joints = sourceModel.joints;
	defaultPose = sourceModel.defaultPose;
	skinSpaceToLocalMats = sourceModel.skinSpaceToLocalMats;
	bounds = sourceModel.bounds;
	hasSky = false;

	for ( int jointIndex = 0; jointIndex < joints.Num(); ++jointIndex ) {
		const idMD5Joint *sourceParent = sourceModel.joints[ jointIndex ].parent;
		if ( sourceParent == NULL ) {
			joints[ jointIndex ].parent = NULL;
			continue;
		}

		const int parentIndex = static_cast<int>( sourceParent - sourceModel.joints.Ptr() );
		if ( parentIndex < 0 || parentIndex >= jointIndex ) {
			return false;
		}

		joints[ jointIndex ].parent = &joints[ parentIndex ];
	}

	idList<idJointMat> bindPoseJoints;
	bindPoseJoints.SetNum( skinSpaceToLocalMats.Num() );
	for ( int jointIndex = 0; jointIndex < skinSpaceToLocalMats.Num(); ++jointIndex ) {
		bindPoseJoints[ jointIndex ] = skinSpaceToLocalMats[ jointIndex ];
		bindPoseJoints[ jointIndex ].Invert();
	}

	rvMD5RVertexFormatDesc vertexFormat;
	R_MD5R_InitSkinnedVertexFormat( vertexFormat );

	for ( int meshIndex = 0; meshIndex < sourceModel.meshes.Num(); ++meshIndex ) {
		const idMD5Mesh &sourceMesh = sourceModel.meshes[ meshIndex ];
		if ( sourceMesh.shader == NULL
			|| sourceMesh.deformInfo == NULL
			|| sourceMesh.baseVectors == NULL
			|| sourceMesh.weightIndex == NULL
			|| sourceMesh.scaledWeights == NULL
			|| sourceMesh.deformInfo->numOutputVerts <= 0
			|| sourceMesh.deformInfo->numIndexes <= 0 ) {
			return false;
		}

		idList<dword> sourceBlendIndices;
		idList<idVec4> sourceBlendWeights;
		const int numSourceVerts = sourceMesh.deformInfo->numSourceVerts;
		if ( numSourceVerts <= 0 || sourceMesh.weightIndex == NULL || sourceMesh.scaledWeights == NULL || sourceMesh.numWeights <= 0 ) {
			return false;
		}
		sourceBlendIndices.SetNum( numSourceVerts );
		sourceBlendWeights.SetNum( numSourceVerts );

		idList<int> jointPaletteLookup;
		jointPaletteLookup.SetNum( joints.Num() );
		for ( int jointIndex = 0; jointIndex < jointPaletteLookup.Num(); ++jointIndex ) {
			jointPaletteLookup[ jointIndex ] = -1;
		}

		rvMD5RPrimBatch primBatch;

		int weightCursor = 0;
		for ( int sourceVertexIndex = 0; sourceVertexIndex < numSourceVerts; ++sourceVertexIndex ) {
			idVec4 vertexBlendWeights;
			vertexBlendWeights.Zero();
			int vertexBlendIndices[ 4 ] = { 0, 0, 0, 0 };
			int influenceCount = 0;

			while ( weightCursor < sourceMesh.numWeights ) {
				const int jointOffset = sourceMesh.weightIndex[ weightCursor * 2 + 0 ];
				const int jointIndex = jointOffset / static_cast<int>( sizeof( idJointMat ) );
				if ( influenceCount >= 4 || jointIndex < 0 || jointIndex >= joints.Num() ) {
					return false;
				}

				int localJointIndex = jointPaletteLookup[ jointIndex ];
				if ( localJointIndex < 0 ) {
					if ( primBatch.transformPalette.Num() >= 256 ) {
						return false;
					}

					localJointIndex = primBatch.transformPalette.Num();
					primBatch.transformPalette.Append( jointIndex );
					jointPaletteLookup[ jointIndex ] = localJointIndex;
				}

				vertexBlendIndices[ influenceCount ] = localJointIndex;
				vertexBlendWeights[ influenceCount ] = sourceMesh.scaledWeights[ weightCursor ].w;

				const bool lastWeightForVertex = ( sourceMesh.weightIndex[ weightCursor * 2 + 1 ] != 0 );
				++weightCursor;
				++influenceCount;

				if ( lastWeightForVertex ) {
					break;
				}
			}

			if ( influenceCount <= 0 ) {
				return false;
			}

			sourceBlendIndices[ sourceVertexIndex ] = R_MD5R_PackBlendIndices( vertexBlendIndices );
			sourceBlendWeights[ sourceVertexIndex ] = vertexBlendWeights;
		}

		if ( weightCursor != sourceMesh.numWeights ) {
			return false;
		}

		modelSurface_t tempSurface;
		memset( &tempSurface, 0, sizeof( tempSurface ) );
		const_cast<idMD5Mesh &>( sourceMesh ).UpdateSurface( NULL, bindPoseJoints.Ptr(), &tempSurface, false );

		const srfTriangles_t *tri = tempSurface.geometry;
		if ( tri == NULL || tri->verts == NULL || tri->numVerts != sourceMesh.deformInfo->numOutputVerts ) {
			if ( tempSurface.geometry != NULL ) {
				R_FreeStaticTriSurf( tempSurface.geometry );
			}
			return false;
		}

		const int vertexBufferIndex = vertexBuffers.Num();
		const int indexBufferIndex = indexBuffers.Num();

		rvMD5RVertexBufferDesc vertexBuffer;
		R_MD5R_InitVertexBufferDesc( vertexBuffer, vertexFormat, tri->numVerts );
		for ( int vertexIndex = 0; vertexIndex < tri->numVerts; ++vertexIndex ) {
			int sourceVertexIndex = vertexIndex;
			if ( vertexIndex >= sourceMesh.deformInfo->numSourceVerts ) {
				const int mirrorIndex = vertexIndex - sourceMesh.deformInfo->numSourceVerts;
				if ( mirrorIndex < 0 || mirrorIndex >= sourceMesh.deformInfo->numMirroredVerts ) {
					R_FreeStaticTriSurf( tempSurface.geometry );
					return false;
				}
				sourceVertexIndex = sourceMesh.deformInfo->mirroredVerts[ mirrorIndex ];
			}

			if ( sourceVertexIndex < 0 || sourceVertexIndex >= sourceBlendIndices.Num() ) {
				R_FreeStaticTriSurf( tempSurface.geometry );
				return false;
			}

			vertexBuffer.positions[ vertexIndex ] = sourceMesh.baseVectors[ vertexIndex * 4 + 0 ];
			vertexBuffer.blendIndices[ vertexIndex ] = sourceBlendIndices[ sourceVertexIndex ];
			vertexBuffer.blendWeights[ vertexIndex ] = sourceBlendWeights[ sourceVertexIndex ];
			vertexBuffer.texCoords[ 0 ][ vertexIndex ].Set( tri->verts[ vertexIndex ].st.x, tri->verts[ vertexIndex ].st.y, 0.0f, 0.0f );
		}
		vertexBuffers.Append( vertexBuffer );

		rvMD5RIndexBufferDesc indexBuffer;
		R_MD5R_InitIndexBufferDesc( indexBuffer, sourceMesh.deformInfo->numIndexes );
		R_MD5R_CopyIndexes( indexBuffer, sourceMesh.deformInfo->indexes, sourceMesh.deformInfo->numIndexes );
		indexBuffers.Append( indexBuffer );

		primBatch.numTransforms = primBatch.transformPalette.Num();
		if ( primBatch.numTransforms <= 0 ) {
			R_FreeStaticTriSurf( tempSurface.geometry );
			return false;
		}
		primBatch.silTraceGeoSpec.vertexStart = 0;
		primBatch.silTraceGeoSpec.vertexCount = tri->numVerts;
		primBatch.silTraceGeoSpec.indexStart = 0;
		primBatch.silTraceGeoSpec.primitiveCount = sourceMesh.deformInfo->numIndexes / 3;
		primBatch.hasSilTraceGeoSpec = true;
		primBatch.drawGeoSpec = primBatch.silTraceGeoSpec;
		primBatch.hasDrawGeoSpec = true;

		rvMD5RMesh mesh;
		mesh.renderModel = this;
		mesh.material = sourceMesh.shader;
		mesh.materialName = sourceMesh.shader->GetName();
		mesh.bounds = tri->bounds;
		mesh.meshIdentifier = meshIndex;
		mesh.silTraceVertexBuffer = vertexBufferIndex;
		mesh.silTraceIndexBuffer = indexBufferIndex;
		mesh.drawVertexBuffer = vertexBufferIndex;
		mesh.drawIndexBuffer = indexBufferIndex;
		mesh.primBatches.Append( primBatch );
		R_MD5R_CalcMeshGeometryProfile( mesh );
		meshes.Append( mesh );

		R_FreeStaticTriSurf( tempSurface.geometry );
	}

	if ( meshes.Num() <= 0 ) {
		return false;
	}

	BuildLevelsOfDetail();

	for ( int meshIndex = 0; meshIndex < meshes.Num(); ++meshIndex ) {
		if ( !BuildDynamicMeshTemplate( meshes[ meshIndex ] ) ) {
			return false;
		}
	}

	return true;
}

/*
========================
rvRenderModelMD5R::InitFromStaticModel
========================
*/
bool rvRenderModelMD5R::InitFromStaticModel( const idRenderModelStatic &sourceModel, rvMD5RSource_t sourceType ) {
	return InitFromStaticModelInternal( sourceModel, sourceType, NULL, NULL, NULL );
}

/*
========================
rvRenderModelMD5R::InitFromProcWorldStaticModel
========================
*/
bool rvRenderModelMD5R::InitFromProcWorldStaticModel(
	const idRenderModelStatic &sourceModel,
	idList<rvMD5RVertexBufferDesc> &sharedVertexBuffers,
	idList<rvMD5RIndexBufferDesc> &sharedIndexBuffers,
	idList<silEdge_t> &sharedSilEdges ) {
	return InitFromStaticModelInternal( sourceModel, MD5R_SOURCE_PROC, &sharedVertexBuffers, &sharedIndexBuffers, &sharedSilEdges );
}

/*
========================
rvRenderModelMD5R::InitFromStaticModelInternal
========================
*/
bool rvRenderModelMD5R::InitFromStaticModelInternal(
	const idRenderModelStatic &sourceModel,
	rvMD5RSource_t sourceType,
	idList<rvMD5RVertexBufferDesc> *sharedVertexBuffers,
	idList<rvMD5RIndexBufferDesc> *sharedIndexBuffers,
	idList<silEdge_t> *sharedSilEdges ) {
	PurgeModel();
	idRenderModelStatic::InitEmpty( sourceModel.Name() );
	source = sourceType;
	reloadable = ( sourceType != MD5R_SOURCE_PROC );
	purged = false;
	md5rVersion = MD5R_VERSION;
	bounds = sourceModel.bounds;
	hasSky = ( sourceType == MD5R_SOURCE_PROC ) && R_MD5R_ModelHasSky( sourceModel );
	SetProcSky( hasSky );

	idList<rvMD5RVertexBufferDesc> *targetVertexBuffers = &vertexBuffers;
	idList<rvMD5RIndexBufferDesc> *targetIndexBuffers = &indexBuffers;
	idList<silEdge_t> *targetSilEdges = &silEdges;

	if ( sharedVertexBuffers != NULL && sharedIndexBuffers != NULL && sharedSilEdges != NULL ) {
		this->sharedVertexBuffers = sharedVertexBuffers;
		this->sharedIndexBuffers = sharedIndexBuffers;
		this->sharedSilEdges = sharedSilEdges;
		targetVertexBuffers = sharedVertexBuffers;
		targetIndexBuffers = sharedIndexBuffers;
		targetSilEdges = sharedSilEdges;
	}

	rvMD5RVertexFormatDesc vertexFormat;
	R_MD5R_InitStaticVertexFormat( vertexFormat );

	const bool dropCollisionHelperSurfaces = R_MD5R_StaticModelHasRenderableSurfaces( sourceModel );
	int meshIdentifier = 0;
	for ( int surfaceIndex = 0; surfaceIndex < sourceModel.surfaces.Num(); ++surfaceIndex ) {
		const modelSurface_t &sourceSurface = sourceModel.surfaces[ surfaceIndex ];
		if ( !R_MD5R_ShouldKeepStaticSurface( sourceSurface, dropCollisionHelperSurfaces ) ) {
			continue;
		}
		const srfTriangles_t *tri = sourceSurface.geometry;
		if ( tri->verts == NULL || tri->indexes == NULL ) {
			continue;
		}
		if ( tri->numVerts <= 0 || tri->numIndexes <= 0 ) {
			continue;
		}

		const int vertexBufferIndex = targetVertexBuffers->Num();
		const int indexBufferIndex = targetIndexBuffers->Num();
		const int silEdgeStart = targetSilEdges->Num();

		rvMD5RVertexBufferDesc vertexBuffer;
		R_MD5R_InitVertexBufferDesc( vertexBuffer, vertexFormat, tri->numVerts );
		R_MD5R_FillStaticVertexBuffer( vertexBuffer, *tri );
		targetVertexBuffers->Append( vertexBuffer );

		rvMD5RIndexBufferDesc indexBuffer;
		R_MD5R_InitIndexBufferDesc( indexBuffer, tri->numIndexes );
		R_MD5R_CopyIndexes( indexBuffer, tri->indexes, tri->numIndexes );
		targetIndexBuffers->Append( indexBuffer );

		if ( tri->numSilEdges > 0 && tri->silEdges != NULL ) {
			for ( int silEdgeIndex = 0; silEdgeIndex < tri->numSilEdges; ++silEdgeIndex ) {
				targetSilEdges->Append( tri->silEdges[ silEdgeIndex ] );
			}
		}

		rvMD5RPrimBatch primBatch;
		primBatch.silTraceGeoSpec.vertexStart = 0;
		primBatch.silTraceGeoSpec.vertexCount = tri->numVerts;
		primBatch.silTraceGeoSpec.indexStart = 0;
		primBatch.silTraceGeoSpec.primitiveCount = tri->numIndexes / 3;
		primBatch.hasSilTraceGeoSpec = true;
		primBatch.drawGeoSpec = primBatch.silTraceGeoSpec;
		primBatch.hasDrawGeoSpec = true;
		if ( tri->numSilEdges > 0 ) {
			primBatch.silEdgeStart = silEdgeStart;
			primBatch.silEdgeCount = tri->numSilEdges;
		}

		rvMD5RMesh mesh;
		mesh.renderModel = this;
		mesh.material = sourceSurface.shader;
		mesh.materialName = sourceSurface.shader->GetName();
		mesh.bounds = tri->bounds;
		mesh.meshIdentifier = meshIdentifier++;
		mesh.silTraceVertexBuffer = vertexBufferIndex;
		mesh.silTraceIndexBuffer = indexBufferIndex;
		mesh.drawVertexBuffer = vertexBufferIndex;
		mesh.drawIndexBuffer = indexBufferIndex;
		mesh.primBatches.Append( primBatch );
		R_MD5R_CalcMeshGeometryProfile( mesh );
		meshes.Append( mesh );
	}

	if ( meshes.Num() <= 0 ) {
		return false;
	}

	BuildLevelsOfDetail();
	if ( !GenerateStaticSurfaces() || geometrySectionsSkipped ) {
		return false;
	}

	return true;
}

/*
========================
rvRenderModelMD5R::InitFromProcWorldModel
========================
*/
void rvRenderModelMD5R::InitFromProcWorldModel(
	Lexer &parser,
	const idList<rvMD5RVertexBufferDesc> &sharedVertexBuffers,
	const idList<rvMD5RIndexBufferDesc> &sharedIndexBuffers,
	const idList<silEdge_t> &sharedSilEdges ) {
	PurgeModel();
	purged = false;
	reloadable = false;
	source = MD5R_SOURCE_PROC;
	md5rVersion = MD5R_VERSION;
	this->sharedVertexBuffers = &sharedVertexBuffers;
	this->sharedIndexBuffers = &sharedIndexBuffers;
	this->sharedSilEdges = &sharedSilEdges;

	parser.ExpectTokenString( "Mesh" );
	ParseMeshes( parser );
	BuildLevelsOfDetail();

	bounds.Clear();
	if ( parser.PeekTokenString( "Bounds" ) ) {
		parser.ExpectTokenString( "Bounds" );
		R_MD5R_ParseFlexibleBounds( parser, bounds );
	}

	if ( parser.PeekTokenString( "HasSky" ) ) {
		parser.ExpectTokenString( "HasSky" );
		hasSky = true;
		SetProcSky( true );
	}

	if ( joints.Num() == 0 && GetVertexBuffers().Num() > 0 && meshes.Num() > 0 ) {
		if ( !GenerateStaticSurfaces() ) {
			metadataOnly = true;
			geometrySectionsSkipped = true;
			common->Warning(
				"rvRenderModelMD5R::InitFromProcWorldModel: parsed packed proc-world model '%s', but static MD5R surface generation could not decode the current buffer layout",
				name.c_str() );
		}
	}
}

/*
========================
rvRenderModelMD5R::ParseVertexFormat
========================
*/
void rvRenderModelMD5R::ParseVertexFormat( Lexer &parser, rvMD5RVertexFormatDesc &vertexFormat ) {
	idToken token;

	vertexFormat = rvMD5RVertexFormatDesc();

	parser.ExpectTokenString( "{" );
	while ( parser.ReadToken( &token ) ) {
		if ( token.Icmp( "}" ) == 0 ) {
			return;
		}

		if ( token.Icmp( "Position" ) == 0 ) {
			vertexFormat.hasPosition = true;
			vertexFormat.positionSwizzled = false;
			vertexFormat.positionDim = parser.ParseInt();
			if ( vertexFormat.positionDim < 1 || vertexFormat.positionDim > 4 ) {
				parser.Error( "Vertex format was initialized with an unsupported position dimension" );
			}
			R_MD5R_ParseVertexDataType( parser, TT_FLOAT, vertexFormat.positionTokenType );
			continue;
		}

		if ( token.Icmp( "PositionSwizzled" ) == 0 || token.Icmp( "Pos3Swizzled" ) == 0 ) {
			vertexFormat.hasPosition = true;
			vertexFormat.positionSwizzled = true;
			vertexFormat.positionDim = 3;
			R_MD5R_ParseVertexDataType( parser, TT_FLOAT, vertexFormat.positionTokenType );
			continue;
		}

		if ( token.Icmp( "BlendIndex" ) == 0 ) {
			vertexFormat.hasBlendIndex = true;
			R_MD5R_ParseVertexDataType( parser, 0, vertexFormat.blendIndexTokenType );
			continue;
		}

		if ( token.Icmp( "BlendWeight" ) == 0 ) {
			vertexFormat.hasBlendWeight = true;
			vertexFormat.blendWeightDim = parser.ParseInt();
			if ( vertexFormat.blendWeightDim < 1 || vertexFormat.blendWeightDim > 4 ) {
				parser.Error( "Vertex format was initialized with an unsupported number of blend weights" );
			}

			vertexFormat.blendWeightTransformCount = parser.ParseInt();
			if ( vertexFormat.blendWeightTransformCount < 1 ) {
				parser.Error( "Vertex format was initialized with an invalid number of blend transforms" );
			}
			if ( vertexFormat.blendWeightTransformCount > vertexFormat.blendWeightDim + 1 ) {
				parser.Error( "Vertex format was initialized with an unsupported number of blend transforms" );
			}

			R_MD5R_ParseVertexDataType( parser, TT_FLOAT, vertexFormat.blendWeightTokenType );
			continue;
		}

		if ( token.Icmp( "Normal" ) == 0 ) {
			vertexFormat.hasNormal = true;
			R_MD5R_ParseVertexDataType( parser, TT_FLOAT, vertexFormat.normalTokenType );
			continue;
		}

		if ( token.Icmp( "Tangent" ) == 0 ) {
			vertexFormat.hasTangent = true;
			R_MD5R_ParseVertexDataType( parser, TT_FLOAT, vertexFormat.tangentTokenType );
			continue;
		}

		if ( token.Icmp( "Binormal" ) == 0 ) {
			vertexFormat.hasBinormal = true;
			R_MD5R_ParseVertexDataType( parser, TT_FLOAT, vertexFormat.binormalTokenType );
			continue;
		}

		if ( token.Icmp( "DiffuseColor" ) == 0 ) {
			vertexFormat.hasDiffuseColor = true;
			R_MD5R_ParseVertexDataType( parser, 0, vertexFormat.diffuseColorTokenType );
			continue;
		}

		if ( token.Icmp( "SpecularColor" ) == 0 ) {
			vertexFormat.hasSpecularColor = true;
			R_MD5R_ParseVertexDataType( parser, 0, vertexFormat.specularColorTokenType );
			continue;
		}

		if ( token.Icmp( "PointSize" ) == 0 ) {
			vertexFormat.hasPointSize = true;
			R_MD5R_ParseVertexDataType( parser, TT_FLOAT, vertexFormat.pointSizeTokenType );
			continue;
		}

		if ( token.Icmp( "TexCoord" ) == 0 ) {
			const int dimension = parser.ParseInt();
			const int texCoordSet = parser.ParseInt();
			if ( dimension < 1 || dimension > 4 ) {
				parser.Error( "Vertex format was initialized with an unsupported texture coordinate dimension" );
			}
			if ( texCoordSet < 0 || texCoordSet >= 7 ) {
				parser.Error( "Vertex format was initialized with an unsupported texture coordinate set" );
			}

			vertexFormat.hasTexCoord[ texCoordSet ] = true;
			vertexFormat.texCoordDim[ texCoordSet ] = dimension;
			R_MD5R_ParseVertexDataType( parser, TT_FLOAT, vertexFormat.texCoordTokenType[ texCoordSet ] );
			continue;
		}

		parser.Error( "Expected vertex format keyword" );
	}

	parser.Error( "Unexpected end of token stream while parsing MD5R vertex format" );
}

/*
========================
rvRenderModelMD5R::ParseVertexBuffer
========================
*/
void rvRenderModelMD5R::ParseVertexBuffer( Lexer &parser, rvMD5RVertexBufferDesc &vertexBuffer ) {
	idToken token;

	vertexBuffer = rvMD5RVertexBufferDesc();

	parser.ExpectTokenString( "{" );
	while ( parser.ReadToken( &token ) ) {
		if ( token.Icmp( "VertexFormat" ) == 0 ) {
			vertexBuffer.hasVertexFormat = true;
			ParseVertexFormat( parser, vertexBuffer.vertexFormat );
			continue;
		}

		if ( token.Icmp( "LoadVertexFormat" ) == 0 ) {
			vertexBuffer.hasLoadVertexFormat = true;
			ParseVertexFormat( parser, vertexBuffer.loadVertexFormat );
			continue;
		}

		if ( token.Icmp( "SystemMemory" ) == 0 ) {
			vertexBuffer.systemMemory = true;
			continue;
		}

		if ( token.Icmp( "VideoMemory" ) == 0 ) {
			vertexBuffer.videoMemory = true;
			continue;
		}

		if ( token.Icmp( "SoA" ) == 0 ) {
			vertexBuffer.soA = true;
			continue;
		}

		if ( token.Icmp( "Vertex" ) == 0 ) {
			break;
		}

		parser.Error( "Unexpected token '%s' in VertexBuffer", token.c_str() );
	}

	if ( !vertexBuffer.hasVertexFormat ) {
		parser.Error( "Expected VertexFormat block in VertexBuffer" );
	}

	if ( vertexBuffer.soA && vertexBuffer.hasLoadVertexFormat ) {
		parser.Error( "SoA is currently not supported with LoadVertexFormat" );
	}

	if ( !vertexBuffer.systemMemory && !vertexBuffer.videoMemory ) {
		vertexBuffer.videoMemory = true;
	}

	if ( !vertexBuffer.hasLoadVertexFormat ) {
		vertexBuffer.loadVertexFormat = vertexBuffer.vertexFormat;
	}

	parser.ExpectTokenString( "[" );
	vertexBuffer.numVertices = parser.ParseInt();
	parser.ExpectTokenString( "]" );
	if ( vertexBuffer.numVertices <= 0 ) {
		parser.Error( "Invalid vertex count" );
	}

	parser.ExpectTokenString( "{" );

	if ( vertexBuffer.loadVertexFormat.hasPosition ) {
		vertexBuffer.positions.SetNum( vertexBuffer.numVertices );
	}
	if ( vertexBuffer.loadVertexFormat.hasBlendIndex ) {
		vertexBuffer.blendIndices.SetNum( vertexBuffer.numVertices );
	}
	if ( vertexBuffer.loadVertexFormat.hasBlendWeight ) {
		vertexBuffer.blendWeights.SetNum( vertexBuffer.numVertices );
	}
	if ( vertexBuffer.loadVertexFormat.hasNormal ) {
		vertexBuffer.normals.SetNum( vertexBuffer.numVertices );
	}
	if ( vertexBuffer.loadVertexFormat.hasTangent ) {
		vertexBuffer.tangents.SetNum( vertexBuffer.numVertices );
	}
	if ( vertexBuffer.loadVertexFormat.hasBinormal ) {
		vertexBuffer.binormals.SetNum( vertexBuffer.numVertices );
	}
	if ( vertexBuffer.loadVertexFormat.hasDiffuseColor ) {
		vertexBuffer.diffuseColors.SetNum( vertexBuffer.numVertices );
	}
	if ( vertexBuffer.loadVertexFormat.hasSpecularColor ) {
		vertexBuffer.specularColors.SetNum( vertexBuffer.numVertices );
	}
	if ( vertexBuffer.loadVertexFormat.hasPointSize ) {
		vertexBuffer.pointSizes.SetNum( vertexBuffer.numVertices );
	}
	for ( int texCoordSet = 0; texCoordSet < 7; ++texCoordSet ) {
		if ( vertexBuffer.loadVertexFormat.hasTexCoord[ texCoordSet ] ) {
			vertexBuffer.texCoords[ texCoordSet ].SetNum( vertexBuffer.numVertices );
		}
	}

	if ( vertexBuffer.soA ) {
		R_MD5R_ParseSoAVertexData( parser, vertexBuffer );
		parser.ExpectTokenString( "}" );
	} else {
		R_MD5R_ParseInterleavedVertexData( parser, vertexBuffer );
		parser.ExpectTokenString( "}" );
	}

	parser.ExpectTokenString( "}" );
}

/*
========================
rvRenderModelMD5R::ParseVertexBuffers
========================
*/
void rvRenderModelMD5R::ParseSharedVertexBuffers( Lexer &parser, idList<rvMD5RVertexBufferDesc> &vertexBuffers ) {
	parser.ExpectTokenString( "[" );
	const int numVertexBuffers = parser.ParseInt();
	parser.ExpectTokenString( "]" );
	parser.ExpectTokenString( "{" );

	if ( numVertexBuffers < 0 ) {
		parser.Error( "Invalid MD5R vertex-buffer count %d", numVertexBuffers );
	}

	vertexBuffers.SetNum( numVertexBuffers );
	for ( int i = 0; i < numVertexBuffers; ++i ) {
		parser.ExpectTokenString( "VertexBuffer" );
		ParseVertexBuffer( parser, vertexBuffers[ i ] );
	}

	parser.ExpectTokenString( "}" );
}

/*
========================
rvRenderModelMD5R::ParseVertexBuffers
========================
*/
void rvRenderModelMD5R::ParseVertexBuffers( Lexer &parser ) {
	ParseSharedVertexBuffers( parser, vertexBuffers );
}

/*
========================
rvRenderModelMD5R::ParseIndexBuffer
========================
*/
void rvRenderModelMD5R::ParseIndexBuffer( Lexer &parser, rvMD5RIndexBufferDesc &indexBuffer ) {
	idToken token;

	indexBuffer = rvMD5RIndexBufferDesc();

	parser.ExpectTokenString( "{" );
	while ( parser.ReadToken( &token ) ) {
		if ( token.Icmp( "SystemMemory" ) == 0 ) {
			indexBuffer.systemMemory = true;
			continue;
		}

		if ( token.Icmp( "VideoMemory" ) == 0 ) {
			indexBuffer.videoMemory = true;
			continue;
		}

		if ( token.Icmp( "BitDepth" ) == 0 ) {
			indexBuffer.bitDepth = parser.ParseInt();
			continue;
		}

		if ( token.Icmp( "Index" ) == 0 ) {
			break;
		}

		parser.Error( "Unexpected token '%s' in IndexBuffer", token.c_str() );
	}

	if ( !indexBuffer.systemMemory && !indexBuffer.videoMemory ) {
		indexBuffer.videoMemory = true;
	}

	if ( indexBuffer.bitDepth != 16 && indexBuffer.bitDepth != 32 ) {
		parser.Error( "Invalid index-buffer bit depth %d", indexBuffer.bitDepth );
	}

	parser.ExpectTokenString( "[" );
	indexBuffer.numIndices = parser.ParseInt();
	parser.ExpectTokenString( "]" );
	if ( indexBuffer.numIndices <= 0 ) {
		parser.Error( "Invalid index count" );
	}

	parser.ExpectTokenString( "{" );
	indexBuffer.indices.SetNum( indexBuffer.numIndices );
	for ( int i = 0; i < indexBuffer.numIndices; ++i ) {
		indexBuffer.indices[ i ] = static_cast<glIndex_t>( parser.ParseInt() );
	}
	parser.ExpectTokenString( "}" );
	parser.ExpectTokenString( "}" );
}

/*
========================
rvRenderModelMD5R::ParseIndexBuffers
========================
*/
void rvRenderModelMD5R::ParseSharedIndexBuffers( Lexer &parser, idList<rvMD5RIndexBufferDesc> &indexBuffers ) {
	parser.ExpectTokenString( "[" );
	const int numIndexBuffers = parser.ParseInt();
	parser.ExpectTokenString( "]" );
	parser.ExpectTokenString( "{" );

	if ( numIndexBuffers < 0 ) {
		parser.Error( "Invalid MD5R index-buffer count %d", numIndexBuffers );
	}

	indexBuffers.SetNum( numIndexBuffers );
	for ( int i = 0; i < numIndexBuffers; ++i ) {
		parser.ExpectTokenString( "IndexBuffer" );
		ParseIndexBuffer( parser, indexBuffers[ i ] );
	}

	parser.ExpectTokenString( "}" );
}

/*
========================
rvRenderModelMD5R::ParseIndexBuffers
========================
*/
void rvRenderModelMD5R::ParseIndexBuffers( Lexer &parser ) {
	ParseSharedIndexBuffers( parser, indexBuffers );
}

/*
========================
rvRenderModelMD5R::ParseSilhouetteEdges
========================
*/
void rvRenderModelMD5R::ParseSharedSilhouetteEdges( Lexer &parser, idList<silEdge_t> &silEdges ) {
	parser.ExpectTokenString( "[" );
	const int numSilEdges = parser.ParseInt();
	parser.ExpectTokenString( "]" );
	parser.ExpectTokenString( "{" );

	if ( numSilEdges < 0 ) {
		parser.Error( "Invalid MD5R silhouette-edge count %d", numSilEdges );
	}

	silEdges.SetNum( numSilEdges );
	for ( int i = 0; i < numSilEdges; ++i ) {
		silEdges[ i ].p1 = parser.ParseInt();
		silEdges[ i ].p2 = parser.ParseInt();
		silEdges[ i ].v1 = parser.ParseInt();
		silEdges[ i ].v2 = parser.ParseInt();
	}

	parser.ExpectTokenString( "}" );
}

/*
========================
rvRenderModelMD5R::ParseSilhouetteEdges
========================
*/
void rvRenderModelMD5R::ParseSilhouetteEdges( Lexer &parser ) {
	ParseSharedSilhouetteEdges( parser, silEdges );
}

/*
========================
rvRenderModelMD5R::ParseLevelOfDetail
========================
*/
void rvRenderModelMD5R::ParseLevelOfDetail( Lexer &parser ) {
	parser.ExpectTokenString( "[" );
	const int numLODs = parser.ParseInt();
	parser.ExpectTokenString( "]" );
	parser.ExpectTokenString( "{" );

	if ( numLODs < 0 ) {
		parser.Error( "Invalid MD5R LOD count %d", numLODs );
	}

	lods.SetNum( numLODs );
	for ( int i = 0; i < numLODs; ++i ) {
		lods[ i ].rangeEnd = parser.ParseFloat();
		lods[ i ].rangeEndSquared = lods[ i ].rangeEnd * lods[ i ].rangeEnd;
	}

	parser.ExpectTokenString( "}" );
}

/*
========================
rvRenderModelMD5R::ParsePrimBatch
========================
*/
void rvRenderModelMD5R::ParsePrimBatch( Lexer &parser, rvMD5RPrimBatch &primBatch ) {
	idToken token;

	primBatch = rvMD5RPrimBatch();

	parser.ExpectTokenString( "{" );
	if ( !parser.ReadToken( &token ) ) {
		parser.Error( "Expected Transform or geometry keyword" );
	}

	if ( token.Icmp( "Transform" ) == 0 ) {
		parser.ExpectTokenString( "[" );
		primBatch.numTransforms = parser.ParseInt();
		parser.ExpectTokenString( "]" );
		parser.ExpectTokenString( "{" );

		if ( primBatch.numTransforms < 0 ) {
			parser.Error( "Primitive batch initialization failed - invalid transform count %d", primBatch.numTransforms );
		}

		primBatch.transformPalette.SetNum( primBatch.numTransforms );
		for ( int i = 0; i < primBatch.numTransforms; ++i ) {
			primBatch.transformPalette[ i ] = parser.ParseInt();
		}

		parser.ExpectTokenString( "}" );
		if ( !parser.ReadToken( &token ) ) {
			parser.Error( "Expected primitive-batch geometry keyword" );
		}
	}

	if ( token.Icmp( "SilTraceIndexedTriList" ) == 0 ) {
		primBatch.silTraceGeoSpec.vertexStart = parser.ParseInt();
		primBatch.silTraceGeoSpec.vertexCount = parser.ParseInt();
		primBatch.silTraceGeoSpec.indexStart = parser.ParseInt();
		primBatch.silTraceGeoSpec.primitiveCount = parser.ParseInt();
		primBatch.hasSilTraceGeoSpec = true;
		if ( !parser.ReadToken( &token ) ) {
			parser.Error( "Expected DrawIndexedTriList, ShadowVerts, ShadowIndexedTriList or }" );
		}
	}

	if ( token.Icmp( "DrawIndexedTriList" ) == 0 ) {
		primBatch.drawGeoSpec.vertexStart = parser.ParseInt();
		primBatch.drawGeoSpec.vertexCount = parser.ParseInt();
		primBatch.drawGeoSpec.indexStart = parser.ParseInt();
		primBatch.drawGeoSpec.primitiveCount = parser.ParseInt();
		primBatch.hasDrawGeoSpec = true;
		if ( !parser.ReadToken( &token ) ) {
			parser.Error( "Expected ShadowVerts, ShadowIndexedTriList or }" );
		}
	}

	if ( token.Icmp( "ShadowVerts" ) == 0 ) {
		primBatch.shadowVolGeoSpec.vertexStart = parser.ParseInt();
		primBatch.shadowVolGeoSpec.vertexCount = primBatch.silTraceGeoSpec.vertexCount * 2;
		if ( primBatch.shadowVolGeoSpec.vertexCount == 0 ) {
			parser.Error( "Primitive batch initialization failed - expected SilTraceIndexedTriList statement" );
		}
		primBatch.hasShadowGeoSpec = true;
		if ( !parser.ReadToken( &token ) ) {
			parser.Error( "Expected SilhouetteEdge or }" );
		}
	} else if ( token.Icmp( "ShadowIndexedTriList" ) == 0 ) {
		primBatch.shadowVolGeoSpec.vertexStart = parser.ParseInt();
		primBatch.shadowVolGeoSpec.vertexCount = parser.ParseInt();
		primBatch.shadowVolGeoSpec.indexStart = parser.ParseInt();
		primBatch.shadowVolGeoSpec.primitiveCount = parser.ParseInt();
		primBatch.numShadowPrimitivesNoCaps = parser.ParseInt();
		primBatch.shadowCapPlaneBits = parser.ParseInt();
		primBatch.hasShadowGeoSpec = true;
		if ( !parser.ReadToken( &token ) ) {
			parser.Error( "Expected SilhouetteEdge or }" );
		}
	}

	if ( token.Icmp( "SilhouetteEdge" ) == 0 ) {
		primBatch.silEdgeStart = parser.ParseInt();
		primBatch.silEdgeCount = parser.ParseInt();
		if ( !parser.ReadToken( &token ) ) {
			parser.Error( "Expected }" );
		}
	}

	if ( token.Icmp( "}" ) != 0 ) {
		parser.Error( "Expected }." );
	}
}

/*
========================
rvRenderModelMD5R::ParseMesh
========================
*/
void rvRenderModelMD5R::ParseMesh( Lexer &parser, int meshIndex ) {
	idToken token;
	idToken materialToken;
	rvMD5RMesh &mesh = meshes[ meshIndex ];
	const idList<rvMD5RVertexBufferDesc> &vertexBuffers = GetVertexBuffers();
	const idList<rvMD5RIndexBufferDesc> &indexBuffers = GetIndexBuffers();

	mesh = rvMD5RMesh();
	mesh.renderModel = this;
	mesh.meshIdentifier = meshIndex;

	parser.ExpectTokenString( "{" );
	if ( !parser.ReadToken( &token ) ) {
		parser.Error( "Expected Material keyword." );
	}

	if ( token.Icmp( "LevelOfDetail" ) == 0 ) {
		mesh.levelOfDetail = parser.ParseInt();
		if ( !parser.ReadToken( &token ) ) {
			parser.Error( "Expected Material keyword." );
		}
	}

	if ( token.Icmp( "Material" ) != 0 ) {
		parser.Error( "Expected Material keyword." );
	}

	parser.ExpectAnyToken( &materialToken );
	mesh.materialName = materialToken;
	mesh.material = declManager->FindMaterial( mesh.materialName.c_str() );

	if ( !parser.ReadToken( &token ) ) {
		parser.Error( "Expected SilTraceBuffers, DrawBuffers, ShadowVolumeBuffers or PrimBatch keyword." );
	}

	if ( token.Icmp( "SilTraceBuffers" ) == 0 ) {
		mesh.silTraceVertexBuffer = parser.ParseInt();
		mesh.silTraceIndexBuffer = parser.ParseInt();
		if ( mesh.silTraceVertexBuffer < 0 || mesh.silTraceVertexBuffer >= vertexBuffers.Num()
			|| mesh.silTraceIndexBuffer < 0 || mesh.silTraceIndexBuffer >= indexBuffers.Num() ) {
			parser.Error( "Invalid buffer reference by SilTraceBuffers statement" );
		}
		if ( !parser.ReadToken( &token ) ) {
			parser.Error( "Expected DrawBuffers, ShadowVolumeBuffers or PrimBatch keyword." );
		}
	}

	if ( token.Icmp( "DrawBuffers" ) == 0 ) {
		mesh.drawVertexBuffer = parser.ParseInt();
		mesh.drawIndexBuffer = parser.ParseInt();
		if ( mesh.drawVertexBuffer < 0 || mesh.drawVertexBuffer >= vertexBuffers.Num()
			|| mesh.drawIndexBuffer < 0 || mesh.drawIndexBuffer >= indexBuffers.Num() ) {
			parser.Error( "Invalid buffer reference by DrawBuffers statement" );
		}
		if ( !parser.ReadToken( &token ) ) {
			parser.Error( "Expected ShadowVolumeBuffers or PrimBatch keyword." );
		}
	}

	if ( token.Icmp( "ShadowVolumeBuffers" ) == 0 ) {
		mesh.shadowVolVertexBuffer = parser.ParseInt();
		mesh.shadowVolIndexBuffer = parser.ParseInt();
		if ( mesh.shadowVolVertexBuffer < 0 || mesh.shadowVolVertexBuffer >= vertexBuffers.Num()
			|| mesh.shadowVolIndexBuffer < 0 || mesh.shadowVolIndexBuffer >= indexBuffers.Num() ) {
			parser.Error( "Invalid buffer reference by ShadowVolumeBuffers statement" );
		}
		if ( !parser.ReadToken( &token ) ) {
			parser.Error( "Expected PrimBatch keyword." );
		}
	}

	if ( token.Icmp( "PrimBatch" ) != 0 ) {
		parser.Error( "Expected PrimBatch keyword." );
	}

	parser.ExpectTokenString( "[" );
	const int numPrimBatches = parser.ParseInt();
	parser.ExpectTokenString( "]" );
	parser.ExpectTokenString( "{" );

	if ( numPrimBatches < 0 ) {
		parser.Error( "Invalid MD5R primitive-batch count %d", numPrimBatches );
	}

	mesh.primBatches.SetNum( numPrimBatches );
	for ( int i = 0; i < numPrimBatches; ++i ) {
		parser.ExpectTokenString( "PrimBatch" );
		ParsePrimBatch( parser, mesh.primBatches[ i ] );
	}

	parser.ExpectTokenString( "}" );
	R_MD5R_CalcMeshGeometryProfile( mesh );

	if ( !parser.ReadToken( &token ) ) {
		parser.Error( "Expected } or Bounds." );
	}

	if ( token.Icmp( "Bounds" ) == 0 ) {
		R_MD5R_ParseFlexibleBounds( parser, mesh.bounds );
		if ( !parser.ReadToken( &token ) ) {
			parser.Error( "Expected }." );
		}
	}

	if ( token.Icmp( "}" ) != 0 ) {
		parser.Error( "Couldn't find expected '}'" );
	}
}

/*
========================
rvRenderModelMD5R::ParseMeshes
========================
*/
void rvRenderModelMD5R::ParseMeshes( Lexer &parser ) {
	parser.ExpectTokenString( "[" );
	const int numMeshes = parser.ParseInt();
	parser.ExpectTokenString( "]" );
	parser.ExpectTokenString( "{" );

	if ( numMeshes < 0 ) {
		parser.Error( "Invalid MD5R mesh count %d", numMeshes );
	}

	meshes.SetNum( numMeshes );
	for ( int i = 0; i < numMeshes; ++i ) {
		parser.ExpectTokenString( "Mesh" );
		ParseMesh( parser, i );
	}

	parser.ExpectTokenString( "}" );
}

/*
========================
rvRenderModelMD5R::BuildLevelsOfDetail
========================
*/
void rvRenderModelMD5R::BuildLevelsOfDetail() {
	int maxReferencedLOD = -1;

	allLODMeshes.Clear();
	for ( int i = 0; i < lods.Num(); ++i ) {
		lods[ i ].meshIndexes.Clear();
	}

	for ( int i = 0; i < meshes.Num(); ++i ) {
		if ( meshes[ i ].levelOfDetail > maxReferencedLOD ) {
			maxReferencedLOD = meshes[ i ].levelOfDetail;
		}
	}

	if ( lods.Num() == 0 && maxReferencedLOD >= 0 ) {
		lods.SetNum( maxReferencedLOD + 1 );
		for ( int i = 0; i < lods.Num(); ++i ) {
			lods[ i ].rangeEnd = R_MD5R_GetDefaultLODRange( i );
			lods[ i ].rangeEndSquared = lods[ i ].rangeEnd * lods[ i ].rangeEnd;
		}
	}

	for ( int i = 0; i < meshes.Num(); ++i ) {
		const int lodIndex = meshes[ i ].levelOfDetail;
		if ( lodIndex < 0 || lodIndex >= lods.Num() ) {
			allLODMeshes.Append( i );
			continue;
		}

		lods[ lodIndex ].meshIndexes.Append( i );
	}
}

/*
========================
rvRenderModelMD5R::GenerateStaticTriSurface
========================
*/
srfTriangles_t *rvRenderModelMD5R::GenerateStaticTriSurface( const rvMD5RMesh &mesh ) const {
	const idList<rvMD5RVertexBufferDesc> &vertexBuffers = GetVertexBuffers();
	const idList<rvMD5RIndexBufferDesc> &indexBuffers = GetIndexBuffers();

	if ( mesh.drawVertexBuffer < 0 || mesh.drawVertexBuffer >= vertexBuffers.Num()
		|| mesh.drawIndexBuffer < 0 || mesh.drawIndexBuffer >= indexBuffers.Num() ) {
		return NULL;
	}

	if ( mesh.numDrawVertices <= 0 || mesh.numDrawIndices <= 0 ) {
		return NULL;
	}

	const rvMD5RVertexBufferDesc &drawVertexBuffer = vertexBuffers[ mesh.drawVertexBuffer ];
	const rvMD5RIndexBufferDesc &drawIndexBuffer = indexBuffers[ mesh.drawIndexBuffer ];

	if ( drawVertexBuffer.positions.Num() != drawVertexBuffer.numVertices ) {
		return NULL;
	}

	srfTriangles_t *tri = R_AllocStaticTriSurf();
	tri->numVerts = mesh.numDrawVertices;
	tri->numIndexes = mesh.numDrawIndices;
	tri->generateNormals = !drawVertexBuffer.loadVertexFormat.hasNormal;
	tri->tangentsCalculated = false;
	tri->facePlanesCalculated = false;

	R_AllocStaticTriSurfVerts( tri, tri->numVerts );
	R_AllocStaticTriSurfIndexes( tri, tri->numIndexes );

	if ( CopyPrimBatchTriangles( mesh, tri->verts, tri->indexes, NULL ) ) {
		return tri;
	}

	int destVertexBase = 0;
	int destIndexBase = 0;

	for ( int primBatchIndex = 0; primBatchIndex < mesh.primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh.primBatches[ primBatchIndex ];
		if ( !primBatch.hasDrawGeoSpec ) {
			continue;
		}

		if ( !R_MD5R_CopyDrawVertices( drawVertexBuffer, primBatch.drawGeoSpec, tri->verts + destVertexBase )
			|| !R_MD5R_CopyDrawIndices( drawIndexBuffer, primBatch.drawGeoSpec, destVertexBase, tri->indexes + destIndexBase ) ) {
			R_FreeStaticTriSurf( tri );
			return NULL;
		}

		destVertexBase += primBatch.drawGeoSpec.vertexCount;
		destIndexBase += primBatch.drawGeoSpec.primitiveCount * 3;
	}

	if ( destVertexBase != tri->numVerts || destIndexBase != tri->numIndexes ) {
		R_FreeStaticTriSurf( tri );
		return NULL;
	}

	return tri;
}

/*
========================
rvRenderModelMD5R::GenerateStaticSurfaces
========================
*/
bool rvRenderModelMD5R::GenerateStaticSurfaces() {
	int builtSurfaces = 0;
	int skippedSurfaces = 0;

	for ( int meshIndex = 0; meshIndex < meshes.Num(); ++meshIndex ) {
		rvMD5RMesh &mesh = meshes[ meshIndex ];
		mesh.surfaceNum = -1;

		srfTriangles_t *tri = GenerateStaticTriSurface( mesh );
		if ( tri == NULL ) {
			++skippedSurfaces;
			continue;
		}

		modelSurface_t surface;
		surface.id = mesh.meshIdentifier;
		surface.shader = ( mesh.material != NULL ) ? mesh.material : tr.defaultMaterial;
		surface.geometry = tri;
		surface.mOriginalSurfaceName = NULL;

		mesh.surfaceNum = NumSurfaces();
		AddSurface( surface );
		++builtSurfaces;
	}

	if ( builtSurfaces == 0 ) {
		geometrySectionsSkipped = ( meshes.Num() > 0 );
		return false;
	}

	geometrySectionsSkipped = ( skippedSurfaces > 0 );
	FinishSurfaces();

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	int primBatchBackedSurfaces = 0;
	for ( int meshIndex = 0; meshIndex < meshes.Num(); ++meshIndex ) {
		rvMD5RMesh &mesh = meshes[ meshIndex ];
		if ( mesh.surfaceNum < 0 || mesh.surfaceNum >= surfaces.Num() ) {
			continue;
		}

		srfTriangles_t *tri = surfaces[ mesh.surfaceNum ].geometry;
		if ( tri == NULL || tri->verts == NULL || tri->indexes == NULL
			|| tri->numVerts != mesh.numDrawVertices || tri->numIndexes != mesh.numDrawIndices ) {
			continue;
		}

		idDrawVert *primBatchVerts = (idDrawVert *)_alloca16( tri->numVerts * sizeof( primBatchVerts[ 0 ] ) );
		glIndex_t *primBatchIndexes = (glIndex_t *)_alloca16( tri->numIndexes * sizeof( primBatchIndexes[ 0 ] ) );
		if ( !CopyPrimBatchTriangles( mesh, primBatchVerts, primBatchIndexes, NULL ) ) {
			continue;
		}

		if ( memcmp( primBatchIndexes, tri->indexes, tri->numIndexes * sizeof( tri->indexes[ 0 ] ) ) != 0 ) {
			continue;
		}

		bool verticesMatch = true;
		for ( int vertIndex = 0; vertIndex < tri->numVerts; ++vertIndex ) {
			if ( !R_MD5R_DrawVertsEquivalent( primBatchVerts[ vertIndex ], tri->verts[ vertIndex ] ) ) {
				verticesMatch = false;
				break;
			}
		}
		if ( !verticesMatch ) {
			continue;
		}

		R_AllocStaticTriSurfSilTraceVerts( tri, tri->numVerts );
		rvSilTraceVertT *surfaceSilTraceVerts = reinterpret_cast<rvSilTraceVertT *>( tri->silTraceVerts );
		for ( int vertIndex = 0; vertIndex < tri->numVerts; ++vertIndex ) {
			surfaceSilTraceVerts[ vertIndex ].xyzw.Set(
				tri->verts[ vertIndex ].xyz.x,
				tri->verts[ vertIndex ].xyz.y,
				tri->verts[ vertIndex ].xyz.z,
				1.0f );
		}

#if defined( _MD5R_SUPPORT )
		tri->primBatchMesh = reinterpret_cast<rvMesh *>( &mesh );
#else
		tri->primBatchMesh = reinterpret_cast<void *>( &mesh );
#endif
		++primBatchBackedSurfaces;
	}
#endif

	if ( skippedSurfaces > 0 ) {
		common->Warning(
			"rvRenderModelMD5R::GenerateStaticSurfaces: built %d/%d static MD5R surface(s) for '%s'; unsupported buffers or geometry remain on the skipped meshes",
			builtSurfaces,
			meshes.Num(),
			name.c_str() );
	}

	return true;
}

/*
========================
R_MD5R_GetBlendIndex
========================
*/
static ID_INLINE int R_MD5R_GetBlendIndex( dword packedBlendIndices, int component ) {
	return static_cast<int>( ( packedBlendIndices >> ( component * 8 ) ) & 0xFFu );
}

/*
========================
R_MD5R_ResolveBlendJoint
========================
*/
static bool R_MD5R_ResolveBlendJoint( const rvMD5RPrimBatch &primBatch, int localTransformIndex, int numJoints, int &jointIndex ) {
	const int numLocalTransforms = Max( primBatch.numTransforms, 1 );
	if ( localTransformIndex < 0 || localTransformIndex >= numLocalTransforms ) {
		return false;
	}

	jointIndex = localTransformIndex;
	if ( primBatch.transformPalette.Num() > 0 ) {
		if ( localTransformIndex >= primBatch.transformPalette.Num() ) {
			return false;
		}
		jointIndex = primBatch.transformPalette[ localTransformIndex ];
	}

	return jointIndex >= 0 && jointIndex < numJoints;
}

/*
========================
R_MD5R_SkinVertexPosition
========================
*/
static bool R_MD5R_SkinVertexPosition(
	const rvMD5RVertexBufferDesc &vertexBuffer,
	int sourceVertexIndex,
	const rvMD5RPrimBatch &primBatch,
	const idJointMat *entJoints,
	int numJoints,
	idVec3 &skinnedPosition ) {
	if ( entJoints == NULL
		|| sourceVertexIndex < 0
		|| sourceVertexIndex >= vertexBuffer.numVertices
		|| vertexBuffer.positions.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	const bool hasBlendIndices = ( vertexBuffer.blendIndices.Num() == vertexBuffer.numVertices );
	const bool hasBlendWeights = ( vertexBuffer.blendWeights.Num() == vertexBuffer.numVertices );
	const idVec4 &sourcePosition = vertexBuffer.positions[ sourceVertexIndex ];
	const dword packedBlendIndices = hasBlendIndices ? vertexBuffer.blendIndices[ sourceVertexIndex ] : 0u;

	idVec4 blendWeights;
	blendWeights.Zero();
	blendWeights.x = 1.0f;
	if ( hasBlendWeights ) {
		blendWeights = vertexBuffer.blendWeights[ sourceVertexIndex ];
	}

	skinnedPosition.Zero();
	float totalWeight = 0.0f;

	for ( int influenceIndex = 0; influenceIndex < 4; ++influenceIndex ) {
		const float weight = idMath::Fabs( blendWeights[ influenceIndex ] );
		if ( weight <= 0.0f ) {
			continue;
		}

		int jointIndex = 0;
		const int localTransformIndex = hasBlendIndices ? R_MD5R_GetBlendIndex( packedBlendIndices, influenceIndex ) : 0;
		if ( !R_MD5R_ResolveBlendJoint( primBatch, localTransformIndex, numJoints, jointIndex ) ) {
			return false;
		}

		skinnedPosition += weight * ( entJoints[ jointIndex ] * sourcePosition );
		totalWeight += weight;
	}

	if ( totalWeight <= 0.0f ) {
		int jointIndex = 0;
		const int localTransformIndex = hasBlendIndices ? R_MD5R_GetBlendIndex( packedBlendIndices, 0 ) : 0;
		if ( !R_MD5R_ResolveBlendJoint( primBatch, localTransformIndex, numJoints, jointIndex ) ) {
			return false;
		}

		skinnedPosition = entJoints[ jointIndex ] * sourcePosition;
	}

	return true;
}

/*
========================
R_MD5R_MapMeshVertexToSourceVertex
========================
*/
static bool R_MD5R_MapMeshVertexToSourceVertex(
	const rvMD5RMesh &mesh,
	int meshVertexIndex,
	int &sourceVertexIndex,
	const rvMD5RPrimBatch *&sourcePrimBatch ) {
	int destVertexBase = 0;

	for ( int primBatchIndex = 0; primBatchIndex < mesh.primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh.primBatches[ primBatchIndex ];
		if ( !primBatch.hasDrawGeoSpec ) {
			continue;
		}

		if ( meshVertexIndex >= destVertexBase && meshVertexIndex < destVertexBase + primBatch.drawGeoSpec.vertexCount ) {
			sourceVertexIndex = primBatch.drawGeoSpec.vertexStart + ( meshVertexIndex - destVertexBase );
			sourcePrimBatch = &primBatch;
			return true;
		}

		destVertexBase += primBatch.drawGeoSpec.vertexCount;
	}

	sourceVertexIndex = -1;
	sourcePrimBatch = NULL;
	return false;
}

/*
========================
R_MD5R_GetDominantBlendJoint
========================
*/
static int R_MD5R_GetDominantBlendJoint(
	const rvMD5RVertexBufferDesc &vertexBuffer,
	int sourceVertexIndex,
	const rvMD5RPrimBatch &primBatch,
	int numJoints ) {
	if ( sourceVertexIndex < 0 || sourceVertexIndex >= vertexBuffer.numVertices ) {
		return 0;
	}

	const bool hasBlendIndices = ( vertexBuffer.blendIndices.Num() == vertexBuffer.numVertices );
	const bool hasBlendWeights = ( vertexBuffer.blendWeights.Num() == vertexBuffer.numVertices );
	const dword packedBlendIndices = hasBlendIndices ? vertexBuffer.blendIndices[ sourceVertexIndex ] : 0u;

	int bestInfluence = 0;
	if ( hasBlendWeights ) {
		float bestWeight = -1.0f;
		const idVec4 &blendWeights = vertexBuffer.blendWeights[ sourceVertexIndex ];
		for ( int influenceIndex = 0; influenceIndex < 4; ++influenceIndex ) {
			const float weight = idMath::Fabs( blendWeights[ influenceIndex ] );
			if ( weight > bestWeight ) {
				bestWeight = weight;
				bestInfluence = influenceIndex;
			}
		}
	}

	int jointIndex = 0;
	const int localTransformIndex = hasBlendIndices ? R_MD5R_GetBlendIndex( packedBlendIndices, bestInfluence ) : 0;
	if ( !R_MD5R_ResolveBlendJoint( primBatch, localTransformIndex, numJoints, jointIndex ) ) {
		return 0;
	}

	return jointIndex;
}

/*
========================
R_MD5R_GetPackedTransformCount
========================
*/
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
static int R_MD5R_GetPackedTransformCount( const rvMD5RMesh &mesh ) {
	int totalTransformCount = 0;

	for ( int primBatchIndex = 0; primBatchIndex < mesh.primBatches.Num(); ++primBatchIndex ) {
		totalTransformCount += Max( mesh.primBatches[ primBatchIndex ].numTransforms, 1 );
	}

	return totalTransformCount;
}

/*
========================
R_MD5R_CanUsePackedDynamicSurface
========================
*/
static bool R_MD5R_CanUsePackedDynamicSurface(
	const rvMD5RMesh &mesh,
	const idList<rvMD5RVertexBufferDesc> &vertexBuffers,
	const idList<rvMD5RIndexBufferDesc> &indexBuffers ) {
	if ( mesh.numDrawVertices <= 0
		|| mesh.numDrawIndices <= 0
		|| mesh.numSilTraceVertices <= 0
		|| mesh.numSilTraceIndices <= 0
		|| mesh.primBatches.Num() <= 0 ) {
		return false;
	}

	if ( mesh.drawVertexBuffer < 0 || mesh.drawVertexBuffer >= vertexBuffers.Num()
		|| mesh.silTraceVertexBuffer < 0 || mesh.silTraceVertexBuffer >= vertexBuffers.Num()
		|| mesh.drawIndexBuffer < 0 || mesh.drawIndexBuffer >= indexBuffers.Num()
		|| mesh.silTraceIndexBuffer < 0 || mesh.silTraceIndexBuffer >= indexBuffers.Num() ) {
		return false;
	}

	const rvMD5RVertexBufferDesc &drawVertexBuffer = vertexBuffers[ mesh.drawVertexBuffer ];
	const rvMD5RVertexBufferDesc &silTraceVertexBuffer = vertexBuffers[ mesh.silTraceVertexBuffer ];
	const rvMD5RIndexBufferDesc &drawIndexBuffer = indexBuffers[ mesh.drawIndexBuffer ];
	const rvMD5RIndexBufferDesc &silTraceIndexBuffer = indexBuffers[ mesh.silTraceIndexBuffer ];

	if ( drawVertexBuffer.positions.Num() != drawVertexBuffer.numVertices
		|| silTraceVertexBuffer.positions.Num() != silTraceVertexBuffer.numVertices
		|| drawIndexBuffer.indices.Num() != drawIndexBuffer.numIndices
		|| silTraceIndexBuffer.indices.Num() != silTraceIndexBuffer.numIndices ) {
		return false;
	}

	int totalDrawVertices = 0;
	int totalDrawIndices = 0;
	int totalSilTraceVertices = 0;
	int totalSilTraceIndices = 0;

	for ( int primBatchIndex = 0; primBatchIndex < mesh.primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh.primBatches[ primBatchIndex ];
		if ( !primBatch.hasDrawGeoSpec || !primBatch.hasSilTraceGeoSpec ) {
			return false;
		}

		const int drawIndexCount = primBatch.drawGeoSpec.primitiveCount * 3;
		const int silTraceIndexCount = primBatch.silTraceGeoSpec.primitiveCount * 3;
		if ( drawIndexCount != silTraceIndexCount ) {
			return false;
		}

		if ( primBatch.drawGeoSpec.vertexStart < 0
			|| primBatch.drawGeoSpec.vertexCount < 0
			|| primBatch.drawGeoSpec.vertexStart + primBatch.drawGeoSpec.vertexCount > drawVertexBuffer.numVertices
			|| primBatch.drawGeoSpec.indexStart < 0
			|| primBatch.drawGeoSpec.indexStart + drawIndexCount > drawIndexBuffer.numIndices
			|| primBatch.silTraceGeoSpec.vertexStart < 0
			|| primBatch.silTraceGeoSpec.vertexCount < 0
			|| primBatch.silTraceGeoSpec.vertexStart + primBatch.silTraceGeoSpec.vertexCount > silTraceVertexBuffer.numVertices
			|| primBatch.silTraceGeoSpec.indexStart < 0
			|| primBatch.silTraceGeoSpec.indexStart + silTraceIndexCount > silTraceIndexBuffer.numIndices ) {
			return false;
		}

		totalDrawVertices += primBatch.drawGeoSpec.vertexCount;
		totalDrawIndices += drawIndexCount;
		totalSilTraceVertices += primBatch.silTraceGeoSpec.vertexCount;
		totalSilTraceIndices += silTraceIndexCount;
	}

	return totalDrawVertices == mesh.numDrawVertices
		&& totalDrawIndices == mesh.numDrawIndices
		&& totalSilTraceVertices == mesh.numSilTraceVertices
		&& totalSilTraceIndices == mesh.numSilTraceIndices;
}

/*
========================
R_MD5R_WriteSkinToModelTransform
========================
*/
static ID_INLINE void R_MD5R_WriteSkinToModelTransform( const idJointMat &skinToModel, float *packedTransform ) {
	memcpy( packedTransform, skinToModel.ToFloatPtr(), 12 * sizeof( packedTransform[ 0 ] ) );
	packedTransform[ 12 ] = 0.0f;
	packedTransform[ 13 ] = 0.0f;
	packedTransform[ 14 ] = 0.0f;
	packedTransform[ 15 ] = 1.0f;
}

/*
========================
rvRenderModelMD5R::UpdatePackedDynamicSurface
========================
*/
static bool R_MD5R_UpdatePackedDynamicSurface(
	const rvMD5RMesh &mesh,
	const idJointMat *entJoints,
	const idList<rvMD5RVertexBufferDesc> &vertexBuffers,
	const idList<rvMD5RIndexBufferDesc> &indexBuffers,
	const idList<silEdge_t> &silEdges,
	int numJoints,
	modelSurface_t &surface,
	bool calculateTangents ) {
	if ( entJoints == NULL || !R_MD5R_CanUsePackedDynamicSurface( mesh, vertexBuffers, indexBuffers ) ) {
		return false;
	}

	const rvMD5RVertexBufferDesc &drawVertexBuffer = vertexBuffers[ mesh.drawVertexBuffer ];
	const rvMD5RVertexBufferDesc &silTraceVertexBuffer = vertexBuffers[ mesh.silTraceVertexBuffer ];
	const int totalTransformCount = R_MD5R_GetPackedTransformCount( mesh );

	srfTriangles_t *tri = surface.geometry;
	if ( tri != NULL ) {
		if ( tri->numVerts == mesh.numDrawVertices
			&& tri->numIndexes == mesh.numDrawIndices
			&& tri->primBatchMesh != NULL ) {
			R_FreeStaticTriSurfVertexCaches( tri );
		} else {
			R_FreeStaticTriSurf( tri );
			tri = NULL;
		}
	}

	if ( tri == NULL ) {
		tri = R_AllocStaticTriSurf();
		surface.geometry = tri;
	}

	const bool hasNormals = ( drawVertexBuffer.normals.Num() == drawVertexBuffer.numVertices );
	const bool hasTangents = ( drawVertexBuffer.tangents.Num() == drawVertexBuffer.numVertices );
	const bool hasBinormals = ( drawVertexBuffer.binormals.Num() == drawVertexBuffer.numVertices );

	tri->deformedSurface = true;
	tri->generateNormals = !hasNormals;
	tri->tangentsCalculated = hasNormals && hasTangents && hasBinormals;
	tri->facePlanesCalculated = false;
	tri->numVerts = mesh.numDrawVertices;
	tri->numIndexes = mesh.numDrawIndices;
	tri->silIndexes = NULL;
	tri->numMirroredVerts = 0;
	tri->mirroredVerts = NULL;
	tri->numDupVerts = 0;
	tri->dupVerts = NULL;
	tri->dominantTris = NULL;
	tri->numSilEdges = 0;
	tri->silEdges = NULL;

	if ( tri->verts == NULL ) {
		R_AllocStaticTriSurfVerts( tri, tri->numVerts );
	}
	if ( tri->indexes == NULL ) {
		R_AllocStaticTriSurfIndexes( tri, tri->numIndexes );
	}
	R_AllocStaticTriSurfSilTraceVerts( tri, tri->numVerts );
	R_AllocStaticSkinToModelTransforms( tri, totalTransformCount );

#if defined( _MD5R_SUPPORT )
	tri->primBatchMesh = const_cast<rvMesh *>( reinterpret_cast<const rvMesh *>( &mesh ) );
#else
	tri->primBatchMesh = const_cast<void *>( reinterpret_cast<const void *>( &mesh ) );
#endif

	if ( mesh.numSilEdges > 0 && mesh.primBatches.Num() > 0 ) {
		const int silEdgeStart = mesh.primBatches[ 0 ].silEdgeStart;
		if ( silEdgeStart >= 0 && silEdgeStart + mesh.numSilEdges <= silEdges.Num() ) {
			tri->numSilEdges = mesh.numSilEdges;
			tri->silEdges = const_cast<silEdge_t *>( silEdges.Ptr() + silEdgeStart );
		}
	}

	// Keep a raw per-prim-batch sil-trace working set so CopyPrimBatchTriangles can
	// rematerialize draw verts even when the draw mesh has extra split vertices.
	idList<rvSilTraceVertT> skinnedSilTraceVerts;
	skinnedSilTraceVerts.SetNum( mesh.numSilTraceVertices );
	rvSilTraceVertT *rawSilTraceVerts = skinnedSilTraceVerts.Ptr();

	int silTraceVertexBase = 0;
	int transformBase = 0;
	for ( int primBatchIndex = 0; primBatchIndex < mesh.primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh.primBatches[ primBatchIndex ];
		const int primBatchTransformCount = Max( primBatch.numTransforms, 1 );

		for ( int transformIndex = 0; transformIndex < primBatchTransformCount; ++transformIndex ) {
			int jointIndex = 0;
			if ( !R_MD5R_ResolveBlendJoint( primBatch, transformIndex, numJoints, jointIndex ) ) {
				return false;
			}

			R_MD5R_WriteSkinToModelTransform(
				entJoints[ jointIndex ],
				tri->skinToModelTransforms + ( ( transformBase + transformIndex ) * 16 ) );
		}

		for ( int localVertexIndex = 0; localVertexIndex < primBatch.silTraceGeoSpec.vertexCount; ++localVertexIndex ) {
			const int destVertexIndex = silTraceVertexBase + localVertexIndex;
			const int sourceVertexIndex = primBatch.silTraceGeoSpec.vertexStart + localVertexIndex;
			if ( destVertexIndex < 0 || destVertexIndex >= mesh.numSilTraceVertices ) {
				return false;
			}

			idVec3 skinnedPosition;
			if ( !R_MD5R_SkinVertexPosition( silTraceVertexBuffer, sourceVertexIndex, primBatch, entJoints, numJoints, skinnedPosition ) ) {
				return false;
			}

			rawSilTraceVerts[ destVertexIndex ].xyzw.Set( skinnedPosition.x, skinnedPosition.y, skinnedPosition.z, 1.0f );
		}

		silTraceVertexBase += primBatch.silTraceGeoSpec.vertexCount;
		transformBase += primBatchTransformCount;
	}

	if ( silTraceVertexBase != mesh.numSilTraceVertices || transformBase != totalTransformCount ) {
		return false;
	}

	if ( !R_MD5R_CopyPrimBatchTriangles(
		tri->verts,
		tri->indexes,
		reinterpret_cast<const rvMesh *>( tri->primBatchMesh ),
		rawSilTraceVerts ) ) {
		return false;
	}

	rvSilTraceVertT *dynamicSilTraceVerts = reinterpret_cast<rvSilTraceVertT *>( tri->silTraceVerts );
	idBounds bounds;
	bounds.Clear();
	for ( int vertIndex = 0; vertIndex < tri->numVerts; ++vertIndex ) {
		const idVec3 &position = tri->verts[ vertIndex ].xyz;
		dynamicSilTraceVerts[ vertIndex ].xyzw.Set( position.x, position.y, position.z, 1.0f );
		bounds.AddPoint( position );
	}

	tri->bounds = bounds;

	if ( calculateTangents && !tri->tangentsCalculated && !r_useDeferredTangents.GetBool() ) {
		R_DeriveTangents( tri );
	}

	return true;
}
#endif

/*
========================
rvRenderModelMD5R::BuildDynamicMeshTemplate
========================
*/
bool rvRenderModelMD5R::BuildDynamicMeshTemplate( rvMD5RMesh &mesh ) {
	if ( mesh.deformInfo != NULL && mesh.baseDrawVerts.Num() > 0 ) {
		return true;
	}

	if ( mesh.deformInfo != NULL ) {
		R_FreeDeformInfo( mesh.deformInfo );
		mesh.deformInfo = NULL;
	}
	mesh.baseDrawVerts.Clear();

	srfTriangles_t *baseTri = GenerateStaticTriSurface( mesh );
	if ( baseTri == NULL || baseTri->verts == NULL || baseTri->indexes == NULL || baseTri->numVerts <= 0 || baseTri->numIndexes <= 0 ) {
		if ( baseTri != NULL ) {
			R_FreeStaticTriSurf( baseTri );
		}
		return false;
	}

	mesh.baseDrawVerts.SetNum( baseTri->numVerts );
	memcpy( mesh.baseDrawVerts.Ptr(), baseTri->verts, baseTri->numVerts * sizeof( mesh.baseDrawVerts[ 0 ] ) );

	idList<int> deformIndexes;
	deformIndexes.SetNum( baseTri->numIndexes );
	for ( int indexNum = 0; indexNum < baseTri->numIndexes; ++indexNum ) {
		deformIndexes[ indexNum ] = baseTri->indexes[ indexNum ];
	}

	mesh.deformInfo = R_BuildDeformInfo(
		baseTri->numVerts,
		mesh.baseDrawVerts.Ptr(),
		baseTri->numIndexes,
		deformIndexes.Ptr(),
		false );

	R_FreeStaticTriSurf( baseTri );

	if ( mesh.deformInfo == NULL ) {
		mesh.baseDrawVerts.Clear();
		return false;
	}

	return true;
}

/*
========================
rvRenderModelMD5R::UpdateDynamicSurface
========================
*/
bool rvRenderModelMD5R::UpdateDynamicSurface( const rvMD5RMesh &mesh, const idJointMat *entJoints, modelSurface_t &surface, bool calculateTangents ) const {
	const idList<rvMD5RVertexBufferDesc> &vertexBuffers = GetVertexBuffers();
	const idList<rvMD5RIndexBufferDesc> &indexBuffers = GetIndexBuffers();
	const idList<silEdge_t> &silEdges = GetSilhouetteEdges();

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( R_MD5R_UpdatePackedDynamicSurface( mesh, entJoints, vertexBuffers, indexBuffers, silEdges, joints.Num(), surface, calculateTangents ) ) {
		return true;
	}
#endif

	if ( mesh.deformInfo == NULL
		|| mesh.baseDrawVerts.Num() != mesh.deformInfo->numSourceVerts
		|| mesh.drawVertexBuffer < 0
		|| mesh.drawVertexBuffer >= vertexBuffers.Num() ) {
		return false;
	}

	const rvMD5RVertexBufferDesc &drawVertexBuffer = vertexBuffers[ mesh.drawVertexBuffer ];
	srfTriangles_t *tri = surface.geometry;

	if ( tri != NULL ) {
		if ( tri->numVerts == mesh.deformInfo->numOutputVerts && tri->numIndexes == mesh.deformInfo->numIndexes ) {
			R_FreeStaticTriSurfVertexCaches( tri );
		} else {
			R_FreeStaticTriSurf( tri );
			tri = NULL;
		}
	}

	if ( tri == NULL ) {
		tri = R_AllocStaticTriSurf();
		surface.geometry = tri;
	}

	tri->deformedSurface = true;
	tri->generateNormals = true;
	tri->tangentsCalculated = false;
	tri->facePlanesCalculated = false;
	tri->numIndexes = mesh.deformInfo->numIndexes;
	tri->indexes = mesh.deformInfo->indexes;
	tri->silIndexes = mesh.deformInfo->silIndexes;
	tri->numMirroredVerts = mesh.deformInfo->numMirroredVerts;
	tri->mirroredVerts = mesh.deformInfo->mirroredVerts;
	tri->numDupVerts = mesh.deformInfo->numDupVerts;
	tri->dupVerts = mesh.deformInfo->dupVerts;
	tri->numSilEdges = mesh.deformInfo->numSilEdges;
	tri->silEdges = mesh.deformInfo->silEdges;
	tri->dominantTris = mesh.deformInfo->dominantTris;
	tri->numVerts = mesh.deformInfo->numOutputVerts;

	if ( tri->verts == NULL ) {
		R_AllocStaticTriSurfVerts( tri, tri->numVerts );
	}

	int destVertexBase = 0;
	for ( int primBatchIndex = 0; primBatchIndex < mesh.primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh.primBatches[ primBatchIndex ];
		if ( !primBatch.hasDrawGeoSpec ) {
			continue;
		}

		for ( int localVertexIndex = 0; localVertexIndex < primBatch.drawGeoSpec.vertexCount; ++localVertexIndex ) {
			const int destVertexIndex = destVertexBase + localVertexIndex;
			const int sourceVertexIndex = primBatch.drawGeoSpec.vertexStart + localVertexIndex;
			if ( destVertexIndex < 0 || destVertexIndex >= mesh.baseDrawVerts.Num() ) {
				return false;
			}

			idVec3 skinnedPosition;
			if ( !R_MD5R_SkinVertexPosition( drawVertexBuffer, sourceVertexIndex, primBatch, entJoints, joints.Num(), skinnedPosition ) ) {
				return false;
			}

			tri->verts[ destVertexIndex ] = mesh.baseDrawVerts[ destVertexIndex ];
			tri->verts[ destVertexIndex ].xyz = skinnedPosition;
		}

		destVertexBase += primBatch.drawGeoSpec.vertexCount;
	}

	if ( destVertexBase != mesh.baseDrawVerts.Num() ) {
		return false;
	}

	const int mirrorBase = tri->numVerts - tri->numMirroredVerts;
	for ( int mirrorIndex = 0; mirrorIndex < tri->numMirroredVerts; ++mirrorIndex ) {
		const int sourceIndex = tri->mirroredVerts[ mirrorIndex ];
		if ( sourceIndex < 0 || sourceIndex >= tri->numVerts ) {
			return false;
		}

		tri->verts[ mirrorBase + mirrorIndex ] = tri->verts[ sourceIndex ];
	}

	R_BoundTriSurf( tri );

	if ( calculateTangents && !r_useDeferredTangents.GetBool() ) {
		R_DeriveTangents( tri );
	}

	return true;
}

/*
========================
rvRenderModelMD5R::GenerateDynamicSurface
========================
*/
bool rvRenderModelMD5R::GenerateDynamicSurface( idRenderModelStatic &staticModel, rvMD5RMesh &mesh, const renderEntity_s &ent, const idJointMat *entJoints, dword surfMask ) {
	const idList<rvMD5RVertexBufferDesc> &vertexBuffers = GetVertexBuffers();
	const idList<rvMD5RIndexBufferDesc> &indexBuffers = GetIndexBuffers();

	if ( !r_skipSuppress.GetBool()
		&& mesh.meshIdentifier >= 0
		&& mesh.meshIdentifier < static_cast<int>( sizeof( unsigned int ) * 8 )
		&& ( static_cast<unsigned int>( ent.suppressSurfaceMask ) & ( 1u << mesh.meshIdentifier ) ) != 0 ) {
		staticModel.DeleteSurfaceWithId( mesh.meshIdentifier );
		staticModel.DeleteSurfaceWithId( mesh.meshIdentifier + MD5R_BackSideSurfaceIdOffset );
		mesh.surfaceNum = -1;
		return false;
	}

	const bool collisionOnly = ( surfMask & SURF_COLLISION ) != 0;
	const idMaterial *shader = R_RemapShaderBySkin( mesh.material, ent.customSkin, ent.customShader );
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	bool canUsePackedDynamicSurface = R_MD5R_CanUsePackedDynamicSurface( mesh, vertexBuffers, indexBuffers );
#else
	const bool canUsePackedDynamicSurface = false;
#endif

	if ( collisionOnly ) {
		if ( shader == NULL || ( shader->GetSurfaceFlags() & SURF_COLLISION ) == 0 ) {
			staticModel.DeleteSurfaceWithId( mesh.meshIdentifier );
			staticModel.DeleteSurfaceWithId( mesh.meshIdentifier + MD5R_BackSideSurfaceIdOffset );
			mesh.surfaceNum = -1;
			return false;
		}
	} else if ( shader == NULL || ( !shader->IsDrawn() && !shader->SurfaceCastsShadow() ) ) {
		staticModel.DeleteSurfaceWithId( mesh.meshIdentifier );
		staticModel.DeleteSurfaceWithId( mesh.meshIdentifier + MD5R_BackSideSurfaceIdOffset );
		mesh.surfaceNum = -1;
		return false;
	}

	// The current hybrid MD5R path can safely rematerialize split draw/sil-trace
	// meshes for ordinary rendering, but shadow and collision consumers still
	// assume the classic tri-surf silhouette topology. Keep those cases on the
	// legacy path until the native rvMesh shadow/trace path is finished.
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( canUsePackedDynamicSurface
		&& mesh.numDrawVertices != mesh.numSilTraceVertices
		&& ( collisionOnly || ( shader != NULL && shader->SurfaceCastsShadow() ) ) ) {
		canUsePackedDynamicSurface = false;
	}
#endif

	if ( !canUsePackedDynamicSurface ) {
		if ( !BuildDynamicMeshTemplate( mesh ) ) {
			staticModel.DeleteSurfaceWithId( mesh.meshIdentifier );
			staticModel.DeleteSurfaceWithId( mesh.meshIdentifier + MD5R_BackSideSurfaceIdOffset );
			mesh.surfaceNum = -1;
			return false;
		}
	}

	modelSurface_t *surface = NULL;
	int surfaceNum = -1;
	if ( staticModel.FindSurfaceWithId( mesh.meshIdentifier, surfaceNum ) ) {
		mesh.surfaceNum = surfaceNum;
		surface = &staticModel.surfaces[ surfaceNum ];
	} else {
		idRenderModelOverlay::RemoveOverlaySurfacesFromModel( &staticModel );

		mesh.surfaceNum = staticModel.NumSurfaces();
		surface = &staticModel.surfaces.Alloc();
		surface->geometry = NULL;
		surface->shader = NULL;
		surface->id = mesh.meshIdentifier;
		surface->mOriginalSurfaceName = NULL;
	}

	if ( !UpdateDynamicSurface( mesh, entJoints, *surface, !collisionOnly ) ) {
		staticModel.DeleteSurfaceWithId( mesh.meshIdentifier );
		staticModel.DeleteSurfaceWithId( mesh.meshIdentifier + MD5R_BackSideSurfaceIdOffset );
		mesh.surfaceNum = -1;
		return false;
	}

	surface->shader = shader;
	srfTriangles_t *frontTri = surface->geometry;
	if ( frontTri == NULL ) {
		staticModel.DeleteSurfaceWithId( mesh.meshIdentifier );
		staticModel.DeleteSurfaceWithId( mesh.meshIdentifier + MD5R_BackSideSurfaceIdOffset );
		mesh.surfaceNum = -1;
		return false;
	}

	if ( !collisionOnly && shader->ShouldCreateBackSides() ) {
		modelSurface_t *backSurface = NULL;
		if ( staticModel.FindSurfaceWithId( mesh.meshIdentifier + MD5R_BackSideSurfaceIdOffset, surfaceNum ) ) {
			backSurface = &staticModel.surfaces[ surfaceNum ];
		} else {
			backSurface = &staticModel.surfaces.Alloc();
			backSurface->geometry = NULL;
			backSurface->shader = NULL;
			backSurface->id = mesh.meshIdentifier + MD5R_BackSideSurfaceIdOffset;
			backSurface->mOriginalSurfaceName = NULL;
		}

		backSurface->shader = shader;
		R_MD5R_CopyAndReverseTriangles( frontTri, &backSurface->geometry );
	} else {
		staticModel.DeleteSurfaceWithId( mesh.meshIdentifier + MD5R_BackSideSurfaceIdOffset );
	}

	staticModel.bounds.AddBounds( frontTri->bounds );
	return true;
}

/*
========================
rvRenderModelMD5R::ParseJoint
========================
*/
void rvRenderModelMD5R::ParseJoint( Lexer &parser, int jointIndex, idJointQuat &worldPose ) {
	idToken nameToken;
	parser.ExpectAnyToken( &nameToken );

	idMD5Joint &joint = joints[ jointIndex ];
	joint.name = nameToken;

	const int parentIndex = parser.ParseInt();
	if ( parentIndex < -1 || parentIndex >= jointIndex ) {
		parser.Error( "Invalid parent index %d for joint '%s'", parentIndex, joint.name.c_str() );
	}
	joint.parent = ( parentIndex >= 0 ) ? &joints[ parentIndex ] : NULL;

	R_MD5R_ParseFlexibleVec3( parser, worldPose.t );
	idVec3 quatXYZ;
	R_MD5R_ParseFlexibleVec3( parser, quatXYZ );
	worldPose.q.x = quatXYZ.x;
	worldPose.q.y = quatXYZ.y;
	worldPose.q.z = quatXYZ.z;
	worldPose.q.w = worldPose.q.CalcW();
}

/*
========================
rvRenderModelMD5R::ParseJoints
========================
*/
void rvRenderModelMD5R::ParseJoints( Lexer &parser ) {
	parser.ExpectTokenString( "[" );
	const int numJoints = parser.ParseInt();
	parser.ExpectTokenString( "]" );
	parser.ExpectTokenString( "{" );

	if ( numJoints < 0 ) {
		parser.Error( "Invalid MD5R joint count %d", numJoints );
	}

	joints.SetNum( numJoints );
	defaultPose.SetNum( numJoints );
	skinSpaceToLocalMats.SetNum( numJoints );

	idList<idJointMat> worldPoseMats;
	worldPoseMats.SetNum( numJoints );

	for ( int i = 0; i < numJoints; ++i ) {
		ParseJoint( parser, i, defaultPose[ i ] );
		worldPoseMats[ i ].SetRotation( defaultPose[ i ].q.ToMat3() );
		worldPoseMats[ i ].SetTranslation( defaultPose[ i ].t );

		if ( joints[ i ].parent != NULL ) {
			const int parentIndex = static_cast<int>( joints[ i ].parent - joints.Ptr() );
			defaultPose[ i ].q = ( worldPoseMats[ i ].ToMat3() * worldPoseMats[ parentIndex ].ToMat3().Transpose() ).ToQuat();
			defaultPose[ i ].t = ( worldPoseMats[ i ].ToVec3() - worldPoseMats[ parentIndex ].ToVec3() ) * worldPoseMats[ parentIndex ].ToMat3().Transpose();
		}
	}

	parser.ExpectTokenString( "}" );

	for ( int i = 0; i < numJoints; ++i ) {
		skinSpaceToLocalMats[ i ] = worldPoseMats[ i ];
		skinSpaceToLocalMats[ i ].Invert();
	}
}

/*
========================
rvRenderModelMD5R::LoadModel

This follows the retail top-level section ordering, parses the packed text
payload into CPU-side draw buffers, and materializes static render surfaces for
non-jointed MD5R models. Jointed MD5R assets now build classic deformation
templates from the packed draw buffers so they can instantiate cached dynamic
surfaces without the retail rvMesh runtime.
========================
*/
void rvRenderModelMD5R::LoadModel() {
	const rvMD5RSource_t loadSource = source;

	PurgeModel();
	purged = false;
	reloadable = true;

	if ( loadSource == MD5R_SOURCE_MD5 ) {
		idAutoPtr<idRenderModelMD5> sourceModel( new idRenderModelMD5 );
		sourceModel->InitFromFile( name.c_str() );

		if ( sourceModel->IsDefaultModel() || !InitFromMD5Model( *sourceModel ) ) {
			MakeDefaultModel();
		}

		fileSystem->ReadFile( name.c_str(), NULL, &timeStamp );
		return;
	}

	if ( loadSource == MD5R_SOURCE_LWO_ASE_FLT ) {
		idAutoPtr<idRenderModelStatic> sourceModel( new idRenderModelStatic );
		sourceModel->InitFromFile( name.c_str() );

		if ( sourceModel->IsDefaultModel() || !InitFromStaticModel( *sourceModel, MD5R_SOURCE_LWO_ASE_FLT ) ) {
			MakeDefaultModel();
		}

		fileSystem->ReadFile( name.c_str(), NULL, &timeStamp );
		return;
	}

	source = MD5R_SOURCE_FILE;

	idAutoPtr<Lexer> parser( LexerFactory::MakeLexer( name.c_str(), LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGESCAPECHARS ) );
	if ( parser.get() == NULL || !parser->LoadFile( name.c_str() ) ) {
		MakeDefaultModel();
		return;
	}

	parser->ExpectTokenString( MD5R_VERSION_STRING );
	md5rVersion = parser->ParseInt();
	if ( md5rVersion != MD5R_VERSION ) {
		parser->Error( "Invalid version %d. Should be version %d", md5rVersion, MD5R_VERSION );
	}

	if ( parser->PeekTokenString( "CommandLine" ) ) {
		idToken commandLineToken;
		parser->ExpectTokenString( "CommandLine" );
		parser->ExpectAnyToken( &commandLineToken );
		commandLine = commandLineToken;
	}

	if ( parser->PeekTokenString( "Joint" ) ) {
		parser->ExpectTokenString( "Joint" );
		ParseJoints( *parser );
	}

	if ( !parser->PeekTokenString( "VertexBuffer" ) ) {
		parser->Error( "Expected VertexBuffer keyword" );
	}
	parser->ExpectTokenString( "VertexBuffer" );
	ParseVertexBuffers( *parser );

	if ( parser->PeekTokenString( "IndexBuffer" ) ) {
		parser->ExpectTokenString( "IndexBuffer" );
		ParseIndexBuffers( *parser );
	}

	if ( parser->PeekTokenString( "SilhouetteEdge" ) ) {
		parser->ExpectTokenString( "SilhouetteEdge" );
		ParseSilhouetteEdges( *parser );
	}

	if ( parser->PeekTokenString( "LevelOfDetail" ) ) {
		parser->ExpectTokenString( "LevelOfDetail" );
		ParseLevelOfDetail( *parser );
	}

	if ( !parser->PeekTokenString( "Mesh" ) ) {
		parser->Error( "Expected Mesh keyword" );
	}
	parser->ExpectTokenString( "Mesh" );
	ParseMeshes( *parser );
	BuildLevelsOfDetail();

	parser->ExpectTokenString( "Bounds" );
	R_MD5R_ParseFlexibleBounds( *parser, bounds );

	if ( parser->PeekTokenString( "HasSky" ) ) {
		parser->ExpectTokenString( "HasSky" );
		hasSky = true;
		SetProcSky( true );
	}

	idToken token;
	while ( parser->ReadToken( &token ) ) {
		parser->Warning( "Ignoring unexpected trailing token '%s' in '%s'", token.c_str(), name.c_str() );
	}

	const bool hasGeometrySections = ( GetVertexBuffers().Num() > 0 || GetIndexBuffers().Num() > 0 || meshes.Num() > 0 );
	metadataOnly = false;
	geometrySectionsSkipped = false;

	if ( joints.Num() == 0 ) {
		if ( hasGeometrySections && !GenerateStaticSurfaces() ) {
			metadataOnly = true;
			geometrySectionsSkipped = true;
			common->Warning(
				"rvRenderModelMD5R::LoadModel: parsed packed-section metadata for '%s', but static MD5R surface generation could not decode the current buffer layout",
				name.c_str() );
		}
	} else if ( hasGeometrySections ) {
		int builtTemplates = 0;
		int skippedTemplates = 0;
		for ( int meshIndex = 0; meshIndex < meshes.Num(); ++meshIndex ) {
			if ( BuildDynamicMeshTemplate( meshes[ meshIndex ] ) ) {
				++builtTemplates;
			} else {
				++skippedTemplates;
			}
		}

		if ( builtTemplates == 0 ) {
			metadataOnly = true;
			geometrySectionsSkipped = true;
			common->Warning(
				"rvRenderModelMD5R::LoadModel: parsed packed-section metadata for '%s', but jointed MD5R template generation could not decode the current buffer layout",
				name.c_str() );
		} else if ( skippedTemplates > 0 ) {
			geometrySectionsSkipped = true;
			common->Warning(
				"rvRenderModelMD5R::LoadModel: built %d/%d jointed MD5R deformation template(s) for '%s'; unsupported buffers remain on the skipped meshes",
				builtTemplates,
				meshes.Num(),
				name.c_str() );
		}
	}

	timeStamp = R_MD5R_ReadBinaryAwareTimestamp( name );
}

/*
========================
rvRenderModelMD5R::PurgeModel
========================
*/
void rvRenderModelMD5R::PurgeModel() {
	idRenderModelStatic::PurgeModel();
	for ( int meshIndex = 0; meshIndex < meshes.Num(); ++meshIndex ) {
		if ( meshes[ meshIndex ].deformInfo != NULL ) {
			R_FreeDeformInfo( meshes[ meshIndex ].deformInfo );
			meshes[ meshIndex ].deformInfo = NULL;
		}
		meshes[ meshIndex ].baseDrawVerts.Clear();
	}
	vertexBuffers.Clear();
	indexBuffers.Clear();
	silEdges.Clear();
	lods.Clear();
	allLODMeshes.Clear();
	meshes.Clear();
	joints.Clear();
	defaultPose.Clear();
	skinSpaceToLocalMats.Clear();
	commandLine.Clear();
	md5rVersion = 0;
	metadataOnly = false;
	geometrySectionsSkipped = false;
	hasSky = false;
	sharedVertexBuffers = NULL;
	sharedIndexBuffers = NULL;
	sharedSilEdges = NULL;
}

/*
========================
rvRenderModelMD5R::Print
========================
*/
void rvRenderModelMD5R::Print() const {
	int totalDrawVerts = 0;
	int totalDrawTris = 0;
	int totalPrimBatches = 0;

	common->Printf( "%s\n", name.c_str() );
	if ( metadataOnly ) {
		common->Printf( "MD5R metadata parsed; packed surface generation is still pending in this build.\n" );
	} else if ( geometrySectionsSkipped ) {
		common->Printf( "MD5R model with partial static-surface coverage; some packed buffers were not decoded.\n" );
	} else {
		common->Printf( "MD5R model.\n" );
	}

	common->Printf(
		"bounds: (%f %f %f) to (%f %f %f)\n",
		bounds[0][0], bounds[0][1], bounds[0][2],
		bounds[1][0], bounds[1][1], bounds[1][2] );
	common->Printf( "    lod prim drawV drawT material\n" );

	for ( int i = 0; i < meshes.Num(); ++i ) {
		const rvMD5RMesh &mesh = meshes[ i ];
		const char *materialName = ( mesh.material != NULL ) ? mesh.material->GetName() : mesh.materialName.c_str();

		totalDrawVerts += mesh.numDrawVertices;
		totalDrawTris += mesh.numDrawPrimitives;
		totalPrimBatches += mesh.primBatches.Num();

		common->Printf(
			"%2i: %3i %4i %5i %5i %s\n",
			i,
			mesh.levelOfDetail,
			mesh.primBatches.Num(),
			mesh.numDrawVertices,
			mesh.numDrawPrimitives,
			materialName );
	}

	common->Printf( "-----\n" );
	common->Printf( "%4i draw verts.\n", totalDrawVerts );
	common->Printf( "%4i draw tris.\n", totalDrawTris );
	common->Printf( "%4i prim batches.\n", totalPrimBatches );
	common->Printf( "%4i joints.\n", joints.Num() );
}

/*
=======================
rvRenderModelMD5R::List
=======================
*/
void rvRenderModelMD5R::List() const {
	int totalDrawVerts = 0;
	int totalDrawTris = 0;

	for ( int i = 0; i < meshes.Num(); ++i ) {
		totalDrawVerts += meshes[ i ].numDrawVertices;
		totalDrawTris += meshes[ i ].numDrawPrimitives;
	}

	common->Printf( " %4ik %3i %4i %4i %s(MD5R)", Memory() / 1024, meshes.Num(), totalDrawVerts, totalDrawTris, Name() );

	if ( metadataOnly ) {
		common->Printf( " (METADATA-ONLY)" );
	} else if ( geometrySectionsSkipped ) {
		common->Printf( " (PARTIAL)" );
	}
	if ( defaulted ) {
		common->Printf( " (DEFAULTED)" );
	}

	common->Printf( "\n" );
}

/*
========================
rvRenderModelMD5R::TouchData
========================
*/
void rvRenderModelMD5R::TouchData() {
	idRenderModelStatic::TouchData();

	for ( int i = 0; i < meshes.Num(); ++i ) {
		const char *materialName = ( meshes[ i ].material != NULL ) ? meshes[ i ].material->GetName() : meshes[ i ].materialName.c_str();
		if ( materialName != NULL && materialName[ 0 ] != '\0' ) {
			declManager->FindMaterial( materialName );
		}
	}
}

/*
========================
rvRenderModelMD5R::InstantiateDynamicModel
========================
*/
idRenderModel *rvRenderModelMD5R::InstantiateDynamicModel( const renderEntity_s *ent, const viewDef_s *view, idRenderModel *cachedModel ) {
	return InstantiateDynamicModel( ent, view, cachedModel, static_cast<dword>( ~SURF_COLLISION ) );
}

/*
========================
rvRenderModelMD5R::InstantiateDynamicModel
========================
*/
idRenderModel *rvRenderModelMD5R::InstantiateDynamicModel( const renderEntity_s *ent, const viewDef_s *view, idRenderModel *cachedModel, dword surfMask ) {
	if ( joints.Num() == 0 ) {
		return this;
	}

	if ( cachedModel != NULL && !r_useCachedDynamicModels.GetBool() ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if ( purged ) {
		common->DWarning( "model %s instantiated while purged", Name() );
		LoadModel();
	}

	if ( ent == NULL || ent->joints == NULL ) {
		common->Printf( "rvRenderModelMD5R::InstantiateDynamicModel: NULL joints on renderEntity for '%s'\n", Name() );
		delete cachedModel;
		return NULL;
	}

	if ( ent->numJoints != joints.Num() ) {
		common->Printf( "rvRenderModelMD5R::InstantiateDynamicModel: renderEntity has different number of joints than model for '%s'\n", Name() );
		delete cachedModel;
		return NULL;
	}

	const idJointMat *entJoints = ent->joints;
	if ( skinSpaceToLocalMats.Num() == joints.Num() ) {
		idJointMat *transformedJoints = (idJointMat *)_alloca16( joints.Num() * sizeof( transformedJoints[ 0 ] ) );
		SIMDProcessor->MultiplyJoints( transformedJoints, ent->joints, skinSpaceToLocalMats.Ptr(), joints.Num() );
		entJoints = transformedJoints;
	}

	idRenderModelStatic *staticModel = NULL;
	if ( cachedModel != NULL ) {
		assert( dynamic_cast<idRenderModelStatic *>( cachedModel ) != NULL );
		staticModel = static_cast<idRenderModelStatic *>( cachedModel );
	} else {
		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty( MD5R_SnapshotName );
	}

	staticModel->bounds.Clear();

	if ( r_showSkel.GetInteger() > 1 ) {
		staticModel->InitEmpty( MD5R_SnapshotName );
		return staticModel;
	}

	int selectedLOD = 0;
	if ( lods.Num() > 0 && view != NULL ) {
		const float distanceSquared = ( view->renderView.vieworg - ent->origin ).LengthSqr();
		while ( selectedLOD < lods.Num() && distanceSquared >= lods[ selectedLOD ].rangeEndSquared ) {
			++selectedLOD;
		}
	}

	idList<bool> activeMeshes;
	activeMeshes.SetNum( meshes.Num() );
	for ( int meshIndex = 0; meshIndex < activeMeshes.Num(); ++meshIndex ) {
		activeMeshes[ meshIndex ] = false;
	}

	for ( int lodMeshIndex = 0; lodMeshIndex < allLODMeshes.Num(); ++lodMeshIndex ) {
		const int meshIndex = allLODMeshes[ lodMeshIndex ];
		if ( meshIndex >= 0 && meshIndex < activeMeshes.Num() ) {
			activeMeshes[ meshIndex ] = true;
		}
	}

	if ( selectedLOD >= 0 && selectedLOD < lods.Num() ) {
		for ( int lodMeshIndex = 0; lodMeshIndex < lods[ selectedLOD ].meshIndexes.Num(); ++lodMeshIndex ) {
			const int meshIndex = lods[ selectedLOD ].meshIndexes[ lodMeshIndex ];
			if ( meshIndex >= 0 && meshIndex < activeMeshes.Num() ) {
				activeMeshes[ meshIndex ] = true;
			}
		}
	}

	for ( int meshIndex = 0; meshIndex < meshes.Num(); ++meshIndex ) {
		rvMD5RMesh &mesh = meshes[ meshIndex ];

		if ( !activeMeshes[ meshIndex ] ) {
			staticModel->DeleteSurfaceWithId( mesh.meshIdentifier );
			staticModel->DeleteSurfaceWithId( mesh.meshIdentifier + MD5R_BackSideSurfaceIdOffset );
			mesh.surfaceNum = -1;
			continue;
		}

		GenerateDynamicSurface( *staticModel, mesh, *ent, entJoints, surfMask );
	}

	return staticModel;
}

/*
========================
rvRenderModelMD5R::IsDynamicModel
========================
*/
dynamicModel_t rvRenderModelMD5R::IsDynamicModel() const {
	return joints.Num() > 0 ? DM_CACHED : DM_STATIC;
}

/*
========================
rvRenderModelMD5R::Bounds
========================
*/
idBounds rvRenderModelMD5R::Bounds( const renderEntity_s *ent ) const {
	if ( ent != NULL && joints.Num() > 0 ) {
		return ent->bounds;
	}
	return bounds;
}

/*
========================
rvRenderModelMD5R::HasCollisionSurface
========================
*/
bool rvRenderModelMD5R::HasCollisionSurface( const renderEntity_s *ent ) const {
	for ( int i = 0; i < meshes.Num(); ++i ) {
		const idMaterial *shader = meshes[ i ].material;
		if ( ent != NULL ) {
			shader = R_RemapShaderBySkin( shader, ent->customSkin, ent->customShader );
		}
		if ( shader == NULL ) {
			continue;
		}

		if ( shader->IsDedicatedCollisionSurface() ) {
			return true;
		}
	}

	return false;
}

/*
========================
rvRenderModelMD5R::NumJoints
========================
*/
int rvRenderModelMD5R::NumJoints() const {
	return joints.Num();
}

/*
========================
rvRenderModelMD5R::GetJoints
========================
*/
const idMD5Joint *rvRenderModelMD5R::GetJoints() const {
	return joints.Num() > 0 ? joints.Ptr() : NULL;
}

/*
========================
rvRenderModelMD5R::GetJointHandle
========================
*/
jointHandle_t rvRenderModelMD5R::GetJointHandle( const char *name ) const {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		return INVALID_JOINT;
	}

	for ( int i = 0; i < joints.Num(); ++i ) {
		if ( idStr::Icmp( joints[ i ].name.c_str(), name ) == 0 ) {
			return static_cast<jointHandle_t>( i );
		}
	}

	return INVALID_JOINT;
}

/*
=========================
rvRenderModelMD5R::GetJointName
=========================
*/
const char *rvRenderModelMD5R::GetJointName( jointHandle_t handle ) const {
	if ( handle < 0 || handle >= joints.Num() ) {
		return "<invalid joint>";
	}

	return joints[ handle ].name;
}

/*
========================
rvRenderModelMD5R::GetDefaultPose
========================
*/
const idJointQuat *rvRenderModelMD5R::GetDefaultPose() const {
	return defaultPose.Num() > 0 ? defaultPose.Ptr() : NULL;
}

/*
========================
rvRenderModelMD5R::GetSkinSpaceToLocalMats
========================
*/
const idJointMat *rvRenderModelMD5R::GetSkinSpaceToLocalMats() const {
	return skinSpaceToLocalMats.Num() > 0 ? skinSpaceToLocalMats.Ptr() : NULL;
}

/*
========================
rvRenderModelMD5R::NearestJoint
========================
*/
int rvRenderModelMD5R::NearestJoint( int surfaceNum, int a, int b, int c ) const {
	if ( surfaceNum > meshes.Num() ) {
		common->Error( "rvRenderModelMD5R::NearestJoint: surfaceNum > meshes.Num()" );
	}

	const idList<rvMD5RVertexBufferDesc> &vertexBuffers = GetVertexBuffers();

	for ( int meshIndex = 0; meshIndex < meshes.Num(); ++meshIndex ) {
		const rvMD5RMesh &mesh = meshes[ meshIndex ];
		if ( mesh.surfaceNum != surfaceNum ) {
			continue;
		}

		if ( mesh.drawVertexBuffer < 0 || mesh.drawVertexBuffer >= vertexBuffers.Num() ) {
			return 0;
		}

		int meshVertexIndex = -1;
		if ( a >= 0 && a < mesh.numDrawVertices ) {
			meshVertexIndex = a;
		} else if ( b >= 0 && b < mesh.numDrawVertices ) {
			meshVertexIndex = b;
		} else if ( c >= 0 && c < mesh.numDrawVertices ) {
			meshVertexIndex = c;
		} else {
			return 0;
		}

		int sourceVertexIndex = -1;
		const rvMD5RPrimBatch *sourcePrimBatch = NULL;
		if ( !R_MD5R_MapMeshVertexToSourceVertex( mesh, meshVertexIndex, sourceVertexIndex, sourcePrimBatch ) || sourcePrimBatch == NULL ) {
			return 0;
		}

		return R_MD5R_GetDominantBlendJoint(
			vertexBuffers[ mesh.drawVertexBuffer ],
			sourceVertexIndex,
			*sourcePrimBatch,
			joints.Num() );
	}

	return 0;
}

/*
========================
rvRenderModelMD5R::GetSurfaceMask
========================
*/
int rvRenderModelMD5R::GetSurfaceMask( const char *surface ) const {
	if ( surface == NULL || surface[ 0 ] == '\0' ) {
		return 0;
	}

	unsigned int mask = 0;
	for ( int i = 0; i < meshes.Num(); ++i ) {
		const rvMD5RMesh &mesh = meshes[ i ];
		if ( mesh.meshIdentifier < 0 || mesh.meshIdentifier >= static_cast<int>( sizeof( mask ) * 8 ) ) {
			continue;
		}

		const char *materialName = ( mesh.material != NULL ) ? mesh.material->GetName() : mesh.materialName.c_str();
		if ( materialName != NULL && idStr::Icmp( materialName, surface ) == 0 ) {
			mask |= ( 1u << mesh.meshIdentifier );
		}
	}

	return static_cast<int>( mask );
}

/*
========================
rvRenderModelMD5R::Memory
========================
*/
int rvRenderModelMD5R::Memory() const {
	const idList<rvMD5RVertexBufferDesc> &vertexBuffers = GetVertexBuffers();
	const idList<rvMD5RIndexBufferDesc> &indexBuffers = GetIndexBuffers();
	const idList<silEdge_t> &silEdges = GetSilhouetteEdges();
	const bool ownsPackedBuffers = ( sharedVertexBuffers == NULL && sharedIndexBuffers == NULL && sharedSilEdges == NULL );

	int total = idRenderModelStatic::Memory();
	total += lods.MemoryUsed();
	total += allLODMeshes.MemoryUsed();
	total += meshes.MemoryUsed();
	total += joints.MemoryUsed();
	total += defaultPose.MemoryUsed();
	total += skinSpaceToLocalMats.MemoryUsed();
	total += commandLine.DynamicMemoryUsed();

	if ( ownsPackedBuffers ) {
		total += vertexBuffers.MemoryUsed();
		total += indexBuffers.MemoryUsed();
		total += silEdges.MemoryUsed();
	}

	for ( int i = 0; i < lods.Num(); ++i ) {
		total += lods[ i ].meshIndexes.MemoryUsed();
	}

	for ( int i = 0; i < meshes.Num(); ++i ) {
		total += meshes[ i ].materialName.DynamicMemoryUsed();
		total += meshes[ i ].primBatches.MemoryUsed();

		for ( int j = 0; j < meshes[ i ].primBatches.Num(); ++j ) {
			total += meshes[ i ].primBatches[ j ].transformPalette.MemoryUsed();
		}
	}

	for ( int i = 0; i < joints.Num(); ++i ) {
		total += joints[ i ].name.DynamicMemoryUsed();
	}

	if ( ownsPackedBuffers ) {
		for ( int i = 0; i < vertexBuffers.Num(); ++i ) {
			total += vertexBuffers[ i ].positions.MemoryUsed();
			total += vertexBuffers[ i ].blendIndices.MemoryUsed();
			total += vertexBuffers[ i ].blendWeights.MemoryUsed();
			total += vertexBuffers[ i ].normals.MemoryUsed();
			total += vertexBuffers[ i ].tangents.MemoryUsed();
			total += vertexBuffers[ i ].binormals.MemoryUsed();
			total += vertexBuffers[ i ].diffuseColors.MemoryUsed();
			total += vertexBuffers[ i ].specularColors.MemoryUsed();
			total += vertexBuffers[ i ].pointSizes.MemoryUsed();
			for ( int texCoordSet = 0; texCoordSet < 7; ++texCoordSet ) {
				total += vertexBuffers[ i ].texCoords[ texCoordSet ].MemoryUsed();
			}
		}

		for ( int i = 0; i < indexBuffers.Num(); ++i ) {
			total += indexBuffers[ i ].indices.MemoryUsed();
		}
	}

	for ( int i = 0; i < meshes.Num(); ++i ) {
		total += meshes[ i ].baseDrawVerts.MemoryUsed();
		if ( meshes[ i ].deformInfo != NULL ) {
			total += R_DeformInfoMemoryUsed( meshes[ i ].deformInfo );
		}
	}

	return total;
}

/*
========================
rvRenderModelMD5R::BuildExportFileName
========================
*/
idStr rvRenderModelMD5R::BuildExportFileName() const {
	idStr exportName = name;
	exportName.StripAbsoluteFileExtension();

	if ( source == MD5R_SOURCE_LWO_ASE_FLT ) {
		exportName += "_static";
	}

	exportName += ".md5r";
	return exportName;
}

/*
========================
rvRenderModelMD5R::CanWriteModelData
========================
*/
bool rvRenderModelMD5R::CanWriteModelData( idStr &reason ) const {
	reason.Clear();

	if ( source == MD5R_SOURCE_PROC ) {
		reason = "proc-world MD5R data must be exported through idRenderWorldLocal::WriteMD5R";
		return false;
	}

	if ( defaulted || IsDefaultModel() ) {
		reason = "the model defaulted during load";
		return false;
	}

	if ( metadataOnly ) {
		reason = "the packed buffers were only parsed as metadata";
		return false;
	}

	if ( geometrySectionsSkipped ) {
		reason = "some packed geometry sections could not be decoded into canonical CPU data";
		return false;
	}

	const idList<rvMD5RVertexBufferDesc> &vertexBuffers = GetVertexBuffers();
	const idList<rvMD5RIndexBufferDesc> &indexBuffers = GetIndexBuffers();

	for ( int vertexBufferIndex = 0; vertexBufferIndex < vertexBuffers.Num(); ++vertexBufferIndex ) {
		const rvMD5RVertexBufferDesc &vertexBuffer = vertexBuffers[ vertexBufferIndex ];
		const rvMD5RVertexFormatDesc &format = vertexBuffer.loadVertexFormat;

		if ( format.hasPosition && vertexBuffer.positions.Num() != vertexBuffer.numVertices ) {
			reason = va( "vertex buffer %d position data was not fully decoded", vertexBufferIndex );
			return false;
		}

		if ( format.hasBlendIndex && vertexBuffer.blendIndices.Num() != vertexBuffer.numVertices ) {
			reason = va( "vertex buffer %d blend-index data was not fully decoded", vertexBufferIndex );
			return false;
		}

		if ( format.hasBlendWeight && vertexBuffer.blendWeights.Num() != vertexBuffer.numVertices ) {
			reason = va( "vertex buffer %d blend-weight data was not fully decoded", vertexBufferIndex );
			return false;
		}

		if ( format.hasNormal && vertexBuffer.normals.Num() != vertexBuffer.numVertices ) {
			reason = va( "vertex buffer %d normal data was not fully decoded", vertexBufferIndex );
			return false;
		}

		if ( format.hasTangent && vertexBuffer.tangents.Num() != vertexBuffer.numVertices ) {
			reason = va( "vertex buffer %d tangent data was not fully decoded", vertexBufferIndex );
			return false;
		}

		if ( format.hasBinormal && vertexBuffer.binormals.Num() != vertexBuffer.numVertices ) {
			reason = va( "vertex buffer %d binormal data was not fully decoded", vertexBufferIndex );
			return false;
		}

		if ( format.hasDiffuseColor && vertexBuffer.diffuseColors.Num() != vertexBuffer.numVertices ) {
			reason = va( "vertex buffer %d diffuse-color data was not fully decoded", vertexBufferIndex );
			return false;
		}

		if ( format.hasSpecularColor && vertexBuffer.specularColors.Num() != vertexBuffer.numVertices ) {
			reason = va( "vertex buffer %d specular-color data was not fully decoded", vertexBufferIndex );
			return false;
		}

		if ( format.hasPointSize && vertexBuffer.pointSizes.Num() != vertexBuffer.numVertices ) {
			reason = va( "vertex buffer %d point-size data was not fully decoded", vertexBufferIndex );
			return false;
		}

		for ( int texCoordSet = 0; texCoordSet < 7; ++texCoordSet ) {
			if ( !format.hasTexCoord[ texCoordSet ] ) {
				continue;
			}

			if ( vertexBuffer.texCoords[ texCoordSet ].Num() != vertexBuffer.numVertices ) {
				reason = va(
					"vertex buffer %d texcoord set %d was not fully decoded",
					vertexBufferIndex,
					texCoordSet );
				return false;
			}
		}
	}

	for ( int indexBufferIndex = 0; indexBufferIndex < indexBuffers.Num(); ++indexBufferIndex ) {
		if ( indexBuffers[ indexBufferIndex ].indices.Num() != indexBuffers[ indexBufferIndex ].numIndices ) {
			reason = va( "index buffer %d was not fully decoded", indexBufferIndex );
			return false;
		}
	}

	for ( int meshIndex = 0; meshIndex < meshes.Num(); ++meshIndex ) {
		const rvMD5RMesh &mesh = meshes[ meshIndex ];

		if ( mesh.drawVertexBuffer >= vertexBuffers.Num() || mesh.drawIndexBuffer >= indexBuffers.Num()
			|| mesh.silTraceVertexBuffer >= vertexBuffers.Num() || mesh.silTraceIndexBuffer >= indexBuffers.Num()
			|| mesh.shadowVolVertexBuffer >= vertexBuffers.Num() || mesh.shadowVolIndexBuffer >= indexBuffers.Num() ) {
			reason = va( "mesh %d references an invalid vertex or index buffer", meshIndex );
			return false;
		}

		for ( int primBatchIndex = 0; primBatchIndex < mesh.primBatches.Num(); ++primBatchIndex ) {
			const rvMD5RPrimBatch &primBatch = mesh.primBatches[ primBatchIndex ];
			if ( primBatch.transformPalette.Num() > 0 && primBatch.transformPalette.Num() != primBatch.numTransforms ) {
				reason = va( "mesh %d primitive batch %d has an incomplete transform palette", meshIndex, primBatchIndex );
				return false;
			}
		}
	}

	return true;
}

/*
========================
rvRenderModelMD5R::WriteVertexFormat
========================
*/
void rvRenderModelMD5R::WriteVertexFormat( idFile &outFile, const rvMD5RVertexFormatDesc &vertexFormat, const char *prepend ) {
	idStr innerIndent = prepend;
	innerIndent += "\t";

	outFile.WriteFloatString( "%sVertexFormat\n", prepend );
	outFile.WriteFloatString( "%s{\n", prepend );

	if ( vertexFormat.hasPosition ) {
		outFile.WriteFloatString(
			"%sPosition %d Float\n",
			innerIndent.c_str(),
			vertexFormat.positionSwizzled ? 3 : vertexFormat.positionDim );
	}

	if ( vertexFormat.hasBlendIndex ) {
		outFile.WriteFloatString( "%sBlendIndex Int\n", innerIndent.c_str() );
	}

	if ( vertexFormat.hasBlendWeight ) {
		outFile.WriteFloatString(
			"%sBlendWeight %d %d Float\n",
			innerIndent.c_str(),
			vertexFormat.blendWeightDim,
			vertexFormat.blendWeightTransformCount );
	}

	if ( vertexFormat.hasNormal ) {
		outFile.WriteFloatString( "%sNormal Float\n", innerIndent.c_str() );
	}

	if ( vertexFormat.hasTangent ) {
		outFile.WriteFloatString( "%sTangent Float\n", innerIndent.c_str() );
	}

	if ( vertexFormat.hasBinormal ) {
		outFile.WriteFloatString( "%sBinormal Float\n", innerIndent.c_str() );
	}

	if ( vertexFormat.hasDiffuseColor ) {
		outFile.WriteFloatString( "%sDiffuseColor Int\n", innerIndent.c_str() );
	}

	for ( int texCoordSet = 0; texCoordSet < 7; ++texCoordSet ) {
		if ( !vertexFormat.hasTexCoord[ texCoordSet ] ) {
			continue;
		}

		outFile.WriteFloatString(
			"%sTexCoord %d %d Float\n",
			innerIndent.c_str(),
			vertexFormat.texCoordDim[ texCoordSet ],
			texCoordSet );
	}

	outFile.WriteFloatString( "%s}\n", prepend );
}

/*
========================
rvRenderModelMD5R::WriteVertexBuffer
========================
*/
void rvRenderModelMD5R::WriteVertexBuffer( idFile &outFile, const rvMD5RVertexBufferDesc &vertexBuffer, const char *prepend ) {
	rvMD5RVertexFormatDesc format = vertexBuffer.loadVertexFormat;
	if ( format.hasPosition && format.positionSwizzled ) {
		format.positionSwizzled = false;
		format.positionDim = 3;
	}

	idStr innerIndent = prepend;
	idStr vertexIndent;

	innerIndent += "\t";
	vertexIndent = innerIndent;
	vertexIndent += "\t";

	outFile.WriteFloatString( "%sVertexBuffer\n", prepend );
	outFile.WriteFloatString( "%s{\n", prepend );
	WriteVertexFormat( outFile, format, innerIndent.c_str() );

	if ( vertexBuffer.systemMemory ) {
		outFile.WriteFloatString( "%sSystemMemory\n", innerIndent.c_str() );
	}
	if ( vertexBuffer.videoMemory || !vertexBuffer.systemMemory ) {
		outFile.WriteFloatString( "%sVideoMemory\n", innerIndent.c_str() );
	}

	outFile.WriteFloatString( "%sVertex [ %d ]\n", innerIndent.c_str(), vertexBuffer.numVertices );
	outFile.WriteFloatString( "%s{\n", innerIndent.c_str() );

	for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
		outFile.WriteFloatString( "%s", vertexIndent.c_str() );

		if ( format.hasPosition ) {
			for ( int component = 0; component < ( format.positionSwizzled ? 3 : format.positionDim ); ++component ) {
				outFile.WriteFloatString( "%f ", vertexBuffer.positions[ vertexIndex ][ component ] );
			}
		}

		if ( format.hasBlendIndex ) {
			outFile.WriteFloatString( "%d ", static_cast<int>( vertexBuffer.blendIndices[ vertexIndex ] ) );
		}

		if ( format.hasBlendWeight ) {
			for ( int component = 0; component < format.blendWeightDim; ++component ) {
				outFile.WriteFloatString( "%f ", vertexBuffer.blendWeights[ vertexIndex ][ component ] );
			}
		}

		if ( format.hasNormal ) {
			outFile.WriteFloatString(
				"%f %f %f ",
				vertexBuffer.normals[ vertexIndex ].x,
				vertexBuffer.normals[ vertexIndex ].y,
				vertexBuffer.normals[ vertexIndex ].z );
		}

		if ( format.hasTangent ) {
			outFile.WriteFloatString(
				"%f %f %f ",
				vertexBuffer.tangents[ vertexIndex ].x,
				vertexBuffer.tangents[ vertexIndex ].y,
				vertexBuffer.tangents[ vertexIndex ].z );
		}

		if ( format.hasBinormal ) {
			outFile.WriteFloatString(
				"%f %f %f ",
				vertexBuffer.binormals[ vertexIndex ].x,
				vertexBuffer.binormals[ vertexIndex ].y,
				vertexBuffer.binormals[ vertexIndex ].z );
		}

		if ( format.hasDiffuseColor ) {
			outFile.WriteFloatString( "%d ", static_cast<int>( vertexBuffer.diffuseColors[ vertexIndex ] ) );
		}

		if ( format.hasSpecularColor ) {
			outFile.WriteFloatString( "%d ", static_cast<int>( vertexBuffer.specularColors[ vertexIndex ] ) );
		}

		if ( format.hasPointSize ) {
			outFile.WriteFloatString( "%f ", vertexBuffer.pointSizes[ vertexIndex ] );
		}

		for ( int texCoordSet = 0; texCoordSet < 7; ++texCoordSet ) {
			if ( !format.hasTexCoord[ texCoordSet ] ) {
				continue;
			}

			for ( int component = 0; component < format.texCoordDim[ texCoordSet ]; ++component ) {
				outFile.WriteFloatString( "%f ", vertexBuffer.texCoords[ texCoordSet ][ vertexIndex ][ component ] );
			}
		}

		outFile.WriteFloatString( "\n" );
	}

	outFile.WriteFloatString( "%s}\n", innerIndent.c_str() );
	outFile.WriteFloatString( "%s}\n", prepend );
}

/*
========================
rvRenderModelMD5R::WriteVertexBuffers
========================
*/
void rvRenderModelMD5R::WriteSharedVertexBuffers( idFile &outFile, const idList<rvMD5RVertexBufferDesc> &vertexBuffers, const char *prepend ) {
	idStr innerIndent = prepend;
	innerIndent += "\t";

	outFile.WriteFloatString( "%sVertexBuffer[ %d ]\n", prepend, vertexBuffers.Num() );
	outFile.WriteFloatString( "%s{\n", prepend );
	for ( int i = 0; i < vertexBuffers.Num(); ++i ) {
		WriteVertexBuffer( outFile, vertexBuffers[ i ], innerIndent.c_str() );
	}
	outFile.WriteFloatString( "%s}\n", prepend );
}

/*
========================
rvRenderModelMD5R::WriteVertexBuffers
========================
*/
void rvRenderModelMD5R::WriteVertexBuffers( idFile &outFile, const char *prepend ) const {
	WriteSharedVertexBuffers( outFile, GetVertexBuffers(), prepend );
}

/*
========================
rvRenderModelMD5R::WriteIndexBuffer
========================
*/
void rvRenderModelMD5R::WriteIndexBuffer( idFile &outFile, const rvMD5RIndexBufferDesc &indexBuffer, const char *prepend ) {
	idStr innerIndent = prepend;
	idStr indexIndent;

	innerIndent += "\t";
	indexIndent = innerIndent;
	indexIndent += "\t";

	outFile.WriteFloatString( "%sIndexBuffer\n", prepend );
	outFile.WriteFloatString( "%s{\n", prepend );

	if ( indexBuffer.systemMemory ) {
		outFile.WriteFloatString( "%sSystemMemory\n", innerIndent.c_str() );
	}
	if ( indexBuffer.videoMemory || !indexBuffer.systemMemory ) {
		outFile.WriteFloatString( "%sVideoMemory\n", innerIndent.c_str() );
	}

	outFile.WriteFloatString( "%sBitDepth %d\n", innerIndent.c_str(), indexBuffer.bitDepth );
	outFile.WriteFloatString( "%sIndex [ %d ]\n", innerIndent.c_str(), indexBuffer.numIndices );
	outFile.WriteFloatString( "%s{\n", innerIndent.c_str() );
	for ( int index = 0; index < indexBuffer.numIndices; ++index ) {
		outFile.WriteFloatString( "%s%d\n", indexIndent.c_str(), indexBuffer.indices[ index ] );
	}
	outFile.WriteFloatString( "%s}\n", innerIndent.c_str() );
	outFile.WriteFloatString( "%s}\n", prepend );
}

/*
========================
rvRenderModelMD5R::WriteIndexBuffers
========================
*/
void rvRenderModelMD5R::WriteSharedIndexBuffers( idFile &outFile, const idList<rvMD5RIndexBufferDesc> &indexBuffers, const char *prepend ) {
	idStr innerIndent = prepend;
	innerIndent += "\t";

	outFile.WriteFloatString( "%sIndexBuffer[ %d ]\n", prepend, indexBuffers.Num() );
	outFile.WriteFloatString( "%s{\n", prepend );
	for ( int i = 0; i < indexBuffers.Num(); ++i ) {
		WriteIndexBuffer( outFile, indexBuffers[ i ], innerIndent.c_str() );
	}
	outFile.WriteFloatString( "%s}\n", prepend );
}

/*
========================
rvRenderModelMD5R::WriteIndexBuffers
========================
*/
void rvRenderModelMD5R::WriteIndexBuffers( idFile &outFile, const char *prepend ) const {
	WriteSharedIndexBuffers( outFile, GetIndexBuffers(), prepend );
}

/*
========================
rvRenderModelMD5R::WriteSilhouetteEdges
========================
*/
void rvRenderModelMD5R::WriteSharedSilhouetteEdges( idFile &outFile, const idList<silEdge_t> &silEdges, const char *prepend ) {
	if ( silEdges.Num() == 0 ) {
		return;
	}

	idStr innerIndent = prepend;
	innerIndent += "\t";

	outFile.WriteFloatString( "%sSilhouetteEdge[ %d ]\n", prepend, silEdges.Num() );
	outFile.WriteFloatString( "%s{\n", prepend );
	for ( int i = 0; i < silEdges.Num(); ++i ) {
		outFile.WriteFloatString(
			"%s%d %d %d %d\n",
			innerIndent.c_str(),
			silEdges[ i ].p1,
			silEdges[ i ].p2,
			silEdges[ i ].v1,
			silEdges[ i ].v2 );
	}
	outFile.WriteFloatString( "%s}\n", prepend );
}

/*
========================
rvRenderModelMD5R::WriteSilhouetteEdges
========================
*/
void rvRenderModelMD5R::WriteSilhouetteEdges( idFile &outFile, const char *prepend ) const {
	WriteSharedSilhouetteEdges( outFile, GetSilhouetteEdges(), prepend );
}

/*
========================
rvRenderModelMD5R::WriteLevelsOfDetail
========================
*/
void rvRenderModelMD5R::WriteLevelsOfDetail( idFile &outFile, const char *prepend ) const {
	if ( lods.Num() == 0 ) {
		return;
	}

	const int numDefaultRanges = static_cast<int>( sizeof( MD5R_DefaultLODRanges ) / sizeof( MD5R_DefaultLODRanges[ 0 ] ) );
	bool writeLODSection = ( lods.Num() > numDefaultRanges );
	if ( !writeLODSection ) {
		for ( int i = 0; i < lods.Num(); ++i ) {
			if ( lods[ i ].rangeEnd != R_MD5R_GetDefaultLODRange( i ) ) {
				writeLODSection = true;
				break;
			}
		}
	}
	if ( !writeLODSection ) {
		return;
	}

	idStr innerIndent = prepend;
	innerIndent += "\t";

	outFile.WriteFloatString( "%sLevelOfDetail[ %d ]\n", prepend, lods.Num() );
	outFile.WriteFloatString( "%s{\n", prepend );
	for ( int i = 0; i < lods.Num(); ++i ) {
		outFile.WriteFloatString( "%s%f\n", innerIndent.c_str(), lods[ i ].rangeEnd );
	}
	outFile.WriteFloatString( "%s}\n", prepend );
}

/*
========================
rvRenderModelMD5R::WritePrimBatch
========================
*/
void rvRenderModelMD5R::WritePrimBatch( idFile &outFile, const rvMD5RPrimBatch &primBatch, const char *prepend ) const {
	idStr innerIndent = prepend;
	innerIndent += "\t";

	outFile.WriteFloatString( "%sPrimBatch\n", prepend );
	outFile.WriteFloatString( "%s{\n", prepend );

	if ( primBatch.transformPalette.Num() > 0 ) {
		idStr transformIndent = innerIndent;
		transformIndent += "\t";

		outFile.WriteFloatString( "%sTransform [ %d ]\n", innerIndent.c_str(), primBatch.numTransforms );
		outFile.WriteFloatString( "%s{\n", innerIndent.c_str() );
		for ( int transformIndex = 0; transformIndex < primBatch.transformPalette.Num(); ++transformIndex ) {
			outFile.WriteFloatString( "%s%d\n", transformIndent.c_str(), primBatch.transformPalette[ transformIndex ] );
		}
		outFile.WriteFloatString( "%s}\n", innerIndent.c_str() );
	}

	if ( primBatch.hasSilTraceGeoSpec ) {
		outFile.WriteFloatString(
			"%sSilTraceIndexedTriList %d %d %d %d\n",
			innerIndent.c_str(),
			primBatch.silTraceGeoSpec.vertexStart,
			primBatch.silTraceGeoSpec.vertexCount,
			primBatch.silTraceGeoSpec.indexStart,
			primBatch.silTraceGeoSpec.primitiveCount );
	}

	if ( primBatch.hasDrawGeoSpec ) {
		outFile.WriteFloatString(
			"%sDrawIndexedTriList %d %d %d %d\n",
			innerIndent.c_str(),
			primBatch.drawGeoSpec.vertexStart,
			primBatch.drawGeoSpec.vertexCount,
			primBatch.drawGeoSpec.indexStart,
			primBatch.drawGeoSpec.primitiveCount );
	}

	if ( primBatch.hasShadowGeoSpec ) {
		if ( primBatch.shadowVolGeoSpec.primitiveCount == 0
			&& primBatch.shadowVolGeoSpec.indexStart == 0
			&& ( !primBatch.hasSilTraceGeoSpec
				|| primBatch.shadowVolGeoSpec.vertexCount == primBatch.silTraceGeoSpec.vertexCount * 2 ) ) {
			outFile.WriteFloatString(
				"%sShadowVerts %d\n",
				innerIndent.c_str(),
				primBatch.shadowVolGeoSpec.vertexStart );
		} else {
			outFile.WriteFloatString(
				"%sShadowIndexedTriList %d %d %d %d %d %d\n",
				innerIndent.c_str(),
				primBatch.shadowVolGeoSpec.vertexStart,
				primBatch.shadowVolGeoSpec.vertexCount,
				primBatch.shadowVolGeoSpec.indexStart,
				primBatch.shadowVolGeoSpec.primitiveCount,
				primBatch.numShadowPrimitivesNoCaps,
				primBatch.shadowCapPlaneBits );
		}
	}

	if ( primBatch.silEdgeCount > 0 ) {
		outFile.WriteFloatString(
			"%sSilhouetteEdge %d %d\n",
			innerIndent.c_str(),
			primBatch.silEdgeStart,
			primBatch.silEdgeCount );
	}

	outFile.WriteFloatString( "%s}\n", prepend );
}

/*
========================
rvRenderModelMD5R::WriteMesh
========================
*/
void rvRenderModelMD5R::WriteMesh( idFile &outFile, const rvMD5RMesh &mesh, const char *prepend ) const {
	idStr innerIndent = prepend;
	idStr primBatchIndent;

	innerIndent += "\t";
	primBatchIndent = innerIndent;
	primBatchIndent += "\t";

	outFile.WriteFloatString( "%sMesh\n", prepend );
	outFile.WriteFloatString( "%s{\n", prepend );

	if ( mesh.levelOfDetail >= 0 ) {
		outFile.WriteFloatString( "%sLevelOfDetail %d\n", innerIndent.c_str(), mesh.levelOfDetail );
	}

	outFile.WriteFloatString( "%sMaterial \"%s\"\n", innerIndent.c_str(), mesh.materialName.c_str() );

	if ( mesh.silTraceVertexBuffer >= 0 && mesh.silTraceIndexBuffer >= 0 ) {
		outFile.WriteFloatString(
			"%sSilTraceBuffers %d %d\n",
			innerIndent.c_str(),
			mesh.silTraceVertexBuffer,
			mesh.silTraceIndexBuffer );
	}

	if ( mesh.drawVertexBuffer >= 0 && mesh.drawIndexBuffer >= 0 ) {
		outFile.WriteFloatString(
			"%sDrawBuffers %d %d\n",
			innerIndent.c_str(),
			mesh.drawVertexBuffer,
			mesh.drawIndexBuffer );
	}

	if ( mesh.shadowVolVertexBuffer >= 0 && mesh.shadowVolIndexBuffer >= 0 ) {
		outFile.WriteFloatString(
			"%sShadowVolumeBuffers %d %d\n",
			innerIndent.c_str(),
			mesh.shadowVolVertexBuffer,
			mesh.shadowVolIndexBuffer );
	}

	outFile.WriteFloatString( "%sPrimBatch [ %d ]\n", innerIndent.c_str(), mesh.primBatches.Num() );
	outFile.WriteFloatString( "%s{\n", innerIndent.c_str() );
	for ( int primBatchIndex = 0; primBatchIndex < mesh.primBatches.Num(); ++primBatchIndex ) {
		WritePrimBatch( outFile, mesh.primBatches[ primBatchIndex ], primBatchIndent.c_str() );
	}
	outFile.WriteFloatString( "%s}\n", innerIndent.c_str() );

	if ( !mesh.bounds.IsCleared() ) {
		outFile.WriteFloatString(
			"%sBounds %f %f %f  %f %f %f\n",
			innerIndent.c_str(),
			mesh.bounds[ 0 ].x,
			mesh.bounds[ 0 ].y,
			mesh.bounds[ 0 ].z,
			mesh.bounds[ 1 ].x,
			mesh.bounds[ 1 ].y,
			mesh.bounds[ 1 ].z );
	}

	outFile.WriteFloatString( "%s}\n", prepend );
}

/*
========================
rvRenderModelMD5R::WriteMeshes
========================
*/
void rvRenderModelMD5R::WriteMeshes( idFile &outFile, const char *prepend ) const {
	idStr innerIndent = prepend;
	innerIndent += "\t";

	outFile.WriteFloatString( "%sMesh[ %d ]\n", prepend, meshes.Num() );
	outFile.WriteFloatString( "%s{\n", prepend );
	for ( int i = 0; i < meshes.Num(); ++i ) {
		WriteMesh( outFile, meshes[ i ], innerIndent.c_str() );
	}
	outFile.WriteFloatString( "%s}\n", prepend );
}

/*
========================
rvRenderModelMD5R::WriteJoints
========================
*/
void rvRenderModelMD5R::WriteJoints( idFile &outFile, const char *prepend ) const {
	if ( joints.Num() == 0 ) {
		return;
	}

	idStr innerIndent = prepend;
	innerIndent += "\t";

	outFile.WriteFloatString( "%sJoint[ %d ]\n", prepend, joints.Num() );
	outFile.WriteFloatString( "%s{\n", prepend );
	for ( int jointIndex = 0; jointIndex < joints.Num(); ++jointIndex ) {
		idJointMat worldPose = skinSpaceToLocalMats[ jointIndex ];
		worldPose.Invert();

		const idJointQuat jointPose = worldPose.ToJointQuat();
		const idCQuat compressedQuat = jointPose.q.ToCQuat();
		const int parentIndex = ( joints[ jointIndex ].parent != NULL )
			? static_cast<int>( joints[ jointIndex ].parent - joints.Ptr() )
			: -1;

		outFile.WriteFloatString(
			"%s\"%s\" %d  %f %f %f  %f %f %f\n",
			innerIndent.c_str(),
			joints[ jointIndex ].name.c_str(),
			parentIndex,
			jointPose.t.x,
			jointPose.t.y,
			jointPose.t.z,
			compressedQuat.x,
			compressedQuat.y,
			compressedQuat.z );
	}
	outFile.WriteFloatString( "%s}\n", prepend );
}

/*
========================
rvRenderModelMD5R::WriteSansBuffers
========================
*/
void rvRenderModelMD5R::WriteSansBuffers( idFile &outFile, const char *prepend ) const {
	if ( meshes.Num() == 0 ) {
		return;
	}

	WriteMeshes( outFile, prepend );
	outFile.WriteFloatString(
		"%sBounds %f %f %f  %f %f %f\n",
		prepend,
		bounds[ 0 ].x,
		bounds[ 0 ].y,
		bounds[ 0 ].z,
		bounds[ 1 ].x,
		bounds[ 1 ].y,
		bounds[ 1 ].z );

	if ( hasSky ) {
		outFile.WriteFloatString( "%sHasSky\n", prepend );
	}
}

/*
========================
rvRenderModelMD5R::WriteModel
========================
*/
void rvRenderModelMD5R::WriteModel( idFile &outFile ) const {
	outFile.WriteFloatString( "MD5RVersion %d\n", MD5R_VERSION );
	WriteJoints( outFile, "" );
	WriteVertexBuffers( outFile, "" );

	if ( GetIndexBuffers().Num() > 0 ) {
		WriteIndexBuffers( outFile, "" );
	}
	if ( GetSilhouetteEdges().Num() > 0 ) {
		WriteSilhouetteEdges( outFile, "" );
	}
	if ( lods.Num() > 0 ) {
		WriteLevelsOfDetail( outFile, "" );
	}

	WriteMeshes( outFile, "" );
	outFile.WriteFloatString(
		"Bounds %f %f %f  %f %f %f\n",
		bounds[ 0 ].x,
		bounds[ 0 ].y,
		bounds[ 0 ].z,
		bounds[ 1 ].x,
		bounds[ 1 ].y,
		bounds[ 1 ].z );

	if ( hasSky ) {
		outFile.WriteFloatString( "HasSky\n" );
	}
}

/*
========================
rvRenderModelMD5R::WriteFile
========================
*/
bool rvRenderModelMD5R::WriteFile( const char *fileName, bool compressed ) const {
	idAutoPtr<idFile> outFile( fileSystem->OpenFileWrite( fileName, "fs_savepath" ) );
	if ( outFile.get() == NULL ) {
		common->Warning(
			"rvRenderModelMD5R::WriteFile: couldn't open '%s' for MD5R export from '%s'",
			fileName,
			name.c_str() );
		return false;
	}

	common->Printf( "writing %s\n", fileName );
	WriteModel( *outFile );
	outFile.reset( NULL );

	if ( compressed ) {
		idLexer::WriteBinaryFile( fileName );
	}

	return true;
}

/*
========================
rvRenderModelMD5R::WriteAll
========================
*/
void rvRenderModelMD5R::WriteAll( bool compressed ) {
	int writtenCount = 0;
	int skippedPurgedCount = 0;
	int skippedDefaultedCount = 0;
	int skippedUnsupportedCount = 0;

	for ( rvRenderModelMD5R *model = modelList; model != NULL; model = model->next ) {
		if ( model->purged ) {
			++skippedPurgedCount;
			continue;
		}

		if ( model->defaulted || model->IsDefaultModel() ) {
			++skippedDefaultedCount;
			continue;
		}

		idStr reason;
		if ( !model->CanWriteModelData( reason ) ) {
			++skippedUnsupportedCount;
			common->Warning(
				"rvRenderModelMD5R::WriteAll: skipping '%s': %s",
				model->Name(),
				reason.c_str() );
			continue;
		}

		const idStr exportName = model->BuildExportFileName();
		if ( model->WriteFile( exportName.c_str(), compressed ) ) {
			++writtenCount;
		} else {
			++skippedUnsupportedCount;
		}
	}

	if ( writtenCount == 0 && skippedPurgedCount == 0 && skippedDefaultedCount == 0 && skippedUnsupportedCount == 0 ) {
		common->Printf( "rvRenderModelMD5R::WriteAll: no MD5R models are currently loaded\n" );
		return;
	}

	common->Printf(
		"rvRenderModelMD5R::WriteAll: wrote %d MD5R model(s)%s\n",
		writtenCount,
		compressed ? " and refreshed binary companions" : "" );

	if ( skippedPurgedCount > 0 || skippedDefaultedCount > 0 || skippedUnsupportedCount > 0 ) {
		common->Printf(
			"rvRenderModelMD5R::WriteAll: skipped %d purged, %d defaulted, %d unsupported model(s)\n",
			skippedPurgedCount,
			skippedDefaultedCount,
			skippedUnsupportedCount );
	}
}
