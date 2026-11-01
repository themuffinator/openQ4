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

int	c_turboUsedVerts;
int c_turboUnusedVerts;

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
static int R_CreateShadowCacheFromSilTraceVerts( idVec4 *vertexCache, int *vertRemap, const idVec3 &lightOrigin, const rvSilTraceVertT *silTraceVerts, int numVerts ) {
	int outVerts = 0;

	for ( int i = 0; i < numVerts; ++i ) {
		if ( vertRemap[i] ) {
			continue;
		}

		const idVec4 &position = silTraceVerts[i].xyzw;
		vertexCache[outVerts + 0][0] = position.x;
		vertexCache[outVerts + 0][1] = position.y;
		vertexCache[outVerts + 0][2] = position.z;
		vertexCache[outVerts + 0][3] = 1.0f;

		vertexCache[outVerts + 1][0] = position.x - lightOrigin[0];
		vertexCache[outVerts + 1][1] = position.y - lightOrigin[1];
		vertexCache[outVerts + 1][2] = position.z - lightOrigin[2];
		vertexCache[outVerts + 1][3] = 0.0f;

		vertRemap[i] = outVerts;
		outVerts += 2;
	}

	return outVerts;
}

static ID_INLINE bool R_PackedShadowVertsMatchTurboLayout( const rvMD5RPrimBatch &primBatch ) {
	return primBatch.hasSilTraceGeoSpec
		&& primBatch.hasShadowGeoSpec
		&& primBatch.shadowVolGeoSpec.vertexStart >= 0
		&& ( primBatch.shadowVolGeoSpec.vertexStart & 1 ) == 0
		&& primBatch.shadowVolGeoSpec.indexStart == 0
		&& primBatch.shadowVolGeoSpec.primitiveCount == 0
		&& primBatch.shadowVolGeoSpec.vertexCount == primBatch.silTraceGeoSpec.vertexCount * 2;
}

static int R_CountPackedTurboShadowingFaces( const rvMD5RMesh &mesh, const rvMD5RIndexBufferDesc &silTraceIndexBuffer, srfCullInfo_t &cullInfo ) {
	if ( cullInfo.facing == NULL ) {
		return -1;
	}

	if ( cullInfo.cullBits == LIGHT_CULL_ALL_FRONT || !r_useShadowProjectedCull.GetBool() ) {
		int numShadowingFaces = 0;
		const byte *batchFacing = cullInfo.facing;

		for ( int primBatchIndex = 0; primBatchIndex < mesh.primBatches.Num(); ++primBatchIndex ) {
			const rvMD5RPrimBatch &primBatch = mesh.primBatches[ primBatchIndex ];
			for ( int triNum = 0; triNum < primBatch.silTraceGeoSpec.primitiveCount; ++triNum ) {
				numShadowingFaces += ( batchFacing[ triNum ] == 0 );
			}
			batchFacing += primBatch.silTraceGeoSpec.primitiveCount;
		}

		return numShadowingFaces;
	}

	byte *modifyFacing = cullInfo.facing;
	const byte *batchCullBits = cullInfo.cullBits;
	int numShadowingFaces = 0;

	for ( int primBatchIndex = 0; primBatchIndex < mesh.primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh.primBatches[ primBatchIndex ];
		const int batchTriangleCount = primBatch.silTraceGeoSpec.primitiveCount;
		const glIndex_t *batchSilTraceSource = silTraceIndexBuffer.indices.Ptr() + primBatch.silTraceGeoSpec.indexStart;

		for ( int triNum = 0; triNum < batchTriangleCount; ++triNum ) {
			if ( !modifyFacing[ triNum ] ) {
				const int silTraceIndex0 = batchSilTraceSource[ triNum * 3 + 0 ];
				const int silTraceIndex1 = batchSilTraceSource[ triNum * 3 + 1 ];
				const int silTraceIndex2 = batchSilTraceSource[ triNum * 3 + 2 ];
				if ( silTraceIndex0 < 0 || silTraceIndex1 < 0 || silTraceIndex2 < 0
					|| silTraceIndex0 >= primBatch.silTraceGeoSpec.vertexCount
					|| silTraceIndex1 >= primBatch.silTraceGeoSpec.vertexCount
					|| silTraceIndex2 >= primBatch.silTraceGeoSpec.vertexCount ) {
					return -1;
				}

				if ( batchCullBits[ silTraceIndex0 ] & batchCullBits[ silTraceIndex1 ] & batchCullBits[ silTraceIndex2 ] ) {
					modifyFacing[ triNum ] = 1;
				} else {
					++numShadowingFaces;
				}
			}
		}

		modifyFacing += batchTriangleCount;
		batchCullBits += primBatch.silTraceGeoSpec.vertexCount;
	}

	return numShadowingFaces;
}

