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


/*
=================
R_FinishDeform

The ambientCache is on the stack, so we don't want to leave a reference
to it that would try to be freed later.  Create the ambientCache immediately.
=================
*/
static void R_FinishDeform( drawSurf_t *drawSurf, srfTriangles_t *newTri, idDrawVert *ac ) {
	if ( !newTri || !ac || newTri->numVerts <= 0 ) {
		return;
	}

	// generate current normals, tangents, and bitangents
	// We might want to support the possibility of deform functions generating
	// explicit normals, and we might also want to allow the cached deformInfo
	// optimization for these.
	// FIXME: this doesn't work, because the deformed surface is just the
	// ambient one, and there isn't an opportunity to generate light interactions
	if ( drawSurf->material->ReceivesLighting() ) {
		newTri->verts = ac;
		R_DeriveTangents( newTri, false );
		newTri->verts = NULL;
	}

	newTri->ambientCache = vertexCache.AllocFrameTemp( ac, newTri->numVerts * sizeof( idDrawVert ) );
	if ( r_useIndexBuffers.GetBool() && newTri->numIndexes > 0 ) {
		newTri->indexCache = vertexCache.AllocFrameTemp( newTri->indexes, newTri->numIndexes * sizeof( newTri->indexes[0] ), true );
	}
	// if we are out of vertex cache, leave it the way it is
	if ( newTri->ambientCache ) {
		drawSurf->geo = newTri;
	}
}

static ID_INLINE idVec3 R_DeformQuadVertexPosition( const srfTriangles_t *tri, int index ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	if ( tri->silTraceVerts != NULL ) {
		const rvSilTraceVertT *silTraceVerts = reinterpret_cast<const rvSilTraceVertT *>( tri->silTraceVerts );
		return silTraceVerts[index].xyzw.ToVec3();
	}
#endif
	return tri->verts[index].xyz;
}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
/*
========================
R_CopyPrimBatchTriangles

Packed MD5R surfaces keep the canonical draw geometry inside the prim batch.
Deform code still needs a classic idDrawVert/index view, so materialize one
through the renderer seam when the source surface doesn't expose tri->verts.
========================
*/
static void R_CopyPrimBatchTriangles( const srfTriangles_t *geo, srfTriangles_t *newTri, idDrawVert *ac ) {
	newTri->numVerts = geo->numVerts;
	newTri->numIndexes = geo->numIndexes;
	newTri->indexes = (glIndex_t *)R_FrameAlloc( newTri->numIndexes * sizeof( newTri->indexes[0] ) );
	renderSystem->CopyPrimBatchTriangles( ac, newTri->indexes, geo->primBatchMesh, geo->silTraceVerts );
}

/*
===============================
R_MaterializePrimBatchTriangles
===============================
*/
static void R_MaterializePrimBatchTriangles( const srfTriangles_t *geo, srfTriangles_t *tri, idDrawVert *tempVerts, glIndex_t *tempIndexes ) {
	memset( tri, 0, sizeof( *tri ) );
	tri->numVerts = geo->numVerts;
	tri->numIndexes = geo->numIndexes;
	tri->verts = tempVerts;
	tri->indexes = tempIndexes;
	renderSystem->CopyPrimBatchTriangles( tempVerts, tempIndexes, geo->primBatchMesh, geo->silTraceVerts );
}

/*
===========================
R_CopyDeformSourceTriangles
===========================
*/
static void R_CopyDeformSourceTriangles( const srfTriangles_t *tri, srfTriangles_t *newTri, idDrawVert *ac ) {
	if ( tri->primBatchMesh != NULL ) {
		R_CopyPrimBatchTriangles( tri, newTri, ac );
		return;
	}

	newTri->numVerts = tri->numVerts;
	newTri->numIndexes = tri->numIndexes;
	newTri->indexes = tri->indexes;

	for ( int i = 0; i < tri->numVerts; ++i ) {
		ac[i] = tri->verts[i];
	}
}
#endif

