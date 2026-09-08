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
#include "../sound/sound.h"

static ID_INLINE idSoundEmitter *R_GetShaderSoundEmitter( int soundEmitterHandle ) {
	if ( soundEmitterHandle == 0 || soundSystem == NULL ) {
		return NULL;
	}

	return soundSystem->EmitterForIndex( SOUNDWORLD_GAME, soundEmitterHandle );
}

static int R_QsortViewEntitiesByModel( const void *lhs, const void *rhs ) {
	const viewEntity_t *const left = *reinterpret_cast<const viewEntity_t *const *>( lhs );
	const viewEntity_t *const right = *reinterpret_cast<const viewEntity_t *const *>( rhs );

	const idRenderModel *const leftModel =
		( left != NULL && left->entityDef != NULL ) ? left->entityDef->parms.hModel : NULL;
	const idRenderModel *const rightModel =
		( right != NULL && right->entityDef != NULL ) ? right->entityDef->parms.hModel : NULL;

	const size_t leftModelKey = reinterpret_cast<size_t>( leftModel );
	const size_t rightModelKey = reinterpret_cast<size_t>( rightModel );
	if ( leftModelKey < rightModelKey ) {
		return -1;
	}
	if ( leftModelKey > rightModelKey ) {
		return 1;
	}

	const size_t leftEntityKey = reinterpret_cast<size_t>( left );
	const size_t rightEntityKey = reinterpret_cast<size_t>( right );
	if ( leftEntityKey < rightEntityKey ) {
		return -1;
	}
	if ( leftEntityKey > rightEntityKey ) {
		return 1;
	}

	return 0;
}

static unsigned int R_SortViewEntities( viewEntity_t ***array ) {
	unsigned int count = 0;
	for ( viewEntity_t *vEntity = tr.viewDef->viewEntitys; vEntity != NULL; vEntity = vEntity->next ) {
		++count;
	}

	viewEntity_t **sortedEntities = (viewEntity_t **)R_FrameAlloc( count * sizeof( *sortedEntities ) );
	*array = sortedEntities;

	viewEntity_t **nextSortedEntity = sortedEntities;
	for ( viewEntity_t *vEntity = tr.viewDef->viewEntitys; vEntity != NULL; vEntity = vEntity->next ) {
		*nextSortedEntity++ = vEntity;
	}

	// Retail Quake 4 clusters view entities by source model before ambient/light
	// submission so repeated dynamic-model work stays hot in cache.
	qsort( sortedEntities, count, sizeof( sortedEntities[0] ), R_QsortViewEntitiesByModel );
	return count;
}

static const float CHECK_BOUNDS_EPSILON = 1.0f;
idCVar bse_frameCounters(
	"bse_frameCounters",
	"0",
	CVAR_RENDERER | CVAR_INTEGER,
	"temporary BSE counters: 0=off, 1=spawn/service/render summary, 2=include drop-stage counts");
idCVar bse_respectConnectedArea(
	"bse_respectConnectedArea",
	"0",
	CVAR_RENDERER | CVAR_BOOL,
	"if 1, cull effect defs when parms.inConnectedArea is false (legacy portal gating)");
idCVar bse_useAreaCull(
	"bse_useAreaCull",
	"1",
	CVAR_RENDERER | CVAR_BOOL,
	"if 1, cull BSE render surfaces when the effect is outside the current portal-visible area set");
idCVar bse_skipCulledLoopService(
	"bse_skipCulledLoopService",
	"0",
	CVAR_RENDERER | CVAR_BOOL,
	"if 1, experimental: defer service for looping BSE effects while portal/PVS culling proves they are invisible");
idCVar bse_culledLoopServiceMaxMsec(
	"bse_culledLoopServiceMaxMsec",
	"1000",
	CVAR_RENDERER | CVAR_INTEGER,
	"maximum owner-time lag before a portal-culled looping BSE effect is serviced once to refresh state and bounds; 0 disables stale refresh");
idCVar bse_culledLoopServiceRefreshBudget(
	"bse_culledLoopServiceRefreshBudget",
	"2",
	CVAR_RENDERER | CVAR_INTEGER,
	"maximum portal-culled looping BSE effects to refresh per rendered view; 0 disables stale refresh");
idCVar bse_useFrustumCull(
	"bse_useFrustumCull",
	"1",
	CVAR_RENDERER | CVAR_BOOL,
	"if 1, cull BSE render surfaces by expanded effect bounds before emitting draw surfaces");
idCVar bse_useCachedFrustumCull(
	"bse_useCachedFrustumCull",
	"0",
	CVAR_RENDERER | CVAR_BOOL,
	"if 1, use cached BSE effect bounds for pre-render frustum culling; faster but less conservative for growing effects");
idCVar bse_useSurfaceFrustumCull(
	"bse_useSurfaceFrustumCull",
	"0",
	CVAR_RENDERER | CVAR_BOOL,
	"if 1, additionally cull individual BSE effect surfaces by local bounds");
idCVar bse_frustumCullExpand(
	"bse_frustumCullExpand",
	"32",
	CVAR_RENDERER | CVAR_FLOAT,
	"world units added to BSE effect bounds before frustum culling");

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
static void R_CreateVertexProgramShadowCacheFromSilTraceVerts( shadowCache_t *temp, const rvSilTraceVertT *silTraceVerts, int numVerts ) {
	for ( int i = 0; i < numVerts; ++i ) {
		const idVec4 &position = silTraceVerts[i].xyzw;
		temp[i * 2 + 0].xyz[0] = position.x;
		temp[i * 2 + 1].xyz[0] = position.x;
		temp[i * 2 + 0].xyz[1] = position.y;
		temp[i * 2 + 1].xyz[1] = position.y;
		temp[i * 2 + 0].xyz[2] = position.z;
		temp[i * 2 + 1].xyz[2] = position.z;
		temp[i * 2 + 0].xyz[3] = 1.0f;
		temp[i * 2 + 1].xyz[3] = 0.0f;
	}
}

static float *R_AllocTexGenTransformAndViewOrg( drawSurf_t *surf ) {
	if ( surf->texGenTransformAndViewOrg == NULL ) {
		surf->texGenTransformAndViewOrg = (float *)R_FrameAlloc( 16 * sizeof( float ) );
	}
	return surf->texGenTransformAndViewOrg;
}

static void R_SetPackedTexGenTransformAndViewOrg( drawSurf_t *surf, const float *transform, const idVec3 &localViewOrigin ) {
	float *packedTransform = R_AllocTexGenTransformAndViewOrg( surf );

	// Retail Quake 4 packs the three texgen rows plus the local view origin so
	// the MD5R ARB2 path can bind them as vertex-program parameters directly.
	packedTransform[0] = transform[0];
	packedTransform[1] = transform[4];
	packedTransform[2] = transform[8];
	packedTransform[3] = transform[12];
	packedTransform[4] = transform[1];
	packedTransform[5] = transform[5];
	packedTransform[6] = transform[9];
	packedTransform[7] = transform[13];
	packedTransform[8] = transform[2];
	packedTransform[9] = transform[6];
	packedTransform[10] = transform[10];
	packedTransform[11] = transform[14];
	packedTransform[12] = localViewOrigin[0];
	packedTransform[13] = localViewOrigin[1];
	packedTransform[14] = localViewOrigin[2];
	packedTransform[15] = 1.0f;
}
#endif


/*
===========================================================================================

VERTEX CACHE GENERATORS

===========================================================================================
*/

void R_TouchVertexCache( vertCache_t *cache ) {
	if ( cache == NULL ) {
		return;
	}

	if ( cache->tag != TAG_TEMP ) {
		vertexCache.Touch( cache );
	}
}

/*
==================
R_StaticIndexCacheAllowed

Static index VBOs only pay off for geometry whose index data survives across
frames (world/brush models, static entity models, prelight volumes, static
interactions). Per-frame regenerated dynamic-model tris (animated MD5,
beams/liquids, and their per-light interaction shadow/light tris) would
allocate and re-upload a static buffer every frame - more expensive than the
client-memory index draw it replaces, and the churn shows up as frame-pacing
spikes. r_useIndexBuffers 2 restores the upload-everything behavior for A/B.
==================
*/
bool R_StaticIndexCacheAllowed( const idRenderEntityLocal *def ) {
	const int mode = r_useIndexBuffers.GetInteger();
	if ( mode >= 2 ) {
		return true;
	}
	if ( mode < 1 ) {
		return false;
	}
	const idRenderModel *model = ( def != NULL ) ? def->parms.hModel : NULL;
	return model == NULL || model->IsDynamicModel() == DM_STATIC;
}

/*
==================
R_CreateAmbientCache

Create it if needed
==================
*/
bool R_CreateAmbientCache( srfTriangles_t *tri, bool needsLighting ) {
	if ( tri == NULL || tri->verts == NULL || tri->numVerts <= 0 ) {
		return false;
	}
	if ( tri->ambientCache ) {
		return true;
	}
	if ( R_GpuSkinning_IsCandidate( tri )
		&& R_GpuSkinning_PrepareAmbientCache( tri, needsLighting ) ) {
		return true;
	}
	// we are going to use it for drawing, so make sure we have the tangents and normals
	if ( needsLighting && !tri->tangentsCalculated ) {
		R_DeriveTangents( tri );
	}

	vertexCache.Alloc( tri->verts, tri->numVerts * sizeof( tri->verts[0] ), &tri->ambientCache );
	if ( !tri->ambientCache ) {
		return false;
	}
	return true;
}

/*
==================
R_CreatePackedSurfaceFrameCaches

Packed MD5R surfaces keep their canonical geometry outside the classic static
ambient/index cache ownership model. In the current Q4SDK_MD5R hybrid build we
still draw through the classic backend, so visible packed surfaces upload a
frame-temp ambient/index view instead of claiming long-lived cache residency.
==================
*/
bool R_CreatePackedSurfaceFrameCaches( srfTriangles_t *tri, bool needsLighting, bool createIndexCache ) {
	if ( tri == NULL || tri->numVerts <= 0 ) {
		return false;
	}
	const bool gpuPrepared = tri->ambientCache == NULL
		&& R_GpuSkinning_IsCandidate( tri )
		&& R_GpuSkinning_PrepareAmbientCache( tri, needsLighting );

	idDrawVert *sourceVerts = tri->verts;
	glIndex_t *sourceIndexes = tri->indexes;

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->primBatchMesh != NULL && ( sourceVerts == NULL || ( createIndexCache && tri->numIndexes > 0 && sourceIndexes == NULL ) ) ) {
		sourceVerts = (idDrawVert *)R_FrameAlloc( tri->numVerts * sizeof( sourceVerts[0] ) );
		if ( createIndexCache && tri->numIndexes > 0 ) {
			sourceIndexes = (glIndex_t *)R_FrameAlloc( tri->numIndexes * sizeof( sourceIndexes[0] ) );
		} else {
			sourceIndexes = NULL;
		}

		renderSystem->CopyPrimBatchTriangles( sourceVerts, sourceIndexes, tri->primBatchMesh, tri->silTraceVerts );
	}
#endif

	if ( sourceVerts == NULL && !gpuPrepared ) {
		return false;
	}

	if ( !gpuPrepared && needsLighting && !tri->tangentsCalculated && sourceVerts == tri->verts ) {
		R_DeriveTangents( tri );
		sourceVerts = tri->verts;
	}

	if ( !gpuPrepared ) {
		tri->ambientCache = vertexCache.AllocFrameTemp( sourceVerts,
			tri->numVerts * sizeof( sourceVerts[0] ) );
		if ( !tri->ambientCache ) {
			return false;
		}
	}

	if ( r_useIndexBuffers.GetBool() && createIndexCache && tri->numIndexes > 0 && sourceIndexes != NULL ) {
		tri->indexCache = vertexCache.AllocFrameTemp( sourceIndexes, tri->numIndexes * sizeof( sourceIndexes[0] ), true );
	} else {
		tri->indexCache = NULL;
	}

	tri->tempAmbientCache = true;
	return true;
}

/*
==================
R_CreateFrameSubmitTri

Runtime BSE models are rebuilt for each rendered view. Submit a frame-local
triangle snapshot so a later subview/main-view rebuild cannot clear the cache
fields used by a draw surface that is already queued for the backend.
==================
*/
static srfTriangles_t *R_CreateFrameSubmitTri( srfTriangles_t *tri, bool needsLighting ) {
	if ( tri == NULL || tri->numVerts <= 0 || tri->numIndexes <= 0 ) {
		return NULL;
	}

	idDrawVert *sourceVerts = tri->verts;
	glIndex_t *sourceIndexes = tri->indexes;

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->primBatchMesh != NULL && ( sourceVerts == NULL || sourceIndexes == NULL ) ) {
		sourceVerts = (idDrawVert *)R_FrameAlloc( tri->numVerts * sizeof( sourceVerts[0] ) );
		sourceIndexes = (glIndex_t *)R_FrameAlloc( tri->numIndexes * sizeof( sourceIndexes[0] ) );
		renderSystem->CopyPrimBatchTriangles( sourceVerts, sourceIndexes, tri->primBatchMesh, tri->silTraceVerts );
	}
#endif

	if ( sourceVerts == NULL || sourceIndexes == NULL ) {
		return NULL;
	}

	if ( needsLighting && !tri->tangentsCalculated && sourceVerts == tri->verts ) {
		R_DeriveTangents( tri );
		sourceVerts = tri->verts;
	}

	srfTriangles_t *submitTri = (srfTriangles_t *)R_FrameAlloc( sizeof( *submitTri ) );
	*submitTri = *tri;
	// The frame snapshot borrows immutable sidecars and the palette from the
	// dynamic-model snapshot. It must never free the source allocation.
	submitTri->gpuSkinningJointPaletteAlloc = NULL;
	submitTri->numGpuSkinningJointPaletteAllocJoints = 0;
	submitTri->verts = sourceVerts;
	submitTri->indexes = sourceIndexes;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	submitTri->primBatchMesh = NULL;
#endif
	submitTri->ambientCache = vertexCache.AllocFrameTemp( sourceVerts, tri->numVerts * sizeof( sourceVerts[0] ) );
	if ( submitTri->ambientCache == NULL ) {
		return NULL;
	}
	submitTri->tempAmbientCache = true;

	if ( r_useIndexBuffers.GetBool() ) {
		submitTri->indexCache = vertexCache.AllocFrameTemp( sourceIndexes, tri->numIndexes * sizeof( sourceIndexes[0] ), true );
		if ( submitTri->indexCache == NULL ) {
			return NULL;
		}
	} else {
		glIndex_t *submitIndexes = (glIndex_t *)R_FrameAlloc( tri->numIndexes * sizeof( sourceIndexes[0] ) );
		memcpy( submitIndexes, sourceIndexes, tri->numIndexes * sizeof( sourceIndexes[0] ) );
		submitTri->indexes = submitIndexes;
		submitTri->indexCache = NULL;
	}

	return submitTri;
}

/*
==================
R_CreateLightingCache

Returns false if the cache couldn't be allocated, in which case the surface should be skipped.
==================
*/
bool R_CreateLightingCache( const idRenderEntityLocal *ent, const idRenderLightLocal *light, srfTriangles_t *tri ) {
	idVec3		localLightOrigin;

	if ( tri == NULL || tri->ambientSurface == NULL || tri->ambientSurface->verts == NULL || tri->ambientSurface->numVerts <= 0 ) {
		return false;
	}

	// fogs and blends don't need light vectors
	if ( light->lightShader->IsFogLight() || light->lightShader->IsBlendLight() ) {
		return true;
	}

	// not needed if we have vertex programs
	if ( tr.backEndRendererHasVertexPrograms ) {
		return true;
	}

	R_GlobalPointToLocal( ent->modelMatrix, light->globalLightOrigin, localLightOrigin );

	int	size = tri->ambientSurface->numVerts * sizeof( lightingCache_t );
	lightingCache_t *cache = (lightingCache_t *)_alloca16( size );

#if 1

	SIMDProcessor->CreateTextureSpaceLightVectors( &cache[0].localLightVector, localLightOrigin,
												tri->ambientSurface->verts, tri->ambientSurface->numVerts, tri->indexes, tri->numIndexes );

#else

	bool *used = (bool *)_alloca16( tri->ambientSurface->numVerts * sizeof( used[0] ) );
	memset( used, 0, tri->ambientSurface->numVerts * sizeof( used[0] ) );

	// because the interaction may be a very small subset of the full surface,
	// it makes sense to only deal with the verts used
	for ( int j = 0; j < tri->numIndexes; j++ ) {
		int i = tri->indexes[j];
		if ( used[i] ) {
			continue;
		}
		used[i] = true;

		idVec3 lightDir;
		const idDrawVert *v;

		v = &tri->ambientSurface->verts[i];

		lightDir = localLightOrigin - v->xyz;

		cache[i].localLightVector[0] = lightDir * v->tangents[0];
		cache[i].localLightVector[1] = lightDir * v->tangents[1];
		cache[i].localLightVector[2] = lightDir * v->normal;
	}

#endif

	vertexCache.Alloc( cache, size, &tri->lightingCache );
	if ( !tri->lightingCache ) {
		return false;
	}
	return true;
}

