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

static const char *MD5_SnapshotName = "_MD5_Snapshot_";
static const int MD5_BackSideSurfaceIdOffset = 1000;

namespace {
	static const unsigned int MD5_MODEL_CACHE_MAGIC = 0x4d34514fU; // "OQ4M"
	static const int MD5_MODEL_CACHE_VERSION = 1;
	static const unsigned int MD5_MODEL_GENERATED_CACHE_PARSER_VERSION = 0x00020002U;	// bump when R_BuildDeformInfo's derivation changes: cache hits no longer cross-check against a fresh rebuild
	static const int MD5_CACHE_MAX_JOINTS = 65536;
	static const int MD5_CACHE_MAX_MESHES = 65536;
	static const int MD5_CACHE_MAX_VERTS = 1 << 20;
	static const int MD5_CACHE_MAX_WEIGHTS = 1 << 24;
	static const int MD5_CACHE_MAX_INDEXES = 1 << 24;
	static const int MD5_CACHE_MAX_SIL_EDGES = 1 << 23;
	static const int MD5_CACHE_MAX_NAME = 4096;
	static const int MD5_CACHE_MAX_MATERIALIZED_VERTS = 1 << 18;
	static const int MD5_CACHE_MAX_MATERIALIZED_WEIGHTS = 1 << 20;
	static const int MD5_CACHE_MAX_MATERIALIZED_INDEXES = 1 << 20;

	// the bulk array codecs stream these structures as raw little-endian bytes;
	// pin their layouts so a change breaks the build instead of the cache format
	static_assert( sizeof( glIndex_t ) == sizeof( int ), "cache bulk IO streams glIndex_t as int" );
	static_assert( sizeof( silEdge_t ) == 4 * sizeof( int ), "cache bulk IO streams silEdge_t as 4 ints" );
	static_assert( sizeof( dominantTri_t ) == 2 * sizeof( int ) + 3 * sizeof( float ), "cache bulk IO streams dominantTri_t as 2 ints + 3 floats" );
	static_assert( sizeof( idVec2 ) == 2 * sizeof( float ), "cache bulk IO streams idVec2 as 2 floats" );
	static_assert( sizeof( idVec4 ) == 4 * sizeof( float ), "cache bulk IO streams idVec4 as 4 floats" );

	struct md5CacheDeformStage_t {
		int numSourceVerts;
		int numOutputVerts;
		idList<glIndex_t> indexes;
		idList<glIndex_t> silIndexes;
		idList<int> mirroredVerts;
		idList<int> dupVerts;
		idList<silEdge_t> silEdges;
		idList<dominantTri_t> dominantTris;

		md5CacheDeformStage_t() : numSourceVerts( 0 ), numOutputVerts( 0 ) {}
	};

	struct md5CacheMeshStage_t {
		idStr materialName;
		idList<idVec2> texCoords;
		int numWeights;
		idList<jointWeight_t> weights;
		idList<idVec4> scaledBaseVectors;
		idList<idVec4> baseVectors;
		idList<idVec4> scaledWeights;
		idList<int> weightIndex;
		int numTris;
		int surfaceNum;
		md5CacheDeformStage_t deform;

		md5CacheMeshStage_t() : numWeights( 0 ), numTris( 0 ), surfaceNum( 0 ) {}
	};

	static bool R_MD5CacheWriteJointQuat( idRenderModelCacheWriter &writer, const idJointQuat &pose ) {
		return writer.WriteFloat( pose.q.x ) && writer.WriteFloat( pose.q.y )
			&& writer.WriteFloat( pose.q.z ) && writer.WriteFloat( pose.q.w )
			&& writer.WriteVec3( pose.t );
	}

	static bool R_MD5CacheReadJointQuat( idRenderModelCacheReader &reader, idJointQuat &pose ) {
		pose.w = 0.0f;
		return reader.ReadFloat( pose.q.x ) && reader.ReadFloat( pose.q.y )
			&& reader.ReadFloat( pose.q.z ) && reader.ReadFloat( pose.q.w )
			&& reader.ReadVec3( pose.t );
	}

	static bool R_MD5CacheWriteJointMat( idRenderModelCacheWriter &writer, const idJointMat &mat ) {
		const float *values = mat.ToFloatPtr();
		for ( int i = 0; i < 12; ++i ) {
			if ( !writer.WriteFloat( values[i] ) ) {
				return false;
			}
		}
		return true;
	}

	static bool R_MD5CacheReadJointMat( idRenderModelCacheReader &reader, idJointMat &mat ) {
		float *values = mat.ToFloatPtr();
		for ( int i = 0; i < 12; ++i ) {
			if ( !reader.ReadFloat( values[i] ) ) {
				return false;
			}
		}
		return true;
	}

}

/*
====================
R_CopyAndReverseTriangles

Retail Quake 4 keeps a separate back-side surface for animated MD5 materials
that request duplicated lighting geometry. Reuse the existing allocation when
the topology matches so cached dynamic models stay cheap to update.
====================
*/
static void R_CopyAndReverseTriangles( const srfTriangles_t *src, srfTriangles_t **dst ) {
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

	memcpy( tri->verts, src->verts, src->numVerts * sizeof( tri->verts[0] ) );

	for ( int i = 0; i < tri->numVerts; ++i ) {
		tri->verts[i].normal = vec3_origin - tri->verts[i].normal;
	}
	R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_UNSUPPORTED_PASS );

	for ( int i = 0; i < tri->numIndexes; i += 3 ) {
		tri->indexes[i + 0] = src->indexes[i + 1];
		tri->indexes[i + 1] = src->indexes[i + 0];
		tri->indexes[i + 2] = src->indexes[i + 2];
	}
}


/***********************************************************************

	idMD5Mesh

***********************************************************************/

static int c_numVerts = 0;
static int c_numWeights = 0;
static int c_numWeightJoints = 0;

typedef struct vertexWeight_s {
	int							vert;
	int							joint;
	idVec3						offset;
	float						jointWeight;
} vertexWeight_t;

/*
====================
idRenderModelMD5::idRenderModelMD5
====================
*/
idRenderModelMD5::idRenderModelMD5() {
	viewEnt = NULL;
}

renderModelCacheType_t idRenderModelMD5::LevelLoadCachePayloadType() const {
	return RENDER_MODEL_CACHE_MD5;
}