/*
=====================
R_SpriteDeform

Assuming all the triangles for this shader are independent quads,
rebuild them as forward facing sprites. When radiusDimension is true,
the resulting sprite is square; otherwise it preserves the authored
rectangular aspect used by Quake 4's rectsprite materials.
=====================
*/
static void R_SpriteDeform( drawSurf_t *surf, bool radiusDimension ) {
	int		i;
	idVec3	mid, delta;
	float	halfWidth, halfHeight;
	idVec3	left, up;
	idVec3	leftDir, upDir;
	const srfTriangles_t	*tri;
	srfTriangles_t	*newTri;

	tri = surf->geo;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	srfTriangles_t	tempTri;
	if ( tri->primBatchMesh != NULL ) {
		idDrawVert *tempVerts = (idDrawVert *)_alloca16( tri->numVerts * sizeof( idDrawVert ) );
		glIndex_t *tempIndexes = (glIndex_t *)_alloca16( tri->numIndexes * sizeof( tempIndexes[0] ) );
		R_MaterializePrimBatchTriangles( tri, &tempTri, tempVerts, tempIndexes );
		tri = &tempTri;
	}
#endif

	if ( tri->numVerts & 3 ) {
		common->Warning( "R_AutospriteDeform: shader had odd vertex count" );
		return;
	}
	if ( tri->numIndexes != ( tri->numVerts >> 2 ) * 6 ) {
		common->Warning( "R_AutospriteDeform: autosprite had odd index count" );
		return;
	}

	R_GlobalVectorToLocal( surf->space->modelMatrix, tr.viewDef->renderView.viewaxis[1], leftDir );
	R_GlobalVectorToLocal( surf->space->modelMatrix, tr.viewDef->renderView.viewaxis[2], upDir );

	if ( tr.viewDef->isMirror ) {
		leftDir = vec3_origin - leftDir;
	}

	// this srfTriangles_t and all its indexes and caches are in frame
	// memory, and will be automatically disposed of
	newTri = (srfTriangles_t *)R_ClearedFrameAlloc( sizeof( *newTri ) );
	newTri->numVerts = tri->numVerts;
	newTri->numIndexes = tri->numIndexes;
	newTri->indexes = (glIndex_t *)R_FrameAlloc( newTri->numIndexes * sizeof( newTri->indexes[0] ) );

	idDrawVert	*ac = (idDrawVert *)_alloca16( newTri->numVerts * sizeof( idDrawVert ) );

	for ( i = 0 ; i < tri->numVerts ; i+=4 ) {
		idVec3 positions[4];
		for ( int j = 0; j < 4; ++j ) {
			positions[j] = R_DeformQuadVertexPosition( tri, i + j );
			ac[i + j] = tri->verts[i + j];
		}

		// Match retail's square autosprite mode but preserve the authored
		// rectangular dimensions for rectsprite materials.
		mid = ( positions[0] + positions[1] + positions[2] + positions[3] ) * 0.25f;
		if ( radiusDimension ) {
			delta = positions[0] - mid;
			halfWidth = delta.Length() * idMath::SQRT_1OVER2;
			halfHeight = halfWidth;
		} else {
			halfWidth = 0.5f * ( positions[2] - positions[0] ).Length();
			halfHeight = 0.5f * ( positions[1] - positions[0] ).Length();

			// Fall back to symmetric sizing if the authored quad is degenerate.
			if ( halfWidth <= 0.0f || halfHeight <= 0.0f ) {
				delta = positions[0] - mid;
				halfWidth = delta.Length() * idMath::SQRT_1OVER2;
				halfHeight = halfWidth;
			}
		}

		left = leftDir * halfWidth;
		up = upDir * halfHeight;

		ac[i+0].xyz = mid + left + up;
		ac[i+0].st[0] = 0;
		ac[i+0].st[1] = 0;
		ac[i+1].xyz = mid - left + up;
		ac[i+1].st[0] = 1;
		ac[i+1].st[1] = 0;
		ac[i+2].xyz = mid - left - up;
		ac[i+2].st[0] = 1;
		ac[i+2].st[1] = 1;
		ac[i+3].xyz = mid + left - up;
		ac[i+3].st[0] = 0;
		ac[i+3].st[1] = 1;

		newTri->indexes[6*(i>>2)+0] = i;
		newTri->indexes[6*(i>>2)+1] = i+1;
		newTri->indexes[6*(i>>2)+2] = i+2;

		newTri->indexes[6*(i>>2)+3] = i;
		newTri->indexes[6*(i>>2)+4] = i+2;
		newTri->indexes[6*(i>>2)+5] = i+3;
	}

	R_FinishDeform( surf, newTri, ac );
}

/*
=====================
R_AutospriteDeform

Square autosprite helper retained for the legacy DFRM_SPRITE path.
=====================
*/
static void R_AutospriteDeform( drawSurf_t *surf ) {
	R_SpriteDeform( surf, true );
}