/*
==================
R_CreatePrivateShadowCache

This is used only for a specific light
==================
*/
void R_CreatePrivateShadowCache( srfTriangles_t *tri ) {
	if ( tri == NULL || !tri->shadowVertexes || tri->numVerts <= 0 ) {
		return;
	}

	vertexCache.Alloc( tri->shadowVertexes, tri->numVerts * sizeof( *tri->shadowVertexes ), &tri->shadowCache );
}

/*
==================
R_CreateVertexProgramShadowCache

This is constant for any number of lights, the vertex program
takes care of projecting the verts to infinity.
==================
*/
void R_CreateVertexProgramShadowCache( srfTriangles_t *tri ) {
	if ( tri == NULL || tri->numVerts <= 0 ) {
		return;
	}

	shadowCache_t *temp = (shadowCache_t *)_alloca16( tri->numVerts * 2 * sizeof( shadowCache_t ) );

#if 1
	if ( tri->verts != NULL ) {
		SIMDProcessor->CreateVertexProgramShadowCache( &temp->xyz, tri->verts, tri->numVerts );
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	} else if ( tri->silTraceVerts != NULL ) {
		R_CreateVertexProgramShadowCacheFromSilTraceVerts(
			temp,
			reinterpret_cast<const rvSilTraceVertT *>( tri->silTraceVerts ),
			tri->numVerts );
#endif
	} else {
		return;
	}

#else

	int numVerts = tri->numVerts;
	const idDrawVert *verts = tri->verts;
	for ( int i = 0; i < numVerts; i++ ) {
		const float *v = verts[i].xyz.ToFloatPtr();
		temp[i*2+0].xyz[0] = v[0];
		temp[i*2+1].xyz[0] = v[0];
		temp[i*2+0].xyz[1] = v[1];
		temp[i*2+1].xyz[1] = v[1];
		temp[i*2+0].xyz[2] = v[2];
		temp[i*2+1].xyz[2] = v[2];
		temp[i*2+0].xyz[3] = 1.0f;		// on the model surface
		temp[i*2+1].xyz[3] = 0.0f;		// will be projected to infinity
	}

#endif

	vertexCache.Alloc( temp, tri->numVerts * 2 * sizeof( shadowCache_t ), &tri->shadowCache );
}

/*
==================
R_SkyboxTexGen
==================
*/
void R_SkyboxTexGen( drawSurf_t *surf, const idVec3 &viewOrg ) {
	int		i;
	idVec3	localViewOrigin;

	R_GlobalPointToLocal( surf->space->modelMatrix, viewOrg, localViewOrigin );

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( surf->geo->primBatchMesh != NULL ) {
		static const float identityTransform[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};

		surf->dynamicTexCoords = NULL;
		R_SetPackedTexGenTransformAndViewOrg( surf, identityTransform, localViewOrigin );
		return;
	}
#endif

	surf->texGenTransformAndViewOrg = NULL;

	int numVerts = surf->geo->numVerts;
	if ( numVerts <= 0 ) {
		surf->dynamicTexCoords = NULL;
		return;
	}
	int size = numVerts * sizeof( idVec3 );
	idVec3 *texCoords = (idVec3 *) _alloca16( size );

	const idDrawVert *verts = surf->geo->verts;
	for ( i = 0; i < numVerts; i++ ) {
		texCoords[i][0] = verts[i].xyz[0] - localViewOrigin[0];
		texCoords[i][1] = verts[i].xyz[1] - localViewOrigin[1];
		texCoords[i][2] = verts[i].xyz[2] - localViewOrigin[2];
	}

	surf->dynamicTexCoords = vertexCache.AllocFrameTemp( texCoords, size );
}

/*
==================
R_WobbleskyTexGen
==================
*/
void R_WobbleskyTexGen( drawSurf_t *surf, const idVec3 &viewOrg ) {
	int		i;
	idVec3	localViewOrigin;

	const int *parms = surf->material->GetTexGenRegisters();

	float	wobbleDegrees = surf->shaderRegisters[ parms[0] ];
	float	wobbleSpeed = surf->shaderRegisters[ parms[1] ];
	float	rotateSpeed = surf->shaderRegisters[ parms[2] ];

	wobbleDegrees = wobbleDegrees * idMath::PI / 180;
	wobbleSpeed = wobbleSpeed * 2 * idMath::PI / 60;
	rotateSpeed = rotateSpeed * 2 * idMath::PI / 60;

	// very ad-hoc "wobble" transform
	float	transform[16];
	float	a = tr.viewDef->floatTime * wobbleSpeed;
	float	s = sin( a ) * sin( wobbleDegrees );
	float	c = cos( a ) * sin( wobbleDegrees );
	float	z = cos( wobbleDegrees );

	idVec3	axis[3];

	axis[2][0] = c;
	axis[2][1] = s;
	axis[2][2] = z;

	axis[1][0] = -sin( a * 2 ) * sin( wobbleDegrees );
	axis[1][2] = -s * sin( wobbleDegrees );
	axis[1][1] = sqrt( 1.0f - ( axis[1][0] * axis[1][0] + axis[1][2] * axis[1][2] ) );

	// make the second vector exactly perpendicular to the first
	axis[1] -= ( axis[2] * axis[1] ) * axis[2];
	axis[1].Normalize();

	// construct the third with a cross
	axis[0].Cross( axis[1], axis[2] );

	// add the rotate
	s = sin( rotateSpeed * tr.viewDef->floatTime );
	c = cos( rotateSpeed * tr.viewDef->floatTime );

	transform[0] = axis[0][0] * c + axis[1][0] * s;
	transform[4] = axis[0][1] * c + axis[1][1] * s;
	transform[8] = axis[0][2] * c + axis[1][2] * s;

	transform[1] = axis[1][0] * c - axis[0][0] * s;
	transform[5] = axis[1][1] * c - axis[0][1] * s;
	transform[9] = axis[1][2] * c - axis[0][2] * s;

	transform[2] = axis[2][0];
	transform[6] = axis[2][1];
	transform[10] = axis[2][2];

	transform[3] = transform[7] = transform[11] = 0.0f;
	transform[12] = transform[13] = transform[14] = 0.0f;

	R_GlobalPointToLocal( surf->space->modelMatrix, viewOrg, localViewOrigin );

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( surf->geo->primBatchMesh != NULL ) {
		surf->dynamicTexCoords = NULL;
		R_SetPackedTexGenTransformAndViewOrg( surf, transform, localViewOrigin );
		return;
	}
#endif

	surf->texGenTransformAndViewOrg = NULL;

	int numVerts = surf->geo->numVerts;
	if ( numVerts <= 0 ) {
		surf->dynamicTexCoords = NULL;
		return;
	}
	int size = numVerts * sizeof( idVec3 );
	idVec3 *texCoords = (idVec3 *) _alloca16( size );

	const idDrawVert *verts = surf->geo->verts;
	for ( i = 0; i < numVerts; i++ ) {
		idVec3 v;

		v[0] = verts[i].xyz[0] - localViewOrigin[0];
		v[1] = verts[i].xyz[1] - localViewOrigin[1];
		v[2] = verts[i].xyz[2] - localViewOrigin[2];

		R_LocalPointToGlobal( transform, v, texCoords[i] );
	}

	surf->dynamicTexCoords = vertexCache.AllocFrameTemp( texCoords, size );
}

static idImage *R_GetFirstLegacyProgrammableStageImage( const newShaderStage_t *newStage ) {
	if ( newStage == NULL ) {
		return NULL;
	}

	for ( int imageIndex = 0; imageIndex < newStage->numShaderTextures; ++imageIndex ) {
		idImage *image = newStage->shaderTextureImages[imageIndex];
		if ( image != NULL ) {
			return image;
		}
	}

	for ( int imageIndex = 0; imageIndex < newStage->numFragmentProgramImages; ++imageIndex ) {
		idImage *image = newStage->fragmentProgramImages[imageIndex];
		if ( image != NULL ) {
			return image;
		}
	}

	return NULL;
}

static idImage *R_GetPOTCorrectionImage( const shaderStage_t *stage ) {
	if ( stage == NULL ) {
		return NULL;
	}

	if ( stage->texture.image != NULL ) {
		return stage->texture.image;
	}

	return R_GetFirstLegacyProgrammableStageImage( stage->newStage );
}

static void R_POTCorrectionTexGen( drawSurf_t *surf ) {
	const idMaterial *material = surf->material;
	const shaderStage_t *stage = NULL;

	const int stageCount = material->GetNumStages();
	for ( int stageNum = 0; stageNum < stageCount; ++stageNum ) {
		const shaderStage_t *candidate = material->GetStage( stageNum );
		if ( candidate->texture.texgen == TG_POT_CORRECTION ) {
			stage = candidate;
			break;
		}
	}

	if ( stage == NULL || R_GetPOTCorrectionImage( stage ) == NULL ) {
		return;
	}

	const int width = tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 + 1;
	const int height = tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 + 1;
	const idVec2 correctionFactor( width / (float)MakePowerOfTwo( width ), height / (float)MakePowerOfTwo( height ) );

	const int numVerts = surf->geo->numVerts;
	if ( numVerts <= 0 || surf->geo->verts == NULL ) {
		surf->dynamicTexCoords = NULL;
		return;
	}

	const int size = numVerts * sizeof( idVec2 );
	idVec2 *texCoords = (idVec2 *)_alloca16( size );
	const idDrawVert *verts = surf->geo->verts;
	for ( int vertNum = 0; vertNum < numVerts; ++vertNum ) {
		texCoords[vertNum].x = correctionFactor.x * verts[vertNum].st.x;
		texCoords[vertNum].y = correctionFactor.y * verts[vertNum].st.y;
	}

	surf->dynamicTexCoords = vertexCache.AllocFrameTemp( texCoords, size );
}

/*
=================
R_SpecularTexGen

Calculates the specular coordinates for cards without vertex programs.
=================
*/
static void R_SpecularTexGen( drawSurf_t *surf, const idVec3 &globalLightOrigin, const idVec3 &viewOrg ) {
	const srfTriangles_t *tri;
	idVec3	localLightOrigin;
	idVec3	localViewOrigin;

	R_GlobalPointToLocal( surf->space->modelMatrix, globalLightOrigin, localLightOrigin );
	R_GlobalPointToLocal( surf->space->modelMatrix, viewOrg, localViewOrigin );

	tri = surf->geo;
	if ( tri->numVerts <= 0 ) {
		surf->dynamicTexCoords = NULL;
		return;
	}

	// FIXME: change to 3 component?
	int	size = tri->numVerts * sizeof( idVec4 );
	idVec4 *texCoords = (idVec4 *) _alloca16( size );

#if 1

	SIMDProcessor->CreateSpecularTextureCoords( texCoords, localLightOrigin, localViewOrigin,
											tri->verts, tri->numVerts, tri->indexes, tri->numIndexes );

#else

	bool *used = (bool *)_alloca16( tri->numVerts * sizeof( used[0] ) );
	memset( used, 0, tri->numVerts * sizeof( used[0] ) );

	// because the interaction may be a very small subset of the full surface,
	// it makes sense to only deal with the verts used
	for ( int j = 0; j < tri->numIndexes; j++ ) {
		int i = tri->indexes[j];
		if ( used[i] ) {
			continue;
		}
		used[i] = true;

		float ilength;

		const idDrawVert *v = &tri->verts[i];

		idVec3 lightDir = localLightOrigin - v->xyz;
		idVec3 viewDir = localViewOrigin - v->xyz;

		ilength = idMath::RSqrt( lightDir * lightDir );
		lightDir[0] *= ilength;
		lightDir[1] *= ilength;
		lightDir[2] *= ilength;

		ilength = idMath::RSqrt( viewDir * viewDir );
		viewDir[0] *= ilength;
		viewDir[1] *= ilength;
		viewDir[2] *= ilength;

		lightDir += viewDir;

		texCoords[i][0] = lightDir * v->tangents[0];
		texCoords[i][1] = lightDir * v->tangents[1];
		texCoords[i][2] = lightDir * v->normal;
		texCoords[i][3] = 1;
	}

#endif

	surf->dynamicTexCoords = vertexCache.AllocFrameTemp( texCoords, size );
}


//=======================================================================================================

/*
=============
R_SetEntityDefViewEntity

If the entityDef isn't already on the viewEntity list, create
a viewEntity and add it to the list with an empty scissor rect.

This does not instantiate dynamic models for the entity yet.
=============
*/
viewEntity_t *R_SetEntityDefViewEntity( idRenderEntityLocal *def ) {
	viewEntity_t		*vModel;

	if ( def->viewCount == tr.viewCount ) {
		return def->viewEntity;
	}
	def->viewCount = tr.viewCount;

	// set the model and modelview matricies
	vModel = (viewEntity_t *)R_ClearedFrameAlloc( sizeof( *vModel ) );
	vModel->entityDef = def;

	// the scissorRect will be expanded as the model bounds is accepted into visible portal chains
	vModel->scissorRect.Clear();

	// copy the model and weapon depth hack for back-end use
	vModel->modelDepthHack = def->parms.modelDepthHack;
	vModel->flatDiffuseColor = def->parms.flatDiffuseColor;
	vModel->flatDiffuseFlags = def->parms.flatDiffuseFlags;
	// View-only entities remain ineligible even in mirrors or render-demo
	// views where the per-view weaponDepthHack boolean is not active.
	if ( def->parms.weaponDepthHackInViewID != 0 || def->parms.allowSurfaceInViewID != 0 ) {
		vModel->flatDiffuseColor.Zero();
		vModel->flatDiffuseFlags = 0;
	}
	if ( !def->referenceBounds.IsCleared() ) {
		const float height = def->referenceBounds[1][2] - def->referenceBounds[0][2];
		if ( height > 0.001f ) {
			vModel->flatDiffuseMinZ = def->referenceBounds[0][2];
			vModel->flatDiffuseInvHeight = 1.0f / height;
		}
	}
	R_AxisToModelMatrix( def->parms.axis, def->parms.origin, vModel->modelMatrix );

	// we may not have a viewDef if we are just creating shadows at entity creation time
	if ( tr.viewDef ) {
		myGlMultMatrix( vModel->modelMatrix, tr.viewDef->worldSpace.modelViewMatrix, vModel->modelViewMatrix );
		vModel->weaponDepthHack = ( def->parms.weaponDepthHackInViewID != 0
			&& def->parms.weaponDepthHackInViewID == tr.viewDef->renderView.viewID );
		vModel->distanceToCamera = ( def->parms.origin - tr.viewDef->renderView.vieworg ).LengthSqr();

		vModel->next = tr.viewDef->viewEntitys;
		tr.viewDef->viewEntitys = vModel;
	}

	def->viewEntity = vModel;

	return vModel;
}

/*
====================
R_TestPointInViewLight
====================
*/
static const float INSIDE_LIGHT_FRUSTUM_SLOP = 32;
// this needs to be greater than the dist from origin to corner of near clip plane
static bool R_TestPointInViewLight( const idVec3 &org, const idRenderLightLocal *light ) {
	int		i;
	idVec3	local;

	for ( i = 0 ; i < 6 ; i++ ) {
		float d = light->frustum[i].Distance( org );
		if ( d > INSIDE_LIGHT_FRUSTUM_SLOP ) {
			return false;
		}
	}

	return true;
}

/*
===================
R_PointInFrustum

Assumes positive sides face outward
===================
*/
static bool R_PointInFrustum( idVec3 &p, idPlane *planes, int numPlanes ) {
	for ( int i = 0 ; i < numPlanes ; i++ ) {
		float d = planes[i].Distance( p );
		if ( d > 0 ) {
			return false;
		}
	}
	return true;
}

