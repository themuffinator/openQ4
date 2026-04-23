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

namespace {

void R_WriteDrawVertToDemo( idDemoFile *f, const idDrawVert &vert ) {
	f->WriteVec3( vert.xyz );
	f->WriteVec2( vert.st );
	f->WriteVec3( vert.normal );
	f->WriteVec3( vert.tangents[0] );
	f->WriteVec3( vert.tangents[1] );
	f->WriteUnsignedChar( vert.color[0] );
	f->WriteUnsignedChar( vert.color[1] );
	f->WriteUnsignedChar( vert.color[2] );
	f->WriteUnsignedChar( vert.color[3] );
}

void R_ReadDrawVertFromDemo( idDemoFile *f, idDrawVert &vert ) {
	f->ReadVec3( vert.xyz );
	f->ReadVec2( vert.st );
	f->ReadVec3( vert.normal );
	f->ReadVec3( vert.tangents[0] );
	f->ReadVec3( vert.tangents[1] );
	f->ReadUnsignedChar( vert.color[0] );
	f->ReadUnsignedChar( vert.color[1] );
	f->ReadUnsignedChar( vert.color[2] );
	f->ReadUnsignedChar( vert.color[3] );
}

bool R_MaterializePrimBatchDecalTriangles( const srfTriangles_t &sourceTri, srfTriangles_t &tempTri, idDrawVert *tempVerts, glIndex_t *tempIndexes ) {
	memset( &tempTri, 0, sizeof( tempTri ) );
	tempTri.bounds = sourceTri.bounds;
	tempTri.facePlanesCalculated = sourceTri.facePlanesCalculated;
	tempTri.facePlanes = sourceTri.facePlanes;
	tempTri.numVerts = sourceTri.numVerts;
	tempTri.verts = tempVerts;
	tempTri.numIndexes = sourceTri.numIndexes;
	tempTri.indexes = tempIndexes;

	if ( sourceTri.numVerts <= 0 || sourceTri.numIndexes <= 0 ) {
		return false;
	}

	// If the packed surface already exposed a live classic tri view, prefer it
	// for fallback decal projection so dynamic MD5R recipients stay on their
	// current skinned pose instead of reconstructing from packed bind-pose data.
	if ( sourceTri.verts != NULL && sourceTri.indexes != NULL ) {
		memcpy( tempVerts, sourceTri.verts, sourceTri.numVerts * sizeof( tempVerts[0] ) );
		memcpy( tempIndexes, sourceTri.indexes, sourceTri.numIndexes * sizeof( tempIndexes[0] ) );
		return true;
	}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	return R_MD5R_CopyPrimBatchTriangles(
		tempVerts,
		tempIndexes,
		reinterpret_cast<const rvMesh *>( sourceTri.primBatchMesh ),
		reinterpret_cast<const rvSilTraceVertT *>( sourceTri.silTraceVerts ) );
#else
	return false;
#endif
}

}

// decalFade	filter 5 0.1
// polygonOffset
// {
// map invertColor( textures/splat )
// blend GL_ZERO GL_ONE_MINUS_SRC
// vertexColor
// clamp
// }

/*
==================
idRenderModelDecal::idRenderModelDecal
==================
*/
idRenderModelDecal::idRenderModelDecal( void ) {
	memset( &tri, 0, sizeof( tri ) );
	tri.verts = verts;
	tri.indexes = indexes;
	material = NULL;
	nextDecal = NULL;
}

/*
==================
idRenderModelDecal::~idRenderModelDecal
==================
*/
idRenderModelDecal::~idRenderModelDecal( void ) {
}

/*
==================
idRenderModelDecal::idRenderModelDecal
==================
*/
idRenderModelDecal *idRenderModelDecal::Alloc( void ) {
	return new idRenderModelDecal;
}

/*
==================
idRenderModelDecal::idRenderModelDecal
==================
*/
void idRenderModelDecal::Free( idRenderModelDecal *decal ) {
	delete decal;
}