/*
=====================
R_TubeDeform

will pivot a rectangular quad along the center of its long axis

Note that a geometric tube with even quite a few sides tube will almost certainly render much faster
than this, so this should only be for faked volumetric tubes.
Make sure this is used with twosided translucent shaders, because the exact side
order may not be correct.
=====================
*/
static void R_TubeDeform( drawSurf_t *surf ) {
	int		i, j;
	int		indexes;
	const srfTriangles_t *tri;
static int edgeVerts[6][2] = {
	{ 0, 1 },
	{ 1, 2 },
	{ 2, 0 },
	{ 3, 4 },
	{ 4, 5 },
	{ 5, 3 }
};

	tri = surf->geo;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	srfTriangles_t	tempTri;
	if ( tri->primBatchMesh != NULL ) {
		idDrawVert *tempVerts = (idDrawVert *)_alloca16( tri->numVerts * sizeof( idDrawVert ) );
		glIndex_t *tempIndexes = (glIndex_t *)_alloca16( tri->numIndexes * sizeof( tempIndexes[0] ) );
		R_MaterializePrimBatchTriangles( tri, &tempTri, tempVerts, tempIndexes );
		tri = &tempTri;
	}
#endif

	if ( tri->numVerts & 3 ) {
		common->Error( "R_AutospriteDeform: shader had odd vertex count" );
	}
	if ( tri->numIndexes != ( tri->numVerts >> 2 ) * 6 ) {
		common->Error( "R_AutospriteDeform: autosprite had odd index count" );
	}

	// we need the view direction to project the minor axis of the tube
	// as the view changes
	idVec3	localView;
	R_GlobalPointToLocal( surf->space->modelMatrix, tr.viewDef->renderView.vieworg, localView ); 

	// this srfTriangles_t and all its indexes and caches are in frame
	// memory, and will be automatically disposed of
	srfTriangles_t *newTri = (srfTriangles_t *)R_ClearedFrameAlloc( sizeof( *newTri ) );
	newTri->numVerts = tri->numVerts;
	newTri->numIndexes = tri->numIndexes;
	newTri->indexes = (glIndex_t *)R_FrameAlloc( newTri->numIndexes * sizeof( newTri->indexes[0] ) );
	memcpy( newTri->indexes, tri->indexes, newTri->numIndexes * sizeof( newTri->indexes[0] ) );

	idDrawVert	*ac = (idDrawVert *)_alloca16( newTri->numVerts * sizeof( idDrawVert ) );
	memset( ac, 0, sizeof( idDrawVert ) * newTri->numVerts );

	// this is a lot of work for two triangles...
	// we could precalculate a lot if it is an issue, but it would mess up
	// the shader abstraction
	for ( i = 0, indexes = 0 ; i < tri->numVerts ; i+=4, indexes+=6 ) {
		float	lengths[2];
		int		nums[2];
		idVec3	mid[2];
		idVec3	major, minor;
		const idDrawVert *v1, *v2;

		// identify the two shortest edges out of the six defined by the indexes
		nums[0] = nums[1] = 0;
		lengths[0] = lengths[1] = 999999;

		for ( j = 0 ; j < 6 ; j++ ) {
			float	l;

			v1 = &tri->verts[tri->indexes[i+edgeVerts[j][0]]];
			v2 = &tri->verts[tri->indexes[i+edgeVerts[j][1]]];

			l = ( v1->xyz - v2->xyz ).Length();
			if ( l < lengths[0] ) {
				nums[1] = nums[0];
				lengths[1] = lengths[0];
				nums[0] = j;
				lengths[0] = l;
			} else if ( l < lengths[1] ) {
				nums[1] = j;
				lengths[1] = l;
			}
		}

		// find the midpoints of the two short edges, which
		// will give us the major axis in object coordinates
		for ( j = 0 ; j < 2 ; j++ ) {
			v1 = &tri->verts[tri->indexes[i+edgeVerts[nums[j]][0]]];
			v2 = &tri->verts[tri->indexes[i+edgeVerts[nums[j]][1]]];

			mid[j][0] = 0.5 * (v1->xyz[0] + v2->xyz[0]);
			mid[j][1] = 0.5 * (v1->xyz[1] + v2->xyz[1]);
			mid[j][2] = 0.5 * (v1->xyz[2] + v2->xyz[2]);
		}

		// find the vector of the major axis
		major = mid[1] - mid[0];

		// re-project the points
		for ( j = 0 ; j < 2 ; j++ ) {
			float	l;
			int	i1 = tri->indexes[i+edgeVerts[nums[j]][0]];
			int	i2 = tri->indexes[i+edgeVerts[nums[j]][1]];

			idDrawVert *av1 = &ac[i1];
			idDrawVert *av2 = &ac[i2];

			*av1 = *(idDrawVert *)&tri->verts[i1];
			*av2 = *(idDrawVert *)&tri->verts[i2];

			l = 0.5 * lengths[j];
			
			// cross this with the view direction to get minor axis
			idVec3	dir = mid[j] - localView;
			minor.Cross( major, dir );
			minor.Normalize();

			if ( j ) {
				av1->xyz = mid[j] - l * minor;
				av2->xyz = mid[j] + l * minor;
			} else {
				av1->xyz = mid[j] + l * minor;
				av2->xyz = mid[j] - l * minor;
			}
		}
	}

	R_FinishDeform( surf, newTri, ac );
}

/*
=====================
R_WindingFromTriangles

=====================
*/
#define	MAX_TRI_WINDING_INDEXES		16
int	R_WindingFromTriangles( const srfTriangles_t *tri, glIndex_t indexes[MAX_TRI_WINDING_INDEXES] ) {
	int			i, j, k, l;

	indexes[0] = tri->indexes[0];
	int numIndexes = 1;
	int	numTris = tri->numIndexes / 3;

	do {
		// find an edge that goes from the current index to another
		// index that isn't already used, and isn't an internal edge
		for ( i = 0 ; i < numTris ; i++ ) {
			for ( j = 0 ; j < 3 ; j++ ) {
				if ( tri->indexes[i*3+j] != indexes[numIndexes-1] ) {
					continue;
				}
				int		next = tri->indexes[i*3+(j+1)%3];

				// make sure it isn't already used
				if ( numIndexes == 1 ) {
					if ( next == indexes[0] ) {
						continue;
					}
				} else {
					for ( k = 1 ; k < numIndexes ; k++ ) {
						if ( indexes[k] == next ) {
							break;
						}
					}
					if ( k != numIndexes ) {
						continue;
					}
				}

				// make sure it isn't an interior edge
				for ( k = 0 ; k < numTris ; k++ ) {
					if ( k == i ) {
						continue;
					}
					for ( l = 0 ; l < 3 ; l++ ) {
						int	a, b;

						a = tri->indexes[k*3+l];
						if ( a != next ) {
							continue;
						}
						b = tri->indexes[k*3+(l+1)%3];
						if ( b != indexes[numIndexes-1] ) {
							continue;
						}

						// this is an interior edge
						break;
					}
					if ( l != 3 ) {
						break;
					}
				}
				if ( k != numTris ) {
					continue;
				}

				// add this to the list
				indexes[numIndexes] = next;
				numIndexes++;
				break;
			}
			if ( j != 3 ) {
				break;
			}
		}
		if ( numIndexes == tri->numVerts ) {
			break;
		}
	} while ( i != numTris );

	return numIndexes;
}