bool idRenderModelMD5::WriteLevelLoadCachePayload( idFile &file ) const {
	if ( purged || defaulted || joints.Num() < 0 || joints.Num() > MD5_CACHE_MAX_JOINTS
		|| defaultPose.Num() != joints.Num() || skinSpaceToLocalMats.Num() != joints.Num()
		|| meshes.Num() < 0 || meshes.Num() > MD5_CACHE_MAX_MESHES ) {
		return false;
	}

	idRenderModelCacheWriter writer( file );
	if ( !writer.WriteUnsignedInt( MD5_MODEL_CACHE_MAGIC ) || !writer.WriteInt( MD5_MODEL_CACHE_VERSION )
		|| !idRenderModelStatic::WriteLevelLoadCachePayload( file ) || !writer.WriteInt( joints.Num() ) ) {
		return false;
	}

	for ( int i = 0; i < joints.Num(); ++i ) {
		int parentIndex = -1;
		if ( joints[i].parent != NULL ) {
			parentIndex = static_cast<int>( joints[i].parent - joints.Ptr() );
		}
		if ( joints[i].name.IsEmpty() || parentIndex < -1 || parentIndex >= i
			|| !writer.WriteString( joints[i].name.c_str(), MD5_CACHE_MAX_NAME )
			|| !writer.WriteInt( parentIndex ) ) {
			return false;
		}
	}

	if ( !writer.WriteInt( defaultPose.Num() ) ) {
		return false;
	}
	for ( int i = 0; i < defaultPose.Num(); ++i ) {
		if ( !R_MD5CacheWriteJointQuat( writer, defaultPose[i] ) ) {
			return false;
		}
	}
	if ( !writer.WriteInt( skinSpaceToLocalMats.Num() ) ) {
		return false;
	}
	for ( int i = 0; i < skinSpaceToLocalMats.Num(); ++i ) {
		if ( !R_MD5CacheWriteJointMat( writer, skinSpaceToLocalMats[i] ) ) {
			return false;
		}
	}

	if ( !writer.WriteInt( meshes.Num() ) ) {
		return false;
	}
	for ( int meshIndex = 0; meshIndex < meshes.Num(); ++meshIndex ) {
		const idMD5Mesh &mesh = meshes[meshIndex];
		if ( mesh.shader == NULL || mesh.shader->GetName() == NULL || mesh.texCoords.Num() <= 0
			|| mesh.texCoords.Num() > MD5_CACHE_MAX_MATERIALIZED_VERTS || mesh.numWeights <= 0
			|| mesh.numWeights > MD5_CACHE_MAX_MATERIALIZED_WEIGHTS || mesh.weights == NULL
			|| mesh.scaledBaseVectors == NULL || mesh.scaledWeights == NULL || mesh.weightIndex == NULL
			|| mesh.baseVectors == NULL || mesh.deformInfo == NULL || mesh.numTris < 0
			|| mesh.numTris > MD5_CACHE_MAX_MATERIALIZED_INDEXES / 3 || mesh.surfaceNum < -1
			|| mesh.surfaceNum > MD5_CACHE_MAX_MESHES
			|| ( mesh.deformInfo->numMirroredVerts > 0 && mesh.deformInfo->mirroredVerts == NULL ) ) {
			return false;
		}

		idList<int> firstWeight;
		idList<int> weightsPerVert;
		firstWeight.SetNum( mesh.texCoords.Num() );
		weightsPerVert.SetNum( mesh.texCoords.Num() );
		int weightCursor = 0;
		for ( int vertIndex = 0; vertIndex < mesh.texCoords.Num(); ++vertIndex ) {
			if ( weightCursor >= mesh.numWeights ) {
				return false;
			}
			const int nextBytes = mesh.weights[weightCursor].nextVertexOffset;
			if ( nextBytes <= 0 || ( nextBytes % static_cast<int>( sizeof( jointWeight_t ) ) ) != 0 ) {
				return false;
			}
			const int vertexWeightCount = nextBytes / sizeof( jointWeight_t );
			if ( vertexWeightCount <= 0 || vertexWeightCount > mesh.numWeights - weightCursor ) {
				return false;
			}
			firstWeight[vertIndex] = weightCursor;
			weightsPerVert[vertIndex] = vertexWeightCount;
			for ( int j = 0; j < vertexWeightCount; ++j ) {
				if ( mesh.weights[weightCursor + j].nextVertexOffset != ( vertexWeightCount - j ) * sizeof( jointWeight_t ) ) {
					return false;
				}
			}
			weightCursor += vertexWeightCount;
		}
		if ( weightCursor != mesh.numWeights || mesh.deformInfo->numSourceVerts != mesh.texCoords.Num()
			|| mesh.deformInfo->numOutputVerts < mesh.deformInfo->numSourceVerts
			|| mesh.deformInfo->numOutputVerts > MD5_CACHE_MAX_MATERIALIZED_VERTS
			|| mesh.deformInfo->numMirroredVerts < 0
			|| mesh.deformInfo->numMirroredVerts != mesh.deformInfo->numOutputVerts - mesh.deformInfo->numSourceVerts ) {
			return false;
		}

		int storedWeightCount = mesh.numWeights;
		for ( int mirrorIndex = 0; mirrorIndex < mesh.deformInfo->numMirroredVerts; ++mirrorIndex ) {
			const int sourceVert = mesh.deformInfo->mirroredVerts[mirrorIndex];
			if ( sourceVert < 0 || sourceVert >= weightsPerVert.Num()
				|| weightsPerVert[sourceVert] > MD5_CACHE_MAX_WEIGHTS - storedWeightCount ) {
				return false;
			}
			storedWeightCount += weightsPerVert[sourceVert];
		}
		if ( storedWeightCount > MD5_CACHE_MAX_MATERIALIZED_WEIGHTS * 2 ) {
			return false;
		}

		if ( !writer.WriteString( mesh.shader->GetName(), MD5_CACHE_MAX_NAME )
			|| !writer.WriteInt( mesh.texCoords.Num() ) ) {
			return false;
		}
		if ( !writer.WriteFloatArray( reinterpret_cast<const float *>( mesh.texCoords.Ptr() ), mesh.texCoords.Num() * 2 ) ) {
			return false;
		}
		if ( !writer.WriteInt( mesh.numWeights ) || !writer.WriteInt( storedWeightCount ) ) {
			return false;
		}
		for ( int i = 0; i < storedWeightCount; ++i ) {
			const jointWeight_t &weight = mesh.weights[i];
			if ( weight.weight < 0.0f || weight.jointMatOffset < 0
				|| ( weight.jointMatOffset % static_cast<int>( sizeof( idJointMat ) ) ) != 0
				|| weight.jointMatOffset / static_cast<int>( sizeof( idJointMat ) ) >= joints.Num()
				|| weight.nextVertexOffset <= 0
				|| !writer.WriteFloat( weight.weight ) || !writer.WriteInt( weight.jointMatOffset )
				|| !writer.WriteInt( weight.nextVertexOffset ) || !writer.WriteVec4( mesh.scaledBaseVectors[i] ) ) {
				return false;
			}
		}
		for ( int i = 0; i < mesh.numWeights; ++i ) {
			if ( !writer.WriteVec4( mesh.scaledWeights[i] )
				|| !writer.WriteInt( mesh.weightIndex[i * 2 + 0] )
				|| !writer.WriteInt( mesh.weightIndex[i * 2 + 1] ) ) {
				return false;
			}
		}

		const deformInfo_t &deform = *mesh.deformInfo;
		if ( deform.numIndexes != mesh.numTris * 3 || deform.numIndexes < 0 || deform.numIndexes > MD5_CACHE_MAX_INDEXES
			|| deform.numMirroredVerts < 0 || deform.numMirroredVerts > deform.numOutputVerts
			|| deform.numDupVerts < 0 || deform.numDupVerts > deform.numOutputVerts
			|| deform.numSilEdges < 0 || deform.numSilEdges > MD5_CACHE_MAX_SIL_EDGES
			|| ( deform.numIndexes > 0 && ( deform.indexes == NULL || deform.silIndexes == NULL ) )
			|| ( deform.numMirroredVerts > 0 && deform.mirroredVerts == NULL )
			|| ( deform.numDupVerts > 0 && deform.dupVerts == NULL )
			|| ( deform.numSilEdges > 0 && deform.silEdges == NULL )
			|| !writer.WriteInt( mesh.numTris ) || !writer.WriteInt( mesh.surfaceNum )
			|| !writer.WriteInt( deform.numSourceVerts ) || !writer.WriteInt( deform.numOutputVerts )
			|| !writer.WriteInt( deform.numIndexes ) ) {
			return false;
		}
		for ( int i = 0; i < deform.numIndexes; ++i ) {
			if ( deform.indexes[i] < 0 || deform.indexes[i] >= deform.numOutputVerts
				|| deform.silIndexes[i] < 0 || deform.silIndexes[i] >= deform.numOutputVerts ) {
				return false;
			}
		}
		idList<int> indexPairs;
		indexPairs.SetNum( deform.numIndexes * 2 );
		for ( int i = 0; i < deform.numIndexes; ++i ) {
			indexPairs[i * 2 + 0] = deform.indexes[i];
			indexPairs[i * 2 + 1] = deform.silIndexes[i];
		}
		if ( !writer.WriteIntArray( indexPairs.Ptr(), indexPairs.Num() ) ) {
			return false;
		}
		if ( !writer.WriteInt( deform.numMirroredVerts ) ) {
			return false;
		}
		for ( int i = 0; i < deform.numMirroredVerts; ++i ) {
			if ( deform.mirroredVerts[i] < 0 || deform.mirroredVerts[i] >= deform.numSourceVerts ) {
				return false;
			}
		}
		if ( !writer.WriteIntArray( deform.mirroredVerts, deform.numMirroredVerts ) ) {
			return false;
		}
		if ( !writer.WriteInt( deform.numDupVerts ) ) {
			return false;
		}
		for ( int i = 0; i < deform.numDupVerts * 2; ++i ) {
			if ( deform.dupVerts[i] < 0 || deform.dupVerts[i] >= deform.numOutputVerts ) {
				return false;
			}
		}
		if ( !writer.WriteIntArray( deform.dupVerts, deform.numDupVerts * 2 ) ) {
			return false;
		}
		if ( !writer.WriteInt( deform.numSilEdges ) ) {
			return false;
		}
		const int planeCount = deform.numIndexes / 3;
		for ( int i = 0; i < deform.numSilEdges; ++i ) {
			const silEdge_t &edge = deform.silEdges[i];
			if ( edge.p1 < 0 || edge.p1 >= planeCount || edge.p2 < 0 || edge.p2 > planeCount
				|| edge.v1 < 0 || edge.v1 >= deform.numOutputVerts || edge.v2 < 0 || edge.v2 >= deform.numOutputVerts ) {
				return false;
			}
		}
		if ( !writer.WriteIntArray( reinterpret_cast<const int *>( deform.silEdges ), deform.numSilEdges * 4 ) ) {
			return false;
		}
		if ( !writer.WriteBool( deform.dominantTris != NULL ) ) {
			return false;
		}
		if ( deform.dominantTris != NULL ) {
			for ( int i = 0; i < deform.numOutputVerts; ++i ) {
				const dominantTri_t &dominant = deform.dominantTris[i];
				if ( dominant.v2 < 0 || dominant.v2 >= deform.numOutputVerts
					|| dominant.v3 < 0 || dominant.v3 >= deform.numOutputVerts
					|| !R_RenderModelCacheFloatIsFinite( dominant.normalizationScale[0] )
					|| !R_RenderModelCacheFloatIsFinite( dominant.normalizationScale[1] )
					|| !R_RenderModelCacheFloatIsFinite( dominant.normalizationScale[2] ) ) {
					return false;
				}
			}
			if ( !writer.WriteBytes( deform.dominantTris, deform.numOutputVerts * static_cast<int>( sizeof( dominantTri_t ) ) ) ) {
				return false;
			}
		}
		if ( deform.numOutputVerts > 0x1fffffff || !writer.WriteInt( deform.numOutputVerts * 4 ) ) {
			return false;
		}
		if ( !writer.WriteFloatArray( reinterpret_cast<const float *>( mesh.baseVectors ), deform.numOutputVerts * 16 ) ) {
			return false;
		}
	}
	return writer.IsValid();
}