/*
=================
idRenderModelDecal::CreateProjectionInfo
=================
*/
bool idRenderModelDecal::CreateProjectionInfo( decalProjectionInfo_t &info, const idFixedWinding &winding, const idVec3 &projectionOrigin, const bool parallel, const float fadeDepth, const idMaterial *material, const int startTime ) {

	if ( winding.GetNumPoints() != NUM_DECAL_BOUNDING_PLANES - 2 ) {
		common->Printf( "idRenderModelDecal::CreateProjectionInfo: winding must have %d points\n", NUM_DECAL_BOUNDING_PLANES - 2 );
		return false;
	}

	assert( material != NULL );

	info.projectionOrigin = projectionOrigin;
	info.material = material;
	info.parallel = parallel;
	info.fadeDepth = fadeDepth;
	info.startTime = startTime;
	info.maxAngle = material->GetDecalInfo().maxAngle;
	info.force = false;

	// get the winding plane and the depth of the projection volume
	idPlane windingPlane;
	winding.GetPlane( windingPlane );
	float depth = windingPlane.Distance( projectionOrigin );

	// find the bounds for the projection
	winding.GetBounds( info.projectionBounds );
	if ( parallel ) {
		const idVec3 transToProj = windingPlane.Normal() * depth;
		const idVec3 transToFade = windingPlane.Normal() * fadeDepth;
		for ( int i = 0; i < winding.GetNumPoints(); i++ ) {
			const idVec3 point = winding[i].ToVec3();
			info.projectionBounds.AddPoint( point + transToProj );
			info.projectionBounds.AddPoint( point - transToFade );
		}
	} else {
		info.projectionBounds.AddPoint( projectionOrigin );
	}

	// calculate the world space projection volume bounding planes, positive sides face outside the decal
	if ( parallel ) {
		for ( int i = 0; i < winding.GetNumPoints(); i++ ) {
			idVec3 edge = winding[(i+1)%winding.GetNumPoints()].ToVec3() - winding[i].ToVec3();
			info.boundingPlanes[i].Normal().Cross( windingPlane.Normal(), edge );
			info.boundingPlanes[i].Normalize();
			info.boundingPlanes[i].FitThroughPoint( winding[i].ToVec3() );
		}
	} else {
		for ( int i = 0; i < winding.GetNumPoints(); i++ ) {
			info.boundingPlanes[i].FromPoints( projectionOrigin, winding[i].ToVec3(), winding[(i+1)%winding.GetNumPoints()].ToVec3() );
		}
	}
	info.boundingPlanes[NUM_DECAL_BOUNDING_PLANES - 2] = windingPlane;
	info.boundingPlanes[NUM_DECAL_BOUNDING_PLANES - 2][3] -= depth;
	info.boundingPlanes[NUM_DECAL_BOUNDING_PLANES - 1] = -windingPlane;

	// fades will be from these plane
	info.fadePlanes[0] = windingPlane;
	info.fadePlanes[0][3] -= fadeDepth;
	info.fadePlanes[1] = -windingPlane;
	info.fadePlanes[1][3] += depth - fadeDepth;

	// calculate the texture vectors for the winding
	float	len, texArea, inva;
	idVec3	temp;
	idVec5	d0, d1;

	const idVec5 &a = winding[0];
	const idVec5 &b = winding[1];
	const idVec5 &c = winding[2];

	d0 = b.ToVec3() - a.ToVec3();
	d0.s = b.s - a.s;
	d0.t = b.t - a.t;
	d1 = c.ToVec3() - a.ToVec3();
	d1.s = c.s - a.s;
	d1.t = c.t - a.t;

	texArea = ( d0[3] * d1[4] ) - ( d0[4] * d1[3] );
	inva = 1.0f / texArea;

    temp[0] = ( d0[0] * d1[4] - d0[4] * d1[0] ) * inva;
    temp[1] = ( d0[1] * d1[4] - d0[4] * d1[1] ) * inva;
    temp[2] = ( d0[2] * d1[4] - d0[4] * d1[2] ) * inva;
	len = temp.Normalize();
	info.textureAxis[0].Normal() = temp * ( 1.0f / len );
	info.textureAxis[0][3] = winding[0].s - ( winding[0].ToVec3() * info.textureAxis[0].Normal() );

    temp[0] = ( d0[3] * d1[0] - d0[0] * d1[3] ) * inva;
    temp[1] = ( d0[3] * d1[1] - d0[1] * d1[3] ) * inva;
    temp[2] = ( d0[3] * d1[2] - d0[2] * d1[3] ) * inva;
	len = temp.Normalize();
	info.textureAxis[1].Normal() = temp * ( 1.0f / len );
	info.textureAxis[1][3] = winding[0].t - ( winding[0].ToVec3() * info.textureAxis[1].Normal() );

	return true;
}