/*
=====================
R_FlareDeform

=====================
*/
/*
static void R_FlareDeform( drawSurf_t *surf ) {
	const srfTriangles_t *tri;
	srfTriangles_t		*newTri;
	idPlane	plane;
	float	dot;
	idVec3	localViewer;
	int		j;

	tri = surf->geo;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	srfTriangles_t	tempTri;
	if ( tri->primBatchMesh != NULL ) {
		idDrawVert *tempVerts = (idDrawVert *)_alloca16( tri->numVerts * sizeof( idDrawVert ) );
		glIndex_t *tempIndexes = (glIndex_t *)_alloca16( tri->numIndexes * sizeof( tempIndexes[0] ) );
		R_MaterializePrimBatchTriangles( tri, &tempTri, tempVerts, tempIndexes );
		tri = &tempTri;
	}
#endif

	if ( tri->numVerts != 4 || tri->numIndexes != 6 ) {
		//FIXME: temp hack for flares on tripleted models
		common->Warning( "R_FlareDeform: not a single quad" );
		return;
	}

	// this srfTriangles_t and all its indexes and caches are in frame
	// memory, and will be automatically disposed of
	newTri = (srfTriangles_t *)R_ClearedFrameAlloc( sizeof( *newTri ) );
	newTri->numVerts = 4;
	newTri->numIndexes = 2*3;
	newTri->indexes = (glIndex_t *)R_FrameAlloc( newTri->numIndexes * sizeof( newTri->indexes[0] ) );
	
	idDrawVert *ac = (idDrawVert *)_alloca16( newTri->numVerts * sizeof( idDrawVert ) );

	// find the plane
	plane.FromPoints( tri->verts[tri->indexes[0]].xyz, tri->verts[tri->indexes[1]].xyz, tri->verts[tri->indexes[2]].xyz );

	// if viewer is behind the plane, draw nothing
	R_GlobalPointToLocal( surf->space->modelMatrix, tr.viewDef->renderView.vieworg, localViewer );
	float distFromPlane = localViewer * plane.Normal() + plane[3];
	if ( distFromPlane <= 0 ) {
		newTri->numIndexes = 0;
		surf->geo = newTri;
		return;
	}

	idVec3	center;
	center = tri->verts[0].xyz;
	for ( j = 1 ; j < tri->numVerts ; j++ ) {
		center += tri->verts[j].xyz;
	}
	center *= 1.0/tri->numVerts;

	idVec3	dir = localViewer - center;
	dir.Normalize();

	dot = dir * plane.Normal();

	// set vertex colors based on plane angle
	int	color = (int)(dot * 8 * 256);
	if ( color > 255 ) {
		color = 255;
	}
	for ( j = 0 ; j < newTri->numVerts ; j++ ) {
		ac[j].color[0] =
		ac[j].color[1] =
		ac[j].color[2] = color;
		ac[j].color[3] = 255;
	}

	float	spread = surf->shaderRegisters[ surf->material->GetDeformRegister(0) ] * r_flareSize.GetFloat();
	idVec3	edgeDir[4][3];
	glIndex_t		indexes[MAX_TRI_WINDING_INDEXES];
	int		numIndexes = R_WindingFromTriangles( tri, indexes );

	surf->material = declManager->FindMaterial( "textures/smf/anamorphicFlare" );

	// only deal with quads
	if ( numIndexes != 4 ) {
		return;
	}

	// compute centroid
	idVec3 centroid, toeye, forward, up, left;
	centroid.Set( 0, 0, 0 );
	for ( int i = 0; i < 4; i++ ) {
		centroid += tri->verts[ indexes[i] ].xyz;
	}
	centroid /= 4;

	// compute basis vectors
	up.Set( 0, 0, 1 );

	toeye = centroid - localViewer;
	toeye.Normalize();
	left = toeye.Cross( up );
	up = left.Cross( toeye );

	left = left * 40 * 6;
	up = up * 40;

	// compute flares
	struct flare_t {
		float	angle;
		float	length;
	};

	static flare_t flares[] = {
		{ 0, 100 },
		{ 90, 100 }
	};

	for ( int i = 0; i < 4; i++ ) {
		memset( ac + i, 0, sizeof( ac[i] ) );
	}

	ac[0].xyz = centroid - left;
	ac[0].st[0] = 0; ac[0].st[1] = 0;

	ac[1].xyz = centroid + up;
	ac[1].st[0] = 1; ac[1].st[1] = 0;

	ac[2].xyz = centroid + left;
	ac[2].st[0] = 1; ac[2].st[1] = 1;

	ac[3].xyz = centroid - up;
	ac[3].st[0] = 0; ac[3].st[1] = 1;

	// setup colors
	for ( j = 0 ; j < newTri->numVerts ; j++ ) {
		ac[j].color[0] =
		ac[j].color[1] =
		ac[j].color[2] = 255;
		ac[j].color[3] = 255;
	}

	// setup indexes
	static glIndex_t	triIndexes[2*3] = {
		0,1,2,  0,2,3
	};

	memcpy( newTri->indexes, triIndexes, sizeof( triIndexes ) );

	R_FinishDeform( surf, newTri, ac );
}
*/