static int R_CreatePackedTurboShadowSideWalls( const rvMD5RPrimBatch &primBatch, const silEdge_t *batchSilEdges, glIndex_t *shadowIndices, const byte *batchFacing ) {
	int emittedIndexCount = 0;
	const int batchTriangleCount = primBatch.silTraceGeoSpec.primitiveCount;

	for ( int silEdgeIndex = 0; silEdgeIndex < primBatch.silEdgeCount; ++silEdgeIndex ) {
		const silEdge_t &sil = batchSilEdges[ silEdgeIndex ];
		// p2 == the batch triangle count is the classic dangling-edge sentinel:
		// an open edge of the mesh, or one whose other face landed in another
		// prim batch. Its missing face counts as lit, as the padded facing entry
		// does for whole surfaces, so each batch's volume stays closed.
		if ( sil.p1 < 0 || sil.p2 < 0
			|| sil.p1 >= batchTriangleCount
			|| sil.p2 > batchTriangleCount
			|| sil.v1 < 0 || sil.v2 < 0
			|| sil.v1 >= primBatch.silTraceGeoSpec.vertexCount
			|| sil.v2 >= primBatch.silTraceGeoSpec.vertexCount ) {
			return -1;
		}

		const int f1 = batchFacing[ sil.p1 ];
		const int f2 = ( sil.p2 == batchTriangleCount ) ? 1 : batchFacing[ sil.p2 ];
		if ( !( f1 ^ f2 ) ) {
			continue;
		}

		const int v1 = primBatch.shadowVolGeoSpec.vertexStart + ( sil.v1 << 1 );
		const int v2 = primBatch.shadowVolGeoSpec.vertexStart + ( sil.v2 << 1 );

		shadowIndices[ emittedIndexCount + 0 ] = v1;
		shadowIndices[ emittedIndexCount + 1 ] = v2 ^ f1;
		shadowIndices[ emittedIndexCount + 2 ] = v2 ^ f2;
		shadowIndices[ emittedIndexCount + 3 ] = v1 ^ f2;
		shadowIndices[ emittedIndexCount + 4 ] = v1 ^ f1;
		shadowIndices[ emittedIndexCount + 5 ] = v2 ^ 1;
		emittedIndexCount += 6;
	}

	return emittedIndexCount;
}