/*
=================
idRenderModelDecal::CreateProjectionInfo
=================
*/
void idRenderModelDecal::GlobalProjectionInfoToLocal( decalProjectionInfo_t &localInfo, const decalProjectionInfo_t &info, const idVec3 &origin, const idMat3 &axis ) {
	float modelMatrix[16];

	R_AxisToModelMatrix( axis, origin, modelMatrix );

	for ( int j = 0; j < NUM_DECAL_BOUNDING_PLANES; j++ ) {
		R_GlobalPlaneToLocal( modelMatrix, info.boundingPlanes[j], localInfo.boundingPlanes[j] );
	}
	R_GlobalPlaneToLocal( modelMatrix, info.fadePlanes[0], localInfo.fadePlanes[0] );
	R_GlobalPlaneToLocal( modelMatrix, info.fadePlanes[1], localInfo.fadePlanes[1] );
	R_GlobalPlaneToLocal( modelMatrix, info.textureAxis[0], localInfo.textureAxis[0] );
	R_GlobalPlaneToLocal( modelMatrix, info.textureAxis[1], localInfo.textureAxis[1] );
	R_GlobalPointToLocal( modelMatrix, info.projectionOrigin, localInfo.projectionOrigin );
	localInfo.projectionBounds = info.projectionBounds;
	localInfo.projectionBounds.TranslateSelf( -origin );
	localInfo.projectionBounds.RotateSelf( axis.Transpose() );
	localInfo.material = info.material;
	localInfo.parallel = info.parallel;
	localInfo.fadeDepth = info.fadeDepth;
	localInfo.startTime = info.startTime;
	localInfo.maxAngle = info.maxAngle;
	localInfo.force = info.force;
}

/*
=================
idRenderModelDecal::AddWinding
=================
*/
void idRenderModelDecal::AddWinding( const idWinding &w, const idMaterial *decalMaterial, const idPlane fadePlanes[2], float fadeDepth, int startTime ) {
	int i;
	float invFadeDepth, fade;

	if ( ( material == NULL || material == decalMaterial ) &&
			tri.numVerts + w.GetNumPoints() < MAX_DECAL_VERTS &&
				tri.numIndexes + ( w.GetNumPoints() - 2 ) * 3 < MAX_DECAL_INDEXES ) {

		material = decalMaterial;

		// add to this decal
		invFadeDepth = -1.0f / fadeDepth;

		for ( i = 0; i < w.GetNumPoints(); i++ ) {
			fade = fadePlanes[0].Distance( w[i].ToVec3() ) * invFadeDepth;
			if ( fade < 0.0f ) {
				fade = fadePlanes[1].Distance( w[i].ToVec3() ) * invFadeDepth;
			}
			if ( fade < 0.0f ) {
				fade = 0.0f;
			} else if ( fade > 0.99f ) {
				fade = 1.0f;
			}
			fade = 1.0f - fade;
			vertDepthFade[tri.numVerts + i] = fade;
			vertLifeSpan[tri.numVerts + i] = 0.0f;
			tri.verts[tri.numVerts + i].Clear();
			tri.verts[tri.numVerts + i].xyz = w[i].ToVec3();
			tri.verts[tri.numVerts + i].st[0] = w[i].s;
			tri.verts[tri.numVerts + i].st[1] = w[i].t;
			tri.verts[tri.numVerts + i].color[0] = 255;
			tri.verts[tri.numVerts + i].color[1] = 255;
			tri.verts[tri.numVerts + i].color[2] = 255;
			tri.verts[tri.numVerts + i].color[3] = 255;
		}
		for ( i = 2; i < w.GetNumPoints(); i++ ) {
			tri.indexes[tri.numIndexes + 0] = tri.numVerts;
			tri.indexes[tri.numIndexes + 1] = tri.numVerts + i - 1;
			tri.indexes[tri.numIndexes + 2] = tri.numVerts + i;
			indexStartTime[tri.numIndexes] =
			indexStartTime[tri.numIndexes + 1] =
			indexStartTime[tri.numIndexes + 2] = (float)startTime;
			tri.numIndexes += 3;
		}
		tri.numVerts += w.GetNumPoints();
		tri.tangentsCalculated = false;
		tri.facePlanesCalculated = false;
		return;
	}

	// if we are at the end of the list, create a new decal
	if ( !nextDecal ) {
		nextDecal = idRenderModelDecal::Alloc();
	}
	// let the next decal on the chain take a look
	nextDecal->AddWinding( w, decalMaterial, fadePlanes, fadeDepth, startTime );
}