static void R_FlareDeform( drawSurf_t *surf ) {
	const srfTriangles_t *tri;
	srfTriangles_t		*newTri;
	idPlane	plane;
	float	dot;
	idVec3	localViewer;
	int		j;

	tri = surf->geo;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	srfTriangles_t	tempTri;
	if ( tri->primBatchMesh != NULL ) {
		idDrawVert *tempVerts = (idDrawVert *)_alloca16( tri->numVerts * sizeof( idDrawVert ) );
		glIndex_t *tempIndexes = (glIndex_t *)_alloca16( tri->numIndexes * sizeof( tempIndexes[0] ) );
		R_MaterializePrimBatchTriangles( tri, &tempTri, tempVerts, tempIndexes );
		tri = &tempTri;
	}
#endif

	if ( tri->numVerts != 4 || tri->numIndexes != 6 ) {
		//FIXME: temp hack for flares on tripleted models
		common->Warning( "R_FlareDeform: not a single quad" );
		return;
	}

	// this srfTriangles_t and all its indexes and caches are in frame
	// memory, and will be automatically disposed of
	newTri = (srfTriangles_t *)R_ClearedFrameAlloc( sizeof( *newTri ) );
	newTri->numVerts = 16;
	newTri->numIndexes = 18*3;
	newTri->indexes = (glIndex_t *)R_FrameAlloc( newTri->numIndexes * sizeof( newTri->indexes[0] ) );
	
	idDrawVert *ac = (idDrawVert *)_alloca16( newTri->numVerts * sizeof( idDrawVert ) );
	memset( ac, 0, sizeof( idDrawVert ) * newTri->numVerts );

	// find the plane
	if ( !plane.FromPoints( tri->verts[tri->indexes[0]].xyz, tri->verts[tri->indexes[1]].xyz, tri->verts[tri->indexes[2]].xyz ) ) {
		newTri->numIndexes = 0;
		surf->geo = newTri;
		return;
	}

	// if viewer is behind the plane, draw nothing
	R_GlobalPointToLocal( surf->space->modelMatrix, tr.viewDef->renderView.vieworg, localViewer );
	float distFromPlane = localViewer * plane.Normal() + plane[3];
	if ( distFromPlane <= 0 ) {
		newTri->numIndexes = 0;
		surf->geo = newTri;
		return;
	}

	idVec3	center;
	center = tri->verts[0].xyz;
	for ( j = 1 ; j < tri->numVerts ; j++ ) {
		center += tri->verts[j].xyz;
	}
	center *= 1.0/tri->numVerts;

	idVec3	dir = localViewer - center;
	dir.Normalize();

	dot = dir * plane.Normal();

	// set vertex colors based on plane angle
	int	color = (int)(dot * 8 * 256);
	if ( color > 255 ) {
		color = 255;
	}
	for ( j = 0 ; j < newTri->numVerts ; j++ ) {
		ac[j].color[0] =
		ac[j].color[1] =
		ac[j].color[2] = color;
		ac[j].color[3] = 255;
	}

	float	spread = surf->shaderRegisters[ surf->material->GetDeformRegister(0) ] * r_flareSize.GetFloat();
	idVec3	edgeDir[4][3];
	glIndex_t		indexes[MAX_TRI_WINDING_INDEXES];
	int		numIndexes = R_WindingFromTriangles( tri, indexes );


	// only deal with quads
	if ( numIndexes != 4 ) {
		return;
	}
	int i;
	// calculate vector directions
	for ( i = 0 ; i < 4 ; i++ ) {
		ac[i].xyz = tri->verts[ indexes[i] ].xyz;
		ac[i].st[0] =
		ac[i].st[1] = 0.5;

		idVec3	toEye = tri->verts[ indexes[i] ].xyz - localViewer;
		toEye.Normalize();

		idVec3	d1 = tri->verts[ indexes[(i+1)%4] ].xyz - localViewer; 
		d1.Normalize();
		edgeDir[i][1].Cross( toEye, d1 );
		edgeDir[i][1].Normalize();
		edgeDir[i][1] = vec3_origin - edgeDir[i][1];

		idVec3	d2 = tri->verts[ indexes[(i+3)%4] ].xyz - localViewer; 
		d2.Normalize();
		edgeDir[i][0].Cross( toEye, d2 );
		edgeDir[i][0].Normalize();

		edgeDir[i][2] = edgeDir[i][0] + edgeDir[i][1];
		edgeDir[i][2].Normalize();
	}

	// build all the points
	ac[4].xyz = tri->verts[ indexes[0] ].xyz + spread * edgeDir[0][0];
	ac[4].st[0] = 0;
	ac[4].st[1] = 0.5;

	ac[5].xyz = tri->verts[ indexes[0] ].xyz + spread * edgeDir[0][2];
	ac[5].st[0] = 0;
	ac[5].st[1] = 0;

	ac[6].xyz = tri->verts[ indexes[0] ].xyz + spread * edgeDir[0][1];
	ac[6].st[0] = 0.5;
	ac[6].st[1] = 0;


	ac[7].xyz = tri->verts[ indexes[1] ].xyz + spread * edgeDir[1][0];
	ac[7].st[0] = 0.5;
	ac[7].st[1] = 0;

	ac[8].xyz = tri->verts[ indexes[1] ].xyz + spread * edgeDir[1][2];
	ac[8].st[0] = 1;
	ac[8].st[1] = 0;

	ac[9].xyz = tri->verts[ indexes[1] ].xyz + spread * edgeDir[1][1];
	ac[9].st[0] = 1;
	ac[9].st[1] = 0.5;


	ac[10].xyz = tri->verts[ indexes[2] ].xyz + spread * edgeDir[2][0];
	ac[10].st[0] = 1;
	ac[10].st[1] = 0.5;

	ac[11].xyz = tri->verts[ indexes[2] ].xyz + spread * edgeDir[2][2];
	ac[11].st[0] = 1;
	ac[11].st[1] = 1;

	ac[12].xyz = tri->verts[ indexes[2] ].xyz + spread * edgeDir[2][1];
	ac[12].st[0] = 0.5;
	ac[12].st[1] = 1;


	ac[13].xyz = tri->verts[ indexes[3] ].xyz + spread * edgeDir[3][0];
	ac[13].st[0] = 0.5;
	ac[13].st[1] = 1;

	ac[14].xyz = tri->verts[ indexes[3] ].xyz + spread * edgeDir[3][2];
	ac[14].st[0] = 0;
	ac[14].st[1] = 1;

	ac[15].xyz = tri->verts[ indexes[3] ].xyz + spread * edgeDir[3][1];
	ac[15].st[0] = 0;
	ac[15].st[1] = 0.5;

	for ( i = 4 ; i < 16 ; i++ ) {
		idVec3	dir = ac[i].xyz - localViewer;
		float len = dir.Normalize();

		float ang = dir * plane.Normal();

//		ac[i].xyz -= dir * spread * 2;
		float newLen = -( distFromPlane / ang );

		if ( newLen > 0 && newLen < len ) {
			ac[i].xyz = localViewer + dir * newLen;
		}

		ac[i].st[0] = 0;
		ac[i].st[1] = 0.5;
	}

#if 1
	static glIndex_t	triIndexes[18*3] = {
		0,4,5,  0,5,6, 0,6,7, 0,7,1, 1,7,8, 1,8,9, 
		15,4,0, 15,0,3, 3,0,1, 3,1,2, 2,1,9, 2,9,10,
		14,15,3, 14,3,13, 13,3,2, 13,2,12, 12,2,11, 11,2,10
	};
#else
	newTri->numIndexes = 12;
	static glIndex_t triIndexes[4*3] = {
		0,1,2, 0,2,3, 0,4,5,0,5,6
	};
#endif

	memcpy( newTri->indexes, triIndexes, sizeof( triIndexes ) );

	R_FinishDeform( surf, newTri, ac );
}



