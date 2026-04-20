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
		if ( !numWeightsForVertex[i] ) {
			parser.Error( "Vertex without any joint weights." );
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
		parser.Warning( "Vertices reference out of range weights in model (%d of %d weights).", maxweight, count );
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

	idDrawVert *verts = (idDrawVert *)_alloca16( texCoords.Num() * sizeof( idDrawVert ) );
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
	UpdateSurface( NULL, joints, &tempSurf, false );
	R_DeriveTangents( tempSurf.geometry, true );

	for ( i = 0; i < deformInfo->numOutputVerts; ++i ) {
		const idDrawVert &tempVert = tempSurf.geometry->verts[i];
		baseVectors[i * 4 + 0].Set( tempVert.xyz.x, tempVert.xyz.y, tempVert.xyz.z, 1.0f );
		baseVectors[i * 4 + 1].Set( tempVert.normal.x, tempVert.normal.y, tempVert.normal.z, 0.0f );
		baseVectors[i * 4 + 2].Set( tempVert.tangents[0].x, tempVert.tangents[0].y, tempVert.tangents[0].z, 0.0f );
		baseVectors[i * 4 + 3].Set( tempVert.tangents[1].x, tempVert.tangents[1].y, tempVert.tangents[1].z, 0.0f );
	}

	R_FreeStaticTriSurf( tempSurf.geometry );
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
void idMD5Mesh::UpdateSurface( const struct renderEntity_s *ent, const idJointMat *entJoints, modelSurface_t *surf, bool calculateTangents ) {
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

	if ( useLegacySkinScale ) {
		TransformScaledVerts( tri->verts, entJoints, ent->shaderParms[ SHADERPARM_MD5_SKINSCALE ] );
		tri->tangentsCalculated = false;
	} else if ( scaledBaseVectors != NULL && weights != NULL ) {
		if ( r_useNewSkinning.GetBool() && calculateTangents && baseVectors != NULL ) {
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
	if ( !tri->tangentsCalculated && !r_useDeferredTangents.GetBool() ) {
		// set face planes, vertex normals, tangents
		R_DeriveTangents( tri );
	}
}

/*
====================
idMD5Mesh::CalcBounds
====================
*/
idBounds idMD5Mesh::CalcBounds( const idJointMat *entJoints ) {
	idBounds	bounds;
	if ( scaledBaseVectors != NULL && weights != NULL ) {
		idDrawVert *verts = (idDrawVert *)_alloca16( deformInfo->numOutputVerts * sizeof( idDrawVert ) );
		SIMDProcessor->TransformVertsNew( verts, deformInfo->numOutputVerts, bounds, entJoints, scaledBaseVectors, weights, numWeights );
	} else {
		idDrawVert *verts = (idDrawVert *)_alloca16( texCoords.Num() * sizeof( idDrawVert ) );

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

	if ( !purged ) {
		PurgeModel();
	}
	purged = false;

	if ( !parser.LoadFile( name ) ) {
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
	fileSystem->ReadFile( name, NULL, &timeStamp );
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
			mesh->UpdateSurface( ent, entJoints, surf, !collisionOnly );
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

		if ( ( shader->GetSurfaceFlags() & SURF_COLLISION ) != 0
			&& !shader->IsDrawn()
			&& !shader->SurfaceCastsShadow() ) {
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
int idRenderModelMD5::NearestJoint( int surfaceNum, int a, int b, int c ) const {
	int i;
	const idMD5Mesh *mesh;

	if ( surfaceNum > meshes.Num() ) {
		common->Error( "idRenderModelMD5::NearestJoint: surfaceNum > meshes.Num()" );
	}

	for ( mesh = meshes.Ptr(), i = 0; i < meshes.Num(); i++, mesh++ ) {
		if ( mesh->surfaceNum == surfaceNum ) {
			return mesh->NearestJoint( a, b, c );
		}
	}
	return 0;
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
	int		total, i;

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
	return total;
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