/*
=================
idRenderModelDecal::AddDepthFadedWinding
=================
*/
void idRenderModelDecal::AddDepthFadedWinding( const idWinding &w, const idMaterial *decalMaterial, const idPlane fadePlanes[2], float fadeDepth, int startTime ) {
	idFixedWinding front, back;

	front = w;
	if ( front.Split( &back, fadePlanes[0], 0.1f ) == SIDE_CROSS ) {
		AddWinding( back, decalMaterial, fadePlanes, fadeDepth, startTime );
	}

	if ( front.Split( &back, fadePlanes[1], 0.1f ) == SIDE_CROSS ) {
		AddWinding( back, decalMaterial, fadePlanes, fadeDepth, startTime );
	}

	AddWinding( front, decalMaterial, fadePlanes, fadeDepth, startTime );
}

/*
=================
idRenderModelDecal::CreateDecal
=================
*/
void idRenderModelDecal::CreateDecal( const idRenderModel *model, const decalProjectionInfo_t &localInfo ) {

	// check all model surfaces
	for ( int surfNum = 0; surfNum < model->NumSurfaces(); surfNum++ ) {
		const modelSurface_t *surf = model->Surface( surfNum );

		// if no geometry or no shader
		if ( !surf->geometry || !surf->shader ) {
			continue;
		}

		// decals and overlays use the same rules
		if ( !localInfo.force && !surf->shader->AllowOverlays() ) {
			continue;
		}

		srfTriangles_t *stri = surf->geometry;

		// if the triangle bounds do not overlap with projection bounds
		if ( !localInfo.projectionBounds.IntersectsBounds( stri->bounds ) ) {
			continue;
		}

		// Packed MD5R surfaces keep their canonical draw data in the prim batch.
		// Prefer the native sil-trace projector when the packed mesh state is
		// usable; fall back to a transient classic view otherwise.
		if ( stri->primBatchMesh != NULL ) {
			if ( R_MD5R_CreateDecalTriangles( this, *stri, localInfo ) ) {
				continue;
			}

			srfTriangles_t tempTri;
			idDrawVert *tempVerts = (idDrawVert *)_alloca16( stri->numVerts * sizeof( tempVerts[0] ) );
			glIndex_t *tempIndexes = (glIndex_t *)_alloca16( stri->numIndexes * sizeof( tempIndexes[0] ) );
			if ( !R_MaterializePrimBatchDecalTriangles( *stri, tempTri, tempVerts, tempIndexes ) ) {
				continue;
			}
			stri = &tempTri;
		}

		// allocate memory for the cull bits
		const int cullByteCount = ( stri->numVerts + 3 ) & ~3;
		byte *cullBits = (byte *)_alloca16( cullByteCount );

		// catagorize all points by the planes
		SIMDProcessor->DecalPointCull( cullBits, localInfo.boundingPlanes, stri->verts, stri->numVerts );

		// find triangles inside the projection volume
		for ( int triNum = 0, index = 0; index < stri->numIndexes; index += 3, triNum++ ) {
			int v1 = stri->indexes[index+0];
			int v2 = stri->indexes[index+1];
			int v3 = stri->indexes[index+2];

			// skip triangles completely off one side
			if ( cullBits[v1] & cullBits[v2] & cullBits[v3] ) {
				continue;
			}

			// skip back facing triangles
			if ( stri->facePlanes && stri->facePlanesCalculated ) {
				const float facing = stri->facePlanes[triNum].Normal() * localInfo.boundingPlanes[NUM_DECAL_BOUNDING_PLANES - 2].Normal();
				if ( facing < localInfo.maxAngle ) {
					continue;
				}
			}

			// create a winding with texture coordinates for the triangle
			idFixedWinding fw;
			fw.SetNumPoints( 3 );
			if ( localInfo.parallel ) {
				for ( int j = 0; j < 3; j++ ) {
					fw[j] = stri->verts[stri->indexes[index+j]].xyz;
					fw[j].s = localInfo.textureAxis[0].Distance( fw[j].ToVec3() );
					fw[j].t = localInfo.textureAxis[1].Distance( fw[j].ToVec3() );
				}
			} else {
				for ( int j = 0; j < 3; j++ ) {
					idVec3 dir;
					float scale;

					fw[j] = stri->verts[stri->indexes[index+j]].xyz;
					dir = fw[j].ToVec3() - localInfo.projectionOrigin;
					localInfo.boundingPlanes[NUM_DECAL_BOUNDING_PLANES - 1].RayIntersection( fw[j].ToVec3(), dir, scale );
					dir = fw[j].ToVec3() + scale * dir;
					fw[j].s = localInfo.textureAxis[0].Distance( dir );
					fw[j].t = localInfo.textureAxis[1].Distance( dir );
				}
			}

			int orBits = cullBits[v1] | cullBits[v2] | cullBits[v3];

			// clip the exact surface triangle to the projection volume
			for ( int j = 0; j < NUM_DECAL_BOUNDING_PLANES; j++ ) {
				if ( orBits & ( 1 << j ) ) {
					if ( !fw.ClipInPlace( -localInfo.boundingPlanes[j] ) ) {
						break;
					}
				}
			}

			if ( fw.GetNumPoints() == 0 ) {
				continue;
			}

			AddDepthFadedWinding( fw, localInfo.material, localInfo.fadePlanes, localInfo.fadeDepth, localInfo.startTime );
		}
	}
}