bool idRenderModelMD5::ReadLevelLoadCachePayload( idFile &file ) {
	idRenderModelCacheReader reader( file );
	unsigned int magic = 0;
	int version = 0;
	if ( !reader.ReadUnsignedInt( magic ) || magic != MD5_MODEL_CACHE_MAGIC
		|| !reader.ReadInt( version ) || version != MD5_MODEL_CACHE_VERSION ) {
		return false;
	}

	idRenderModelMD5 staged;
	if ( !staged.idRenderModelStatic::ReadLevelLoadCachePayload( file ) || staged.surfaces.Num() != 0 ) {
		return false;
	}

	int jointCount = 0;
	if ( !reader.ReadCount( jointCount, MD5_CACHE_MAX_JOINTS )
		|| !reader.Reserve( jointCount, sizeof( idStr ) + sizeof( int ) + sizeof( idJointQuat ) + sizeof( idJointMat ) ) ) {
		return false;
	}
	idList<idStr> jointNames;
	idList<int> jointParents;
	idList<idJointQuat> decodedPose;
	idList<idJointMat> decodedSkinMats;
	jointNames.SetNum( jointCount );
	jointParents.SetNum( jointCount );
	for ( int i = 0; i < jointCount; ++i ) {
		if ( !reader.ReadString( jointNames[i], MD5_CACHE_MAX_NAME ) || jointNames[i].IsEmpty()
			|| !reader.ReadInt( jointParents[i] ) || jointParents[i] < -1 || jointParents[i] >= i ) {
			return false;
		}
	}

	int poseCount = 0;
	if ( !reader.ReadCount( poseCount, MD5_CACHE_MAX_JOINTS ) || poseCount != jointCount ) {
		return false;
	}
	decodedPose.SetNum( poseCount );
	for ( int i = 0; i < poseCount; ++i ) {
		if ( !R_MD5CacheReadJointQuat( reader, decodedPose[i] ) ) {
			return false;
		}
	}
	int skinMatCount = 0;
	if ( !reader.ReadCount( skinMatCount, MD5_CACHE_MAX_JOINTS ) || skinMatCount != jointCount ) {
		return false;
	}
	decodedSkinMats.SetNum( skinMatCount );
	for ( int i = 0; i < skinMatCount; ++i ) {
		if ( !R_MD5CacheReadJointMat( reader, decodedSkinMats[i] ) ) {
			return false;
		}
	}

	int meshCount = 0;
	if ( !reader.ReadCount( meshCount, MD5_CACHE_MAX_MESHES, sizeof( md5CacheMeshStage_t ) ) ) {
		return false;
	}
	idList<md5CacheMeshStage_t> decodedMeshes;
	decodedMeshes.SetNum( meshCount );
	for ( int meshIndex = 0; meshIndex < meshCount; ++meshIndex ) {
		md5CacheMeshStage_t &mesh = decodedMeshes[meshIndex];
		int texCoordCount = 0;
		int storedWeightCount = 0;
		if ( !reader.ReadString( mesh.materialName, MD5_CACHE_MAX_NAME ) || mesh.materialName.IsEmpty()
			|| !reader.ReadCount( texCoordCount, MD5_CACHE_MAX_VERTS, sizeof( idVec2 ) ) || texCoordCount <= 0
			|| texCoordCount > MD5_CACHE_MAX_MATERIALIZED_VERTS ) {
			return false;
		}
		mesh.texCoords.SetNum( texCoordCount );
		if ( !reader.ReadFloatArray( reinterpret_cast<float *>( mesh.texCoords.Ptr() ), texCoordCount * 2 ) ) {
			return false;
		}
		if ( !reader.ReadCount( mesh.numWeights, MD5_CACHE_MAX_WEIGHTS ) || mesh.numWeights <= 0
			|| mesh.numWeights > MD5_CACHE_MAX_MATERIALIZED_WEIGHTS
			|| !reader.ReadCount( storedWeightCount, MD5_CACHE_MAX_WEIGHTS ) || storedWeightCount < mesh.numWeights
			|| storedWeightCount > MD5_CACHE_MAX_MATERIALIZED_WEIGHTS * 2
			|| !reader.Reserve( storedWeightCount, sizeof( jointWeight_t ) + sizeof( idVec4 ) ) ) {
			return false;
		}
		mesh.weights.SetNum( storedWeightCount );
		mesh.scaledBaseVectors.SetNum( storedWeightCount );
		for ( int i = 0; i < storedWeightCount; ++i ) {
			jointWeight_t &weight = mesh.weights[i];
			if ( !reader.ReadFloat( weight.weight ) || weight.weight < 0.0f
				|| !reader.ReadInt( weight.jointMatOffset ) || weight.jointMatOffset < 0
				|| ( weight.jointMatOffset % static_cast<int>( sizeof( idJointMat ) ) ) != 0
				|| weight.jointMatOffset / static_cast<int>( sizeof( idJointMat ) ) >= jointCount
				|| !reader.ReadInt( weight.nextVertexOffset ) || weight.nextVertexOffset <= 0
				|| ( weight.nextVertexOffset % static_cast<int>( sizeof( jointWeight_t ) ) ) != 0
				|| !reader.ReadVec4( mesh.scaledBaseVectors[i] ) ) {
				return false;
			}
		}

		if ( mesh.numWeights > 0x3fffffff || !reader.Reserve( mesh.numWeights, sizeof( idVec4 ) + sizeof( int ) * 2 ) ) {
			return false;
		}
		mesh.scaledWeights.SetNum( mesh.numWeights );
		mesh.weightIndex.SetNum( mesh.numWeights * 2 );
		for ( int i = 0; i < mesh.numWeights; ++i ) {
			if ( !reader.ReadVec4( mesh.scaledWeights[i] )
				|| !reader.ReadInt( mesh.weightIndex[i * 2 + 0] )
				|| mesh.weightIndex[i * 2 + 0] < 0
				|| ( mesh.weightIndex[i * 2 + 0] % static_cast<int>( sizeof( idJointMat ) ) ) != 0
				|| mesh.weightIndex[i * 2 + 0] / static_cast<int>( sizeof( idJointMat ) ) >= jointCount
				|| !reader.ReadInt( mesh.weightIndex[i * 2 + 1] )
				|| ( mesh.weightIndex[i * 2 + 1] != 0 && mesh.weightIndex[i * 2 + 1] != 1 ) ) {
				return false;
			}
		}

		if ( !reader.ReadCount( mesh.numTris, MD5_CACHE_MAX_INDEXES / 3 )
			|| !reader.ReadInt( mesh.surfaceNum ) || mesh.surfaceNum < -1 || mesh.surfaceNum > MD5_CACHE_MAX_MESHES
			|| !reader.ReadCount( mesh.deform.numSourceVerts, MD5_CACHE_MAX_VERTS )
			|| mesh.deform.numSourceVerts != texCoordCount
			|| !reader.ReadCount( mesh.deform.numOutputVerts, MD5_CACHE_MAX_VERTS )
			|| mesh.deform.numOutputVerts < mesh.deform.numSourceVerts
			|| mesh.deform.numOutputVerts > MD5_CACHE_MAX_MATERIALIZED_VERTS ) {
			return false;
		}

		int deformIndexCount = 0;
		if ( !reader.ReadCount( deformIndexCount, MD5_CACHE_MAX_INDEXES )
			|| deformIndexCount != mesh.numTris * 3
			|| deformIndexCount > MD5_CACHE_MAX_MATERIALIZED_INDEXES
			|| !reader.Reserve( deformIndexCount, sizeof( glIndex_t ) * 2 ) ) {
			return false;
		}
		mesh.deform.indexes.SetNum( deformIndexCount );
		mesh.deform.silIndexes.SetNum( deformIndexCount );
		idList<int> indexPairs;
		indexPairs.SetNum( deformIndexCount * 2 );
		if ( !reader.ReadIntArray( indexPairs.Ptr(), indexPairs.Num() ) ) {
			return false;
		}
		for ( int i = 0; i < deformIndexCount; ++i ) {
			mesh.deform.indexes[i] = indexPairs[i * 2 + 0];
			mesh.deform.silIndexes[i] = indexPairs[i * 2 + 1];
			if ( mesh.deform.indexes[i] < 0 || mesh.deform.indexes[i] >= mesh.deform.numOutputVerts
				|| mesh.deform.silIndexes[i] < 0 || mesh.deform.silIndexes[i] >= mesh.deform.numOutputVerts ) {
				return false;
			}
		}

		int mirroredCount = 0;
		if ( !reader.ReadCount( mirroredCount, mesh.deform.numOutputVerts, sizeof( int ) )
			|| mirroredCount != mesh.deform.numOutputVerts - mesh.deform.numSourceVerts ) {
			return false;
		}
		mesh.deform.mirroredVerts.SetNum( mirroredCount );
		if ( !reader.ReadIntArray( mesh.deform.mirroredVerts.Ptr(), mirroredCount ) ) {
			return false;
		}
		for ( int i = 0; i < mirroredCount; ++i ) {
			if ( mesh.deform.mirroredVerts[i] < 0
				|| mesh.deform.mirroredVerts[i] >= mesh.deform.numSourceVerts ) {
				return false;
			}
		}

		int dupCount = 0;
		if ( !reader.ReadCount( dupCount, mesh.deform.numOutputVerts ) || dupCount > 0x3fffffff
			|| !reader.Reserve( dupCount * 2, sizeof( int ) ) ) {
			return false;
		}
		mesh.deform.dupVerts.SetNum( dupCount * 2 );
		if ( !reader.ReadIntArray( mesh.deform.dupVerts.Ptr(), mesh.deform.dupVerts.Num() ) ) {
			return false;
		}
		for ( int i = 0; i < mesh.deform.dupVerts.Num(); ++i ) {
			if ( mesh.deform.dupVerts[i] < 0
				|| mesh.deform.dupVerts[i] >= mesh.deform.numOutputVerts ) {
				return false;
			}
		}

		int silEdgeCount = 0;
		if ( !reader.ReadCount( silEdgeCount, MD5_CACHE_MAX_SIL_EDGES, sizeof( silEdge_t ) ) ) {
			return false;
		}
		mesh.deform.silEdges.SetNum( silEdgeCount );
		if ( !reader.ReadIntArray( reinterpret_cast<int *>( mesh.deform.silEdges.Ptr() ), silEdgeCount * 4 ) ) {
			return false;
		}
		const int planeCount = deformIndexCount / 3;
		for ( int i = 0; i < silEdgeCount; ++i ) {
			const silEdge_t &edge = mesh.deform.silEdges[i];
			if ( edge.p1 < 0 || edge.p1 >= planeCount || edge.p2 < 0 || edge.p2 > planeCount
				|| edge.v1 < 0 || edge.v1 >= mesh.deform.numOutputVerts
				|| edge.v2 < 0 || edge.v2 >= mesh.deform.numOutputVerts ) {
				return false;
			}
		}

		bool hasDominantTris = false;
		if ( !reader.ReadBool( hasDominantTris ) ) {
			return false;
		}
		if ( hasDominantTris ) {
			if ( !reader.Reserve( mesh.deform.numOutputVerts, sizeof( dominantTri_t ) ) ) {
				return false;
			}
			mesh.deform.dominantTris.SetNum( mesh.deform.numOutputVerts );
			if ( !reader.ReadBytes( mesh.deform.dominantTris.Ptr(),
					mesh.deform.numOutputVerts * static_cast<int>( sizeof( dominantTri_t ) ) ) ) {
				return false;
			}
			for ( int i = 0; i < mesh.deform.numOutputVerts; ++i ) {
				dominantTri_t &dominant = mesh.deform.dominantTris[i];
				dominant.v2 = LittleLong( dominant.v2 );
				dominant.v3 = LittleLong( dominant.v3 );
				if ( dominant.v2 < 0 || dominant.v2 >= mesh.deform.numOutputVerts
					|| dominant.v3 < 0 || dominant.v3 >= mesh.deform.numOutputVerts ) {
					return false;
				}
				for ( int scale = 0; scale < 3; ++scale ) {
					dominant.normalizationScale[scale] = LittleFloat( dominant.normalizationScale[scale] );
					if ( !R_RenderModelCacheFloatIsFinite( dominant.normalizationScale[scale] ) ) {
						return false;
					}
				}
			}
		}

		int baseVectorCount = 0;
		if ( mesh.deform.numOutputVerts > 0x1fffffff
			|| !reader.ReadCount( baseVectorCount, MD5_CACHE_MAX_VERTS * 4, sizeof( idVec4 ) )
			|| baseVectorCount != mesh.deform.numOutputVerts * 4 ) {
			return false;
		}
		mesh.baseVectors.SetNum( baseVectorCount );
		if ( !reader.ReadFloatArray( reinterpret_cast<float *>( mesh.baseVectors.Ptr() ), baseVectorCount * 4 ) ) {
			return false;
		}

		idList<int> weightsPerVert;
		weightsPerVert.SetNum( texCoordCount );
		int cursor = 0;
		for ( int vertIndex = 0; vertIndex < texCoordCount; ++vertIndex ) {
			if ( cursor >= mesh.numWeights ) {
				return false;
			}
			const int groupCount = mesh.weights[cursor].nextVertexOffset / sizeof( jointWeight_t );
			if ( groupCount <= 0 || groupCount > mesh.numWeights - cursor ) {
				return false;
			}
			weightsPerVert[vertIndex] = groupCount;
			for ( int j = 0; j < groupCount; ++j ) {
				if ( mesh.weights[cursor + j].nextVertexOffset != ( groupCount - j ) * sizeof( jointWeight_t ) ) {
					return false;
				}
			}
			cursor += groupCount;
		}
		if ( cursor != mesh.numWeights ) {
			return false;
		}
		int expectedStoredWeights = mesh.numWeights;
		for ( int i = 0; i < mirroredCount; ++i ) {
			const int groupCount = weightsPerVert[mesh.deform.mirroredVerts[i]];
			if ( groupCount > MD5_CACHE_MAX_WEIGHTS - expectedStoredWeights ) {
				return false;
			}
			expectedStoredWeights += groupCount;
			if ( cursor + groupCount > mesh.weights.Num() ) {
				return false;
			}
			for ( int j = 0; j < groupCount; ++j ) {
				if ( mesh.weights[cursor + j].nextVertexOffset != ( groupCount - j ) * sizeof( jointWeight_t ) ) {
					return false;
				}
			}
			cursor += groupCount;
		}
		if ( expectedStoredWeights != mesh.weights.Num() || cursor != mesh.weights.Num() ) {
			return false;
		}

		int completedWeightVertices = 0;
		for ( int i = 0; i < mesh.numWeights; ++i ) {
			completedWeightVertices += mesh.weightIndex[i * 2 + 1];
		}
		if ( completedWeightVertices != texCoordCount || mesh.weightIndex[mesh.numWeights * 2 - 1] != 1 ) {
			return false;
		}
	}
	if ( !reader.IsValid() ) {
		return false;
	}

	staged.joints.SetNum( jointCount );
	for ( int i = 0; i < jointCount; ++i ) {
		staged.joints[i].name = jointNames[i];
		staged.joints[i].parent = ( jointParents[i] >= 0 ) ? &staged.joints[jointParents[i]] : NULL;
	}
	staged.defaultPose.Swap( decodedPose );
	staged.skinSpaceToLocalMats.Swap( decodedSkinMats );
	staged.meshes.SetNum( meshCount );
	for ( int meshIndex = 0; meshIndex < meshCount; ++meshIndex ) {
		const md5CacheMeshStage_t &source = decodedMeshes[meshIndex];
		idMD5Mesh &mesh = staged.meshes[meshIndex];
		mesh.shader = declManager->FindMaterial( source.materialName.c_str() );
		if ( mesh.shader == NULL || idStr::Icmp( mesh.shader->GetName(), source.materialName.c_str() ) != 0
			|| mesh.shader->UseUnsmoothedTangents() != ( source.deform.dominantTris.Num() > 0 ) ) {
			return false;
		}

		// The reader fully range-validated every derived array and enforced
		// numOutputVerts == numSourceVerts + mirroredVerts.Num(); allocate the
		// deform info directly instead of re-deriving it just to overwrite it.
		deformInfo_t *deform = R_AllocDeformInfo( source.deform.numSourceVerts, source.deform.numOutputVerts,
			source.deform.indexes.Num(), source.deform.mirroredVerts.Num(),
			source.deform.dupVerts.Num() / 2, source.deform.silEdges.Num(),
			source.deform.dominantTris.Num() > 0 );
		if ( deform == NULL ) {
			return false;
		}
		memcpy( deform->indexes, source.deform.indexes.Ptr(), deform->numIndexes * sizeof( deform->indexes[0] ) );
		memcpy( deform->silIndexes, source.deform.silIndexes.Ptr(), deform->numIndexes * sizeof( deform->silIndexes[0] ) );
		if ( deform->numMirroredVerts > 0 ) {
			memcpy( deform->mirroredVerts, source.deform.mirroredVerts.Ptr(), deform->numMirroredVerts * sizeof( deform->mirroredVerts[0] ) );
		}
		if ( deform->numDupVerts > 0 ) {
			memcpy( deform->dupVerts, source.deform.dupVerts.Ptr(), deform->numDupVerts * 2 * sizeof( deform->dupVerts[0] ) );
		}
		if ( deform->numSilEdges > 0 ) {
			memcpy( deform->silEdges, source.deform.silEdges.Ptr(), deform->numSilEdges * sizeof( deform->silEdges[0] ) );
		}
		if ( deform->dominantTris != NULL ) {
			memcpy( deform->dominantTris, source.deform.dominantTris.Ptr(), deform->numOutputVerts * sizeof( deform->dominantTris[0] ) );
		}

		mesh.texCoords = source.texCoords;
		mesh.numWeights = source.numWeights;
		mesh.weights = static_cast<jointWeight_t *>( Mem_Alloc16( source.weights.Num() * sizeof( mesh.weights[0] ) ) );
		mesh.scaledBaseVectors = static_cast<idVec4 *>( Mem_Alloc16( source.scaledBaseVectors.Num() * sizeof( mesh.scaledBaseVectors[0] ) ) );
		mesh.baseVectors = static_cast<idVec4 *>( Mem_Alloc16( source.baseVectors.Num() * sizeof( mesh.baseVectors[0] ) ) );
		mesh.scaledWeights = static_cast<idVec4 *>( Mem_Alloc16( source.scaledWeights.Num() * sizeof( mesh.scaledWeights[0] ) ) );
		mesh.weightIndex = static_cast<int *>( Mem_Alloc16( source.weightIndex.Num() * sizeof( mesh.weightIndex[0] ) ) );
		memcpy( mesh.weights, source.weights.Ptr(), source.weights.Num() * sizeof( mesh.weights[0] ) );
		memcpy( mesh.scaledBaseVectors, source.scaledBaseVectors.Ptr(), source.scaledBaseVectors.Num() * sizeof( mesh.scaledBaseVectors[0] ) );
		memcpy( mesh.baseVectors, source.baseVectors.Ptr(), source.baseVectors.Num() * sizeof( mesh.baseVectors[0] ) );
		memcpy( mesh.scaledWeights, source.scaledWeights.Ptr(), source.scaledWeights.Num() * sizeof( mesh.scaledWeights[0] ) );
		memcpy( mesh.weightIndex, source.weightIndex.Ptr(), source.weightIndex.Num() * sizeof( mesh.weightIndex[0] ) );
		mesh.numTris = source.numTris;
		mesh.surfaceNum = source.surfaceNum;
		mesh.currentTime = 0.0f;
		mesh.deformInfo = deform;
		mesh.BuildGpuSkinningSidecar( jointCount );
	}

	SwapLevelLoadCacheState( staged );
	joints.Swap( staged.joints );
	defaultPose.Swap( staged.defaultPose );
	skinSpaceToLocalMats.Swap( staged.skinSpaceToLocalMats );
	meshes.Swap( staged.meshes );
	viewEnt = NULL;
	return true;
}