static int R_CreatePackedTurboShadowCaps( const rvMD5RPrimBatch &primBatch, const rvMD5RIndexBufferDesc &silTraceIndexBuffer, glIndex_t *shadowIndices, const byte *batchFacing ) {
	const int batchTriangleCount = primBatch.silTraceGeoSpec.primitiveCount;
	const glIndex_t *batchSilTraceSource = silTraceIndexBuffer.indices.Ptr() + primBatch.silTraceGeoSpec.indexStart;
	int emittedIndexCount = 0;

	for ( int triNum = 0; triNum < batchTriangleCount; ++triNum ) {
		if ( batchFacing[ triNum ] ) {
			continue;
		}

		const int silTraceIndex0 = batchSilTraceSource[ triNum * 3 + 0 ];
		const int silTraceIndex1 = batchSilTraceSource[ triNum * 3 + 1 ];
		const int silTraceIndex2 = batchSilTraceSource[ triNum * 3 + 2 ];
		if ( silTraceIndex0 < 0 || silTraceIndex1 < 0 || silTraceIndex2 < 0
			|| silTraceIndex0 >= primBatch.silTraceGeoSpec.vertexCount
			|| silTraceIndex1 >= primBatch.silTraceGeoSpec.vertexCount
			|| silTraceIndex2 >= primBatch.silTraceGeoSpec.vertexCount ) {
			return -1;
		}

		const int i0 = primBatch.shadowVolGeoSpec.vertexStart + ( silTraceIndex0 << 1 );
		const int i1 = primBatch.shadowVolGeoSpec.vertexStart + ( silTraceIndex1 << 1 );
		const int i2 = primBatch.shadowVolGeoSpec.vertexStart + ( silTraceIndex2 << 1 );

		shadowIndices[ emittedIndexCount + 0 ] = i2;
		shadowIndices[ emittedIndexCount + 1 ] = i1;
		shadowIndices[ emittedIndexCount + 2 ] = i0;
		shadowIndices[ emittedIndexCount + 3 ] = i0 ^ 1;
		shadowIndices[ emittedIndexCount + 4 ] = i1 ^ 1;
		shadowIndices[ emittedIndexCount + 5 ] = i2 ^ 1;
		emittedIndexCount += 6;
	}

	return emittedIndexCount;
}