/*
=====================
idRenderModelDecal::RemoveFadedDecals
=====================
*/
idRenderModelDecal *idRenderModelDecal::RemoveFadedDecals( idRenderModelDecal *decals, int time ) {
	int i, j, minTime, newNumIndexes, newNumVerts;
	int inUse[MAX_DECAL_VERTS];
	decalInfo_t	decalInfo;
	idRenderModelDecal *nextDecal;
	bool geometryChanged;

	if ( decals == NULL ) {
		return NULL;
	}

	const int oldNumIndexes = decals->tri.numIndexes;
	const int oldNumVerts = decals->tri.numVerts;

	// recursively free any next decals
	decals->nextDecal = RemoveFadedDecals( decals->nextDecal, time );

	// free the decals if no material set
	if ( decals->material == NULL ) {
		nextDecal = decals->nextDecal;
		Free( decals );
		return nextDecal;
	}
	
	decalInfo = decals->material->GetDecalInfo();
	minTime = time - decalInfo.stayTime;
	geometryChanged = false;

	newNumIndexes = 0;
	for ( i = 0; i < decals->tri.numIndexes; i += 3 ) {
		if ( decals->indexStartTime[i] > (float)minTime ) {
			// keep this triangle
			if ( newNumIndexes != i ) {
				for ( j = 0; j < 3; j++ ) {
					decals->tri.indexes[newNumIndexes+j] = decals->tri.indexes[i+j];
					decals->indexStartTime[newNumIndexes+j] = decals->indexStartTime[i+j];
				}
			}
			newNumIndexes += 3;
		}
	}

	// free the decals if all trianges faded away
	if ( newNumIndexes == 0 ) {
		nextDecal = decals->nextDecal;
		Free( decals );
		return nextDecal;
	}

	decals->tri.numIndexes = newNumIndexes;
	if ( newNumIndexes != oldNumIndexes ) {
		geometryChanged = true;
	}

	memset( inUse, 0, sizeof( inUse ) );
	for ( i = 0; i < decals->tri.numIndexes; i++ ) {
		inUse[decals->tri.indexes[i]] = 1;
	}

	newNumVerts = 0;
	for ( i = 0; i < decals->tri.numVerts; i++ ) {
		if ( !inUse[i] ) {
			geometryChanged = true;
			continue;
		}
		if ( newNumVerts != i ) {
			geometryChanged = true;
		}
		decals->tri.verts[newNumVerts] = decals->tri.verts[i];
		decals->vertDepthFade[newNumVerts] = decals->vertDepthFade[i];
		decals->vertLifeSpan[newNumVerts] = decals->vertLifeSpan[i];
		inUse[i] = newNumVerts;
		newNumVerts++;
	}
	if ( newNumVerts != oldNumVerts ) {
		geometryChanged = true;
	}
	decals->tri.numVerts = newNumVerts;

	for ( i = 0; i < decals->tri.numIndexes; i++ ) {
		const glIndex_t oldIndex = decals->tri.indexes[i];
		const glIndex_t newIndex = inUse[oldIndex];
		if ( newIndex != oldIndex ) {
			geometryChanged = true;
		}
		decals->tri.indexes[i] = newIndex;
	}

	if ( geometryChanged ) {
		decals->tri.tangentsCalculated = false;
		decals->tri.facePlanesCalculated = false;
	}

	return decals;
}