/*
=====================
R_ExpandDeform

Expands the surface along it's normals by a shader amount
=====================
*/
static void R_ExpandDeform( drawSurf_t *surf ) {
	int		i;
	const srfTriangles_t	*tri;
	srfTriangles_t	*newTri;
	idDrawVert *ac;

	tri = surf->geo;

	// this srfTriangles_t and all its indexes and caches are in frame
	// memory, and will be automatically disposed of
	newTri = (srfTriangles_t *)R_ClearedFrameAlloc( sizeof( *newTri ) );
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	ac = (idDrawVert *)_alloca16( tri->numVerts * sizeof( idDrawVert ) );
	R_CopyDeformSourceTriangles( tri, newTri, ac );
	if ( tri->primBatchMesh != NULL ) {
		// Packed MD5R materialization reconstructs XYZ/ST/color data, so expand
		// must rebuild the tangent basis before it can offset along normals.
		newTri->verts = ac;
		R_DeriveTangents( newTri, false );
		newTri->verts = NULL;
	}
#else
	newTri->numVerts = tri->numVerts;
	newTri->numIndexes = tri->numIndexes;
	newTri->indexes = tri->indexes;
	ac = (idDrawVert *)_alloca16( newTri->numVerts * sizeof( idDrawVert ) );
#endif

	float dist = surf->shaderRegisters[ surf->material->GetDeformRegister(0) ];
	for ( i = 0 ; i < newTri->numVerts ; i++ ) {
#if !defined( _MD5R_SUPPORT ) && !defined( Q4SDK_MD5R )
		ac[i] = *(idDrawVert *)&tri->verts[i];
#endif
		ac[i].xyz += ac[i].normal * dist;
	}

	R_FinishDeform( surf, newTri, ac );
}

/*
=====================
R_MoveDeform

Moves the surface along the X axis, mostly just for demoing the deforms
=====================
*/
static void  R_MoveDeform( drawSurf_t *surf ) {
	int		i;
	const srfTriangles_t	*tri;
	srfTriangles_t	*newTri;

	tri = surf->geo;

	// this srfTriangles_t and all its indexes and caches are in frame
	// memory, and will be automatically disposed of
	newTri = (srfTriangles_t *)R_ClearedFrameAlloc( sizeof( *newTri ) );
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	idDrawVert *ac = (idDrawVert *)_alloca16( tri->numVerts * sizeof( idDrawVert ) );
	R_CopyDeformSourceTriangles( tri, newTri, ac );
#else
	newTri->numVerts = tri->numVerts;
	newTri->numIndexes = tri->numIndexes;
	newTri->indexes = tri->indexes;
	idDrawVert *ac = (idDrawVert *)_alloca16( newTri->numVerts * sizeof( idDrawVert ) );
#endif

	float dist = surf->shaderRegisters[ surf->material->GetDeformRegister(0) ];
	for ( i = 0 ; i < newTri->numVerts ; i++ ) {
#if !defined( _MD5R_SUPPORT ) && !defined( Q4SDK_MD5R )
		ac[i] = *(idDrawVert *)&tri->verts[i];
#endif
		ac[i].xyz[0] += dist;
	}

	R_FinishDeform( surf, newTri, ac );
}

//=====================================================================================