srfTriangles_t *R_CreatePackedTurboShadowVolume( const idRenderEntityLocal *ent,
		const srfTriangles_t *tri, const idRenderLightLocal *light, srfCullInfo_t &cullInfo ) {
	if ( tr.backEndRenderer != BE_ARB2 || !tr.backEndRendererHasVertexPrograms ) {
		return NULL;
	}

	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( tri );
	const rvMD5RVertexBufferDesc *shadowVertexBuffer = R_MD5R_GetShadowVertexBufferForTri( tri );
	const idList<silEdge_t> *silhouetteEdges = R_MD5R_GetSilhouetteEdgeListForTri( tri );
	const rvMD5RIndexBufferDesc *silTraceIndexBuffer = R_MD5R_GetSilTraceIndexBufferForTri( tri );
	if ( mesh == NULL || shadowVertexBuffer == NULL || silhouetteEdges == NULL || silTraceIndexBuffer == NULL
		|| mesh->primBatches.Num() <= 0
		|| shadowVertexBuffer->numVertices <= 0
		|| shadowVertexBuffer->positions.Num() != shadowVertexBuffer->numVertices
		|| silTraceIndexBuffer->numIndices <= 0
		|| silTraceIndexBuffer->indices.Num() != silTraceIndexBuffer->numIndices
		|| tri->numVerts <= 0
		|| tri->numIndexes <= 0 ) {
		return NULL;
	}

	int totalSilTraceVertices = 0;
	int totalSilTraceTriangles = 0;
	int totalSilEdges = 0;
	int maxShadowVertexCount = 0;

	for ( int primBatchIndex = 0; primBatchIndex < mesh->primBatches.Num(); ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
		const int batchSilTraceIndexCount = primBatch.silTraceGeoSpec.primitiveCount * 3;
		if ( !R_PackedShadowVertsMatchTurboLayout( primBatch )
			|| primBatch.silTraceGeoSpec.vertexCount < 0
			|| primBatch.silTraceGeoSpec.primitiveCount < 0
			|| primBatch.silTraceGeoSpec.indexStart < 0
			|| primBatch.silTraceGeoSpec.indexStart + batchSilTraceIndexCount > silTraceIndexBuffer->numIndices
			|| primBatch.shadowVolGeoSpec.vertexStart + primBatch.shadowVolGeoSpec.vertexCount > shadowVertexBuffer->numVertices ) {
			return NULL;
		}

		if ( primBatch.silEdgeCount < 0 ) {
			return NULL;
		}
		if ( primBatch.silEdgeCount > 0 ) {
			if ( primBatch.silEdgeStart < 0
				|| primBatch.silEdgeStart + primBatch.silEdgeCount > silhouetteEdges->Num() ) {
				return NULL;
			}
		}

		totalSilTraceVertices += primBatch.silTraceGeoSpec.vertexCount;
		totalSilTraceTriangles += primBatch.silTraceGeoSpec.primitiveCount;
		totalSilEdges += primBatch.silEdgeCount;
		maxShadowVertexCount = Max( maxShadowVertexCount, primBatch.shadowVolGeoSpec.vertexStart + primBatch.shadowVolGeoSpec.vertexCount );
	}

	// The current hybrid interaction cull helpers still operate on the surfaced
	// tri-surf view, so only keep the packed path when the packed sil-trace view
	// matches the materialized CPU topology exactly.
	if ( totalSilTraceVertices != tri->numVerts
		|| totalSilTraceTriangles != tri->numIndexes / 3
		|| ( mesh->numSilTraceVertices > 0 && totalSilTraceVertices != mesh->numSilTraceVertices )
		|| ( mesh->numSilTracePrimitives > 0 && totalSilTraceTriangles != mesh->numSilTracePrimitives ) ) {
		return NULL;
	}

	R_CalcInteractionFacing( ent, tri, light, cullInfo );
	if ( r_useShadowProjectedCull.GetBool() ) {
		R_CalcInteractionCullBits( ent, tri, light, cullInfo );
	}

	const int numShadowingFaces = R_CountPackedTurboShadowingFaces( *mesh, *silTraceIndexBuffer, cullInfo );
	if ( numShadowingFaces <= 0 ) {
		return NULL;
	}

	srfTriangles_t *newTri = R_AllocStaticTriSurf();
	newTri->numVerts = maxShadowVertexCount;
	newTri->primBatchMesh = tri->primBatchMesh;
	newTri->skinToModelTransforms = tri->skinToModelTransforms;
	newTri->skinToModelTransformsAlloc = NULL;
	newTri->numSkinToModelTransforms = tri->numSkinToModelTransforms;

	const int numPrimBatches = mesh->primBatches.Num();
	R_AllocStaticTriSurfIndexes( newTri, ( 2 * numPrimBatches ) + ( 6 * ( numShadowingFaces + totalSilEdges ) ) );

	glIndex_t *batchHeader = newTri->indexes;
	glIndex_t *shadowIndices = batchHeader + ( 2 * numPrimBatches );
	const byte *batchFacing = cullInfo.facing;
	newTri->numIndexes = 0;
	newTri->numShadowIndexesNoCaps = 0;

	for ( int primBatchIndex = 0; primBatchIndex < numPrimBatches; ++primBatchIndex ) {
		const rvMD5RPrimBatch &primBatch = mesh->primBatches[ primBatchIndex ];
		const silEdge_t *batchSilEdges = silhouetteEdges->Ptr() + primBatch.silEdgeStart;

		const int sideWallIndexCount = R_CreatePackedTurboShadowSideWalls( primBatch, batchSilEdges, shadowIndices, batchFacing );
		if ( sideWallIndexCount < 0 ) {
			R_ReallyFreeStaticTriSurf( newTri );
			return NULL;
		}
		shadowIndices += sideWallIndexCount;

		const int capIndexCount = R_CreatePackedTurboShadowCaps( primBatch, *silTraceIndexBuffer, shadowIndices, batchFacing );
		if ( capIndexCount < 0 ) {
			R_ReallyFreeStaticTriSurf( newTri );
			return NULL;
		}
		const int totalIndexCount = sideWallIndexCount + capIndexCount;
		shadowIndices += capIndexCount;

		batchHeader[ primBatchIndex * 2 + 0 ] = sideWallIndexCount;
		batchHeader[ primBatchIndex * 2 + 1 ] = totalIndexCount;
		newTri->numIndexes += totalIndexCount;
		newTri->numShadowIndexesNoCaps += sideWallIndexCount;
		batchFacing += primBatch.silTraceGeoSpec.primitiveCount;
	}

	newTri->numShadowIndexesNoFrontCaps = newTri->numIndexes;
	R_ResizeStaticTriSurfIndexes( newTri, newTri->numIndexes + ( 2 * numPrimBatches ) );
	newTri->shadowCapPlaneBits = SHADOW_CAP_INFINITE;
	newTri->bounds.Clear();
	return newTri;
}
#endif