/*
=====================
idRenderModelDecal::AddDecalDrawSurf
=====================
*/
void idRenderModelDecal::AddDecalDrawSurf( viewEntity_t *space ) {
	if ( r_skipDecals.GetBool() || tri.numIndexes == 0 || tri.numVerts == 0 ) {
		return;
	}

	const decalInfo_t decalInfo = material->GetDecalInfo();
	const int stayTime = ( decalInfo.stayTime > 0 ) ? decalInfo.stayTime : 1;
	const float stayTimeFloat = (float)stayTime;
	const float renderTime = (float)tr.viewDef->renderView.time;
	const int numStages = material->GetNumStages();
	const int vertexBytes = tri.numVerts * sizeof( idDrawVert );
	const int decalColorStride = tri.numVerts * 4;
	const int totalColorBytes = decalColorStride * numStages;
	const int totalAmbientBytes = vertexBytes + totalColorBytes;
	byte *vertexAndColorData = (byte *)R_FrameAlloc( totalAmbientBytes );
	idDrawVert *uploadedVerts = reinterpret_cast<idDrawVert *>( vertexAndColorData );
	byte *stageColors = vertexAndColorData + vertexBytes;

	// Projected decals are built from clipped positions/UVs only. Rebuild the
	// tangent basis from that projected mesh so direct-light and shadow-map
	// receivers don't use stale or undefined normals/tangents.
	if ( tri.numVerts > 0 && tri.numIndexes > 0 && !tri.tangentsCalculated ) {
		R_DeriveTangents( &tri, false );
	}

	memcpy( vertexAndColorData, tri.verts, vertexBytes );

	memset( vertLifeSpan, 0, tri.numVerts * sizeof( vertLifeSpan[0] ) );
	for ( int indexBase = 0; indexBase < tri.numIndexes; indexBase += 3 ) {
		float life = renderTime - indexStartTime[indexBase];
		if ( life > stayTimeFloat ) {
			continue;
		}
		if ( life < 0.0f ) {
			life = 0.0f;
		}
		life /= stayTimeFloat;
		vertLifeSpan[tri.indexes[indexBase + 0]] = life;
		vertLifeSpan[tri.indexes[indexBase + 1]] = life;
		vertLifeSpan[tri.indexes[indexBase + 2]] = life;
	}

	tr.pc.c_numDecalIndexes += tri.numIndexes;

	if ( numStages > 0 && tri.numVerts > 0 ) {
		const int numRegisters = ( material->GetNumRegisters() > EXP_REG_NUM_PREDEFINED ) ? material->GetNumRegisters() : EXP_REG_NUM_PREDEFINED;
		float *regs = (float *)_alloca16( numRegisters * sizeof( regs[0] ) );
		float shaderParms[MAX_ENTITY_SHADER_PARMS];

		memset( stageColors, 0, totalColorBytes );
		memset( shaderParms, 0, sizeof( shaderParms ) );

		for ( int stage = 0; stage < numStages; stage++ ) {
			const shaderStage_t *pStage = material->GetStage( stage );
			byte *stageColor = stageColors + stage * decalColorStride;

			for ( int indexBase = 0; indexBase < tri.numIndexes; indexBase += 3 ) {
				const glIndex_t triVertIndex0 = tri.indexes[indexBase + 0];
				const glIndex_t triVertIndex1 = tri.indexes[indexBase + 1];
				const glIndex_t triVertIndex2 = tri.indexes[indexBase + 2];

				// DecalLife / DecalSpawn map to parm4 / parm5 and are evaluated per
				// triangle before the stage colors are baked into the draw surf.
				shaderParms[4] = vertLifeSpan[triVertIndex0];
				shaderParms[5] = indexStartTime[indexBase];
				material->EvaluateStageRegisters( stage, regs, shaderParms, tr.frameShaderTime );

				const float colorScale = vertDepthFade[triVertIndex0] * 255.0f;
				for ( int k = 0; k < 4; k++ ) {
					const int icolor = idMath::FtoiFast( regs[pStage->color.registers[k]] * colorScale );
					const byte colorByte = (byte)idMath::ClampInt( 0, 255, icolor );
					stageColor[triVertIndex0 * 4 + k] = colorByte;
					stageColor[triVertIndex1 * 4 + k] = colorByte;
					stageColor[triVertIndex2 * 4 + k] = colorByte;
				}
			}
		}

		// Keep the first stage color in the uploaded vertices as a fallback path
		// without mutating the persistent decal mesh.
		for ( int v = 0; v < tri.numVerts; v++ ) {
			const byte *vertexColor = stageColors + v * 4;
			for ( int k = 0; k < 4; k++ ) {
				uploadedVerts[v].color[k] = vertexColor[k];
			}
		}
	}

	// copy the tri and indexes to temp heap memory,
	// because if we are running multi-threaded, we wouldn't
	// be able to reorganize the index list
	srfTriangles_t *newTri = (srfTriangles_t *)R_FrameAlloc( sizeof( *newTri ) );
	*newTri = tri;

	// Snapshot the vertices plus any per-stage color blocks into one transient
	// upload so the draw surf matches Quake 4's frame-temp decal submission.
	newTri->ambientCache = vertexCache.AllocFrameTemp( vertexAndColorData, totalAmbientBytes );

	// create the drawsurf
	R_AddDrawSurf( newTri, space, &space->entityDef->parms, material, space->scissorRect );

	drawSurf_t *drawSurf = tr.viewDef->drawSurfs[tr.viewDef->numDrawSurfs - 1];
	drawSurf->decalColorCache = ( totalColorBytes > 0 ) ? newTri->ambientCache : NULL;
	drawSurf->decalColorOffset = ( totalColorBytes > 0 ) ? vertexBytes : 0;
	drawSurf->decalColorStride = decalColorStride;
	drawSurf->decalColorStageCount = numStages;
}