/*
=============
R_SetLightDefViewLight

If the lightDef isn't already on the viewLight list, create
a viewLight and add it to the list with an empty scissor rect.
=============
*/
viewLight_t *R_SetLightDefViewLight( idRenderLightLocal *light ) {
	viewLight_t *vLight;
	light->referencedFrameNum = tr.frameCount;

	if ( light->viewCount == tr.viewCount ) {
		return light->viewLight;
	}
	light->viewCount = tr.viewCount;

	// add to the view light chain
	vLight = (viewLight_t *)R_ClearedFrameAlloc( sizeof( *vLight ) );
	vLight->lightDef = light;

	// the scissorRect will be expanded as the light bounds is accepted into visible portal chains
	vLight->scissorRect.Clear();

	// calculate the shadow cap optimization states
	vLight->viewInsideLight = R_TestPointInViewLight( tr.viewDef->renderView.vieworg, light );
	if ( !vLight->viewInsideLight ) {
		vLight->viewSeesShadowPlaneBits = 0;
		for ( int i = 0 ; i < light->numShadowFrustums ; i++ ) {
			float d = light->shadowFrustums[i].planes[5].Distance( tr.viewDef->renderView.vieworg );
			if ( d < INSIDE_LIGHT_FRUSTUM_SLOP ) {
				vLight->viewSeesShadowPlaneBits|= 1 << i;
			}
		}
	} else {
		// this should not be referenced in this case
		vLight->viewSeesShadowPlaneBits = 63;
	}

	// see if the light center is in view, which will allow us to cull invisible shadows
	vLight->viewSeesGlobalLightOrigin = R_PointInFrustum( light->globalLightOrigin, tr.viewDef->frustum, 4 );

	// copy data used by backend
	vLight->globalLightOrigin = light->globalLightOrigin;
	vLight->pointLight = light->parms.pointLight;
	vLight->parallel = light->parms.parallel;
	vLight->lightRadius = light->parms.lightRadius;
	vLight->lightAxis = light->parms.axis;
	vLight->lightProject[0] = light->lightProject[0];
	vLight->lightProject[1] = light->lightProject[1];
	vLight->lightProject[2] = light->lightProject[2];
	vLight->lightProject[3] = light->lightProject[3];
	vLight->fogPlane = light->frustum[5];
	vLight->frustumTris = light->frustumTris;
	vLight->falloffImage = light->falloffImage;
	vLight->lightShader = light->lightShader;
	vLight->shaderRegisters = NULL;		// allocated and evaluated in R_AddLightSurfaces

	// link the view light
	vLight->next = tr.viewDef->viewLights;
	tr.viewDef->viewLights = vLight;

	light->viewLight = vLight;

	return vLight;
}

/*
=================
idRenderWorldLocal::CreateLightDefInteractions

When a lightDef is determined to effect the view (contact the frustum and non-0 light), it will check to
make sure that it has interactions for all the entityDefs that it might possibly contact.

This does not guarantee that all possible interactions for this light are generated, only that
the ones that may effect the current view are generated. so it does need to be called every view.

This does not cause entityDefs to create dynamic models, all work is done on the referenceBounds.

All entities that have non-empty interactions with viewLights will
have viewEntities made for them and be put on the viewEntity list,
even if their surfaces aren't visible, because they may need to cast shadows.

Interactions are usually removed when a entityDef or lightDef is modified, unless the change
is known to not effect them, so there is no danger of getting a stale interaction, we just need to
check that needed ones are created.

An interaction can be at several levels:

Don't interact (but share an area) (numSurfaces = 0)
Entity reference bounds touches light frustum, but surfaces haven't been generated (numSurfaces = -1)
Shadow surfaces have been generated, but light surfaces have not.  The shadow surface may still be empty due to bounds being conservative.
Both shadow and light surfaces have been generated.  Either or both surfaces may still be empty due to conservative bounds.

=================
*/
void idRenderWorldLocal::CreateLightDefInteractions( idRenderLightLocal *ldef ) {
	areaReference_t		*eref;
	areaReference_t		*lref;
	idRenderEntityLocal		*edef;
	portalArea_t	*area;
	idInteraction	*inter;

	for ( lref = ldef->references ; lref ; lref = lref->ownerNext ) {
		area = lref->area;

		// check all the models in this area
		for ( eref = area->entityRefs.areaNext ; eref != &area->entityRefs ; eref = eref->areaNext ) {
			edef = eref->entity;

			// if the entity doesn't have any light-interacting surfaces, we could skip this,
			// but we don't want to instantiate dynamic models yet, so we can't check that on
			// most things

			// if the entity isn't viewed
			if ( tr.viewDef && edef->viewCount != tr.viewCount ) {
				// if the light doesn't cast shadows, skip
				if ( !ldef->lightShader->LightCastsShadows() ) {
					continue;
				}
				// if we are suppressing its shadow in this view, skip
				if ( !r_skipSuppress.GetBool() ) {
					if ( edef->parms.suppressShadowInViewID && edef->parms.suppressShadowInViewID == tr.viewDef->renderView.viewID ) {
						continue;
					}
					if ( edef->parms.suppressShadowInLightID && edef->parms.suppressShadowInLightID == ldef->parms.lightId ) {
						continue;
					}
				}
			}

			// some big outdoor meshes are flagged to not create any dynamic interactions
			// when the level designer knows that nearby moving lights shouldn't actually hit them
			if ( edef->parms.noDynamicInteractions && edef->world->generateAllInteractionsCalled ) {
				continue;
			}

			// if any of the edef's interaction match this light, we don't
			// need to consider it. 
			if ( r_useInteractionTable.GetBool() && this->interactionTable ) {
				// allocating these tables may take several megs on big maps, but it saves 3% to 5% of
				// the CPU time.  The table is updated at interaction::AllocAndLink() and interaction::UnlinkAndFree()
				int index = ldef->index * this->interactionTableWidth + edef->index;
				inter = this->interactionTable[ index ];
				if ( inter ) {
					// if this entity wasn't in view already, the scissor rect will be empty,
					// so it will only be used for shadow casting
					if ( !inter->IsEmpty() ) {
						R_SetEntityDefViewEntity( edef );
					}
					continue;
				}
			} else {
				// scan the doubly linked lists, which may have several dozen entries

				// we could check either model refs or light refs for matches, but it is
				// assumed that there will be less lights in an area than models
				// so the entity chains should be somewhat shorter (they tend to be fairly close).
				for ( inter = edef->firstInteraction; inter != NULL; inter = inter->entityNext ) {
					if ( inter->lightDef == ldef ) {
						break;
					}
				}

				// if we already have an interaction, we don't need to do anything
				if ( inter != NULL ) {
					// if this entity wasn't in view already, the scissor rect will be empty,
					// so it will only be used for shadow casting
					if ( !inter->IsEmpty() ) {
						R_SetEntityDefViewEntity( edef );
					}
					continue;
				}
			}

			//
			// create a new interaction, but don't do any work other than bbox to frustum culling
			//
			idInteraction *inter = idInteraction::AllocAndLink( edef, ldef );

			// do a check of the entity reference bounds against the light frustum,
			// trying to avoid creating a viewEntity if it hasn't been already
			float	modelMatrix[16];
			float	*m;

			if ( edef->viewCount == tr.viewCount ) {
				m = edef->viewEntity->modelMatrix;
			} else {
				R_AxisToModelMatrix( edef->parms.axis, edef->parms.origin, modelMatrix );
				m = modelMatrix;
			}

			if ( R_CullLocalBox( edef->referenceBounds, m, 6, ldef->frustum ) ) {
				inter->MakeEmpty();
				continue;
			}

			// we will do a more precise per-surface check when we are checking the entity

			// if this entity wasn't in view already, the scissor rect will be empty,
			// so it will only be used for shadow casting
			R_SetEntityDefViewEntity( edef );
		}
	}
}

//===============================================================================================================

/*
=================
R_LinkLightSurf
=================
*/
bool R_LinkLightSurf( const drawSurf_t **link, const srfTriangles_t *tri, const viewEntity_t *space,
				   const idRenderLightLocal *light, const idMaterial *shader, const idScreenRect &scissor, bool viewInsideShadow ) {
	drawSurf_t		*drawSurf;
	const int		limitBatchSize = r_limitBatchSize.GetInteger();

	if ( limitBatchSize > 0 && tri->numIndexes <= limitBatchSize ) {
		return false;
	}

	if ( !space ) {
		space = &tr.viewDef->worldSpace;
	}

	drawSurf = (drawSurf_t *)R_FrameAlloc( sizeof( *drawSurf ) );
	// Receiver and stencil-volume drawSurfs do not run R_FinalizeDrawSurf.
	// Clear the embedded slot so whole-drawSurf copies never read indeterminate
	// contract bytes; scene packets seal their role-specific NOT_APPLICABLE copy.
	memset( &drawSurf->classicDeform, 0, sizeof( drawSurf->classicDeform ) );

	drawSurf->geo = tri;
	drawSurf->space = space;
	drawSurf->material = shader;
	drawSurf->scissorRect = scissor;
	drawSurf->area = NULL;
	drawSurf->dsFlags = 0;
	drawSurf->dynamicTexCoords = NULL;
	drawSurf->texGenTransformAndViewOrg = NULL;
	drawSurf->decalColorCache = NULL;
	drawSurf->decalColorOffset = 0;
	drawSurf->decalColorStride = 0;
	drawSurf->decalColorStageCount = 0;
	if ( viewInsideShadow ) {
		drawSurf->dsFlags |= DSF_VIEW_INSIDE_SHADOW;
	}

	if ( !shader ) {
		// shadows won't have a shader
		drawSurf->shaderRegisters = NULL;
	} else {
		// process the shader expressions for conditionals / color / texcoords;
		// shared with the ambient pass through the per-view register memo
		drawSurf->shaderRegisters = R_SetupDrawSurfShaderRegisters( space, NULL, shader );

		// calculate the specular coordinates if we aren't using vertex programs
		if ( !tr.backEndRendererHasVertexPrograms && !r_skipSpecular.GetBool()  ) {
			R_SpecularTexGen( drawSurf, light->globalLightOrigin, tr.viewDef->renderView.vieworg );
			// if we failed to allocate space for the specular calculations, drop the surface
			if ( !drawSurf->dynamicTexCoords ) {
				return false;
			}
		}
	}

	// actually link it in
	drawSurf->nextOnLight = *link;
	*link = drawSurf;
	return true;
}

static const portalArea_t *R_FallbackDrawSurfArea( const viewEntity_t *space ) {
	if ( space == NULL || space->entityDef == NULL || space->entityDef->entityRefs == NULL ) {
		return NULL;
	}

	return space->entityDef->entityRefs->area;
}

static const portalArea_t *R_DrawSurfAreaForGlobalPoint( const idVec3 &point ) {
	if ( tr.viewDef == NULL || tr.viewDef->renderWorld == NULL ) {
		return NULL;
	}

	const int areaNum = tr.viewDef->renderWorld->PointInArea( point );
	if ( areaNum >= 0 && areaNum < tr.viewDef->renderWorld->NumAreas() ) {
		return &tr.viewDef->renderWorld->portalAreas[ areaNum ];
	}
	return NULL;
}

static const portalArea_t *R_ResolveDrawSurfAreaUncached( const srfTriangles_t *tri, const viewEntity_t *space ) {
	if ( tri == NULL || space == NULL || tr.viewDef == NULL || tr.viewDef->renderWorld == NULL ) {
		return NULL;
	}

	if ( !tri->bounds.IsCleared() ) {
		idVec3 globalCenter;
		R_LocalPointToGlobal( space->modelMatrix, tri->bounds.GetCenter(), globalCenter );
		const portalArea_t *centerArea = R_DrawSurfAreaForGlobalPoint( globalCenter );
		if ( centerArea != NULL ) {
			return centerArea;
		}

		idVec3 localPoints[8];
		tri->bounds.ToPoints( localPoints );
		for ( int pointIndex = 0; pointIndex < 8; pointIndex++ ) {
			idVec3 globalPoint;
			R_LocalPointToGlobal( space->modelMatrix, localPoints[pointIndex], globalPoint );
			const portalArea_t *cornerArea = R_DrawSurfAreaForGlobalPoint( globalPoint );
			if ( cornerArea != NULL ) {
				return cornerArea;
			}
		}
	}

	return R_FallbackDrawSurfArea( space );
}

typedef struct drawSurfAreaMemo_s {
	const srfTriangles_t *		tri;
	const idRenderEntityLocal *	entityDef;
	const portalArea_t *		area;
	idBounds					bounds;
	float						modelMatrix[16];
	int							entityModFrame;
} drawSurfAreaMemo_t;

static const int DRAWSURF_AREA_MEMO_MAX = 65536;
static idList<drawSurfAreaMemo_t>	s_drawSurfAreaMemo;
static idHashIndex					s_drawSurfAreaMemoHash;
static idHashIndex					s_drawSurfAreaMemoEntityHash;

static ID_INLINE int R_DrawSurfAreaMemoKey( const idRenderEntityLocal *entityDef, const srfTriangles_t *tri ) {
	return ( int )( ( ( ( uintptr_t )entityDef ) >> 4 ) ^ ( ( ( uintptr_t )tri ) >> 4 ) );
}

static ID_INLINE int R_DrawSurfAreaMemoEntityKey( const idRenderEntityLocal *entityDef ) {
	return ( int )( ( ( uintptr_t )entityDef ) >> 4 );
}

void R_ClearDrawSurfAreaMemo( void ) {
	s_drawSurfAreaMemo.Clear();
	s_drawSurfAreaMemoHash.Free();
	s_drawSurfAreaMemoEntityHash.Free();
}

void R_InvalidateDrawSurfAreaMemoForEntity( const idRenderEntityLocal *entityDef ) {
	// Tombstone instead of removing so hash indexes stay stable; the size cap in
	// R_ResolveDrawSurfArea reclaims tombstones by clearing everything. A second
	// hash keyed on the entity alone lets invalidation walk only this entity's
	// entries rather than scanning the whole memo on every FreeEntityDef.
	const int entityKey = R_DrawSurfAreaMemoEntityKey( entityDef );
	for ( int i = s_drawSurfAreaMemoEntityHash.First( entityKey ); i != -1;
			i = s_drawSurfAreaMemoEntityHash.Next( i ) ) {
		if ( s_drawSurfAreaMemo[i].entityDef == entityDef ) {
			s_drawSurfAreaMemo[i].entityDef = NULL;
		}
	}
}

static const portalArea_t *R_ResolveDrawSurfArea( const srfTriangles_t *tri, const viewEntity_t *space ) {
	if ( tri == NULL || space == NULL || tr.viewDef == NULL || tr.viewDef->renderWorld == NULL ) {
		return NULL;
	}

	const idRenderEntityLocal *entityDef = space->entityDef;
	if ( entityDef == NULL ) {
		// frame-temporary spaces (BSE effects) have no stable identity to key on
		return R_ResolveDrawSurfAreaUncached( tri, space );
	}

	// A memo hit requires every input the uncached resolve reads to be
	// bit-identical to the stored resolve: tri->bounds and space->modelMatrix
	// by value, and lastModifiedFrameNum for the entityRefs fallback path,
	// so a hit can never change drawSurf->area.
	const int key = R_DrawSurfAreaMemoKey( entityDef, tri );
	int entryIndex = -1;
	for ( int i = s_drawSurfAreaMemoHash.First( key ); i != -1; i = s_drawSurfAreaMemoHash.Next( i ) ) {
		const drawSurfAreaMemo_t &entry = s_drawSurfAreaMemo[i];
		if ( entry.tri != tri || entry.entityDef != entityDef ) {
			continue;
		}
		entryIndex = i;
		if ( entry.entityModFrame == entityDef->lastModifiedFrameNum
				&& memcmp( &entry.bounds, &tri->bounds, sizeof( entry.bounds ) ) == 0
				&& memcmp( entry.modelMatrix, space->modelMatrix, sizeof( entry.modelMatrix ) ) == 0 ) {
			return entry.area;
		}
		break;
	}

	const portalArea_t *area = R_ResolveDrawSurfAreaUncached( tri, space );

	if ( entryIndex == -1 ) {
		if ( s_drawSurfAreaMemo.Num() >= DRAWSURF_AREA_MEMO_MAX ) {
			R_ClearDrawSurfAreaMemo();
		}
		entryIndex = s_drawSurfAreaMemo.Num();
		drawSurfAreaMemo_t &newEntry = s_drawSurfAreaMemo.Alloc();
		newEntry.tri = tri;
		newEntry.entityDef = entityDef;
		s_drawSurfAreaMemoHash.Add( key, entryIndex );
		s_drawSurfAreaMemoEntityHash.Add( R_DrawSurfAreaMemoEntityKey( entityDef ), entryIndex );
	}
	drawSurfAreaMemo_t &entry = s_drawSurfAreaMemo[entryIndex];
	entry.area = area;
	entry.bounds = tri->bounds;
	memcpy( entry.modelMatrix, space->modelMatrix, sizeof( entry.modelMatrix ) );
	entry.entityModFrame = entityDef->lastModifiedFrameNum;
	return area;
}