/*
====================
idMD5Mesh::idMD5Mesh
====================
*/
idMD5Mesh::idMD5Mesh() {
	weights			= NULL;
	scaledBaseVectors = NULL;
	baseVectors		= NULL;
	scaledWeights	= NULL;
	weightIndex		= NULL;
	shader			= NULL;
	numTris			= 0;
	deformInfo		= NULL;
	surfaceNum		= 0;
	currentTime		= 0.0f;
	gpuSkinningNumJoints = 0;
	gpuSkinningFallback = GPU_SKINNING_FALLBACK_MISSING_SKIN_VERTICES;
}

/*
====================
idMD5Mesh::~idMD5Mesh
====================
*/
idMD5Mesh::~idMD5Mesh() {
	Mem_Free16( weights );
	Mem_Free16( scaledBaseVectors );
	Mem_Free16( baseVectors );
	Mem_Free16( scaledWeights );
	Mem_Free16( weightIndex );
	if ( deformInfo ) {
		R_FreeDeformInfo( deformInfo );
		deformInfo = NULL;
	}
}

/*
====================
idMD5Mesh::ParseMesh
====================
*/
void idMD5Mesh::ParseMesh( idLexer &parser, int numJoints, const idJointMat *joints ) {
	idToken		token;
	idToken		name;
	int			num;
	int			count;
	int			jointnum;
	idStr		shaderName;
	int			i, j;
	idList<int>	tris;
	idList<int>	firstWeightForVertex;
	idList<int>	numWeightsForVertex;
	int			maxweight;
	idList<vertexWeight_t> tempWeights;

	parser.ExpectTokenString( "{" );

	if ( parser.CheckTokenString( "name" ) ) {
		parser.ReadToken( &name );
	}

	parser.ExpectTokenString( "shader" );
	parser.ReadToken( &token );
	shaderName = token;
	shader = declManager->FindMaterial( shaderName );

	parser.ExpectTokenString( "numverts" );
	count = parser.ParseInt();
	if ( count < 0 ) {
		parser.Error( "Invalid size: %s", token.c_str() );
	}

	texCoords.SetNum( count );
	firstWeightForVertex.SetNum( count );
	numWeightsForVertex.SetNum( count );

	numWeights = 0;
	maxweight = 0;
	for ( i = 0; i < texCoords.Num(); i++ ) {
		parser.ExpectTokenString( "vert" );
		parser.ParseInt();

		parser.Parse1DMatrix( 2, texCoords[i].ToFloatPtr() );

		firstWeightForVertex[i] = parser.ParseInt();
		numWeightsForVertex[i] = parser.ParseInt();
		if ( numWeightsForVertex[i] <= 0 ) {
			// A negative count would skip the fill loop below, leaving count at
			// 0 and driving the weightIndex[count*2-1] write out of bounds.
			parser.Error( "Vertex without any joint weights." );
		}
		if ( firstWeightForVertex[i] < 0 ) {
			// A negative base index reads tempWeights below its buffer and can
			// also mask the maxweight range check.
			parser.Error( "Vertex with negative first weight index." );
		}

		numWeights += numWeightsForVertex[i];
		if ( numWeightsForVertex[i] + firstWeightForVertex[i] > maxweight ) {
			maxweight = numWeightsForVertex[i] + firstWeightForVertex[i];
		}
	}

	parser.ExpectTokenString( "numtris" );
	count = parser.ParseInt();
	if ( count < 0 ) {
		parser.Error( "Invalid size: %d", count );
	}

	tris.SetNum( count * 3 );
	numTris = count;
	for ( i = 0; i < count; i++ ) {
		parser.ExpectTokenString( "tri" );
		parser.ParseInt();
		tris[i * 3 + 0] = parser.ParseInt();
		tris[i * 3 + 1] = parser.ParseInt();
		tris[i * 3 + 2] = parser.ParseInt();
	}

	parser.ExpectTokenString( "numweights" );
	count = parser.ParseInt();
	if ( count < 0 ) {
		parser.Error( "Invalid size: %d", count );
	}
	if ( maxweight > count ) {
		// This was a warning, but the insertion-sort and fill loops below index
		// tempWeights[firstWeight + j] directly (idList::operator[] is unchecked
		// in release), so an out-of-range reference is an OOB read/write, not a
		// cosmetic issue. Stock models never trip this.
		parser.Error( "Vertices reference out of range weights in model (%d of %d weights).", maxweight, count );
	}

	tempWeights.SetNum( count );
	for ( i = 0; i < count; i++ ) {
		parser.ExpectTokenString( "weight" );
		parser.ParseInt();

		jointnum = parser.ParseInt();
		if ( jointnum < 0 || jointnum >= numJoints ) {
			parser.Error( "Joint Index out of range(%d): %d", numJoints, jointnum );
		}

		tempWeights[i].joint = jointnum;
		tempWeights[i].jointWeight = parser.ParseFloat();
		parser.Parse1DMatrix( 3, tempWeights[i].offset.ToFloatPtr() );
	}

	for ( i = 0; i < texCoords.Num(); ++i ) {
		const int firstWeight = firstWeightForVertex[i];
		const int vertexWeightCount = numWeightsForVertex[i];

		for ( j = 1; j < vertexWeightCount; ++j ) {
			vertexWeight_t sortedWeight = tempWeights[firstWeight + j];
			int insertIndex = j - 1;

			while ( insertIndex >= 0
				&& tempWeights[firstWeight + insertIndex].jointWeight < sortedWeight.jointWeight ) {
				tempWeights[firstWeight + insertIndex + 1] = tempWeights[firstWeight + insertIndex];
				--insertIndex;
			}
			tempWeights[firstWeight + insertIndex + 1] = sortedWeight;
		}
	}

	scaledWeights = (idVec4 *)Mem_Alloc16( numWeights * sizeof( scaledWeights[0] ) );
	weightIndex = (int *)Mem_Alloc16( numWeights * 2 * sizeof( weightIndex[0] ) );
	memset( weightIndex, 0, numWeights * 2 * sizeof( weightIndex[0] ) );

	count = 0;
	for ( i = 0; i < texCoords.Num(); i++ ) {
		num = firstWeightForVertex[i];
		for ( j = 0; j < numWeightsForVertex[i]; j++, num++, count++ ) {
			scaledWeights[count].ToVec3() = tempWeights[num].offset * tempWeights[num].jointWeight;
			scaledWeights[count].w = tempWeights[num].jointWeight;
			weightIndex[count * 2 + 0] = tempWeights[num].joint * sizeof( idJointMat );
		}
		weightIndex[count * 2 - 1] = 1;
	}

	parser.ExpectTokenString( "}" );

	c_numVerts += texCoords.Num();
	c_numWeights += numWeights;
	c_numWeightJoints++;
	for ( i = 0; i < numWeights; i++ ) {
		c_numWeightJoints += weightIndex[i * 2 + 1];
	}

	// Heap, not _alloca16: this is sized by the mesh's vertex count, and a large
	// enough mesh walked off the end of the thread stack. Android runs the engine
	// on an SDL thread with a 1MB stack, where a ~17k vertex mesh is a megabyte
	// of alloca and the first verts[i].Clear() dies on the guard page.
	idTempArray16< idDrawVert > vertsBuffer( texCoords.Num() );
	idDrawVert *verts = vertsBuffer.Ptr();
	for ( i = 0; i < texCoords.Num(); i++ ) {
		verts[i].Clear();
		verts[i].st = texCoords[i];
	}
	TransformVerts( verts, joints );
	deformInfo = R_BuildDeformInfo( texCoords.Num(), verts, tris.Num(), tris.Ptr(), shader->UseUnsmoothedTangents() );

	currentTime = 0.0f;

	weights = (jointWeight_t *)Mem_Alloc16( numWeights * sizeof( weights[0] ) );
	scaledBaseVectors = (idVec4 *)Mem_Alloc16( numWeights * sizeof( scaledBaseVectors[0] ) );

	count = 0;
	for ( i = 0; i < texCoords.Num(); ++i ) {
		const int vertexWeightCount = numWeightsForVertex[i];
		const int firstWeight = firstWeightForVertex[i];

		for ( j = 0; j < vertexWeightCount; ++j, ++count ) {
			const vertexWeight_t &tempWeight = tempWeights[firstWeight + j];

			weights[count].weight = tempWeight.jointWeight;
			weights[count].jointMatOffset = tempWeight.joint * sizeof( idJointMat );
			weights[count].nextVertexOffset = ( vertexWeightCount - j ) * sizeof( weights[0] );

			scaledBaseVectors[count].ToVec3() = tempWeight.offset * tempWeight.jointWeight;
			scaledBaseVectors[count].w = tempWeight.jointWeight;
		}
	}

	int mirroredWeightCount = 0;
	for ( i = 0; i < deformInfo->numMirroredVerts; ++i ) {
		mirroredWeightCount += numWeightsForVertex[deformInfo->mirroredVerts[i]];
	}

	if ( mirroredWeightCount > 0 ) {
		idVec4 *newScaledBaseVectors = (idVec4 *)Mem_Alloc16( ( numWeights + mirroredWeightCount ) * sizeof( scaledBaseVectors[0] ) );
		jointWeight_t *newWeights = (jointWeight_t *)Mem_Alloc16( ( numWeights + mirroredWeightCount ) * sizeof( weights[0] ) );

		memcpy( newScaledBaseVectors, scaledBaseVectors, numWeights * sizeof( scaledBaseVectors[0] ) );
		memcpy( newWeights, weights, numWeights * sizeof( weights[0] ) );
		Mem_Free16( scaledBaseVectors );
		Mem_Free16( weights );
		scaledBaseVectors = newScaledBaseVectors;
		weights = newWeights;

		int appendWeight = numWeights;
		for ( i = 0; i < deformInfo->numMirroredVerts; ++i ) {
			const int mirroredVert = deformInfo->mirroredVerts[i];
			const int vertexWeightCount = numWeightsForVertex[mirroredVert];
			const int firstWeight = firstWeightForVertex[mirroredVert];

			for ( j = 0; j < vertexWeightCount; ++j, ++appendWeight ) {
				const vertexWeight_t &tempWeight = tempWeights[firstWeight + j];

				weights[appendWeight].weight = tempWeight.jointWeight;
				weights[appendWeight].jointMatOffset = tempWeight.joint * sizeof( idJointMat );
				weights[appendWeight].nextVertexOffset = ( vertexWeightCount - j ) * sizeof( weights[0] );

				scaledBaseVectors[appendWeight].ToVec3() = tempWeight.offset * tempWeight.jointWeight;
				scaledBaseVectors[appendWeight].w = tempWeight.jointWeight;
			}
		}
	}

	baseVectors = (idVec4 *)Mem_Alloc16( deformInfo->numOutputVerts * 4 * sizeof( baseVectors[0] ) );
	modelSurface_t tempSurf;
	memset( &tempSurf, 0, sizeof( tempSurf ) );
	UpdateSurface( NULL, joints, &tempSurf, false, false );
	R_DeriveTangents( tempSurf.geometry, true );

	for ( i = 0; i < deformInfo->numOutputVerts; ++i ) {
		const idDrawVert &tempVert = tempSurf.geometry->verts[i];
		baseVectors[i * 4 + 0].Set( tempVert.xyz.x, tempVert.xyz.y, tempVert.xyz.z, 1.0f );
		baseVectors[i * 4 + 1].Set( tempVert.normal.x, tempVert.normal.y, tempVert.normal.z, 0.0f );
		baseVectors[i * 4 + 2].Set( tempVert.tangents[0].x, tempVert.tangents[0].y, tempVert.tangents[0].z, 0.0f );
		baseVectors[i * 4 + 3].Set( tempVert.tangents[1].x, tempVert.tangents[1].y, tempVert.tangents[1].z, 0.0f );
	}

	R_FreeStaticTriSurf( tempSurf.geometry );
	BuildGpuSkinningSidecar( numJoints );
}