/*
====================
idRenderModelDecal::ReadFromDemoFile
====================
*/
void idRenderModelDecal::ReadFromDemoFile( idDemoFile *f ) {
	int numDecals;
	idRenderModelDecal *decal;

	while ( nextDecal != NULL ) {
		idRenderModelDecal *next = nextDecal->nextDecal;
		idRenderModelDecal::Free( nextDecal );
		nextDecal = next;
	}

	memset( &tri, 0, sizeof( tri ) );
	memset( verts, 0, sizeof( verts ) );
	memset( vertDepthFade, 0, sizeof( vertDepthFade ) );
	memset( vertLifeSpan, 0, sizeof( vertLifeSpan ) );
	memset( indexes, 0, sizeof( indexes ) );
	memset( indexStartTime, 0, sizeof( indexStartTime ) );
	tri.verts = verts;
	tri.indexes = indexes;
	material = NULL;

	f->ReadInt( numDecals );
	if ( numDecals < 0 || numDecals > 1024 ) {
		common->Error( "idRenderModelDecal::ReadFromDemoFile: bad decal count %d", numDecals );
	}

	if ( numDecals == 0 ) {
		return;
	}

	decal = this;
	for ( int decalIndex = 0; decalIndex < numDecals; decalIndex++ ) {
		const char *materialName = f->ReadHashString();

		decal->material = ( materialName[0] != '\0' ) ? declManager->FindMaterial( materialName ) : NULL;

		f->ReadInt( decal->tri.numVerts );
		if ( decal->tri.numVerts < 0 || decal->tri.numVerts > MAX_DECAL_VERTS ) {
			common->Error( "idRenderModelDecal::ReadFromDemoFile: bad vert count %d", decal->tri.numVerts );
		}

		for ( int vertIndex = 0; vertIndex < decal->tri.numVerts; vertIndex++ ) {
			R_ReadDrawVertFromDemo( f, decal->tri.verts[vertIndex] );
			f->ReadFloat( decal->vertDepthFade[vertIndex] );
			f->ReadFloat( decal->vertLifeSpan[vertIndex] );
		}

		f->ReadInt( decal->tri.numIndexes );
		if ( decal->tri.numIndexes < 0 || decal->tri.numIndexes > MAX_DECAL_INDEXES ) {
			common->Error( "idRenderModelDecal::ReadFromDemoFile: bad index count %d", decal->tri.numIndexes );
		}

		for ( int index = 0; index < decal->tri.numIndexes; index++ ) {
			int storedIndex;
			int storedStartTime;

			f->ReadInt( storedIndex );
			decal->tri.indexes[index] = storedIndex;
			f->ReadInt( storedStartTime );
			decal->indexStartTime[index] = (float)storedStartTime;
		}

		if ( decalIndex + 1 < numDecals ) {
			decal->nextDecal = idRenderModelDecal::Alloc();
			decal = decal->nextDecal;
		}
	}
}