/*
=====================
R_CreateVertexProgramTurboShadowVolume

are dangling edges that are outside the light frustum still making planes?
=====================
*/
srfTriangles_t *R_CreateVertexProgramTurboShadowVolume( const idRenderEntityLocal *ent, 
														const srfTriangles_t *tri, const idRenderLightLocal *light,
														srfCullInfo_t &cullInfo ) {
	int		i, j;
	srfTriangles_t	*newTri;
	silEdge_t	*sil;
	const glIndex_t *indexes;
	const byte *facing;

	R_CalcInteractionFacing( ent, tri, light, cullInfo );
	if ( r_useShadowProjectedCull.GetBool() ) {
		R_CalcInteractionCullBits( ent, tri, light, cullInfo );
	}

	int numFaces = tri->numIndexes / 3;
	int	numShadowingFaces = 0;
	facing = cullInfo.facing;

	// if all the triangles are inside the light frustum
	if ( cullInfo.cullBits == LIGHT_CULL_ALL_FRONT || !r_useShadowProjectedCull.GetBool() ) {

		// count the number of shadowing faces
		for ( i = 0; i < numFaces; i++ ) {
			numShadowingFaces += facing[i];
		}
		numShadowingFaces = numFaces - numShadowingFaces;

	} else {

		// make all triangles that are outside the light frustum "facing", so they won't cast shadows
		indexes = tri->indexes;
		byte *modifyFacing = cullInfo.facing;
		const byte *cullBits = cullInfo.cullBits;
		for ( j = i = 0; i < tri->numIndexes; i += 3, j++ ) {
			if ( !modifyFacing[j] ) {
				int	i1 = indexes[i+0];
				int	i2 = indexes[i+1];
				int	i3 = indexes[i+2];
				if ( cullBits[i1] & cullBits[i2] & cullBits[i3] ) {
					modifyFacing[j] = 1;
				} else {
					numShadowingFaces++;
				}
			}
		}
	}

	if ( !numShadowingFaces ) {
		// no faces are inside the light frustum and still facing the right way
		return NULL;
	}

	// shadowVerts will be NULL on these surfaces, so the shadowVerts will be taken from the ambient surface
	newTri = R_AllocStaticTriSurf();

	newTri->numVerts = tri->numVerts * 2;

	// alloc the max possible size
#ifdef USE_TRI_DATA_ALLOCATOR
	R_AllocStaticTriSurfIndexes( newTri, ( numShadowingFaces + tri->numSilEdges ) * 6 );
	glIndex_t *tempIndexes = newTri->indexes;
	glIndex_t *shadowIndexes = newTri->indexes;
#else
	// sil edges grow with the surface, so a large surface builds its sil
	// planes on the heap rather than walking off the end of the stack
	OPENQ4_ALLOC16_SCRATCH( glIndex_t, tempIndexes, tri->numSilEdges * 6 );
	glIndex_t *shadowIndexes = tempIndexes;
#endif

	// create new triangles along sil planes
	for ( sil = tri->silEdges, i = tri->numSilEdges; i > 0; i--, sil++ ) {

		int f1 = facing[sil->p1];
		int f2 = facing[sil->p2];

		if ( !( f1 ^ f2 ) ) {
			continue;
		}

		int v1 = sil->v1 << 1;
		int v2 = sil->v2 << 1;

		// set the two triangle winding orders based on facing
		// without using a poorly-predictable branch

		shadowIndexes[0] = v1;
		shadowIndexes[1] = v2 ^ f1;
		shadowIndexes[2] = v2 ^ f2;
		shadowIndexes[3] = v1 ^ f2;
		shadowIndexes[4] = v1 ^ f1;
		shadowIndexes[5] = v2 ^ 1;

		shadowIndexes += 6;
	}

	int	numShadowIndexes = shadowIndexes - tempIndexes;

	// we aren't bothering to separate front and back caps on these
	newTri->numIndexes = newTri->numShadowIndexesNoFrontCaps = numShadowIndexes + numShadowingFaces * 6;
	newTri->numShadowIndexesNoCaps = numShadowIndexes;
	newTri->shadowCapPlaneBits = SHADOW_CAP_INFINITE;

#ifdef USE_TRI_DATA_ALLOCATOR
	// decrease the size of the memory block to only store the used indexes
	R_ResizeStaticTriSurfIndexes( newTri, newTri->numIndexes );
#else
	// allocate memory for the indexes
	R_AllocStaticTriSurfIndexes( newTri, newTri->numIndexes );
	// copy the indexes we created for the sil planes
	SIMDProcessor->Memcpy( newTri->indexes, tempIndexes, numShadowIndexes * sizeof( tempIndexes[0] ) );
#endif

	// these have no effect, because they extend to infinity
	newTri->bounds.Clear();

	// put some faces on the model and some on the distant projection
	indexes = tri->indexes;
	shadowIndexes = newTri->indexes + numShadowIndexes;
	for ( i = 0, j = 0; i < tri->numIndexes; i += 3, j++ ) {
		if ( facing[j] ) {
			continue;
		}

		int i0 = indexes[i+0] << 1;
		shadowIndexes[2] = i0;
		shadowIndexes[3] = i0 ^ 1;
		int i1 = indexes[i+1] << 1;
		shadowIndexes[1] = i1;
		shadowIndexes[4] = i1 ^ 1;
		int i2 = indexes[i+2] << 1;
		shadowIndexes[0] = i2;
		shadowIndexes[5] = i2 ^ 1;

		shadowIndexes += 6;
	}

	return newTri;
}