/*
====================
idMD5Mesh::BuildGpuSkinningSidecar

Build an immutable output-vertex stream. More than four non-zero influences is
an exactness failure, not an invitation to silently truncate retail content.
====================
*/
void idMD5Mesh::BuildGpuSkinningSidecar( int numJoints ) {
	gpuBindPoseVerts.Clear();
	gpuSkinningVerts.Clear();
	gpuSkinningNumJoints = numJoints;
	gpuSkinningFallback = GPU_SKINNING_FALLBACK_NONE;
	if ( deformInfo == NULL || baseVectors == NULL || weights == NULL
		|| deformInfo->numOutputVerts <= 0 ) {
		gpuSkinningFallback = GPU_SKINNING_FALLBACK_MISSING_BIND_POSE;
		return;
	}

	gpuBindPoseVerts.SetNum( deformInfo->numOutputVerts );
	gpuSkinningVerts.SetNum( deformInfo->numOutputVerts );
	idList<gpuSkinningInfluence_t> influences;
	int weightCursor = 0;
	for ( int vertexIndex = 0; vertexIndex < deformInfo->numOutputVerts; ++vertexIndex ) {
		idDrawVert &bindPose = gpuBindPoseVerts[vertexIndex];
		bindPose.Clear();
		memset( bindPose.color2, 0, sizeof( bindPose.color2 ) );
		bindPose.xyz = baseVectors[vertexIndex * 4 + 0].ToVec3();
		bindPose.normal = baseVectors[vertexIndex * 4 + 1].ToVec3();
		bindPose.tangents[0] = baseVectors[vertexIndex * 4 + 2].ToVec3();
		bindPose.tangents[1] = baseVectors[vertexIndex * 4 + 3].ToVec3();
		int textureVertex = vertexIndex;
		if ( textureVertex >= texCoords.Num() ) {
			const int mirrorIndex = textureVertex - texCoords.Num();
			textureVertex = mirrorIndex >= 0 && mirrorIndex < deformInfo->numMirroredVerts
				? deformInfo->mirroredVerts[mirrorIndex] : -1;
		}
		if ( textureVertex >= 0 && textureVertex < texCoords.Num() ) {
			bindPose.st = texCoords[textureVertex];
		}

		const int groupCount = weights[weightCursor].nextVertexOffset / sizeof( weights[0] );
		if ( groupCount <= 0 ) {
			gpuSkinningFallback = GPU_SKINNING_FALLBACK_MALFORMED_WEIGHTS;
			break;
		}
		influences.SetNum( groupCount );
		for ( int influenceIndex = 0; influenceIndex < groupCount; ++influenceIndex ) {
			const jointWeight_t &weight = weights[weightCursor + influenceIndex];
			influences[influenceIndex].jointIndex = weight.jointMatOffset / sizeof( idJointMat );
			influences[influenceIndex].weight = weight.weight;
		}

		gpuSkinningPackResult_t result;
		if ( !R_GpuSkinning_PackVertexExact( influences.Ptr(), influences.Num(), numJoints,
			false, gpuSkinningVerts[vertexIndex], result )
			&& gpuSkinningFallback == GPU_SKINNING_FALLBACK_NONE ) {
			gpuSkinningFallback = result.reason;
		}
		weightCursor += groupCount;
	}

	if ( gpuSkinningFallback != GPU_SKINNING_FALLBACK_NONE ) {
		// Keep immutable source arrays for diagnostics, but admission remains
		// fail-closed for the whole mesh.
		return;
	}
}

/*
====================
idMD5Mesh::TransformVerts
====================
*/
void idMD5Mesh::TransformVerts( idDrawVert *verts, const idJointMat *entJoints ) {
	SIMDProcessor->TransformVerts( verts, texCoords.Num(), entJoints, scaledWeights, weightIndex, numWeights );
}

/*
====================
idMD5Mesh::TransformScaledVerts

Special transform to make the mesh seem fat or skinny.  May be used for zombie deaths
====================
*/
void idMD5Mesh::TransformScaledVerts( idDrawVert *verts, const idJointMat *entJoints, float scale ) {
	idVec4 *scaledWeights = (idVec4 *) _alloca16( numWeights * sizeof( scaledWeights[0] ) );
	SIMDProcessor->Mul( scaledWeights[0].ToFloatPtr(), scale, this->scaledWeights[0].ToFloatPtr(), numWeights * 4 );
	SIMDProcessor->TransformVerts( verts, texCoords.Num(), entJoints, scaledWeights, weightIndex, numWeights );
}

/*
====================
idMD5Mesh::UpdateLod
====================
*/
bool idMD5Mesh::UpdateLod( const struct renderEntity_s *ent, const struct viewEntity_s *viewEnt, const modelSurface_t *surf ) {
	if ( surf->geometry == NULL ) {
		return true;
	}

	if ( viewEnt != NULL && r_lod_animations_distance.GetInteger() != 0 && ent->suppressLOD != 1 ) {
		if ( currentTime > r_lod_animations_wait.GetFloat()
			|| viewEnt->distanceToCamera < r_lod_animations_distance.GetFloat() ) {
			currentTime = 0.0f;
		} else if ( viewEnt->screenCoverage < r_lod_animations_coverage.GetFloat() ) {
			currentTime += tr.deltaTime;
			return false;
		}
	}

	return true;
}