/*
======================
R_ClippedLightScissorRectangle
======================
*/
idScreenRect R_ClippedLightScissorRectangle( viewLight_t *vLight ) {
	int i, j;
	const idRenderLightLocal *light = vLight->lightDef;
	idScreenRect r;
	idFixedWinding w;

	r.Clear();

	for ( i = 0 ; i < 6 ; i++ ) {
		const idWinding *ow = light->frustumWindings[i];

		// projected lights may have one of the frustums degenerated
		if ( !ow ) {
			continue;
		}

		// the light frustum planes face out from the light,
		// so the planes that have the view origin on the negative
		// side will be the "back" faces of the light, which must have
		// some fragment inside the portalStack to be visible
		if ( light->frustum[i].Distance( tr.viewDef->renderView.vieworg ) >= 0 ) {
			continue;
		}

		w = *ow;

		// now check the winding against each of the frustum planes
		for ( j = 0; j < 5; j++ ) {
			if ( !w.ClipInPlace( -tr.viewDef->frustum[j] ) ) {
				break;
			}
		}

		// project these points to the screen and add to bounds
		const int windingPointCount = w.GetNumPoints();
		for ( j = 0; j < windingPointCount; j++ ) {
			idPlane		eye, clip;
			idVec3		ndc;

			R_TransformModelToClip( w[j].ToVec3(), tr.viewDef->worldSpace.modelViewMatrix, tr.viewDef->projectionMatrix, eye, clip );

			if ( clip[3] <= 0.01f ) {
				clip[3] = 0.01f;
			}

			R_TransformClipToDevice( clip, tr.viewDef, ndc );

			float windowX = 0.5f * ( 1.0f + ndc[0] ) * ( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 );
			float windowY = 0.5f * ( 1.0f + ndc[1] ) * ( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 );

			if ( windowX > tr.viewDef->scissor.x2 ) {
				windowX = tr.viewDef->scissor.x2;
			} else if ( windowX < tr.viewDef->scissor.x1 ) {
				windowX = tr.viewDef->scissor.x1;
			}
			if ( windowY > tr.viewDef->scissor.y2 ) {
				windowY = tr.viewDef->scissor.y2;
			} else if ( windowY < tr.viewDef->scissor.y1 ) {
				windowY = tr.viewDef->scissor.y1;
			}

			r.AddPoint( windowX, windowY );
		}
	}

	// add the fudge boundary
	r.Expand();

	return r;
}

/*
==================
R_CalcLightScissorRectangle

The light screen bounds will be used to crop the scissor rect during
stencil clears and interaction drawing
==================
*/
int	c_clippedLight, c_unclippedLight;

idScreenRect	R_CalcLightScissorRectangle( viewLight_t *vLight ) {
	idScreenRect	r;
	srfTriangles_t *tri;
	idPlane			eye, clip;
	idVec3			ndc;

	if ( vLight->lightDef->parms.pointLight && !tr_levelshotProjectionShiftActive ) {
		idBounds bounds;
		idRenderLightLocal *lightDef = vLight->lightDef;
		// same ProjectionBounds contract as R_CalcEntityScissorRectangle: it can
		// return true having added no points, leaving the bounds Clear()ed, so
		// IsCleared() is the test rather than the return value
		bounds.Clear();
		tr.viewDef->viewFrustum.ProjectionBounds(
				idBox( lightDef->parms.origin, lightDef->parms.lightRadius, lightDef->parms.axis ), bounds );
		if ( bounds.IsCleared() ) {
			return vLight->scissorRect;
		}
		return R_ScreenRectFromViewFrustumBounds( bounds );
	}

	if ( r_useClippedLightScissors.GetInteger() == 2 ) {
		return R_ClippedLightScissorRectangle( vLight );
	}

	r.Clear();

	tri = vLight->lightDef->frustumTris;
	for ( int i = 0 ; i < tri->numVerts ; i++ ) {
		R_TransformModelToClip( tri->verts[i].xyz, tr.viewDef->worldSpace.modelViewMatrix,
			tr.viewDef->projectionMatrix, eye, clip );

		// if it is near clipped, clip the winding polygons to the view frustum
		if ( clip[3] <= 1 ) {
			c_clippedLight++;
			if ( r_useClippedLightScissors.GetInteger() ) {
				return R_ClippedLightScissorRectangle( vLight );
			} else {
				r.x1 = r.y1 = 0;
				r.x2 = ( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 ) - 1;
				r.y2 = ( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 ) - 1;
				return r;
			}
		}

		R_TransformClipToDevice( clip, tr.viewDef, ndc );

		float windowX = 0.5f * ( 1.0f + ndc[0] ) * ( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 );
		float windowY = 0.5f * ( 1.0f + ndc[1] ) * ( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 );

		if ( windowX > tr.viewDef->scissor.x2 ) {
			windowX = tr.viewDef->scissor.x2;
		} else if ( windowX < tr.viewDef->scissor.x1 ) {
			windowX = tr.viewDef->scissor.x1;
		}
		if ( windowY > tr.viewDef->scissor.y2 ) {
			windowY = tr.viewDef->scissor.y2;
		} else if ( windowY < tr.viewDef->scissor.y1 ) {
			windowY = tr.viewDef->scissor.y1;
		}

		r.AddPoint( windowX, windowY );
	}

	// add the fudge boundary
	r.Expand();

	c_unclippedLight++;

	return r;
}

static bool R_IsUsablePrelightModel( idRenderModel *model ) {
	if ( model == NULL ) {
		return false;
	}
	if ( renderModelManager == NULL || !renderModelManager->ContainsModel( model ) ) {
		return false;
	}
	if ( !model->IsLoaded() || model->IsDefaultModel() ) {
		return false;
	}

	return true;
}

static idRenderModel *R_ViewLightPrelightModel( viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->lightDef == NULL || !r_useOptimizedShadows.GetBool() ) {
		return NULL;
	}

	idRenderLightLocal *light = vLight->lightDef;
	idRenderModel *prelightModel = vLight->lightDef->parms.prelightModel;
	idRenderModel *sanitizedPrelightModel = R_SanitizePrelightModelPointer( prelightModel );
	if ( sanitizedPrelightModel != prelightModel ) {
		light->parms.prelightModel = sanitizedPrelightModel;
		prelightModel = sanitizedPrelightModel;
	}
	if ( prelightModel == NULL ) {
		return NULL;
	}
	if ( !R_IsUsablePrelightModel( prelightModel ) ) {
		common->DWarning( "R_ViewLightPrelightModel: discarding stale prelight model pointer for lightDef %d", light->index );
		light->parms.prelightModel = NULL;
		return NULL;
	}

	return prelightModel;
}

/*
=================
R_ShadowMapLightWillUseShadowMaps

Front-end mirror of the backend shadow-map admission policy
(RB_ShadowMapLightSupportReason). When this returns true the light's stencil
shadow volumes are never drawn, so their generation and linking can be
skipped. The mirror must stay conservative: any condition the backend could
fail at render time is covered by the per-light sticky fallback flag and the
backend-published resource generation.
=================
*/
bool R_ShadowMapLightWillUseShadowMaps( const idRenderLightLocal *lightDef ) {
	if ( !r_shadowMapSkipStencilShadows.GetBool() || !r_useShadowMap.GetBool() || !r_shadows.GetBool() ) {
		return false;
	}
	// a per-view update budget can defer any light to the stencil path mid-frame
	if ( r_shadowMapMaxUpdatesPerView.GetInteger() > 0 ) {
		return false;
	}
	// Subview policies 1/2 can reject a fresh map after front-end submission
	// (reuse-or-stencil / always-stencil). Keep legacy volumes available so a
	// cache miss or backend without subview reuse never loses the shadow.
	if ( tr.viewDef != NULL && tr.viewDef->isSubview
			&& r_shadowMapSubviewPolicy.GetInteger() > 0 ) {
		return false;
	}
	if ( lightDef == NULL || lightDef->shadowMapStencilFallbackSticky ) {
		return false;
	}
	// Optimized prelights replace every static-world per-surface volume with
	// one combined volume.  Whether every such surface is representable in a
	// shadow map is known only after the light's complete interaction list is
	// walked.  Keep entity/dynamic volumes resident for these lights so a later
	// prelight map miss can still select a complete whole-light stencil path.
	if ( r_useOptimizedShadows.GetBool()
			&& R_LightHasRealPrelightModel( lightDef->parms ) ) {
		return false;
	}
	if ( lightDef->parms.noShadows || lightDef->parms.noDynamicShadows ) {
		return false;
	}
	if ( lightDef->lightShader == NULL || lightDef->lightShader->IsAmbientLight() || !lightDef->lightShader->LightCastsShadows() ) {
		return false;
	}
	if ( glConfig.maxTextureUnits < 6 || glConfig.maxTextureImageUnits < 6 ) {
		return false;
	}
	// parallel lights carry pointLight=true but render through the projected
	// path with a synthesized orthographic projection (see shadow classification)
	const bool pointLightPath = lightDef->parms.pointLight && !lightDef->parms.parallel;
	if ( pointLightPath ) {
		if ( !r_shadowMapPointLights.GetBool() || !glConfig.cubeMapAvailable ) {
			return false;
		}
	}
	return RB_ShadowMapResourcesKnownGood( pointLightPath );
}

/*
=================
R_AddOptimizedPrelightShadows
=================
*/
static void R_AddOptimizedPrelightShadows( viewLight_t *vLight ) {
	// Mapped lights retain per-surface volumes so a partial filtered map can
	// stamp only its genuinely missing casters. The combined prelight remains
	// available for stencil-only lights.
	if ( vLight != NULL &&
		R_ShadowMapsNeedPerSurfaceStencilVolumes(
			vLight->lightDef ) ) {
		return;
	}

	idRenderModel *prelightModel = R_ViewLightPrelightModel( vLight );
	if ( prelightModel == NULL ) {
		// A cached interaction may still remember that its per-surface volume
		// was replaced by this prelight. If the model became unavailable, its
		// non-mapped stock casters make mapped ownership incomplete.
		vLight->shadowMapIncompleteMapMask |=
			vLight->shadowMapPrelightMapMissingMask;
		return;
	}

	// prelight volumes are stencil-path inputs; a shadow-mapped light never
	// draws them, and they remain available on the next frame if the backend
	// flags a fallback
	if ( R_ShadowMapLightWillUseShadowMaps( vLight->lightDef ) ) {
		return;
	}

	idRenderLightLocal *light = vLight->lightDef;
	if ( !R_IsUsablePrelightModel( prelightModel ) ) {
		common->DWarning( "R_AddOptimizedPrelightShadows: discarding stale prelight model pointer for lightDef %d", light->index );
		light->parms.prelightModel = NULL;
		vLight->shadowMapIncompleteMapMask |=
			vLight->shadowMapPrelightMapMissingMask;
		return;
	}
	light->parms.prelightModel = prelightModel;

	if ( !prelightModel->NumSurfaces() ) {
		common->Error( "no surfs in prelight model '%s'", prelightModel->Name() );
	}

	const modelSurface_t *surface = prelightModel->Surface( 0 );
	if ( surface == NULL || surface->geometry == NULL ) {
		common->Error( "R_AddLightSurfaces: prelight model '%s' without geometry", prelightModel->Name() );
	}

	srfTriangles_t	*tri = surface->geometry;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( !tri->shadowVertexes && tri->primBatchMesh == NULL ) {
#else
	if ( !tri->shadowVertexes ) {
#endif
		common->Error( "R_AddLightSurfaces: prelight model '%s' without shadowVertexes", prelightModel->Name() );
	}

	// These shadows will all have valid bounds, and can be culled normally.
	if ( r_useShadowCulling.GetBool() ) {
		if ( R_CullLocalBox( tri->bounds, tr.viewDef->worldSpace.modelMatrix, 5, tr.viewDef->frustum ) ) {
			// The combined volume proves every pending prelight caster
			// irrelevant to this view, so remove rather than satisfy the
			// full-chain obligation. Ready bits only describe linked volumes.
			vLight->shadowMapPrelightStencilRequiredMask = 0;
			vLight->shadowMapPrelightMapMissingMask = 0;
			return;
		}
	}

	// Classic prelight shadows upload explicit shadow verts/indexes here.
	// Packed MD5R prelight meshes keep those resources inside the prim-batch
	// representation and bypass the legacy cache path.
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->primBatchMesh == NULL ) {
#endif
		if ( !tri->shadowCache ) {
			R_CreatePrivateShadowCache( tri );
			if ( !tri->shadowCache ) {
				vLight->shadowMapIncompleteMapMask |=
					vLight->shadowMapPrelightMapMissingMask;
				return;
			}
		}

		R_TouchVertexCache( tri->shadowCache );

		if ( !tri->indexCache && r_useIndexBuffers.GetBool() && tri->numIndexes > 0 ) {
			vertexCache.Alloc( tri->indexes, tri->numIndexes * sizeof( tri->indexes[0] ), &tri->indexCache, true );
		}
		if ( tri->indexCache ) {
			R_TouchVertexCache( tri->indexCache );
		}
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	}
#endif

	// dmap combines every static-world caster into one prelight volume.  It
	// cannot preserve per-material no-self ownership, so retain the retail
	// global-shadow routing: the combined volume must protect both LOCAL and
	// GLOBAL receivers.  Static surfaces that genuinely need no-self behavior
	// remain entities and therefore use their separate local volume.
	const bool linkedPrelightVolume = R_LinkLightSurf(
		&vLight->globalShadows,
		tri, NULL, light, NULL, vLight->scissorRect, true /* FIXME? */ );
	vLight->shadowMapIncompleteMapMask |=
		vLight->shadowMapPrelightMapMissingMask;
	if ( linkedPrelightVolume ) {
		vLight->shadowMapPrelightStencilReadyMask |=
			vLight->shadowMapPrelightStencilRequiredMask &
			( SHADOWMAP_RECEIVER_MASK_LOCAL |
				SHADOWMAP_RECEIVER_MASK_GLOBAL );
	}
}