/*
====================
idRenderModelDecal::WriteToDemoFile
====================
*/
void idRenderModelDecal::WriteToDemoFile( idDemoFile *f ) const {
	int numDecals;
	const idRenderModelDecal *decal;

	numDecals = 0;
	for ( decal = this; decal != NULL; decal = decal->nextDecal ) {
		numDecals++;
	}

	f->WriteInt( numDecals );

	for ( decal = this; decal != NULL; decal = decal->nextDecal ) {
		const char *materialName = ( decal->material != NULL ) ? decal->material->GetName() : "";

		f->WriteHashString( materialName );
		f->WriteInt( decal->tri.numVerts );
		for ( int vertIndex = 0; vertIndex < decal->tri.numVerts; vertIndex++ ) {
			R_WriteDrawVertToDemo( f, decal->tri.verts[vertIndex] );
			f->WriteFloat( decal->vertDepthFade[vertIndex] );
			f->WriteFloat( decal->vertLifeSpan[vertIndex] );
		}

		f->WriteInt( decal->tri.numIndexes );
		for ( int index = 0; index < decal->tri.numIndexes; index++ ) {
			f->WriteInt( decal->tri.indexes[index] );
			f->WriteInt( idMath::FtoiFast( decal->indexStartTime[index] ) );
		}
	}
}