/*
====================
idMD5Mesh::UpdateSurface
====================
*/
void idMD5Mesh::UpdateSurface( const struct renderEntity_s *ent, const idJointMat *entJoints, modelSurface_t *surf, bool calculateTangents, bool allowGpuSkinning ) {
	int i, base;
	srfTriangles_t *tri;

	tr.pc.c_deformedSurfaces++;
	tr.pc.c_deformedVerts += deformInfo->numOutputVerts;
	tr.pc.c_deformedIndexes += deformInfo->numIndexes;

	surf->shader = shader;

	if ( surf->geometry ) {
		// if the number of verts and indexes are the same we can re-use the triangle surface
		// the number of indexes must be the same to assure the correct amount of memory is allocated for the facePlanes
		if ( surf->geometry->numVerts == deformInfo->numOutputVerts && surf->geometry->numIndexes == deformInfo->numIndexes ) {
			R_FreeStaticTriSurfVertexCaches( surf->geometry );
		} else {
			R_FreeStaticTriSurf( surf->geometry );
			surf->geometry = R_AllocStaticTriSurf();
		}
	} else {
		surf->geometry = R_AllocStaticTriSurf();
	}

	tri = surf->geometry;
	R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_NONE );

	// note that some of the data is references, and should not be freed
	tri->deformedSurface = true;
	tri->tangentsCalculated = false;
	tri->facePlanesCalculated = false;

	tri->numIndexes = deformInfo->numIndexes;
	tri->indexes = deformInfo->indexes;
	tri->silIndexes = deformInfo->silIndexes;
	tri->numMirroredVerts = deformInfo->numMirroredVerts;
	tri->mirroredVerts = deformInfo->mirroredVerts;
	tri->numDupVerts = deformInfo->numDupVerts;
	tri->dupVerts = deformInfo->dupVerts;
	tri->numSilEdges = deformInfo->numSilEdges;
	tri->silEdges = deformInfo->silEdges;
	tri->dominantTris = deformInfo->dominantTris;
	tri->numVerts = deformInfo->numOutputVerts;

	if ( tri->verts == NULL ) {
		R_AllocStaticTriSurfVerts( tri, tri->numVerts );
		for ( i = 0; i < deformInfo->numSourceVerts; i++ ) {
			tri->verts[i].Clear();
			tri->verts[i].st = texCoords[i];
		}
		base = deformInfo->numOutputVerts - deformInfo->numMirroredVerts;
		for ( i = 0; i < deformInfo->numMirroredVerts; ++i ) {
			tri->verts[base + i] = tri->verts[deformInfo->mirroredVerts[i]];
		}
	}

	const bool useLegacySkinScale = ( ent != NULL && ent->shaderParms[ SHADERPARM_MD5_SKINSCALE ] != 0.0f );
	bool gpuSkinningContractAttached = false;
	if ( useLegacySkinScale ) {
		R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_SKIN_SCALE );
	} else if ( !allowGpuSkinning ) {
		R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_UNSUPPORTED_PASS );
	} else if ( !r_gpuSkinning.GetBool() ) {
		R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_DISABLED );
	} else if ( !r_useNewSkinning.GetBool() ) {
		R_GpuSkinning_ClearSurfaceContract( tri, GPU_SKINNING_FALLBACK_UNSUPPORTED_PASS );
	} else {
		gpuSkinningContractAttached = R_GpuSkinning_AttachSurfaceContract(
			tri, gpuBindPoseVerts.Ptr(), gpuSkinningVerts.Ptr(), gpuBindPoseVerts.Num(),
			entJoints != NULL ? entJoints[0].ToFloatPtr() : NULL, gpuSkinningNumJoints,
			GPU_SKINNING_JOINT_FLOATS, false, gpuSkinningFallback );
	}

	const uint64 cpuSkinStart = R_GpuSkinning_ReadMicroseconds();

	if ( useLegacySkinScale ) {
		TransformScaledVerts( tri->verts, entJoints, ent->shaderParms[ SHADERPARM_MD5_SKINSCALE ] );
		tri->tangentsCalculated = false;
	} else if ( scaledBaseVectors != NULL && weights != NULL ) {
		if ( gpuSkinningContractAttached ) {
			SIMDProcessor->TransformVertsNew( tri->verts, deformInfo->numOutputVerts, tri->bounds,
				entJoints, scaledBaseVectors, weights, numWeights );
			tri->tangentsCalculated = false;
		} else if ( r_useNewSkinning.GetBool() && calculateTangents && baseVectors != NULL ) {
			if ( r_useFastSkinning.GetBool() ) {
				SIMDProcessor->TransformVertsAndTangentsFast( tri->verts, deformInfo->numOutputVerts, tri->bounds,
					entJoints, baseVectors, weights, numWeights );
			} else {
				SIMDProcessor->TransformVertsAndTangents( tri->verts, deformInfo->numOutputVerts, tri->bounds,
					entJoints, baseVectors, weights, numWeights );
			}
			tri->tangentsCalculated = true;
		} else {
			SIMDProcessor->TransformVertsNew( tri->verts, deformInfo->numOutputVerts, tri->bounds,
				entJoints, scaledBaseVectors, weights, numWeights );
			tri->tangentsCalculated = false;
		}
	} else {
		TransformVerts( tri->verts, entJoints );
		tri->tangentsCalculated = false;
	}

	if ( useLegacySkinScale || scaledBaseVectors == NULL || weights == NULL ) {
		// replicate the mirror seam vertexes for the legacy vertex path
		base = deformInfo->numOutputVerts - deformInfo->numMirroredVerts;
		for ( i = 0; i < deformInfo->numMirroredVerts; i++ ) {
			tri->verts[base + i] = tri->verts[deformInfo->mirroredVerts[i]];
		}

		R_BoundTriSurf( tri );
	}

	if ( r_deriveBiTangents.GetBool() && tri->tangentsCalculated ) {
		for ( i = 0; i < deformInfo->numOutputVerts; ++i ) {
			idVec3 bitangent = tri->verts[i].normal.Cross( tri->verts[i].tangents[0] );
			if ( bitangent * tri->verts[i].tangents[1] < 0.0f ) {
				bitangent = -bitangent;
			}
			tri->verts[i].tangents[1] = bitangent;
		}
	}

	// If a surface is going to be have a lighting interaction generated, it will also have to call
	// R_DeriveTangents() to get normals, tangents, and face planes.  If it only
	// needs shadows generated, it will only have to generate face planes.  If it only
	// has ambient drawing, or is culled, no additional work will be necessary
	if ( !tri->tangentsCalculated && !r_useDeferredTangents.GetBool() && !gpuSkinningContractAttached ) {
		// set face planes, vertex normals, tangents
		R_DeriveTangents( tri );
	}

	const uint64 cpuSkinEnd = R_GpuSkinning_ReadMicroseconds();
	const uint64 elapsedMicroseconds = cpuSkinEnd >= cpuSkinStart
		? cpuSkinEnd - cpuSkinStart : 0;
	R_GpuSkinning_RecordCpuSkinning( elapsedMicroseconds, tri->numVerts,
		gpuSkinningContractAttached );
}

/*
====================
idMD5Mesh::CalcBounds
====================
*/
idBounds idMD5Mesh::CalcBounds( const idJointMat *entJoints ) {
	idBounds	bounds;
	// Both scratch buffers are sized by the mesh's vertex count, the same count
	// that overflowed the stack in ParseMesh. This runs per frame for animated
	// models, so the small case stays on the stack.
	if ( scaledBaseVectors != NULL && weights != NULL ) {
		OPENQ4_ALLOC16_SCRATCH( idDrawVert, verts, deformInfo->numOutputVerts );
		SIMDProcessor->TransformVertsNew( verts, deformInfo->numOutputVerts, bounds, entJoints, scaledBaseVectors, weights, numWeights );
	} else {
		OPENQ4_ALLOC16_SCRATCH( idDrawVert, verts, texCoords.Num() );

		TransformVerts( verts, entJoints );
		SIMDProcessor->MinMax( bounds[0], bounds[1], verts, texCoords.Num() );
	}

	return bounds;
}

/*
====================
idMD5Mesh::NearestJoint
====================
*/
int idMD5Mesh::NearestJoint( int a, int b, int c ) const {
	int i, bestJoint, vertNum, weightVertNum;
	float bestWeight;

	// duplicated vertices might not have weights
	if ( a >= 0 && a < texCoords.Num() ) {
		vertNum = a;
	} else if ( b >= 0 && b < texCoords.Num() ) {
		vertNum = b;
	} else if ( c >= 0 && c < texCoords.Num() ) {
		vertNum = c;
	} else {
		// all vertices are duplicates which shouldn't happen
		return 0;
	}

	// find the first weight for this vertex
 	weightVertNum = 0;
	for( i = 0; weightVertNum < vertNum; i++ ) {
		weightVertNum += weightIndex[i*2+1];
	}

	// get the joint for the largest weight
	bestWeight = scaledWeights[i].w;
	bestJoint = weightIndex[i*2+0] / sizeof( idJointMat );
	for( ; weightIndex[i*2+1] == 0; i++ ) {
		if ( scaledWeights[i].w > bestWeight ) {
			bestWeight = scaledWeights[i].w;
			bestJoint = weightIndex[i*2+0] / sizeof( idJointMat );
		}
	}
	return bestJoint;
}

/*
====================
idMD5Mesh::NumVerts
====================
*/
int idMD5Mesh::NumVerts( void ) const {
	return texCoords.Num();
}

/*
====================
idMD5Mesh::NumTris
====================
*/
int	idMD5Mesh::NumTris( void ) const {
	return numTris;
}

/*
====================
idMD5Mesh::NumWeights
====================
*/
int	idMD5Mesh::NumWeights( void ) const {
	return numWeights;
}

/***********************************************************************

	idRenderModelMD5

***********************************************************************/

/*
====================
idRenderModelMD5::ParseJoint
====================
*/
void idRenderModelMD5::ParseJoint( idLexer &parser, idMD5Joint *joint, idJointQuat *defaultPose ) {
	idToken	token;
	int		num;

	//
	// parse name
	//
	parser.ReadToken( &token );
	joint->name = token;

	//
	// parse parent
	//
	num = parser.ParseInt();
	if ( num < 0 ) {
		joint->parent = NULL;
	} else {
		if ( num >= joints.Num() - 1 ) {
			parser.Error( "Invalid parent for joint '%s'", joint->name.c_str() );
		}
		joint->parent = &joints[ num ];
	}

	//
	// parse default pose
	//
	parser.Parse1DMatrix( 3, defaultPose->t.ToFloatPtr() );
	parser.Parse1DMatrix( 3, defaultPose->q.ToFloatPtr() );
	defaultPose->q.w = defaultPose->q.CalcW();
}

/*
====================
idRenderModelMD5::InitFromFile
====================
*/
void idRenderModelMD5::InitFromFile( const char *fileName ) {
	name = fileName;
	LoadModel();
}