/*
=================
R_AddLightSurfaces

Calc the light shader values, removing any light from the viewLight list
if it is determined to not have any visible effect due to being flashed off or turned off.

Adds entities to the viewEntity list if they are needed for shadow casting.

Add any precomputed shadow volumes.

Removes lights from the viewLights list if they are completely
turned off, or completely off screen.

Create any new interactions needed between the viewLights
and the viewEntitys due to game movement
=================
*/
void R_AddLightSurfaces( void ) {
	viewLight_t		*vLight;
	idRenderLightLocal *light;
	viewLight_t		**ptr;

	// go through each visible light, possibly removing some from the list
	ptr = &tr.viewDef->viewLights;
	while ( *ptr ) {
		vLight = *ptr;
		light = vLight->lightDef;

		const idMaterial	*lightShader = light->lightShader;
		if ( !lightShader ) {
			common->Error( "R_AddLightSurfaces: NULL lightShader" );
		}
		const bool authoredSpecularProbe = lightShader->HasSpecularProbe();
		const bool specularProbeRequested = authoredSpecularProbe
			&& r_rendererModernQuality.GetBool()
			&& r_rendererReflectionProbes.GetBool()
			&& vLight->pointLight
			&& !vLight->parallel;
		// A namespaced probe light is metadata, never an additive classic light.
		// When its leaf/master gate or supported point-volume contract is absent,
		// remove it exactly as an inactive light.  When admitted, retain the
		// visibility/scissor record for clustered probe collection below but do
		// not create interactions or shadows for it.
		if ( authoredSpecularProbe && !specularProbeRequested ) {
			*ptr = vLight->next;
			light->viewCount = -1;
			continue;
		}

		// see if we are suppressing the light in this view
		if ( R_ShouldSuppressViewLightForLevelshot( tr.viewDef->renderView.viewID, light->parms.allowLightInViewID ) ) {
			*ptr = vLight->next;
			light->viewCount = -1;
			continue;
		}

		if ( !r_skipSuppress.GetBool() ) {
			if ( light->parms.suppressLightInViewID
			&& light->parms.suppressLightInViewID == tr.viewDef->renderView.viewID ) {
				*ptr = vLight->next;
				light->viewCount = -1;
				continue;
			}
			if ( light->parms.allowLightInViewID 
			&& light->parms.allowLightInViewID != tr.viewDef->renderView.viewID ) {
				*ptr = vLight->next;
				light->viewCount = -1;
				continue;
			}
		}

		// evaluate the light shader registers
		float *lightRegs =(float *)R_FrameAlloc( lightShader->GetNumRegisters() * sizeof( float ) );
		idSoundEmitter *lightSoundEmitter = R_GetShaderSoundEmitter( light->parms.referenceSoundHandle );
		vLight->shaderRegisters = lightRegs;
		lightShader->EvaluateRegisters( lightRegs, light->parms.shaderParms, tr.viewDef, lightSoundEmitter );

		// if this is a purely additive light and no stage in the light shader evaluates
		// to a positive light value, we can completely skip the light
		if ( !specularProbeRequested && !lightShader->IsFogLight() && !lightShader->IsBlendLight() ) {
			int lightStageNum;
			const int lightStageCount = lightShader->GetNumStages();
			for ( lightStageNum = 0 ; lightStageNum < lightStageCount ; lightStageNum++ ) {
				const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

				// ignore stages that fail the condition
				if ( !lightRegs[ lightStage->conditionRegister ] ) {
					continue;
				}

				const int *registers = lightStage->color.registers;

				// snap tiny values to zero to avoid lights showing up with the wrong color
				if ( lightRegs[ registers[0] ] < 0.001f ) {
					lightRegs[ registers[0] ] = 0.0f;
				}
				if ( lightRegs[ registers[1] ] < 0.001f ) {
					lightRegs[ registers[1] ] = 0.0f;
				}
				if ( lightRegs[ registers[2] ] < 0.001f ) {
					lightRegs[ registers[2] ] = 0.0f;
				}

				// FIXME:	when using the following values the light shows up bright red when using nvidia drivers/hardware
				//			this seems to have been fixed ?
				//lightRegs[ registers[0] ] = 1.5143074e-005f;
				//lightRegs[ registers[1] ] = 1.5483369e-005f;
				//lightRegs[ registers[2] ] = 1.7014690e-005f;

				if ( lightRegs[ registers[0] ] > 0.0f ||
						lightRegs[ registers[1] ] > 0.0f ||
							lightRegs[ registers[2] ] > 0.0f ) {
					break;
				}
			}
			if ( lightStageNum == lightStageCount ) {
				// we went through all the stages and didn't find one that adds anything
				// remove the light from the viewLights list, and change its frame marker
				// so interaction generation doesn't think the light is visible and
				// create a shadow for it
				*ptr = vLight->next;
				light->viewCount = -1;
				continue;
			}
		}

		if ( r_useLightScissors.GetBool() ) {
			// calculate the screen area covered by the light frustum
			// which will be used to crop the stencil cull
			idScreenRect scissorRect = R_CalcLightScissorRectangle( vLight );
			// intersect with the portal crossing scissor rectangle
			vLight->scissorRect.Intersect( scissorRect );

			if ( r_showLightScissors.GetBool() ) {
				R_ShowColoredScreenRect( vLight->scissorRect, light->index );
			}
		}

#if 0
		// this never happens, because CullLightByPortals() does a more precise job
		if ( vLight->scissorRect.IsEmpty() ) {
			// this light doesn't touch anything on screen, so remove it from the list
			*ptr = vLight->next;
			continue;
		}
#endif

		// this one stays on the list
		ptr = &vLight->next;
		if ( specularProbeRequested ) {
			tr.pc.c_viewLights++;
			continue;
		}

		// if we are doing a soft-shadow novelty test, regenerate the light with
		// a random offset every time
		if ( r_lightSourceRadius.GetFloat() != 0.0f ) {
			for ( int i = 0 ; i < 3 ; i++ ) {
				light->globalLightOrigin[i] += r_lightSourceRadius.GetFloat() * ( -1 + 2 * (rand()&0xfff)/(float)0xfff );
			}
		}

		// create interactions with all entities the light may touch, and add viewEntities
		// that may cast shadows, even if they aren't directly visible.  Any real work
		// will be deferred until we walk through the viewEntities
		tr.viewDef->renderWorld->CreateLightDefInteractions( light );
		tr.pc.c_viewLights++;

		// fog lights will need to draw the light frustum triangles, so make sure they
		// are in the vertex cache
		if ( lightShader->IsFogLight() ) {
			if ( !light->frustumTris->ambientCache ) {
				if ( !R_CreateAmbientCache( light->frustumTris, false ) ) {
					// skip if we are out of vertex memory
					continue;
				}
			}
			// touch the surface so it won't get purged
			R_TouchVertexCache( light->frustumTris->ambientCache );
		}

		// Optimized prelight shadows are added after model interactions have
		// established whether any static-world no-self receivers need local routing.
	}
}

//===============================================================================================================

/*
==================
R_IssueEntityDefCallback
==================
*/
bool R_IssueEntityDefCallback( idRenderEntityLocal *def ) {
	bool update;
	idBounds	oldBounds;

	if ( r_checkBounds.GetBool() ) {
		oldBounds = def->referenceBounds;
	}

	def->archived = false;		// will need to be written to the demo file
	tr.pc.c_entityDefCallbacks++;
	if ( tr.viewDef ) {
		update = def->parms.callback( &def->parms, &tr.viewDef->renderView );
	} else {
		update = def->parms.callback( &def->parms, NULL );
	}

	if ( !def->parms.hModel ) {
		common->Error( "R_IssueEntityDefCallback: dynamic entity callback didn't set model" );
	}

	if ( r_checkBounds.GetBool() ) {
		if (	oldBounds[0][0] > def->referenceBounds[0][0] + CHECK_BOUNDS_EPSILON ||
				oldBounds[0][1] > def->referenceBounds[0][1] + CHECK_BOUNDS_EPSILON ||
				oldBounds[0][2] > def->referenceBounds[0][2] + CHECK_BOUNDS_EPSILON ||
				oldBounds[1][0] < def->referenceBounds[1][0] - CHECK_BOUNDS_EPSILON ||
				oldBounds[1][1] < def->referenceBounds[1][1] - CHECK_BOUNDS_EPSILON ||
				oldBounds[1][2] < def->referenceBounds[1][2] - CHECK_BOUNDS_EPSILON ) {
			common->Printf( "entity %i callback extended reference bounds\n", def->index );
		}
	}

	return update;
}

/*
===================
R_HashJointMatrices

FNV-1a over the joint matrices as 64-bit words. The game owns and mutates the
joint array in place, so pointer identity alone cannot prove a skinned
snapshot is still valid; this content hash can.
===================
*/
unsigned long long R_HashJointMatrices( const idJointMat *joints, int numJoints ) {
	const unsigned long long fnvOffset = 14695981039346656037ULL;
	const unsigned long long fnvPrime = 1099511628211ULL;

	unsigned long long hash = fnvOffset;
	const int totalBytes = numJoints * static_cast<int>( sizeof( joints[0] ) );
	const int wordCount = totalBytes / static_cast<int>( sizeof( unsigned long long ) );
	static_assert( sizeof( idJointMat ) % sizeof( unsigned long long ) == 0,
		"joint matrix hashing expects complete 64-bit words" );
	const byte *bytes = reinterpret_cast<const byte *>( joints );
	for ( int i = 0; i < wordCount; ++i ) {
		unsigned long long word;
		memcpy( &word, bytes + i * sizeof( word ), sizeof( word ) );
		hash ^= word;
		hash *= fnvPrime;
	}
	return hash;
}

/*
===================
R_EntityDefDynamicModel

Issues a deferred entity callback if necessary.
If the model isn't dynamic, it returns the original.
Returns the cached dynamic model if present, otherwise creates
it and any necessary overlays
===================
*/
idRenderModel *R_EntityDefDynamicModel( idRenderEntityLocal *def, bool collisionOnly ) {
	bool callbackUpdate;

	// allow deferred entities to construct themselves
	if ( def->parms.callback ) {
		callbackUpdate = R_IssueEntityDefCallback( def );
	} else {
		callbackUpdate = false;
	}

	idRenderModel *model = def->parms.hModel;

	if ( !model ) {
		common->Error( "R_EntityDefDynamicModel: NULL model" );
	}

	model->SetViewEntity( def->viewEntity );

	if ( model->IsDynamicModel() == DM_STATIC ) {
		def->dynamicModel = NULL;
		def->dynamicModelFrameCount = 0;
		def->dynamicCollisionModel = NULL;
		return model;
	}

	// continously animating models (particle systems, etc) will have their snapshot updated every single view
	if ( callbackUpdate || ( model->IsDynamicModel() == DM_CONTINUOUS && def->dynamicModelFrameCount != tr.frameCount ) ) {
		R_ClearEntityDefDynamicModel( def );
	}

	if ( collisionOnly && model->HasCollisionSurface( &def->parms ) ) {
		if ( !def->dynamicCollisionModel ) {
			def->cachedDynamicCollisionModel = model->InstantiateDynamicModel( &def->parms, tr.viewDef,
				def->cachedDynamicCollisionModel, SURF_COLLISION );
			def->dynamicCollisionModel = def->cachedDynamicCollisionModel;
		}
		return def->dynamicCollisionModel;
	}

	// if we don't have a snapshot of the dynamic model, generate it now
	if ( !def->dynamicModel ) {

		// instantiate the snapshot of the dynamic model, possibly reusing memory from the cached snapshot
		def->cachedDynamicModel = model->InstantiateDynamicModel( &def->parms, tr.viewDef, def->cachedDynamicModel,
			static_cast<dword>( ~SURF_COLLISION ) );

		if ( def->cachedDynamicModel ) {

			// add any overlays to the snapshot of the dynamic model
			if ( def->overlay && !r_skipOverlays.GetBool() ) {
				def->overlay->AddOverlaySurfacesToModel( def->cachedDynamicModel );
			} else {
				idRenderModelOverlay::RemoveOverlaySurfacesFromModel( def->cachedDynamicModel );
			}

			if ( r_checkBounds.GetBool() ) {
				idBounds b = def->cachedDynamicModel->Bounds();
				if (	b[0][0] < def->referenceBounds[0][0] - CHECK_BOUNDS_EPSILON ||
						b[0][1] < def->referenceBounds[0][1] - CHECK_BOUNDS_EPSILON ||
						b[0][2] < def->referenceBounds[0][2] - CHECK_BOUNDS_EPSILON ||
						b[1][0] > def->referenceBounds[1][0] + CHECK_BOUNDS_EPSILON ||
						b[1][1] > def->referenceBounds[1][1] + CHECK_BOUNDS_EPSILON ||
						b[1][2] > def->referenceBounds[1][2] + CHECK_BOUNDS_EPSILON ) {
					common->Printf( "entity %i dynamic model exceeded reference bounds\n", def->index );
				}
			}
		}

		def->dynamicModel = def->cachedDynamicModel;
		def->dynamicModelFrameCount = tr.frameCount;

		// record the joint-content hash this snapshot was built from so a later
		// transform-only UpdateEntityDef can prove the snapshot is still valid
		if ( r_useRepeatedStateReuse.GetBool() && def->dynamicModel != NULL
			&& def->parms.joints != NULL && def->parms.numJoints > 0 ) {
			def->dynamicModelJointsHash = R_HashJointMatrices( def->parms.joints, def->parms.numJoints );
			def->dynamicModelJointsHashValid = true;
		} else {
			def->dynamicModelJointsHashValid = false;
		}
	}

	// set model depth hack value
	if ( def->dynamicModel && model->DepthHack() != 0.0f && tr.viewDef ) {
		idPlane eye, clip;
		idVec3 ndc;
		R_TransformModelToClip( def->parms.origin, tr.viewDef->worldSpace.modelViewMatrix, tr.viewDef->projectionMatrix, eye, clip );
		R_TransformClipToDevice( clip, tr.viewDef, ndc );
		def->parms.modelDepthHack = model->DepthHack() * ( 1.0f - ndc.z );
	}

	// FIXME: if any of the surfaces have deforms, create a frame-temporary model with references to the
	// undeformed surfaces.  This would allow deforms to be light interacting.

	return def->dynamicModel;
}

/*
=================
R_SetupDrawSurfShaderRegisters
=================
*/
const float *R_SetupDrawSurfShaderRegisters( const viewEntity_t *space, const renderEntity_t *renderEntity,
		const idMaterial *shader ) {
	static float refRegs[MAX_EXPRESSION_REGISTERS];	// don't put on stack, or VC++ will do a page touch
	static const float defaultShaderParms[MAX_ENTITY_SHADER_PARMS] = { 0.0f };
	const float *shaderParms = defaultShaderParms;
	float generatedShaderParms[MAX_ENTITY_SHADER_PARMS];
	idSoundEmitter *soundEmitter = NULL;

	if ( shader == NULL ) {
		return NULL;
	}

	const float *constRegs = shader->ConstantRegisters();
	if ( constRegs != NULL ) {
		return constRegs;
	}

	// within one view, every evaluation for the same (entity, material) pair
	// sees identical inputs (entity shaderParms, view time, sound emitter), so
	// the ambient-pass result can be shared with the per-light R_LinkLightSurf
	// evaluations (id's original "FIXME: share with the ambient surface?").
	// Only entity-owned parms are eligible: BSE effects pass stack-local
	// renderEntity_t copies, and referenceShader rewrites the parms.
	viewEntity_t *memoSpace = NULL;
	if ( r_useRedundantStateFiltering.GetBool()
			&& space != NULL && space->entityDef != NULL
			&& ( renderEntity == NULL || renderEntity == &space->entityDef->parms )
			&& space->entityDef->parms.referenceShader == NULL ) {
		memoSpace = const_cast<viewEntity_t *>( space );
		for ( int i = 0; i < memoSpace->numShaderRegisterMemos; i++ ) {
			if ( memoSpace->shaderRegisterMemoMaterials[i] == shader ) {
				return memoSpace->shaderRegisterMemoRegs[i];
			}
		}
	}

	float *regs = (float *)R_FrameAlloc( shader->GetNumRegisters() * sizeof( float ) );

	if ( renderEntity != NULL ) {
		shaderParms = renderEntity->shaderParms;
		soundEmitter = R_GetShaderSoundEmitter( renderEntity->referenceSoundHandle );
	} else if ( space != NULL && space->entityDef != NULL ) {
		shaderParms = space->entityDef->parms.shaderParms;
		soundEmitter = R_GetShaderSoundEmitter( space->entityDef->parms.referenceSoundHandle );
	}

	// GetStage(0) below returns &stages[0], which is NULL-based when the material
	// has no stages (idMaterial only allocates the stage array when numStages>0),
	// so require at least one stage before reading pStage->color.registers.
	if ( renderEntity != NULL && renderEntity->referenceShader != NULL
			&& renderEntity->referenceShader->GetNumStages() > 0 ) {
		const shaderStage_t *pStage;

		renderEntity->referenceShader->EvaluateRegisters( refRegs, renderEntity->shaderParms, tr.viewDef, soundEmitter );
		pStage = renderEntity->referenceShader->GetStage( 0 );

		memcpy( generatedShaderParms, renderEntity->shaderParms, sizeof( generatedShaderParms ) );
		generatedShaderParms[0] = refRegs[ pStage->color.registers[0] ];
		generatedShaderParms[1] = refRegs[ pStage->color.registers[1] ];
		generatedShaderParms[2] = refRegs[ pStage->color.registers[2] ];
		shaderParms = generatedShaderParms;
	}

	shader->EvaluateRegisters( regs, shaderParms, tr.viewDef, soundEmitter );

	if ( memoSpace != NULL
			&& memoSpace->numShaderRegisterMemos < (int)( sizeof( memoSpace->shaderRegisterMemoMaterials ) / sizeof( memoSpace->shaderRegisterMemoMaterials[0] ) ) ) {
		memoSpace->shaderRegisterMemoMaterials[memoSpace->numShaderRegisterMemos] = shader;
		memoSpace->shaderRegisterMemoRegs[memoSpace->numShaderRegisterMemos] = regs;
		memoSpace->numShaderRegisterMemos++;
	}
	return regs;
}

