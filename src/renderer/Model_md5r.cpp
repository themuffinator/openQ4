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
			R_MD5R_SkipNumericValues( parser, format.specularColorTokenType, 1 );
		}

		if ( format.hasPointSize ) {
			R_MD5R_SkipNumericValues( parser, format.pointSizeTokenType, 1 );
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
		for ( int vertexIndex = 0; vertexIndex < vertexBuffer.numVertices; ++vertexIndex ) {
			R_MD5R_SetPosition( vertexBuffer.positions[ vertexIndex ], parser, format.positionTokenType, format.positionDim );
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
		R_MD5R_SkipNumericValues( parser, format.specularColorTokenType, vertexBuffer.numVertices );
	}

	if ( format.hasPointSize ) {
		R_MD5R_SkipNumericValues( parser, format.pointSizeTokenType, vertexBuffer.numVertices );
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

#if defined( _MD5R_SUPPORT )
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
	next( modelList ) {
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
	for ( int texCoordSet = 0; texCoordSet < 7; ++texCoordSet ) {
		if ( vertexBuffer.loadVertexFormat.hasTexCoord[ texCoordSet ] ) {
			vertexBuffer.texCoords[ texCoordSet ].SetNum( vertexBuffer.numVertices );
		}
	}

	if ( vertexBuffer.soA && vertexBuffer.loadVertexFormat.positionSwizzled ) {
		common->Warning(
			"rvRenderModelMD5R::ParseVertexBuffer: '%s' uses SoA swizzled MD5R positions, which are not decoded yet; static surface generation will be skipped for this buffer",
			name.c_str() );
		vertexBuffer.positions.Clear();
		vertexBuffer.blendIndices.Clear();
		vertexBuffer.blendWeights.Clear();
		vertexBuffer.normals.Clear();
		vertexBuffer.tangents.Clear();
		vertexBuffer.binormals.Clear();
		vertexBuffer.diffuseColors.Clear();
		for ( int texCoordSet = 0; texCoordSet < 7; ++texCoordSet ) {
			vertexBuffer.texCoords[ texCoordSet ].Clear();
		}
		if ( !parser.SkipBracedSection( false ) ) {
			parser.Error( "Malformed Vertex data block" );
		}
	} else if ( vertexBuffer.soA ) {
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
void rvRenderModelMD5R::ParseVertexBuffers( Lexer &parser ) {
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
void rvRenderModelMD5R::ParseIndexBuffers( Lexer &parser ) {
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
rvRenderModelMD5R::ParseSilhouetteEdges
========================
*/
void rvRenderModelMD5R::ParseSilhouetteEdges( Lexer &parser ) {
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

		if ( primBatch.numTransforms > 25 ) {
			parser.Error( "Primitive batch initialization failed - too many transforms per batch" );
		}

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
			lods[ i ].rangeEnd = idMath::INFINITY;
			lods[ i ].rangeEndSquared = idMath::INFINITY;
		}

		common->DPrintf(
			"rvRenderModelMD5R::BuildLevelsOfDetail: '%s' referenced LODs without explicit ranges; synthetic ranges default to infinity until rvMesh runtime support is ported\n",
			name.c_str() );
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

	if ( !BuildDynamicMeshTemplate( mesh ) ) {
		staticModel.DeleteSurfaceWithId( mesh.meshIdentifier );
		staticModel.DeleteSurfaceWithId( mesh.meshIdentifier + MD5R_BackSideSurfaceIdOffset );
		mesh.surfaceNum = -1;
		return false;
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
	PurgeModel();
	purged = false;
	reloadable = true;
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
	}

	idToken token;
	while ( parser->ReadToken( &token ) ) {
		parser->Warning( "Ignoring unexpected trailing token '%s' in '%s'", token.c_str(), name.c_str() );
	}

	const bool hasGeometrySections = ( vertexBuffers.Num() > 0 || indexBuffers.Num() > 0 || meshes.Num() > 0 );
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

	fileSystem->ReadFile( name.c_str(), NULL, &timeStamp );
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

		if ( ( shader->GetSurfaceFlags() & SURF_COLLISION ) != 0
			&& !shader->IsDrawn()
			&& !shader->SurfaceCastsShadow() ) {
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
	int total = idRenderModelStatic::Memory();
	total += vertexBuffers.MemoryUsed();
	total += indexBuffers.MemoryUsed();
	total += silEdges.MemoryUsed();
	total += lods.MemoryUsed();
	total += allLODMeshes.MemoryUsed();
	total += meshes.MemoryUsed();
	total += joints.MemoryUsed();
	total += defaultPose.MemoryUsed();
	total += skinSpaceToLocalMats.MemoryUsed();
	total += commandLine.DynamicMemoryUsed();

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

	for ( int i = 0; i < vertexBuffers.Num(); ++i ) {
		total += vertexBuffers[ i ].positions.MemoryUsed();
		total += vertexBuffers[ i ].blendIndices.MemoryUsed();
		total += vertexBuffers[ i ].blendWeights.MemoryUsed();
		total += vertexBuffers[ i ].normals.MemoryUsed();
		total += vertexBuffers[ i ].tangents.MemoryUsed();
		total += vertexBuffers[ i ].binormals.MemoryUsed();
		total += vertexBuffers[ i ].diffuseColors.MemoryUsed();
		for ( int texCoordSet = 0; texCoordSet < 7; ++texCoordSet ) {
			total += vertexBuffers[ i ].texCoords[ texCoordSet ].MemoryUsed();
		}
	}

	for ( int i = 0; i < indexBuffers.Num(); ++i ) {
		total += indexBuffers[ i ].indices.MemoryUsed();
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

	for ( int vertexBufferIndex = 0; vertexBufferIndex < vertexBuffers.Num(); ++vertexBufferIndex ) {
		const rvMD5RVertexBufferDesc &vertexBuffer = vertexBuffers[ vertexBufferIndex ];
		const rvMD5RVertexFormatDesc &format = vertexBuffer.loadVertexFormat;

		if ( format.hasSpecularColor ) {
			reason = va( "vertex buffer %d uses specular-color data that OpenQ4 does not retain for MD5R export", vertexBufferIndex );
			return false;
		}

		if ( format.hasPointSize ) {
			reason = va( "vertex buffer %d uses point-size data that OpenQ4 does not retain for MD5R export", vertexBufferIndex );
			return false;
		}

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
void rvRenderModelMD5R::WriteVertexFormat( idFile &outFile, const rvMD5RVertexFormatDesc &vertexFormat, const char *prepend ) const {
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
void rvRenderModelMD5R::WriteVertexBuffer( idFile &outFile, const rvMD5RVertexBufferDesc &vertexBuffer, const char *prepend ) const {
	const rvMD5RVertexFormatDesc &format = vertexBuffer.loadVertexFormat;
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
void rvRenderModelMD5R::WriteVertexBuffers( idFile &outFile, const char *prepend ) const {
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
rvRenderModelMD5R::WriteIndexBuffer
========================
*/
void rvRenderModelMD5R::WriteIndexBuffer( idFile &outFile, const rvMD5RIndexBufferDesc &indexBuffer, const char *prepend ) const {
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
void rvRenderModelMD5R::WriteIndexBuffers( idFile &outFile, const char *prepend ) const {
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
rvRenderModelMD5R::WriteSilhouetteEdges
========================
*/
void rvRenderModelMD5R::WriteSilhouetteEdges( idFile &outFile, const char *prepend ) const {
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
rvRenderModelMD5R::WriteLevelsOfDetail
========================
*/
void rvRenderModelMD5R::WriteLevelsOfDetail( idFile &outFile, const char *prepend ) const {
	if ( lods.Num() == 0 ) {
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
rvRenderModelMD5R::WriteModel
========================
*/
void rvRenderModelMD5R::WriteModel( idFile &outFile ) const {
	outFile.WriteFloatString( "MD5RVersion %d\n", MD5R_VERSION );
	WriteJoints( outFile, "" );
	WriteVertexBuffers( outFile, "" );

	if ( indexBuffers.Num() > 0 ) {
		WriteIndexBuffers( outFile, "" );
	}
	if ( silEdges.Num() > 0 ) {
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