/*
====================
idRenderModelMD5::LoadModel

used for initial loads, reloadModel, and reloading the data of purged models
Upon exit, the model will absolutely be valid, but possibly as a default model
====================
*/
void idRenderModelMD5::LoadModel() {
	int			version;
	int			i;
	int			num;
	int			parentNum;
	idToken		token;
	idLexer		parser( LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGESCAPECHARS );
	idJointQuat	*pose;
	idMD5Joint	*joint;
	idJointMat *poseMat3;
	const idStr sourceName = name;

	if ( !purged ) {
		PurgeModel();
	}
	purged = false;
	char cacheSettingsBuffer[1024];
	idStr::snPrintf( cacheSettingsBuffer, sizeof( cacheSettingsBuffer ),
		"md5-payload=2;merge=%s;slopv=%s;slopt=%s;slopn=%s;silremap=%s;binary=%s;force-md5r=%s;convert-md5=%s",
		r_mergeModelSurfaces.GetString(), r_slopVertex.GetString(), r_slopTexCoord.GetString(),
		r_slopNormal.GetString(), cvarSystem->GetCVarString( "r_useSilRemap" ),
		cvarSystem->GetCVarString( "com_binaryRead" ), cvarSystem->GetCVarString( "r_forceConvertMD5R" ),
		cvarSystem->GetCVarString( "r_convertMD5toMD5R" ) );
	const idStr cacheSettings = cacheSettingsBuffer;
	if ( R_TryReadGeneratedRenderModelCache( *this, sourceName.c_str(),
			MD5_MODEL_GENERATED_CACHE_PARSER_VERSION, cacheSettings.c_str() ) ) {
		if ( name.Icmp( sourceName ) == 0 ) {
			return;
		}
		fileSystem->DiscardGeneratedCache( GENERATED_CACHE_RENDER_MODEL, sourceName.c_str(),
			MD5_MODEL_GENERATED_CACHE_PARSER_VERSION, cacheSettings.c_str() );
		PurgeModel();
		name = sourceName;
		purged = false;
	}

	if ( !parser.LoadFile( sourceName ) ) {
		MakeDefaultModel();
		return;
	}

	parser.ExpectTokenString( MD5_VERSION_STRING );
	version = parser.ParseInt();

	if ( version != MD5_VERSION ) {
		parser.Error( "Invalid version %d.  Should be version %d\n", version, MD5_VERSION );
	}

	//
	// skip commandline
	//
	parser.ExpectTokenString( "commandline" );
	parser.ReadToken( &token );

	// parse num joints
	parser.ExpectTokenString( "numJoints" );
	num  = parser.ParseInt();
	joints.SetGranularity( 1 );
	joints.SetNum( num );
	defaultPose.SetGranularity( 1 );
	defaultPose.SetNum( num );
	poseMat3 = ( idJointMat * )_alloca16( num * sizeof( *poseMat3 ) );

	// parse num meshes
	parser.ExpectTokenString( "numMeshes" );
	num = parser.ParseInt();
	if ( num < 0 ) {
		parser.Error( "Invalid size: %d", num );
	}
	meshes.SetGranularity( 1 );
	meshes.SetNum( num );

	//
	// parse joints
	//
	parser.ExpectTokenString( "joints" );
	parser.ExpectTokenString( "{" );
	pose = defaultPose.Ptr();
	joint = joints.Ptr();
	for( i = 0; i < joints.Num(); i++, joint++, pose++ ) {
		ParseJoint( parser, joint, pose );
		poseMat3[ i ].SetRotation( pose->q.ToMat3() );
		poseMat3[ i ].SetTranslation( pose->t );
		if ( joint->parent ) {
			parentNum = joint->parent - joints.Ptr();
			pose->q = ( poseMat3[ i ].ToMat3() * poseMat3[ parentNum ].ToMat3().Transpose() ).ToQuat();
			pose->t = ( poseMat3[ i ].ToVec3() - poseMat3[ parentNum ].ToVec3() ) * poseMat3[ parentNum ].ToMat3().Transpose();
		}
	}
	parser.ExpectTokenString( "}" );

	skinSpaceToLocalMats.SetGranularity( 1 );
	skinSpaceToLocalMats.SetNum( joints.Num() );
	for ( i = 0; i < joints.Num(); ++i ) {
		skinSpaceToLocalMats[i] = poseMat3[i];
		skinSpaceToLocalMats[i].Invert();
	}

	for( i = 0; i < meshes.Num(); i++ ) {
		parser.ExpectTokenString( "mesh" );
		meshes[ i ].ParseMesh( parser, defaultPose.Num(), poseMat3 );
	}

	//
	// calculate the bounds of the model
	//
	CalculateBounds( poseMat3 );

	// set the timestamp for reloadmodels
	fileSystem->ReadFile( sourceName, NULL, &timeStamp );
	R_WriteGeneratedRenderModelCache( *this, sourceName.c_str(),
		MD5_MODEL_GENERATED_CACHE_PARSER_VERSION, cacheSettings.c_str() );
}

/*
==============
idRenderModelMD5::Print
==============
*/
void idRenderModelMD5::Print() const {
	const idMD5Mesh	*mesh;
	int			i;

	common->Printf( "%s\n", name.c_str() );
	common->Printf( "Dynamic model.\n" );
	common->Printf( "Generated smooth normals.\n" );
	common->Printf( "    verts  tris weights material\n" );
	int	totalVerts = 0;
	int	totalTris = 0;
	int	totalWeights = 0;
	for( mesh = meshes.Ptr(), i = 0; i < meshes.Num(); i++, mesh++ ) {
		totalVerts += mesh->NumVerts();
		totalTris += mesh->NumTris();
		totalWeights += mesh->NumWeights();
		common->Printf( "%2i: %5i %5i %7i %s\n", i, mesh->NumVerts(), mesh->NumTris(), mesh->NumWeights(), mesh->shader->GetName() );
	}	
	common->Printf( "-----\n" );
	common->Printf( "%4i verts.\n", totalVerts );
	common->Printf( "%4i tris.\n", totalTris );
	common->Printf( "%4i weights.\n", totalWeights );
	common->Printf( "%4i joints.\n", joints.Num() );
}

/*
==============
idRenderModelMD5::List
==============
*/
void idRenderModelMD5::List() const {
	int			i;
	const idMD5Mesh	*mesh;
	int			totalTris = 0;
	int			totalVerts = 0;

	for( mesh = meshes.Ptr(), i = 0; i < meshes.Num(); i++, mesh++ ) {
		totalTris += mesh->numTris;
		totalVerts += mesh->NumVerts();
	}
	common->Printf( " %4ik %3i %4i %4i %s(MD5)", Memory()/1024, meshes.Num(), totalVerts, totalTris, Name() );

	if ( defaulted ) {
		common->Printf( " (DEFAULTED)" );
	}

	common->Printf( "\n" );
}

/*
====================
idRenderModelMD5::CalculateBounds
====================
*/
void idRenderModelMD5::CalculateBounds( const idJointMat *entJoints ) {
	int			i;
	idMD5Mesh	*mesh;

	bounds.Clear();
	for( mesh = meshes.Ptr(), i = 0; i < meshes.Num(); i++, mesh++ ) {
		bounds.AddBounds( mesh->CalcBounds( entJoints ) );
	}
}

/*
====================
idRenderModelMD5::Bounds

This calculates a rough bounds by using the joint radii without
transforming all the points
====================
*/
idBounds idRenderModelMD5::Bounds( const renderEntity_t *ent ) const {
#if 0
	// we can't calculate a rational bounds without an entity,
	// because joints could be positioned to deform it into an
	// arbitrarily large shape
	if ( !ent ) {
		common->Error( "idRenderModelMD5::Bounds: called without entity" );
	}
#endif

	if ( !ent ) {
		// this is the bounds for the reference pose
		return bounds;
	}

	return ent->bounds;
}

/*
====================
idRenderModelMD5::BoundsFromJoints
====================
*/
bool idRenderModelMD5::BoundsFromJoints( const idJointMat *entJoints, idBounds &outBounds ) const {
	if ( entJoints == NULL ) {
		return false;
	}

	outBounds.Clear();
	for ( int i = 0; i < meshes.Num(); ++i ) {
		outBounds.AddBounds( const_cast<idMD5Mesh &>( meshes[i] ).CalcBounds( entJoints ) );
	}

	return !outBounds.IsCleared();
}

/*
====================
idRenderModelMD5::DrawJoints
====================
*/
void idRenderModelMD5::DrawJoints( const renderEntity_t *ent, const struct viewDef_s *view ) const {
	int					i;
	int					num;
	idVec3				pos;
	const idJointMat	*joint;
	const idMD5Joint	*md5Joint;
	int					parentNum;

	num = ent->numJoints;
	joint = ent->joints;
	md5Joint = joints.Ptr();	
	for( i = 0; i < num; i++, joint++, md5Joint++ ) {
		pos = ent->origin + joint->ToVec3() * ent->axis;
		if ( md5Joint->parent ) {
			parentNum = md5Joint->parent - joints.Ptr();
			session->rw->DebugLine( colorWhite, ent->origin + ent->joints[ parentNum ].ToVec3() * ent->axis, pos );
		}

		session->rw->DebugLine( colorRed,	pos, pos + joint->ToMat3()[ 0 ] * 2.0f * ent->axis );
		session->rw->DebugLine( colorGreen,	pos, pos + joint->ToMat3()[ 1 ] * 2.0f * ent->axis );
		session->rw->DebugLine( colorBlue,	pos, pos + joint->ToMat3()[ 2 ] * 2.0f * ent->axis );
	}

	idBounds bounds;

	bounds.FromTransformedBounds( ent->bounds, vec3_zero, ent->axis );
	session->rw->DebugBounds( colorMagenta, bounds, ent->origin );

	if ( ( r_jointNameScale.GetFloat() != 0.0f ) && ( bounds.Expand( 128.0f ).ContainsPoint( view->renderView.vieworg - ent->origin ) ) ) {
		idVec3	offset( 0, 0, r_jointNameOffset.GetFloat() );
		float	scale;

		scale = r_jointNameScale.GetFloat();
		joint = ent->joints;
		num = ent->numJoints;
		for( i = 0; i < num; i++, joint++ ) {
			pos = ent->origin + joint->ToVec3() * ent->axis;
			session->rw->DrawText( joints[ i ].name, pos + offset, scale, colorWhite, view->renderView.viewaxis, 1 );
		}
	}
}

/*
====================
idRenderModelMD5::InstantiateDynamicModel
====================
*/
idRenderModel *idRenderModelMD5::InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel ) {
	return InstantiateDynamicModel( ent, view, cachedModel, static_cast<dword>( ~SURF_COLLISION ) );
}