/*
=====================
R_TurbulentDeform

Turbulently deforms the XYZ, S, and T values
=====================
*/
static void  R_TurbulentDeform( drawSurf_t *surf ) {
	int		i;
	const srfTriangles_t	*tri;
	srfTriangles_t	*newTri;

	tri = surf->geo;

	// this srfTriangles_t and all its indexes and caches are in frame
	// memory, and will be automatically disposed of
	newTri = (srfTriangles_t *)R_ClearedFrameAlloc( sizeof( *newTri ) );
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	idDrawVert *ac = (idDrawVert *)_alloca16( tri->numVerts * sizeof( idDrawVert ) );
	R_CopyDeformSourceTriangles( tri, newTri, ac );
#else
	newTri->numVerts = tri->numVerts;
	newTri->numIndexes = tri->numIndexes;
	newTri->indexes = tri->indexes;
	idDrawVert *ac = (idDrawVert *)_alloca16( newTri->numVerts * sizeof( idDrawVert ) );
#endif

	idDeclTable	*table = (idDeclTable *)surf->material->GetDeformDecl();
	float range = surf->shaderRegisters[ surf->material->GetDeformRegister(0) ];
	float timeOfs = surf->shaderRegisters[ surf->material->GetDeformRegister(1) ];
	float domain = surf->shaderRegisters[ surf->material->GetDeformRegister(2) ];
	float tOfs = 0.5;

	for ( i = 0 ; i < newTri->numVerts ; i++ ) {
		float	f = ac[i].xyz[0] * 0.003 + ac[i].xyz[1] * 0.007 + ac[i].xyz[2] * 0.011;

		f = timeOfs + domain * f;
		f += timeOfs;

		ac[i].st[0] += range * table->TableLookup( f );
		ac[i].st[1] += range * table->TableLookup( f + tOfs );
	}

	R_FinishDeform( surf, newTri, ac );
}

//=====================================================================================

/*
=====================
AddTriangleToIsland_r

=====================
*/
#define	MAX_EYEBALL_TRIS	10
#define	MAX_EYEBALL_ISLANDS	6

typedef struct {
	int		tris[MAX_EYEBALL_TRIS];
	int		numTris;
	idBounds	bounds;
	idVec3		mid;
} eyeIsland_t;

static void AddTriangleToIsland_r( const srfTriangles_t *tri, int triangleNum, bool *usedList, eyeIsland_t *island ) {
	int		a, b, c;

	usedList[triangleNum] = true;

	// add to the current island
	if ( island->numTris == MAX_EYEBALL_TRIS ) {
		common->Error( "MAX_EYEBALL_TRIS" );
	}
	island->tris[island->numTris] = triangleNum;
	island->numTris++;

	// recurse into all neighbors
	a = tri->indexes[triangleNum*3];
	b = tri->indexes[triangleNum*3+1];
	c = tri->indexes[triangleNum*3+2];

	island->bounds.AddPoint( tri->verts[a].xyz );
	island->bounds.AddPoint( tri->verts[b].xyz );
	island->bounds.AddPoint( tri->verts[c].xyz );

	int	numTri = tri->numIndexes / 3;
	for ( int i = 0 ; i < numTri ; i++ ) {
		if ( usedList[i] ) {
			continue;
		}
		if ( tri->indexes[i*3+0] == a 
			|| tri->indexes[i*3+1] == a 
			|| tri->indexes[i*3+2] == a 
			|| tri->indexes[i*3+0] == b 
			|| tri->indexes[i*3+1] == b 
			|| tri->indexes[i*3+2] == b 
			|| tri->indexes[i*3+0] == c 
			|| tri->indexes[i*3+1] == c 
			|| tri->indexes[i*3+2] == c ) {
			AddTriangleToIsland_r( tri, i, usedList, island );
		}
	}
}