/*
=================
R_FinalizeDrawSurf
=================
*/
void R_FinalizeDrawSurf( drawSurf_t *drawSurf ) {
	if ( drawSurf == NULL || drawSurf->material == NULL ) {
		return;
	}

	R_ClassicDeformDomain_BeginDrawSurf( drawSurf );
	R_DeformDrawSurf( drawSurf );
	R_ClassicDeformDomain_EndDrawSurf( drawSurf );

	switch( drawSurf->material->Texgen() ) {
		case TG_SKYBOX_CUBE:
			R_SkyboxTexGen( drawSurf, tr.viewDef->renderView.vieworg );
			break;
		case TG_WOBBLESKY_CUBE:
			R_WobbleskyTexGen( drawSurf, tr.viewDef->renderView.vieworg );
			break;
		case TG_POT_CORRECTION:
			R_POTCorrectionTexGen( drawSurf );
			break;
	}
}

/*
=================
R_AddDrawSurf
=================
*/
void R_AddDrawSurf( const srfTriangles_t *tri, const viewEntity_t *space, const renderEntity_t *renderEntity,
					const idMaterial *shader, const idScreenRect &scissor, int extraDrawSurfFlags ) {
	drawSurf_t		*drawSurf;

	drawSurf = (drawSurf_t *)R_FrameAlloc( sizeof( *drawSurf ) );
	drawSurf->geo = tri;
	drawSurf->space = space;
	drawSurf->material = shader;
	drawSurf->scissorRect = scissor;
	drawSurf->sort = shader->GetSort() + tr.sortOffset;
	drawSurf->area = NULL;
	drawSurf->dsFlags = extraDrawSurfFlags;
	if ( tr.inWorldGuiEmissionDepth > 0 ) {
		drawSurf->dsFlags |= DSF_IN_WORLD_GUI;
	}
	drawSurf->dynamicTexCoords = NULL;
	drawSurf->texGenTransformAndViewOrg = NULL;
	drawSurf->decalColorCache = NULL;
	drawSurf->decalColorOffset = 0;
	drawSurf->decalColorStride = 0;
	drawSurf->decalColorStageCount = 0;

	// bumping this offset each time causes surfaces with equal sort orders to still
	// deterministically draw in the order they are added
	tr.sortOffset += 0.000001f;

	// if it doesn't fit, resize the list
	if ( tr.viewDef->numDrawSurfs == tr.viewDef->maxDrawSurfs ) {
		drawSurf_t	**old = tr.viewDef->drawSurfs;
		int			count;

		if ( tr.viewDef->maxDrawSurfs == 0 ) {
			tr.viewDef->maxDrawSurfs = INITIAL_DRAWSURFS;
			count = 0;
		} else {
			count = tr.viewDef->maxDrawSurfs * sizeof( tr.viewDef->drawSurfs[0] );
			tr.viewDef->maxDrawSurfs *= 2;
		}
		tr.viewDef->drawSurfs = (drawSurf_t **)R_FrameAlloc( tr.viewDef->maxDrawSurfs * sizeof( tr.viewDef->drawSurfs[0] ) );
		if ( count > 0 ) {
			memcpy( tr.viewDef->drawSurfs, old, count );
		}
	}
	tr.viewDef->drawSurfs[tr.viewDef->numDrawSurfs] = drawSurf;
	tr.viewDef->numDrawSurfs++;

	// Keep shadow-caster and main draw-surf material evaluation on the same code path.
	drawSurf->shaderRegisters = R_SetupDrawSurfShaderRegisters( space, renderEntity, shader );
	drawSurf->area = tr.viewDef->skipDrawSurfAreaResolve ? R_FallbackDrawSurfArea( space ) : R_ResolveDrawSurfArea( tri, space );

	R_FinalizeDrawSurf( drawSurf );

	// check for gui surfaces
	idUserInterface	*gui = NULL;

	if ( !space->entityDef ) {
		gui = shader->GlobalGui();
	} else {
		int guiNum = shader->GetEntityGui() - 1;
		if ( guiNum >= 0 && guiNum < MAX_RENDERENTITY_GUI ) {
			gui = renderEntity->gui[ guiNum ];
		}
		if ( gui == NULL ) {
			gui = shader->GlobalGui();
		}
	}

	if ( gui ) {
		// force guis on the fast time
		float oldFloatTime;
		int oldTime;

		oldFloatTime = tr.viewDef->floatTime;
		oldTime = tr.viewDef->renderView.time;
// jmarshall - gui time
		//tr.viewDef->floatTime = game->GetTimeGroupTime( 1 ) * 0.001;
		//tr.viewDef->renderView.time = game->GetTimeGroupTime( 1 );
// jmarshall end

		idBounds ndcBounds;

		if ( !R_PreciseCullSurface( drawSurf, ndcBounds ) ) {
			// did we ever use this to forward an entity color to a gui that didn't set color?
//			memcpy( tr.guiShaderParms, shaderParms, sizeof( tr.guiShaderParms ) );
			R_RenderGuiSurf( gui, drawSurf );
		}

		tr.viewDef->floatTime = oldFloatTime;
		tr.viewDef->renderView.time = oldTime;
	}

	// we can't add subviews at this point, because that would
	// increment tr.viewCount, messing up the rest of the surface
	// adds for this view
}

/*
============================
R_ShouldUseExpandedDeformScissor

Retail flare/sprite-style deforms can expand far beyond the authored source
quad, so clipping them to the entity's projected reference bounds boxes the
effect back into a visibly wrong rectangle.
============================
*/
static bool R_ShouldUseExpandedDeformScissor( const idMaterial *shader ) {
	if ( shader == NULL ) {
		return false;
	}

	switch ( shader->Deform() ) {
		case DFRM_SPRITE:
		case DFRM_RECTSPRITE:
		case DFRM_TUBE:
		case DFRM_FLARE:
			return true;
		default:
			return false;
	}
}

/*
===============
R_AddAmbientDrawsurfs

Adds surfaces for the given viewEntity
Walks through the viewEntitys list and creates drawSurf_t for each surface of
each viewEntity that has a non-empty scissorRect
===============
*/
static void R_AddAmbientDrawsurfs( viewEntity_t *vEntity ) {
	int					i, total;
	idRenderEntityLocal	*def;
	srfTriangles_t		*tri;
	idRenderModel		*model;
	const idMaterial	*shader;

	def = vEntity->entityDef;

	if ( def->dynamicModel ) {
		model = def->dynamicModel;
	} else {
		model = def->parms.hModel;
	}

	// add all the surfaces
	total = model->NumSurfaces();
	for ( i = 0 ; i < total ; i++ ) {
		const modelSurface_t	*surf = model->Surface( i );

		if ( surf->id >= 0 && surf->id < static_cast<int>( sizeof( unsigned int ) * 8 )
			&& ( static_cast<unsigned int>( def->parms.suppressSurfaceMask ) & ( 1u << surf->id ) ) != 0 ) {
			continue;
		}

		// for debugging, only show a single surface at a time
		if ( r_singleSurface.GetInteger() >= 0 && i != r_singleSurface.GetInteger() ) {
			continue;
		}

		tri = surf->geometry;
		if ( !tri ) {
			continue;
		}
		if ( !tri->numIndexes ) {
			continue;
		}
		shader = surf->shader;
		shader = R_RemapShaderBySkin( shader, def->parms.customSkin, def->parms.customShader );

		R_GlobalShaderOverride( &shader );

		if ( !shader ) {	
			continue;
		}
		if ( !shader->IsDrawn() ) {
			continue;
		}

		// debugging tool to make sure we are have the correct pre-calculated bounds
		if ( r_checkBounds.GetBool() && tri->verts != NULL ) {
			int j, k;
			for ( j = 0 ; j < tri->numVerts ; j++ ) {
				for ( k = 0 ; k < 3 ; k++ ) {
					if ( tri->verts[j].xyz[k] > tri->bounds[1][k] + CHECK_BOUNDS_EPSILON
						|| tri->verts[j].xyz[k] < tri->bounds[0][k] - CHECK_BOUNDS_EPSILON ) {
						common->Printf( "bad tri->bounds on %s:%s\n", def->parms.hModel->Name(), shader->GetName() );
						break;
					}
					if ( tri->verts[j].xyz[k] > def->referenceBounds[1][k] + CHECK_BOUNDS_EPSILON
						|| tri->verts[j].xyz[k] < def->referenceBounds[0][k] - CHECK_BOUNDS_EPSILON ) {
						common->Printf( "bad referenceBounds on %s:%s\n", def->parms.hModel->Name(), shader->GetName() );
						break;
					}
				}
				if ( k != 3 ) {
					break;
				}
			}
		}

		if ( R_ShouldDisableEntityCullingForLevelshot() || !R_CullLocalBox( tri->bounds, vEntity->modelMatrix, 5, tr.viewDef->frustum ) ) {

			def->visibleCount = tr.viewCount;
			const idScreenRect &surfaceScissor = R_ShouldUseExpandedDeformScissor( shader ) ? tr.viewDef->scissor : vEntity->scissorRect;

			const bool packedSurface = R_TriHasPrimBatchMesh( tri );
			if ( !packedSurface && tri->verts == NULL ) {
				continue;
			}
			if ( packedSurface ) {
				if ( !R_CreatePackedSurfaceFrameCaches( tri, shader->ReceivesLighting(), true ) ) {
					// packed MD5R surfaces still need a transient draw view in the hybrid backend
					return;
				}
			} else {
				if ( !R_CreateAmbientCache( tri, shader->ReceivesLighting() ) ) {
					// don't add anything if the vertex cache was too full to give us an ambient cache
					return;
				}

				if ( !tri->indexCache && tri->indexes != NULL && tri->numIndexes > 0 && R_StaticIndexCacheAllowed( def ) ) {
					vertexCache.Alloc( tri->indexes, tri->numIndexes * sizeof( tri->indexes[0] ), &tri->indexCache, true );
				}
			}

			R_TouchVertexCache( tri->ambientCache );
			if ( tri->indexCache ) {
				R_TouchVertexCache( tri->indexCache );
			}

			// add the surface for drawing
			R_AddDrawSurf( tri, vEntity, &vEntity->entityDef->parms, shader, surfaceScissor );

			// powerup shells/overlays render as a second pass on top of the base surface.
			const idMaterial *overlayShader = def->parms.overlayShader;
			// Respect material overlay policy so translucent / gui-like surfaces don't get shell bleed.
			if ( overlayShader != NULL && !shader->HasGui() && shader->AllowOverlays() ) {
				R_GlobalShaderOverride( &overlayShader );
				if ( overlayShader != NULL && overlayShader->IsDrawn() ) {
					R_AddDrawSurf( tri, vEntity, &vEntity->entityDef->parms, overlayShader, surfaceScissor );
				}
			}

			// ambientViewCount is used to allow light interactions to be rejected
			// if the ambient surface isn't visible at all
			tri->ambientViewCount = tr.viewCount;
		}
	}

	// add the lightweight decal surfaces
	for ( idRenderModelDecal *decal = def->decals; decal; decal = decal->Next() ) {
		decal->AddDecalDrawSurf( vEntity );
	}
}

/*
==================
R_CalcEntityScissorRectangle
==================
*/
idScreenRect R_CalcEntityScissorRectangle( viewEntity_t *vEntity ) {
	idBounds bounds;
	idRenderEntityLocal *def = vEntity->entityDef;

	// ProjectionBounds can return TRUE having added nothing at all, leaving the
	// bounds exactly as it Clear()ed them -- +/-idMath::INFINITY, i.e. 1e30.
	// Its "if ( !outside ) return true" path (Frustum.cpp) means "no corner sat
	// outside a side plane", but corners are only ADDED under
	// "cull1 == 0 && start.x > 0.0f", so a box at or behind the eye plane
	// contributes no points while leaving outside == 0.
	//
	// So the return value is not the test; IsCleared() is. This is the same
	// guard idInteraction::CalcInteractionScissorRectangle already applies to
	// its own ClippedProjectionBounds result (Interaction.cpp), which is why
	// the light path never showed this and the entity path did.
	//
	// Measured on Android/Adreno 750, game/airdefense1: the cleared bounds
	// reached R_ScreenRectFromViewFrustumBounds and became a garbage rect that
	// IsEmpty() did not reject, so the entity drew clipped to a few pixels
	// instead of keeping its portal rect. Nothing clears colour, so everything
	// it stopped covering kept the previous frame -- the frozen screen with one
	// small live box. r_useEntityScissors 0 was the bisect that found it.
	//
	// Clear() first so IsCleared() is still meaningful on the early-out path,
	// which writes every component before this ever runs.
	bounds.Clear();
	tr.viewDef->viewFrustum.ProjectionBounds(
			idBox( def->referenceBounds, def->parms.origin, def->parms.axis ), bounds );

	if ( bounds.IsCleared() ) {
		// the caller intersects this into vEntity->scissorRect, so handing back
		// what it already holds leaves the entity on the portal flood's rect
		return vEntity->scissorRect;
	}

	return R_ScreenRectFromViewFrustumBounds( bounds );
}

static bool R_EffectUsesViewModelDepth( const rvRenderEffectLocal *def ) {
	if ( def == NULL ) {
		return false;
	}
	return def->parms.weaponDepthHackInViewID != 0 || def->parms.allowSurfaceInViewID != 0;
}

static bool R_EffectPointAreaVisible( const idRenderWorldLocal *world, const idVec3 &point ) {
	if ( world == NULL || tr.viewDef == NULL || !r_usePortals.GetBool() ) {
		return true;
	}

	const int areaNum = world->PointInArea( point );
	if ( areaNum < 0 || areaNum >= world->NumAreas() ) {
		return true;
	}

	return world->portalAreas[ areaNum ].viewCount == tr.viewCount;
}

static bool R_EffectLocalBoundsUsable( const idBounds &bounds ) {
	if ( bounds.IsCleared() ) {
		return false;
	}

	for ( int axis = 0; axis < 3; ++axis ) {
		const float minValue = bounds[0][axis];
		const float maxValue = bounds[1][axis];
		if ( FLOAT_IS_NAN( minValue ) || FLOAT_IS_NAN( maxValue ) || minValue > maxValue ) {
			return false;
		}
	}

	return true;
}

static bool R_EffectCachedRenderBounds( const rvRenderEffectLocal *def, idBounds &bounds ) {
	if ( def == NULL || def->dynamicModel == NULL || def->dynamicModelFrameCount <= 0 ) {
		return false;
	}

	bool hasBounds = false;
	bounds.Clear();
	const int numSurfaces = def->dynamicModel->NumSurfaces();
	for ( int surfaceIndex = 0; surfaceIndex < numSurfaces; ++surfaceIndex ) {
		const modelSurface_t *surface = def->dynamicModel->Surface( surfaceIndex );
		if ( surface == NULL || surface->geometry == NULL ) {
			continue;
		}

		const srfTriangles_t *tri = surface->geometry;
		if ( tri->numVerts <= 0 || tri->numIndexes <= 0 || !R_EffectLocalBoundsUsable( tri->bounds ) ) {
			continue;
		}

		if ( !hasBounds ) {
			bounds = tri->bounds;
			hasBounds = true;
		} else {
			bounds.AddBounds( tri->bounds );
		}
	}

	return hasBounds;
}

static bool R_EffectBuildCullBounds( const rvRenderEffectLocal *def, idBounds &bounds, bool &hasCachedRenderBounds, bool includeCachedRenderBounds = true ) {
	hasCachedRenderBounds = false;
	bounds.Clear();

	if ( def == NULL ) {
		return false;
	}

	if ( R_EffectLocalBoundsUsable( def->referenceBounds ) ) {
		bounds = def->referenceBounds;
	}

	idBounds cachedRenderBounds;
	if ( includeCachedRenderBounds && R_EffectCachedRenderBounds( def, cachedRenderBounds ) ) {
		hasCachedRenderBounds = true;
		if ( bounds.IsCleared() ) {
			bounds = cachedRenderBounds;
		} else {
			bounds.AddBounds( cachedRenderBounds );
		}
	}

	return !bounds.IsCleared();
}