/*
=====================
R_CreateTurboShadowVolume
=====================
*/
srfTriangles_t *R_CreateTurboShadowVolume( const idRenderEntityLocal *ent,
											const srfTriangles_t *tri, const idRenderLightLocal *light,
											srfCullInfo_t &cullInfo ) {
	int		i, j;
	idVec3	localLightOrigin;
	srfTriangles_t	*newTri;
	silEdge_t	*sil;
	const glIndex_t *indexes;
	const byte *facing;

	R_CalcInteractionFacing( ent, tri, light, cullInfo );
	if ( r_useShadowProjectedCull.GetBool() ) {
		R_CalcInteractionCullBits( ent, tri, light, cullInfo );
	}

	int numFaces = tri->numIndexes / 3;
	int	numShadowingFaces = 0;
	facing = cullInfo.facing;

	// if all the triangles are inside the light frustum
	if ( cullInfo.cullBits == LIGHT_CULL_ALL_FRONT || !r_useShadowProjectedCull.GetBool() ) {

		// count the number of shadowing faces
		for ( i = 0; i < numFaces; i++ ) {
			numShadowingFaces += facing[i];
		}
		numShadowingFaces = numFaces - numShadowingFaces;

	} else {

		// make all triangles that are outside the light frustum "facing", so they won't cast shadows
		indexes = tri->indexes;
		byte *modifyFacing = cullInfo.facing;
		const byte *cullBits = cullInfo.cullBits;
		for ( j = i = 0; i < tri->numIndexes; i += 3, j++ ) {
			if ( !modifyFacing[j] ) {
				int	i1 = indexes[i+0];
				int	i2 = indexes[i+1];
				int	i3 = indexes[i+2];
				if ( cullBits[i1] & cullBits[i2] & cullBits[i3] ) {
					modifyFacing[j] = 1;
				} else {
					numShadowingFaces++;
				}
			}
		}
	}

	if ( !numShadowingFaces ) {
		// no faces are inside the light frustum and still facing the right way
		return NULL;
	}

	newTri = R_AllocStaticTriSurf();

#ifdef USE_TRI_DATA_ALLOCATOR
	R_AllocStaticTriSurfShadowVerts( newTri, tri->numVerts * 2 );
	shadowCache_t *shadowVerts = newTri->shadowVertexes;
#else
	shadowCache_t *shadowVerts = (shadowCache_t *)_alloca16( tri->numVerts * 2 * sizeof( shadowVerts[0] ) );
#endif

	R_GlobalPointToLocal( ent->modelMatrix, light->globalLightOrigin, localLightOrigin );

	int	*vertRemap = (int *)_alloca16( tri->numVerts * sizeof( vertRemap[0] ) );

	SIMDProcessor->Memset( vertRemap, -1, tri->numVerts * sizeof( vertRemap[0] ) );

	for ( i = 0, j = 0; i < tri->numIndexes; i += 3, j++ ) {
		if ( facing[j] ) {
			continue;
		}
		// this may pull in some vertexes that are outside
		// the frustum, because they connect to vertexes inside
		vertRemap[tri->silIndexes[i+0]] = 0;
		vertRemap[tri->silIndexes[i+1]] = 0;
		vertRemap[tri->silIndexes[i+2]] = 0;
	}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->silTraceVerts != NULL ) {
		newTri->numVerts = R_CreateShadowCacheFromSilTraceVerts(
			&shadowVerts->xyz,
			vertRemap,
			localLightOrigin,
			reinterpret_cast<const rvSilTraceVertT *>( tri->silTraceVerts ),
			tri->numVerts );
	} else