/*
=====================
R_EyeballDeform

Each eyeball surface should have an separate upright triangle behind it, long end
pointing out the eye, and another single triangle in front of the eye for the focus point.
=====================
*/
static void R_EyeballDeform( drawSurf_t *surf ) {
	int		i, j, k;
	const srfTriangles_t	*tri;
	srfTriangles_t	*newTri;
	eyeIsland_t	islands[MAX_EYEBALL_ISLANDS];
	int			numIslands;
	bool		triUsed[MAX_EYEBALL_ISLANDS*MAX_EYEBALL_TRIS];

	tri = surf->geo;
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	srfTriangles_t	tempTri;
	if ( tri->primBatchMesh != NULL ) {
		idDrawVert *tempVerts = (idDrawVert *)_alloca16( tri->numVerts * sizeof( idDrawVert ) );
		glIndex_t *tempIndexes = (glIndex_t *)_alloca16( tri->numIndexes * sizeof( tempIndexes[0] ) );
		R_MaterializePrimBatchTriangles( tri, &tempTri, tempVerts, tempIndexes );
		tri = &tempTri;
	}
#endif

	// separate all the triangles into islands
	int		numTri = tri->numIndexes / 3;
	if ( numTri > MAX_EYEBALL_ISLANDS*MAX_EYEBALL_TRIS ) {
		common->Printf( "R_EyeballDeform: too many triangles in surface" );
		return;
	}
	memset( triUsed, 0, sizeof( triUsed ) );

	for ( numIslands = 0  ; numIslands < MAX_EYEBALL_ISLANDS ; numIslands++ ) {
		islands[numIslands].numTris = 0;
		islands[numIslands].bounds.Clear();
		for ( i = 0 ; i < numTri ; i++ ) {
			if ( !triUsed[i] ) {
				AddTriangleToIsland_r( tri, i, triUsed, &islands[numIslands] );
				break;
			}
		}
		if ( i == numTri ) {
			break;
		}
	}

	// assume we always have two eyes, two origins, and two targets
	if ( numIslands != 3 ) {
		common->Printf( "R_EyeballDeform: %i triangle islands\n", numIslands );
		return;
	}

	// this srfTriangles_t and all its indexes and caches are in frame
	// memory, and will be automatically disposed of

	// the surface cannot have more indexes or verts than the original
	newTri = (srfTriangles_t *)R_ClearedFrameAlloc( sizeof( *newTri ) );
	memset( newTri, 0, sizeof( *newTri ) );
	newTri->numVerts = tri->numVerts;
	newTri->numIndexes = tri->numIndexes;
	newTri->indexes = (glIndex_t *)R_FrameAlloc( tri->numIndexes * sizeof( newTri->indexes[0] ) );
	idDrawVert *ac = (idDrawVert *)_alloca16( tri->numVerts * sizeof( idDrawVert ) );

	newTri->numIndexes = 0;

	// decide which islands are the eyes and points
	for ( i = 0 ; i < numIslands ; i++ ) {
		islands[i].mid = islands[i].bounds.GetCenter();
	}

	for ( i = 0 ; i < numIslands ; i++ ) {
		eyeIsland_t		*island = &islands[i];

		if ( island->numTris == 1 ) {
			continue;
		}

		// the closest single triangle point will be the eye origin
		// and the next-to-farthest will be the focal point
		idVec3	origin, focus;
		int		originIsland = 0;
		float	dist[MAX_EYEBALL_ISLANDS];
		int		sortOrder[MAX_EYEBALL_ISLANDS];

		for ( j = 0 ; j < numIslands ; j++ ) {
			idVec3	dir = islands[j].mid - island->mid;
			dist[j] = dir.Length();
			sortOrder[j] = j;
			for ( k = j-1 ; k >= 0 ; k-- ) {
				if ( dist[k] > dist[k+1] ) {
					int	temp = sortOrder[k];
					sortOrder[k] = sortOrder[k+1];
					sortOrder[k+1] = temp;
					float	ftemp = dist[k];
					dist[k] = dist[k+1];
					dist[k+1] = ftemp;
				}
			}
		}

		originIsland = sortOrder[1];
		origin = islands[originIsland].mid;

		focus = islands[sortOrder[2]].mid;

		// determine the projection directions based on the origin island triangle
		idVec3	dir = focus - origin;
		dir.Normalize();

		const idVec3 &p1 = tri->verts[tri->indexes[islands[originIsland].tris[0]+0]].xyz;
		const idVec3 &p2 = tri->verts[tri->indexes[islands[originIsland].tris[0]+1]].xyz;
		const idVec3 &p3 = tri->verts[tri->indexes[islands[originIsland].tris[0]+2]].xyz;

		idVec3	v1 = p2 - p1;
		v1.Normalize();
		idVec3	v2 = p3 - p1;
		v2.Normalize();

		// texVec[0] will be the normal to the origin triangle
		idVec3	texVec[2];

		texVec[0].Cross( v1, v2 );

		texVec[1].Cross( texVec[0], dir );

		for ( j = 0 ; j < 2 ; j++ ) {
			texVec[j] -= dir * ( texVec[j] * dir );
			texVec[j].Normalize();
		}

		// emit these triangles, generating the projected texcoords

		for ( j = 0 ; j < islands[i].numTris ; j++ ) {
			for ( k = 0 ; k < 3 ; k++ ) {
				int	index = islands[i].tris[j] * 3;

				index = tri->indexes[index+k];
				newTri->indexes[newTri->numIndexes++] = index;

				ac[index].xyz = tri->verts[index].xyz;

				idVec3	local = tri->verts[index].xyz - origin;

				ac[index].st[0] = 0.5 + local * texVec[0];
				ac[index].st[1] = 0.5 + local * texVec[1];
			}
		}
	}

	R_FinishDeform( surf, newTri, ac );
}

//==========================================================================================


/*
=====================
R_ParticleDeform

Emit particles from the surface instead of drawing it
=====================
*/
static void R_ParticleDeform( drawSurf_t *surf, bool useArea ) {
	
}

//========================================================================================

/*
=================
R_DeformDrawSurf
=================
*/
void R_DeformDrawSurf( drawSurf_t *drawSurf ) {
	if ( !drawSurf->material ) {
		return;
	}

	if ( r_skipDeforms.GetBool() ) {
		return;
	}
	switch ( drawSurf->material->Deform() ) {
	case DFRM_NONE:
		return;
	case DFRM_SPRITE:
		R_AutospriteDeform( drawSurf );
		break;
	case DFRM_RECTSPRITE:
		R_SpriteDeform( drawSurf, false );
		break;
	case DFRM_TUBE:
		R_TubeDeform( drawSurf );
		break;
	case DFRM_FLARE:
		R_FlareDeform( drawSurf );
		break;
	case DFRM_EXPAND:
		R_ExpandDeform( drawSurf );
		break;
	case DFRM_MOVE:
		R_MoveDeform( drawSurf );
		break;
	case DFRM_TURB:
		R_TurbulentDeform( drawSurf );
		break;
	case DFRM_EYEBALL:
		R_EyeballDeform( drawSurf );
		break;
	case DFRM_PARTICLE:
		R_ParticleDeform( drawSurf, true );
		break;
	case DFRM_PARTICLE2:
		R_ParticleDeform( drawSurf, false );
		break;
	}
}