static bool R_EffectBoundsAreaVisible( const idRenderWorldLocal *world, const rvRenderEffectLocal *def, bool requireCachedRenderBounds ) {
	if ( world == NULL || def == NULL || !r_usePortals.GetBool() ) {
		return true;
	}

	idBounds cullBounds;
	bool hasCachedRenderBounds = false;
	if ( !R_EffectBuildCullBounds( def, cullBounds, hasCachedRenderBounds ) ) {
		return true;
	}
	if ( requireCachedRenderBounds && !hasCachedRenderBounds ) {
		return true;
	}

	idBounds worldBounds;
	worldBounds.FromTransformedBounds( cullBounds, def->parms.origin, def->parms.axis );
	worldBounds.ExpandSelf( Max( 0.0f, bse_frustumCullExpand.GetFloat() ) );
	for ( int axis = 0; axis < 3; ++axis ) {
		const float minValue = worldBounds[0][axis];
		const float maxValue = worldBounds[1][axis];
		const float span = maxValue - minValue;
		if ( FLOAT_IS_NAN( minValue ) || FLOAT_IS_NAN( maxValue ) || minValue > maxValue || span >= 1e4f ) {
			// BoundsInAreas is intentionally limited to normal portal-sized queries.
			// Area culling is only an optimization, so keep large sky/smoke effects visible.
			return true;
		}
	}

	int areas[32];
	const int numAreas = world->BoundsInAreas( worldBounds, areas, sizeof( areas ) / sizeof( areas[0] ) );
	if ( numAreas <= 0 ) {
		return true;
	}

	for ( int areaIndex = 0; areaIndex < numAreas; ++areaIndex ) {
		const int areaNum = areas[areaIndex];
		if ( areaNum >= 0 && areaNum < world->NumAreas() && world->portalAreas[areaNum].viewCount == tr.viewCount ) {
			return true;
		}
	}

	return false;
}

static bool R_CullEffectByArea( const idRenderWorldLocal *world, const rvRenderEffectLocal *def, bool requireCachedRenderBounds = false ) {
	if ( !bse_useAreaCull.GetBool() || R_EffectUsesViewModelDepth( def ) || R_IsPortalSkyView() ) {
		return false;
	}

	if ( R_EffectPointAreaVisible( world, def->parms.origin ) ) {
		return false;
	}

	if ( def->parms.hasEndOrigin && R_EffectPointAreaVisible( world, def->parms.endOrigin ) ) {
		return false;
	}

	return !R_EffectBoundsAreaVisible( world, def, requireCachedRenderBounds );
}

static bool R_CullEffectByFrustum( const rvRenderEffectLocal *def, bool requireCachedRenderBounds = false ) {
	if ( !bse_useFrustumCull.GetBool() || R_EffectUsesViewModelDepth( def ) || R_ShouldDisableEntityCullingForLevelshot() || R_IsPortalSkyView() ) {
		return false;
	}

	idBounds expandedBounds;
	bool hasCachedRenderBounds = false;
	const bool includeCachedRenderBounds = bse_useCachedFrustumCull.GetBool();
	if ( !R_EffectBuildCullBounds( def, expandedBounds, hasCachedRenderBounds, includeCachedRenderBounds ) ) {
		return false;
	}
	if ( requireCachedRenderBounds && includeCachedRenderBounds && !hasCachedRenderBounds ) {
		return false;
	}
	expandedBounds.ExpandSelf( Max( 0.0f, bse_frustumCullExpand.GetFloat() ) );

	float modelMatrix[16];
	R_AxisToModelMatrix( def->parms.axis, def->parms.origin, modelMatrix );
	return R_CullLocalBox( expandedBounds, modelMatrix, 5, tr.viewDef->frustum );
}

static bool R_ShouldRefreshCulledLoopService( const rvRenderEffectLocal *def ) {
	const int maxLagMsec = Max( 0, bse_culledLoopServiceMaxMsec.GetInteger() );
	const int refreshBudget = Max( 0, bse_culledLoopServiceRefreshBudget.GetInteger() );
	if ( def == NULL || maxLagMsec <= 0 || refreshBudget <= 0 || def->serviceTime < 0 ) {
		return false;
	}
	if ( def->gameTime - def->serviceTime < maxLagMsec ) {
		return false;
	}

	// Spread refresh work over several frames so stale culled loops cannot all
	// service on the same frame when a portal view changes.
	return ( ( tr.frameCount + def->index ) & 7 ) == 0;
}

/*
===============
R_AddEffectSurfaces
===============
*/
void R_AddEffectSurfaces(void) {
	idRenderWorldLocal* world = tr.viewDef->renderWorld;
	const int counterMode = bse_frameCounters.GetInteger();
	int totalDefs = 0;
	int spawned = 0;
	int serviced = 0;
	int expired = 0;
	int alive = 0;
	int renderedEffects = 0;
	int renderedSurfaces = 0;
	int effectsNoIndexedSurfaces = 0;
	int effectsNoIndexedButHasVerts = 0;
	int surfaceVisited = 0;
	int surfaceNoGeom = 0;
	int surfaceNoIndexes = 0;
	int surfaceNoIndexesWithVerts = 0;
	int surfaceFrustumCull = 0;
	int surfaceNoShader = 0;
	int surfaceNotDrawn = 0;
	int surfaceCacheFail = 0;
	int serviceSpawnGateTrue = 0;
	int serviceSpawnGateFalse = 0;
	int serviceGateLagMin = 0x7fffffff;
	int serviceGateLagMax = (-0x7fffffff - 1);
	int deferredService = 0;
	int refreshedCulledService = 0;
	const int refreshCulledLoopServiceBudget = Max( 0, bse_culledLoopServiceRefreshBudget.GetInteger() );
	int dropNotConnected = 0;
	int dropViewSuppress = 0;
	int dropAreaCull = 0;
	int dropDefFrustumCull = 0;
	int dropNoModel = 0;
	int dropNoModelSurfaces = 0;
	int dropClearedBounds = 0;
	int dropFrustumCull = 0;
	int dropScissor = 0;

	for (int i = 0; i < world->effectsDef.Num(); i++) {
		rvRenderEffectLocal* def = world->effectsDef[i];
		if (!def) {
			continue;
		}
		++totalDefs;
		if (def->newEffect) {
			++spawned;
		}
		const int gateLag = def->gameTime - def->serviceTime;
		if (gateLag < serviceGateLagMin) {
			serviceGateLagMin = gateLag;
		}
		if (gateLag > serviceGateLagMax) {
			serviceGateLagMax = gateLag;
		}
		if (gateLag > 0) {
			++serviceSpawnGateTrue;
		}
		else {
			++serviceSpawnGateFalse;
		}

		const bool canDeferLoopService =
			bse_skipCulledLoopService.GetBool() &&
			def->parms.loop &&
			def->effect != NULL &&
			!def->expired;
		bool refreshCulledLoopService = false;
		if ( canDeferLoopService ) {
			// Deferring service is only safe when a previous rendered model gives
			// the portal test real particle bounds instead of owner-origin guesses.
			if ( R_CullEffectByArea( world, def, true ) ) {
				if ( refreshedCulledService >= refreshCulledLoopServiceBudget || !R_ShouldRefreshCulledLoopService( def ) ) {
					++deferredService;
					++dropAreaCull;
					continue;
				}
				refreshCulledLoopService = true;
			}
		}

		if ( def->updateFramenum != tr.frameCount ) {
			// Effect servicing is view-independent. Run it once per rendered frame,
			// then let each view reuse the resulting particle state/model rebuild.
			++serviced;
			const float ownerTimeSeconds = static_cast<float>(def->gameTime) * 0.001f;
			const float presentationTimeSeconds = tr.viewDef->floatTime;
			def->updateFramenum = tr.frameCount;
			if (bse->ServiceEffect(def, ownerTimeSeconds, presentationTimeSeconds)) {
				++expired;
				if (def->dynamicModel) {
					delete def->dynamicModel;
					def->dynamicModel = NULL;
					def->dynamicModelFrameCount = 0;
				}
				continue;
			}
			if ( refreshCulledLoopService ) {
				++refreshedCulledService;
			}
		} else if ( def->expired ) {
			++expired;
			continue;
		}

		if ( refreshCulledLoopService && R_CullEffectByArea( world, def, true ) ) {
			++dropAreaCull;
			continue;
		}
		++alive;

		if (!def->effect) {
			continue;
		}

		// Some game-lib paths don't keep inConnectedArea up-to-date for renderEffect_t.
		// Keep this gate optional so view/world effects are not silently culled.
		if (bse_respectConnectedArea.GetBool()) {
			if (!def->parms.inConnectedArea &&
				def->parms.allowSurfaceInViewID == 0 &&
				def->parms.weaponDepthHackInViewID == 0) {
				++dropNotConnected;
				continue;
			}
		}

		if ( R_ShouldSuppressViewModelForLevelshot( tr.viewDef->renderView.viewID, def->parms.allowSurfaceInViewID, def->parms.weaponDepthHackInViewID ) ) {
			++dropViewSuppress;
			continue;
		}

		if (!r_skipSuppress.GetBool()) {
			if (def->parms.suppressSurfaceInViewID && def->parms.suppressSurfaceInViewID == tr.viewDef->renderView.viewID) {
				++dropViewSuppress;
				continue;
			}
			if (def->parms.allowSurfaceInViewID && def->parms.allowSurfaceInViewID != tr.viewDef->renderView.viewID) {
				++dropViewSuppress;
				continue;
			}
		}

		if ( !canDeferLoopService ) {
			// Pre-render culling is a broad-phase optimization. Require cached
			// rendered bounds so new or previously hidden effects get one chance
			// to build current geometry before any portal/PVS rejection.
			if ( R_CullEffectByArea( world, def, true ) ) {
				++dropAreaCull;
				continue;
			}

			if ( R_CullEffectByFrustum( def ) ) {
				++dropDefFrustumCull;
				continue;
			}
		}

		def->dynamicModel = bse->RenderEffect(def, tr.viewDef);
		if (!def->dynamicModel) {
			++dropNoModel;
			def->dynamicModelFrameCount = 0;
			continue;
		}
		def->dynamicModelFrameCount = tr.frameCount;

		idRenderModel* model = def->dynamicModel;
		if (model->NumSurfaces() <= 0) {
			++dropNoModelSurfaces;
			continue;
		}

		const idBounds localBounds = model->Bounds(NULL);
		if (localBounds.IsCleared()) {
			++dropClearedBounds;
			continue;
		}
		def->referenceBounds = localBounds;

		// Now that BSE has rebuilt the model for this view, run the authoritative
		// area test with actual particle/trail bounds.
		if ( R_CullEffectByArea( world, def ) ) {
			++dropAreaCull;
			continue;
		}

		viewEntity_t* vEffect = (viewEntity_t*)R_ClearedFrameAlloc(sizeof(*vEffect));
		vEffect->entityDef = NULL;
		vEffect->weaponDepthHack = (def->parms.weaponDepthHackInViewID != 0 && def->parms.weaponDepthHackInViewID == tr.viewDef->renderView.viewID);
		vEffect->modelDepthHack = def->parms.modelDepthHack;
		R_AxisToModelMatrix(def->parms.axis, def->parms.origin, vEffect->modelMatrix);
		myGlMultMatrix(vEffect->modelMatrix, tr.viewDef->worldSpace.modelViewMatrix, vEffect->modelViewMatrix);

		if (bse_useFrustumCull.GetBool() && !R_ShouldDisableEntityCullingForLevelshot()) {
			idBounds expandedBounds = localBounds;
			expandedBounds.ExpandSelf( Max( 0.0f, bse_frustumCullExpand.GetFloat() ) );
			if (R_CullLocalBox(expandedBounds, vEffect->modelMatrix, 5, tr.viewDef->frustum)) {
				++dropFrustumCull;
				continue;
			}
		}

		// Stock Quake 4 effects are portal-scissored from area visibility, not
		// tightly projected from dynamic-model bounds. Bound-based scissors can
		// clip deformed/sprite effect geometry (for example rocket flare cards).
		vEffect->scissorRect = tr.viewDef->scissor;
		if (vEffect->scissorRect.IsEmpty()) {
			++dropScissor;
			continue;
		}

		renderEntity_t renderParms;
		memset(&renderParms, 0, sizeof(renderParms));
		renderParms.origin = def->parms.origin;
		renderParms.axis = def->parms.axis;
		renderParms.suppressSurfaceInViewID = def->parms.suppressSurfaceInViewID;
		renderParms.allowSurfaceInViewID = def->parms.allowSurfaceInViewID;
		renderParms.modelDepthHack = def->parms.modelDepthHack;
		renderParms.weaponDepthHackInViewID = def->parms.weaponDepthHackInViewID;
		renderParms.referenceSoundHandle = def->parms.referenceSoundHandle;
		for (int parm = 0; parm < MAX_ENTITY_SHADER_PARMS; ++parm) {
			renderParms.shaderParms[parm] = def->parms.shaderParms[parm];
		}

		const int total = model->NumSurfaces();
		int effectSurfaceCount = 0;
		int effectNoIndexes = 0;
		int effectNoIndexesWithVerts = 0;
		for (int s = 0; s < total; ++s) {
			++surfaceVisited;
			if (r_singleSurface.GetInteger() >= 0 && s != r_singleSurface.GetInteger()) {
				continue;
			}

			const modelSurface_t* surf = model->Surface(s);
			if (!surf || !surf->geometry) {
				++surfaceNoGeom;
				continue;
			}
			if (!surf->geometry->numIndexes) {
				if (surf->geometry->numVerts > 0) {
					++surfaceNoIndexesWithVerts;
					++effectNoIndexesWithVerts;
				}
				++surfaceNoIndexes;
				++effectNoIndexes;
				continue;
			}

			srfTriangles_t* tri = surf->geometry;
			if (bse_useSurfaceFrustumCull.GetBool() && !R_ShouldDisableEntityCullingForLevelshot()) {
				if (R_CullLocalBox(tri->bounds, vEffect->modelMatrix, 5, tr.viewDef->frustum)) {
					++surfaceFrustumCull;
					continue;
				}
			}

			const idMaterial* shader = surf->shader;
			R_GlobalShaderOverride(&shader);
			if (!shader) {
				++surfaceNoShader;
				continue;
			}
			if (!shader->IsDrawn()) {
				++surfaceNotDrawn;
				continue;
			}

			srfTriangles_t *submitTri = R_CreateFrameSubmitTri( tri, shader->ReceivesLighting() );
			if ( submitTri == NULL ) {
				++surfaceCacheFail;
				continue;
			}
			R_TouchVertexCache( submitTri->ambientCache );
			if ( submitTri->indexCache != NULL ) {
				R_TouchVertexCache( submitTri->indexCache );
			}

			R_AddDrawSurf(submitTri, vEffect, &renderParms, shader, vEffect->scissorRect, DSF_BSE_EFFECT);
			tri->ambientViewCount = tr.viewCount;
			def->visibleCount = tr.viewCount;
			++effectSurfaceCount;
			++renderedSurfaces;
		}
		if (effectSurfaceCount > 0) {
			++renderedEffects;
		}
		else if (total > 0) {
			++effectsNoIndexedSurfaces;
			if (effectNoIndexesWithVerts > 0) {
				++effectsNoIndexedButHasVerts;
			}
		}
	}

	if (counterMode > 0) {
		common->Printf(
			"BSE frame %d view %d mode=%d: spawned=%d serviced=%d refreshed=%d deferred=%d rendered=%d defs=%d alive=%d expired=%d surfaces=%d\n",
			tr.frameCount,
			tr.viewDef->renderView.viewID,
			counterMode,
			spawned,
			serviced,
			refreshedCulledService,
			deferredService,
			renderedEffects,
			totalDefs,
			alive,
			expired,
			renderedSurfaces);
		common->Printf(
			"BSE drops: notConnected=%d viewSuppress=%d area=%d defFrustum=%d noModel=%d noSurfaces=%d clearedBounds=%d frustum=%d scissor=%d\n",
			dropNotConnected,
			dropViewSuppress,
			dropAreaCull,
			dropDefFrustumCull,
			dropNoModel,
			dropNoModelSurfaces,
			dropClearedBounds,
			dropFrustumCull,
			dropScissor);
		common->Printf(
			"BSE surf: visited=%d noGeom=%d noIndexes=%d noIdxWithVerts=%d frustum=%d noShader=%d notDrawn=%d cacheFail=%d added=%d\n",
			surfaceVisited,
			surfaceNoGeom,
			surfaceNoIndexes,
			surfaceNoIndexesWithVerts,
			surfaceFrustumCull,
			surfaceNoShader,
			surfaceNotDrawn,
			surfaceCacheFail,
			renderedSurfaces);
		common->Printf(
			"BSE effect geom: noIndexed=%d noIndexedWithVerts=%d rendered=%d\n",
			effectsNoIndexedSurfaces,
			effectsNoIndexedButHasVerts,
			renderedEffects);
		common->Printf(
			"BSE service gate: true=%d false=%d lagMin=%d lagMax=%d\n",
			serviceSpawnGateTrue,
			serviceSpawnGateFalse,
			(serviceGateLagMin == 0x7fffffff) ? 0 : serviceGateLagMin,
			(serviceGateLagMax == (-0x7fffffff - 1)) ? 0 : serviceGateLagMax);
	}
}