#endif
	if ( tri->verts != NULL ) {
		newTri->numVerts = SIMDProcessor->CreateShadowCache( &shadowVerts->xyz, vertRemap, localLightOrigin, tri->verts, tri->numVerts );
	} else {
		R_ReallyFreeStaticTriSurf( newTri );
		return NULL;
	}

	c_turboUsedVerts += newTri->numVerts;
	c_turboUnusedVerts += tri->numVerts * 2 - newTri->numVerts;

#ifdef USE_TRI_DATA_ALLOCATOR
	R_ResizeStaticTriSurfShadowVerts( newTri, newTri->numVerts );
#else
	R_AllocStaticTriSurfShadowVerts( newTri, newTri->numVerts );
	SIMDProcessor->Memcpy( newTri->shadowVertexes, shadowVerts, newTri->numVerts * sizeof( shadowVerts[0] ) );
#endif

	// alloc the max possible size
#ifdef USE_TRI_DATA_ALLOCATOR
	R_AllocStaticTriSurfIndexes( newTri, ( numShadowingFaces + tri->numSilEdges ) * 6 );
	glIndex_t *tempIndexes = newTri->indexes;
	glIndex_t *shadowIndexes = newTri->indexes;
#else
	OPENQ4_ALLOC16_SCRATCH( glIndex_t, tempIndexes, tri->numSilEdges * 6 );
	glIndex_t *shadowIndexes = tempIndexes;