/*
====================
idRenderModelMD5::InstantiateDynamicModel
====================
*/
idRenderModel *idRenderModelMD5::InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel, dword surfMask ) {
	int					i, surfaceNum;
	idMD5Mesh			*mesh;
	idRenderModelStatic	*staticModel;
	const bool			collisionOnly = ( surfMask & SURF_COLLISION ) != 0;
	const idJointMat *	entJoints = ent->joints;
	bool				gpuJointPaletteReady = false;

	if ( cachedModel && !r_useCachedDynamicModels.GetBool() ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if ( purged ) {
		common->DWarning( "model %s instantiated while purged", Name() );
		LoadModel();
	}

	if ( !ent->joints ) {
		common->Printf( "idRenderModelMD5::InstantiateDynamicModel: NULL joints on renderEntity for '%s'\n", Name() );
		delete cachedModel;
		return NULL;
	} else if ( ent->numJoints != joints.Num() ) {
		common->Printf( "idRenderModelMD5::InstantiateDynamicModel: renderEntity has different number of joints than model for '%s'\n", Name() );
		delete cachedModel;
		return NULL;
	}

	tr.pc.c_generateMd5++;

	if ( r_useNewSkinning.GetBool() && !collisionOnly && skinSpaceToLocalMats.Num() == joints.Num() ) {
		idJointMat *transformedJoints = (idJointMat *)_alloca16( joints.Num() * sizeof( transformedJoints[0] ) );
		SIMDProcessor->MultiplyJoints( transformedJoints, ent->joints, skinSpaceToLocalMats.Ptr(), joints.Num() );
		entJoints = transformedJoints;
		gpuJointPaletteReady = true;
	}

	if ( cachedModel ) {
		assert( dynamic_cast<idRenderModelStatic *>(cachedModel) != NULL );
		assert( idStr::Icmp( cachedModel->Name(), MD5_SnapshotName ) == 0 );
		staticModel = static_cast<idRenderModelStatic *>(cachedModel);
	} else {
		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty( MD5_SnapshotName );
	}

	staticModel->bounds.Clear();

	if ( r_showSkel.GetInteger() ) {
		if ( ( view != NULL ) && ( !r_skipSuppress.GetBool() || !ent->suppressSurfaceInViewID || ( ent->suppressSurfaceInViewID != view->renderView.viewID ) ) ) {
			// only draw the skeleton
			DrawJoints( ent, view );
		}

		if ( r_showSkel.GetInteger() > 1 ) {
			// turn off the model when showing the skeleton
			staticModel->InitEmpty( MD5_SnapshotName );
			return staticModel;
		}
	}

	// create all the surfaces
	for( mesh = meshes.Ptr(), i = 0; i < meshes.Num(); i++, mesh++ ) {
		if ( ent != NULL && i < static_cast<int>( sizeof( unsigned int ) * 8 )
			&& ( static_cast<unsigned int>( ent->suppressSurfaceMask ) & ( 1u << i ) ) != 0 ) {
			staticModel->DeleteSurfaceWithId( i );
			staticModel->DeleteSurfaceWithId( i + MD5_BackSideSurfaceIdOffset );
			mesh->surfaceNum = -1;
			continue;
		}

		// avoid deforming the surface if it will be a nodraw due to a skin remapping
		// FIXME: may have to still deform clipping hulls
		const idMaterial *shader = mesh->shader;
		
		shader = R_RemapShaderBySkin( shader, ent->customSkin, ent->customShader );
		
		if ( collisionOnly ) {
			if ( !shader || ( shader->GetSurfaceFlags() & SURF_COLLISION ) == 0 ) {
				staticModel->DeleteSurfaceWithId( i );
				staticModel->DeleteSurfaceWithId( i + MD5_BackSideSurfaceIdOffset );
				mesh->surfaceNum = -1;
				continue;
			}
		} else if ( !shader || ( !shader->IsDrawn() && !shader->SurfaceCastsShadow() ) ) {
			staticModel->DeleteSurfaceWithId( i );
			staticModel->DeleteSurfaceWithId( i + MD5_BackSideSurfaceIdOffset );
			mesh->surfaceNum = -1;
			continue;
		}

		modelSurface_t *surf;

		if ( staticModel->FindSurfaceWithId( i, surfaceNum ) ) {
			mesh->surfaceNum = surfaceNum;
			surf = &staticModel->surfaces[surfaceNum];
		} else {

			// Remove Overlays before adding new surfaces
			idRenderModelOverlay::RemoveOverlaySurfacesFromModel( staticModel );

			mesh->surfaceNum = staticModel->NumSurfaces();
			surf = &staticModel->surfaces.Alloc();
			surf->geometry = NULL;
			surf->shader = NULL;
			surf->id = i;
		}

		if ( collisionOnly || mesh->UpdateLod( ent, viewEnt, surf ) ) {
			// GUI quads are positioned from CPU-skinned vertices. Keep their
			// supporting surface on that same path so GPU rounding cannot place
			// the mesh in front of its coplanar GUI (for example weapon displays).
			mesh->UpdateSurface( ent, entJoints, surf, !collisionOnly,
				!collisionOnly && gpuJointPaletteReady && !shader->HasGui() );
		}
		srfTriangles_t *frontTri = surf->geometry;

		if ( !collisionOnly && shader->ShouldCreateBackSides() ) {
			modelSurface_t *backSurf;

			if ( staticModel->FindSurfaceWithId( i + MD5_BackSideSurfaceIdOffset, surfaceNum ) ) {
				backSurf = &staticModel->surfaces[surfaceNum];
			} else {
				backSurf = &staticModel->surfaces.Alloc();
				backSurf->geometry = NULL;
				backSurf->shader = NULL;
				backSurf->id = i + MD5_BackSideSurfaceIdOffset;
			}

			backSurf->shader = mesh->shader;
			R_CopyAndReverseTriangles( frontTri, &backSurf->geometry );
		} else {
			staticModel->DeleteSurfaceWithId( i + MD5_BackSideSurfaceIdOffset );
		}

		staticModel->bounds.AddPoint( frontTri->bounds[0] );
		staticModel->bounds.AddPoint( frontTri->bounds[1] );
	}

	return staticModel;
}

/*
====================
idRenderModelMD5::HasCollisionSurface
====================
*/
bool idRenderModelMD5::HasCollisionSurface( const renderEntity_t *ent ) const {
	for ( int i = 0; i < meshes.Num(); ++i ) {
		const idMaterial *shader = R_RemapShaderBySkin( meshes[i].shader, ent->customSkin, ent->customShader );
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
====================
idRenderModelMD5::SetViewEntity
====================
*/
void idRenderModelMD5::SetViewEntity( const struct viewEntity_s *ve ) {
	viewEnt = ve;
}

/*
====================
idRenderModelMD5::GetSkinSpaceToLocalMats
====================
*/
const idJointMat *idRenderModelMD5::GetSkinSpaceToLocalMats( void ) const {
	return skinSpaceToLocalMats.Ptr();
}

/*
====================
idRenderModelMD5::IsDynamicModel
====================
*/
dynamicModel_t idRenderModelMD5::IsDynamicModel() const {
	return DM_CACHED;
}

/*
====================
idRenderModelMD5::NumJoints
====================
*/
int idRenderModelMD5::NumJoints( void ) const {
	return joints.Num();
}

/*
====================
idRenderModelMD5::GetJoints
====================
*/
const idMD5Joint *idRenderModelMD5::GetJoints( void ) const {
	return joints.Ptr();
}

/*
====================
idRenderModelMD5::GetDefaultPose
====================
*/
const idJointQuat *idRenderModelMD5::GetDefaultPose( void ) const {
	return defaultPose.Ptr();
}

/*
====================
idRenderModelMD5::GetJointHandle
====================
*/
jointHandle_t idRenderModelMD5::GetJointHandle( const char *name ) const {
	const idMD5Joint *joint;
	int	i;
	
	joint = joints.Ptr();
	for( i = 0; i < joints.Num(); i++, joint++ ) {
		if ( idStr::Icmp( joint->name.c_str(), name ) == 0 ) {
			return ( jointHandle_t )i;
		}
	}

	return INVALID_JOINT;
}

/*
=====================
idRenderModelMD5::GetJointName
=====================
*/
const char *idRenderModelMD5::GetJointName( jointHandle_t handle ) const {
	if ( ( handle < 0 ) || ( handle >= joints.Num() ) ) {
		return "<invalid joint>";
	}

	return joints[ handle ].name;
}

/*
====================
idRenderModelMD5::NearestJoint
====================
*/
int idRenderModelMD5::NearestJoint( int surfaceId, int a, int b, int c ) const {
	// Dynamic snapshots retain the source mesh number as their stable surface ID.
	// Two-sided materials add a reversed surface with the same vertices and an
	// offset ID, so both generated surfaces resolve to the same authored mesh.
	int meshIndex = surfaceId;
	if ( meshIndex >= MD5_BackSideSurfaceIdOffset ) {
		meshIndex -= MD5_BackSideSurfaceIdOffset;
	}

	if ( meshIndex < 0 || meshIndex >= meshes.Num() ) {
		return 0;
	}

	return meshes[ meshIndex ].NearestJoint( a, b, c );
}

/*
====================
idRenderModelMD5::TouchData

models that are already loaded at level start time
will still touch their materials to make sure they
are kept loaded
====================
*/
void idRenderModelMD5::TouchData() {
	idMD5Mesh	*mesh;
	int			i;

	for( mesh = meshes.Ptr(), i = 0; i < meshes.Num(); i++, mesh++ ) {
		declManager->FindMaterial( mesh->shader->GetName() );
	}
}

/*
===================
idRenderModelMD5::PurgeModel

frees all the data, but leaves the class around for dangling references,
which can regenerate the data with LoadModel()
===================
*/
void idRenderModelMD5::PurgeModel() {
	purged = true;
	joints.Clear();
	defaultPose.Clear();
	skinSpaceToLocalMats.Clear();
	meshes.Clear();
	viewEnt = NULL;
}

/*
===================
idRenderModelMD5::Memory
===================
*/
int	idRenderModelMD5::Memory() const {
	size_t	total;
	int		i;

	total = sizeof( *this );
	total += joints.MemoryUsed() + defaultPose.MemoryUsed() + skinSpaceToLocalMats.MemoryUsed() + meshes.MemoryUsed();

	// count up strings
	for ( i = 0; i < joints.Num(); i++ ) {
		total += joints[i].name.DynamicMemoryUsed();
	}

	// count up meshes
	for ( i = 0 ; i < meshes.Num() ; i++ ) {
		const idMD5Mesh *mesh = &meshes[i];

		total += mesh->texCoords.MemoryUsed();
		total += mesh->numWeights * ( sizeof( mesh->scaledWeights[0] ) + sizeof( mesh->weightIndex[0] ) * 2 );
		total += mesh->numWeights * ( sizeof( mesh->weights[0] ) + sizeof( mesh->scaledBaseVectors[0] ) );
		total += mesh->deformInfo->numOutputVerts * 4 * sizeof( mesh->baseVectors[0] );

		// sum up deform info
		total += sizeof( mesh->deformInfo );
		total += R_DeformInfoMemoryUsed( mesh->deformInfo );
	}
	return idLib::SizeToInt( total, "idRenderModelMD5::Memory" );
}

/*
====================
idRenderModelMD5::GetSurfaceMask
====================
*/
int idRenderModelMD5::GetSurfaceMask( const char *surface ) const {
	if ( surface == NULL || surface[0] == '\0' ) {
		return 0;
	}

	int mask = 0;
	for ( int i = 0; i < meshes.Num() && i < static_cast<int>( sizeof( unsigned int ) * 8 ); ++i ) {
		const idMaterial *shader = meshes[i].shader;
		if ( shader != NULL && idStr::Icmp( shader->GetName(), surface ) == 0 ) {
			mask |= ( 1u << i );
		}
	}

	return mask;
}