/*
=========================================================================

	Through-world outlines

	REF_OUTLINE_THROUGH_WORLD asks for a silhouette wherever the entity is,
	including places the portal walk never reached - an ally on the far side of
	the level. Occlusion is the only thing being defeated: the view frustum still
	applies, because nothing off screen needs a ring.

	Everything these entities produce goes on viewDef->outlineDrawSurfs. Keeping
	them off the main list is the whole safety argument. They are in no depth
	buffer, receive no light and cast no shadow, so the depth fill, the ambient
	pass, the interaction passes, the cel passes and the motion vectors would all
	draw them wrong - and would do so by simply not knowing about them. A pass
	only sees these surfaces if it goes looking for the second list.

	They also never reach the rimlight or the brightskin. Both of those shade the
	body, and both compare depth-equal against a depth value that was never
	written.

=========================================================================
*/

// Only surfaces the outline shell can actually carry are worth submitting. This
// mirrors what RB_PlayerVisibilityEffectsSurfaceAllowed and
// RB_PlayerVisibilityOutlineShellAllowed accept, which is what lets the submit
// below stay a short, provably sufficient setup instead of a copy of
// R_AddDrawSurf: no gui hookup, no deform, no subview, no sort games.
static bool R_ThroughWorldOutlineShaderAllowed( const idMaterial *shader ) {
	if ( shader == NULL || !shader->IsDrawn() ) {
		return false;
	}
	// Perforated is allowed: a Quake 4 player body is alpha tested for small
	// details and is still player-shaped, so the shell traces the right silhouette.
	if ( shader->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	if ( shader->Deform() != DFRM_NONE ) {
		return false;
	}
	if ( shader->HasGui() || shader->HasSubview() || shader->SuppressInSubview() ) {
		return false;
	}
	if ( shader->IsPortalSky() || shader->GetSort() >= SS_POST_PROCESS ) {
		return false;
	}

	return true;
}

static bool R_ThroughWorldOutlineEntityAllowed( const idRenderEntityLocal *def ) {
	if ( def == NULL || def->parms.hModel == NULL ) {
		return false;
	}
	if ( ( def->parms.outlineFlags & REF_OUTLINE_THROUGH_WORLD ) == 0 ) {
		return false;
	}
	// Nothing to draw is the cheapest possible answer, and the game clears the
	// colour rather than the flag when it turns the effect off.
	if ( def->parms.outlineColor[3] <= 0.0f || def->parms.outlineWidth <= 0.0f ) {
		return false;
	}
	// The portal walk already reached it, so it is lit, depth tested and outlined
	// by the ordinary path. Forcing it again would double the ring.
	if ( def->viewCount == tr.viewCount ) {
		return false;
	}
	if ( r_skipEntities.GetBool() ) {
		return false;
	}
	if ( r_singleEntity.GetInteger() >= 0 && r_singleEntity.GetInteger() != def->index ) {
		return false;
	}
	// The same view-id suppression the portal walk honours. Without it a player
	// whose own body is suppressed in their own view would ring themselves.
	if ( !r_skipSuppress.GetBool() ) {
		if ( def->parms.suppressSurfaceInViewID
				&& def->parms.suppressSurfaceInViewID == tr.viewDef->renderView.viewID ) {
			return false;
		}
		if ( def->parms.allowSurfaceInViewID
				&& def->parms.allowSurfaceInViewID != tr.viewDef->renderView.viewID ) {
			return false;
		}
	}
	if ( R_ShouldSuppressViewModelForLevelshot( tr.viewDef->renderView.viewID,
			def->parms.allowSurfaceInViewID, def->parms.weaponDepthHackInViewID ) ) {
		return false;
	}
	// Off screen stays off screen. tr.viewDef->frustum is the unconstrained view
	// frustum: R_ConstrainViewFrustum only narrows viewFrustum, which nothing here
	// consults, so this cull is independent of what else happened to be visible.
	if ( !R_ShouldDisableEntityCullingForLevelshot()
			&& R_CullLocalBox( def->referenceBounds, def->modelMatrix, 5, tr.viewDef->frustum ) ) {
		return false;
	}

	return true;
}

static void R_AddThroughWorldOutlineDrawSurf( const srfTriangles_t *tri, const viewEntity_t *space,
		const renderEntity_t *renderEntity, const idMaterial *shader ) {
	viewDef_t *viewDef = tr.viewDef;

	if ( viewDef->numOutlineDrawSurfs == viewDef->maxOutlineDrawSurfs ) {
		drawSurf_t **old = viewDef->outlineDrawSurfs;
		const int used = viewDef->maxOutlineDrawSurfs * sizeof( viewDef->outlineDrawSurfs[0] );

		viewDef->maxOutlineDrawSurfs = ( viewDef->maxOutlineDrawSurfs == 0 ) ? 64 : viewDef->maxOutlineDrawSurfs * 2;
		viewDef->outlineDrawSurfs = (drawSurf_t **)R_FrameAlloc(
			viewDef->maxOutlineDrawSurfs * sizeof( viewDef->outlineDrawSurfs[0] ) );
		if ( used > 0 ) {
			memcpy( viewDef->outlineDrawSurfs, old, used );
		}
	}

	// Cleared frame memory, so every field this surface does not use reads as zero
	// rather than as whatever the allocator last held.
	drawSurf_t *drawSurf = (drawSurf_t *)R_ClearedFrameAlloc( sizeof( *drawSurf ) );
	drawSurf->geo = tri;
	drawSurf->space = space;
	drawSurf->material = shader;
	drawSurf->scissorRect = space->scissorRect;
	drawSurf->sort = shader->GetSort();
	drawSurf->dsFlags = DSF_OUTLINE_ONLY;
	drawSurf->shaderRegisters = R_SetupDrawSurfShaderRegisters( space, renderEntity, shader );

	viewDef->outlineDrawSurfs[viewDef->numOutlineDrawSurfs] = drawSurf;
	viewDef->numOutlineDrawSurfs++;
}

static void R_AddThroughWorldOutlineEntity( idRenderEntityLocal *def ) {
	viewEntity_t *vEntity = R_SetEntityDefViewEntity( def );
	if ( vEntity == NULL ) {
		return;
	}

	// The shell reaches outside the model's own bounds and the entity was never
	// assigned a portal rect, so it clips against the whole view.
	vEntity->scissorRect = tr.viewDef->scissor;

	idRenderModel *model = R_EntityDefDynamicModel( def );
	if ( model == NULL ) {
		return;
	}

	const int numSurfaces = model->NumSurfaces();
	for ( int i = 0; i < numSurfaces; i++ ) {
		const modelSurface_t *surf = model->Surface( i );

		if ( surf->id >= 0 && surf->id < static_cast<int>( sizeof( unsigned int ) * 8 )
			&& ( static_cast<unsigned int>( def->parms.suppressSurfaceMask ) & ( 1u << surf->id ) ) != 0 ) {
			continue;
		}
		if ( r_singleSurface.GetInteger() >= 0 && i != r_singleSurface.GetInteger() ) {
			continue;
		}

		srfTriangles_t *tri = surf->geometry;
		if ( tri == NULL || tri->numIndexes <= 0 ) {
			continue;
		}

		const idMaterial *shader = R_RemapShaderBySkin( surf->shader, def->parms.customSkin, def->parms.customShader );
		R_GlobalShaderOverride( &shader );
		if ( !R_ThroughWorldOutlineShaderAllowed( shader ) ) {
			continue;
		}

		// Player models are packed MD5R meshes and carry no idDrawVert array of
		// their own, so they need the packed-to-classic conversion the ambient pass
		// uses. Rejecting them instead left an ally marked by a few stray arcs off
		// whichever surfaces happened not to be packed.
		//
		// needsLighting is false throughout: the shell reads positions and normals
		// and never a lighting vector.
		if ( R_TriHasPrimBatchMesh( tri ) ) {
			if ( !R_CreatePackedSurfaceFrameCaches( tri, false, true ) ) {
				return;
			}
		} else {
			if ( tri->verts == NULL ) {
				continue;
			}
			if ( !R_CreateAmbientCache( tri, false ) ) {
				return;
			}
			if ( tri->indexCache == NULL && tri->indexes != NULL && R_StaticIndexCacheAllowed( def ) ) {
				vertexCache.Alloc( tri->indexes, tri->numIndexes * sizeof( tri->indexes[0] ), &tri->indexCache, true );
			}
		}

		R_TouchVertexCache( tri->ambientCache );
		if ( tri->indexCache ) {
			R_TouchVertexCache( tri->indexCache );
		}

		// Deliberately not setting tri->ambientViewCount. That is the flag light
		// interactions test to decide whether a surface is visible this view, and
		// leaving it alone is what keeps an entity that exists only for its ring
		// out of every light list.
		R_AddThroughWorldOutlineDrawSurf( tri, vEntity, &def->parms, shader );
	}
}

/*
===================
R_AddThroughWorldOutlines

Runs at the very end of the view build, after every pass that walks viewEntitys or
builds interactions has already run, so the entities added here cannot be picked up
by any of them.
===================
*/
void R_AddThroughWorldOutlines( void ) {
	tr.viewDef->outlineDrawSurfs = NULL;
	tr.viewDef->numOutlineDrawSurfs = 0;
	tr.viewDef->maxOutlineDrawSurfs = 0;

	if ( tr.viewDef->renderWorld == NULL || r_skipPlayerVisibilityEffects.GetBool() ) {
		return;
	}

	idRenderWorldLocal *world = static_cast<idRenderWorldLocal *>( tr.viewDef->renderWorld );

	// The registry holds a handful of handles at most, which is the point: finding
	// the players that want a ring must not cost a walk of every entity in the map.
	for ( int i = 0; i < world->throughWorldOutlineEntities.Num(); i++ ) {
		const int handle = world->throughWorldOutlineEntities[i];
		if ( handle < 0 || handle >= world->entityDefs.Num() ) {
			continue;
		}

		idRenderEntityLocal *def = world->entityDefs[handle];
		if ( !R_ThroughWorldOutlineEntityAllowed( def ) ) {
			continue;
		}

		R_AddThroughWorldOutlineEntity( def );
	}
}

/*
===================
R_AddModelSurfaces

Here is where dynamic models actually get instantiated, and necessary
interactions get created.  This is all done on a sort-by-model basis
to keep source data in cache (most likely L2) as any interactions and
shadows are generated, since dynamic models will typically be lit by
two or more lights.
===================
*/
void R_AddModelSurfaces( void ) {
	viewEntity_t		**sortedEntities;
	idInteraction		*inter, *next;
	idRenderModel		*model;
	const unsigned int	numViewEntities = R_SortViewEntities( &sortedEntities );
	const bool			useEntityScissors = r_useEntityScissors.GetBool() && !R_ShouldDisableEntityCullingForLevelshot();
	const bool			showEntityScissors = r_showEntityScissors.GetBool();
	const bool			lodEntities = r_lod_entities.GetBool();
	const float			lodEntitiesPercent = r_lod_entities_percent.GetFloat();

	// clear the ambient surface list
	tr.viewDef->numDrawSurfs = 0;
	tr.viewDef->maxDrawSurfs = 0;	// will be set to INITIAL_DRAWSURFS on R_AddDrawSurf

	for ( int i = static_cast<int>( numViewEntities ) - 1; i >= 0; --i ) {
		viewEntity_t *vEntity = sortedEntities[i];

		if ( useEntityScissors ) {
			// calculate the screen area covered by the entity
			idScreenRect scissorRect = R_CalcEntityScissorRectangle( vEntity );
			// intersect with the portal crossing scissor rectangle
			vEntity->scissorRect.Intersect( scissorRect );

			if ( showEntityScissors ) {
				R_ShowColoredScreenRect( vEntity->scissorRect, vEntity->entityDef->index );
			}
		}

		if ( vEntity->scissorRect.IsEmpty() || glConfig.vidWidth <= 0 || glConfig.vidHeight <= 0 ) {
			vEntity->screenCoverage = 0.0f;
		} else {
			const float sizeY = static_cast<float>( vEntity->scissorRect.y2 - vEntity->scissorRect.y1 );
			const float sizeX = static_cast<float>( vEntity->scissorRect.x2 - vEntity->scissorRect.x1 );
			vEntity->screenCoverage = 0.5f * ( sizeY / glConfig.vidHeight + sizeX / glConfig.vidWidth );
		}

		if ( lodEntities && vEntity->screenCoverage < lodEntitiesPercent ) {
			vEntity->scissorRect.Clear();
		}

		// add the ambient surface if it has a visible rectangle
		if ( !vEntity->scissorRect.IsEmpty() ) {
			model = R_EntityDefDynamicModel( vEntity->entityDef );
			if ( model == NULL || model->NumSurfaces() <= 0 ) {
				continue;
			}

			R_AddAmbientDrawsurfs( vEntity );
			tr.pc.c_visibleViewEntities++;
		} else {
			tr.pc.c_shadowViewEntities++;
		}

		//
		// for all the entity / light interactions on this entity, add them to the view
		//
		// all empty interactions are at the end of the list so once the
		// first is encountered all the remaining interactions are empty
		for ( inter = vEntity->entityDef->firstInteraction; inter != NULL && inter->numSurfaces != 0; inter = next ) {
			next = inter->entityNext;

			// skip any lights that aren't currently visible
			// this is run after any lights that are turned off have already
			// been removed from the viewLights list, and had their viewCount cleared
			if ( inter->lightDef->viewCount != tr.viewCount ) {
				continue;
			}
			inter->AddActiveInteraction();
		}
	}
}

/*
=====================
R_RemoveUnecessaryViewLights
=====================
*/
void R_RemoveUnecessaryViewLights( void ) {
	viewLight_t		*vLight;

	// go through each visible light
	for ( vLight = tr.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		R_AddOptimizedPrelightShadows( vLight );

		// if the light didn't have any lit surfaces visible, there is no need to
		// draw any of the shadows.  We still keep the vLight for debugging
		// draws
		if ( !vLight->localInteractions && !vLight->globalInteractions && !vLight->translucentInteractions ) {
			vLight->localShadows = NULL;
			vLight->globalShadows = NULL;
			vLight->localShadowMapStencilSupplements = NULL;
			vLight->globalShadowMapStencilSupplements = NULL;
			vLight->localShadowMapCasters = NULL;
			vLight->globalShadowMapCasters = NULL;
			vLight->localShadowMapDynamicCasters = NULL;
			vLight->globalShadowMapDynamicCasters = NULL;
			vLight->localTranslucentShadowMapCasters = NULL;
			vLight->globalTranslucentShadowMapCasters = NULL;
		}
	}

	if ( r_useShadowSurfaceScissor.GetBool() ) {
		// shrink the light scissor rect to only intersect the surfaces that will actually be drawn.
		// This doesn't seem to actually help, perhaps because the surface scissor
		// rects aren't actually the surface, but only the portal clippings.
		for ( vLight = tr.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
			const drawSurf_t	*surf;
			idScreenRect	surfRect;

			if ( !vLight->lightShader->LightCastsShadows() ) {
				continue;
			}

			surfRect.Clear();

			for ( surf = vLight->globalInteractions ; surf ; surf = surf->nextOnLight ) {
				surfRect.Union( surf->scissorRect );
			}
			for ( surf = vLight->localShadows ; surf ; surf = surf->nextOnLight ) {
				const_cast<drawSurf_t *>(surf)->scissorRect.Intersect( surfRect );
			}

			for ( surf = vLight->localInteractions ; surf ; surf = surf->nextOnLight ) {
				surfRect.Union( surf->scissorRect );
			}
			for ( surf = vLight->globalShadows ; surf ; surf = surf->nextOnLight ) {
				const_cast<drawSurf_t *>(surf)->scissorRect.Intersect( surfRect );
			}

			for ( surf = vLight->translucentInteractions ; surf ; surf = surf->nextOnLight ) {
				surfRect.Union( surf->scissorRect );
			}

			vLight->scissorRect.Intersect( surfRect );
		}
	}
}