#endif

	// create new triangles along sil planes
	for ( sil = tri->silEdges, i = tri->numSilEdges; i > 0; i--, sil++ ) {

		int f1 = facing[sil->p1];
		int f2 = facing[sil->p2];

		if ( !( f1 ^ f2 ) ) {
			continue;
		}

		int v1 = vertRemap[sil->v1];
		int v2 = vertRemap[sil->v2];

		// set the two triangle winding orders based on facing
		// without using a poorly-predictable branch

		shadowIndexes[0] = v1;
		shadowIndexes[1] = v2 ^ f1;
		shadowIndexes[2] = v2 ^ f2;
		shadowIndexes[3] = v1 ^ f2;
		shadowIndexes[4] = v1 ^ f1;
		shadowIndexes[5] = v2 ^ 1;

		shadowIndexes += 6;
	}

	int numShadowIndexes = shadowIndexes - tempIndexes;

	// we aren't bothering to separate front and back caps on these
	newTri->numIndexes = newTri->numShadowIndexesNoFrontCaps = numShadowIndexes + numShadowingFaces * 6;
	newTri->numShadowIndexesNoCaps = numShadowIndexes;
	newTri->shadowCapPlaneBits = SHADOW_CAP_INFINITE;

#ifdef USE_TRI_DATA_ALLOCATOR
	// decrease the size of the memory block to only store the used indexes
	R_ResizeStaticTriSurfIndexes( newTri, newTri->numIndexes );
#else
	// allocate memory for the indexes
	R_AllocStaticTriSurfIndexes( newTri, newTri->numIndexes );
	// copy the indexes we created for the sil planes
	SIMDProcessor->Memcpy( newTri->indexes, tempIndexes, numShadowIndexes * sizeof( tempIndexes[0] ) );
#endif

	// these have no effect, because they extend to infinity
	newTri->bounds.Clear();

	// put some faces on the model and some on the distant projection
	indexes = tri->silIndexes;
	shadowIndexes = newTri->indexes + numShadowIndexes;
	for ( i = 0, j = 0; i < tri->numIndexes; i += 3, j++ ) {
		if ( facing[j] ) {
			continue;
		}

		int i0 = vertRemap[indexes[i+0]];
		shadowIndexes[2] = i0;
		shadowIndexes[3] = i0 ^ 1;
		int i1 = vertRemap[indexes[i+1]];
		shadowIndexes[1] = i1;
		shadowIndexes[4] = i1 ^ 1;
		int i2 = vertRemap[indexes[i+2]];
		shadowIndexes[0] = i2;
		shadowIndexes[5] = i2 ^ 1;

		shadowIndexes += 6;
	}

	return newTri;
}


/*
=====================
R_CreateTurboShadowVolumeForSurface

Packed MD5R prim-batch surfaces only participate in the infinite turbo-shadow
path. The classic capped stencil builder below assumes ordinary tri-surf
geometry and should never be fed packed surfaces directly.
=====================
*/
srfTriangles_t *R_CreateTurboShadowVolumeForSurface( const idRenderEntityLocal *ent,
		const srfTriangles_t *tri, const idRenderLightLocal *light, srfCullInfo_t &cullInfo ) {
	gpuSkinningSurface_t gpuSurface;
	const bool gpuStencilFallback = r_gpuSkinning.GetBool()
		&& R_GpuSkinning_IsCandidate( tri )
		&& R_GpuSkinning_GetSurface( tri, gpuSurface );
	srfTriangles_t *shadowTri = NULL;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->primBatchMesh != NULL ) {
		shadowTri = R_CreatePackedTurboShadowVolume( ent, tri, light, cullInfo );
	}
#endif

	if ( shadowTri == NULL && tr.backEndRendererHasVertexPrograms
		&& r_useShadowVertexProgram.GetBool() ) {
		shadowTri = R_CreateVertexProgramTurboShadowVolume( ent, tri, light, cullInfo );
	}
	if ( shadowTri == NULL ) {
		shadowTri = R_CreateTurboShadowVolume( ent, tri, light, cullInfo );
	}
	if ( shadowTri != NULL && gpuStencilFallback ) {
		R_GpuSkinning_RecordFallback( GPU_SKINNING_FALLBACK_STENCIL_VOLUME );
	}
	return shadowTri;
}
