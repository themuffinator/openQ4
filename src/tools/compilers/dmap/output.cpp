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




#include "dmap.h"

#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

//=================================================================================


#if 0

should we try and snap values very close to 0.5, 0.25, 0.125, etc?

  do we write out normals, or just a "smooth shade" flag?
resolved: normals.  otherwise adjacent facet shaded surfaces get their
		  vertexes merged, and they would have to be split apart before drawing

  do we save out "wings" for shadow silhouette info?


#endif

static	idFile	*procFile;

#define	AREANUM_DIFFERENT	-2
/*
=============
PruneNodes_r

Any nodes that have all children with the same
area can be combined into a single leaf node

Returns the area number of all children, or
AREANUM_DIFFERENT if not the same.
=============
*/
int	PruneNodes_r( node_t *node ) {
	int		a1, a2;

	if ( node->planenum == PLANENUM_LEAF ) {
		return node->area;
	}

	a1 = PruneNodes_r( node->children[0] );
	a2 = PruneNodes_r( node->children[1] );

	if ( a1 != a2 || a1 == AREANUM_DIFFERENT ) {
		return AREANUM_DIFFERENT;
	}

	// free all the nodes below this point
	FreeTreePortals_r( node->children[0] );
	FreeTreePortals_r( node->children[1] );
	FreeTree_r( node->children[0] );
	FreeTree_r( node->children[1] );

	// change this node to a leaf
	node->planenum = PLANENUM_LEAF;
	node->area = a1;

	return a1;
}

static void WriteFloat( idFile *f, float v )
{
	if ( idMath::Fabs(v - idMath::Rint(v)) < 0.001 ) {
		f->WriteFloatString( "%i ", (int)idMath::Rint(v) );
	}
	else {
		f->WriteFloatString( "%f ", v );
	}
}

void Write1DMatrix( idFile *f, int x, float *m ) {
	int		i;

	f->WriteFloatString( "( " );

	for ( i = 0; i < x; i++ ) {
		WriteFloat( f, m[i] );
	}

	f->WriteFloatString( ") " );
}

/*
Groups become one surface when their material and merge group match. Curved
surfaces leave one group per plane, so an area can hold many thousands of groups;
comparing each with every other made writing the .proc quadratic in them
(issue #209). SurfaceBuckets chains each group to the next compatible group in
list order, in one pass.
*/
static int SurfaceKey( const optimizeGroup_t *group ) {
	const uintptr_t m = reinterpret_cast<uintptr_t>( group->material );
	const uintptr_t g = reinterpret_cast<uintptr_t>( group->mergeGroup );
	const unsigned int key = static_cast<unsigned int>( m >> 4 ) ^ static_cast<unsigned int>( m >> 20 )
		^ static_cast<unsigned int>( g >> 4 ) * 2654435761u ^ static_cast<unsigned int>( g >> 20 );
	return static_cast<int>( key & 0x7fffffffu );
}

// groups[] in list order; next[i] is the next group compatible with groups[i]
static void SurfaceBuckets( optimizeGroup_t *list, idList<optimizeGroup_t *> &groups, idList<int> &next ) {
	idHashIndex	lastOfKey;
	idList<int>	last;

	groups.Clear();
	for ( optimizeGroup_t *group = list ; group ; group = group->nextGroup ) {
		groups.Append( group );
	}
	next.SetNum( groups.Num() );
	lastOfKey.Clear( 1024, groups.Num() > 0 ? groups.Num() : 1 );
	for ( int i = 0 ; i < groups.Num() ; i++ ) {
		next[i] = -1;
		const int key = SurfaceKey( groups[i] );
		int bucket;
		for ( bucket = lastOfKey.First( key ) ; bucket != -1 ; bucket = lastOfKey.Next( bucket ) ) {
			const optimizeGroup_t *other = groups[last[bucket]];
			if ( other->material == groups[i]->material && other->mergeGroup == groups[i]->mergeGroup ) {
				break;
			}
		}
		if ( bucket == -1 ) {
			bucket = last.Append( i );
			lastOfKey.Add( key, bucket );
		} else {
			next[last[bucket]] = i;
			last[bucket] = i;
		}
	}
}

static int CountUniqueShaders( optimizeGroup_t *groups ) {
	idList<optimizeGroup_t *>	list;
	idList<int>					next;
	idList<bool>				counted;
	int							count = 0;

	// a surface is written for every bucket with at least one triangle
	SurfaceBuckets( groups, list, next );
	counted.AssureSize( list.Num(), false );
	for ( int i = 0 ; i < list.Num() ; i++ ) {
		if ( counted[i] ) {
			continue;
		}
		bool hasTris = false;
		for ( int j = i ; j != -1 ; j = next[j] ) {
			counted[j] = true;
			hasTris |= ( list[j]->triList != NULL );
		}
		if ( hasTris ) {
			count++;
		}
	}

	return count;
}


/*
==============
MatchVert
==============
*/
#define	XYZ_EPSILON	0.01
#define	ST_EPSILON	0.001
#define	COSINE_EPSILON	0.999

static bool MatchVert( const idDrawVert *a, const idDrawVert *b ) {
	if ( idMath::Fabs( a->xyz[0] - b->xyz[0] ) > XYZ_EPSILON ) {
		return false;
	}
	if ( idMath::Fabs( a->xyz[1] - b->xyz[1] ) > XYZ_EPSILON ) {
		return false;
	}
	if ( idMath::Fabs( a->xyz[2] - b->xyz[2] ) > XYZ_EPSILON ) {
		return false;
	}
	if ( idMath::Fabs( a->st[0] - b->st[0] ) > ST_EPSILON ) {
		return false;
	}
	if ( idMath::Fabs( a->st[1] - b->st[1] ) > ST_EPSILON ) {
		return false;
	}

	// if the normal is 0 (smoothed normals), consider it a match
	if ( a->normal[0] == 0 && a->normal[1] == 0 && a->normal[2] == 0 
		&& b->normal[0] == 0 && b->normal[1] == 0 && b->normal[2] == 0 ) {
		return true;
	}

	// otherwise do a dot-product cosine check
	if ( DotProduct( a->normal, b->normal ) < COSINE_EPSILON ) {
		return false;
	}

	return true;
}

// Unit-sized spatial buckets keep the epsilon search local. Search both sides
// of a bucket boundary and retain the earliest matching vertex, just as the
// original linear search did. Float keys avoid narrowing large map coordinates
// to an integer; floor() still makes every key an integral position.
typedef std::array<float, 3> mapTriVertexCell_t;

struct mapTriVertexCellHash_t {
	size_t operator()( const mapTriVertexCell_t &cell ) const {
		size_t hash = 0;
		for ( int i = 0; i < 3; i++ ) {
			hash ^= std::hash<float>()( cell[i] ) + size_t( 0x9e3779b9u ) + ( hash << 6 ) + ( hash >> 2 );
		}
		return hash;
	}
};

/*
====================
ShareMapTriVerts

Converts independent triangles to shared vertex triangles
====================
*/
srfTriangles_t	*ShareMapTriVerts( const mapTri_t *tris ) {
	const int count = CountTriList( tris );
	if ( count < 0 || count > idMath::INT_MAX / 3 ) {
		common->Error( "ShareMapTriVerts: too many triangles (%i)", count );
		return NULL;
	}

	srfTriangles_t *uTri = R_AllocStaticTriSurf();
	const int numIndexes = count * 3;
	R_AllocStaticTriSurfIndexes( uTri, numIndexes );
	if ( numIndexes != 0 && uTri->indexes == NULL ) {
		R_FreeStaticTriSurf( uTri );
		common->Error( "ShareMapTriVerts: could not allocate %i indexes", numIndexes );
		return NULL;
	}

	// Keep references to the first occurrence of each vertex until sharing is
	// complete. Reserving three full vertices per triangle can exceed the heap's
	// single-allocation limit even when the shared surface is small (#181).
	std::vector<const idDrawVert *> uniqueVerts;
	std::unordered_map<mapTriVertexCell_t, std::vector<int>, mapTriVertexCellHash_t> cells;
	int index = 0;
	for ( const mapTri_t *step = tris; step; step = step->next ) {
		for ( int i = 0; i < 3; i++ ) {
			const idDrawVert *dv = &step->v[i];
			mapTriVertexCell_t cell;
			float neighbors[3][2];
			int numNeighbors[3];
			for ( int axis = 0; axis < 3; axis++ ) {
				if ( !std::isfinite( dv->xyz[axis] ) ) {
					R_FreeStaticTriSurf( uTri );
					common->Error( "ShareMapTriVerts: non-finite vertex position" );
					return NULL;
				}
				cell[axis] = float( std::floor( double( dv->xyz[axis] ) ) );
				// Leave room for rounding in MatchVert's float subtraction.
				neighbors[axis][0] = float( std::floor( double( dv->xyz[axis] ) - 2 * XYZ_EPSILON ) );
				neighbors[axis][1] = float( std::floor( double( dv->xyz[axis] ) + 2 * XYZ_EPSILON ) );
				numNeighbors[axis] = neighbors[axis][0] == neighbors[axis][1] ? 1 : 2;
			}

			int match = int( uniqueVerts.size() );
			for ( int x = 0; x < numNeighbors[0]; x++ ) {
				for ( int y = 0; y < numNeighbors[1]; y++ ) {
					for ( int z = 0; z < numNeighbors[2]; z++ ) {
						const mapTriVertexCell_t neighbor = { neighbors[0][x], neighbors[1][y], neighbors[2][z] };
						const auto found = cells.find( neighbor );
						if ( found == cells.end() ) {
							continue;
						}
						for ( int candidate : found->second ) {
							if ( candidate < match && MatchVert( uniqueVerts[candidate], dv ) ) {
								match = candidate;
								break;
							}
						}
					}
				}
			}
			if ( match == int( uniqueVerts.size() ) ) {
				uniqueVerts.push_back( dv );
				cells[cell].push_back( match );
			}
			uTri->indexes[index++] = match;
		}
	}

	const int numVerts = int( uniqueVerts.size() );
	R_AllocStaticTriSurfVerts( uTri, numVerts );
	if ( numVerts != 0 && uTri->verts == NULL ) {
		R_FreeStaticTriSurf( uTri );
		common->Error( "ShareMapTriVerts: could not allocate %i shared vertices", numVerts );
		return NULL;
	}
	for ( int i = 0; i < numVerts; i++ ) {
		uTri->verts[i].Clear();
		uTri->verts[i].xyz = uniqueVerts[i]->xyz;
		uTri->verts[i].normal = uniqueVerts[i]->normal;
		uTri->verts[i].st = uniqueVerts[i]->st;
	}
	uTri->numVerts = numVerts;
	uTri->numIndexes = numIndexes;

	return uTri;
}

/*
==================
CleanupUTriangles
==================
*/
static void CleanupUTriangles( srfTriangles_t *tri ) {
	// perform cleanup operations

	R_RangeCheckIndexes( tri );
	R_CreateSilIndexes( tri );
//	R_RemoveDuplicatedTriangles( tri );	// this may remove valid overlapped transparent triangles
	R_RemoveDegenerateTriangles( tri );
//	R_RemoveUnusedVerts( tri );

	R_FreeStaticTriSurfSilIndexes( tri );
}

/*
====================
WriteUTriangles

Writes text verts and indexes to procfile
====================
*/
static void WriteUTriangles( const srfTriangles_t *uTris ) {
	int			col;
	int			i;

	// emit this chain
	procFile->WriteFloatString( "/* numVerts = */ %i /* numIndexes = */ %i\n", 
		uTris->numVerts, uTris->numIndexes );

	// verts
	col = 0;
	for ( i = 0 ; i < uTris->numVerts ; i++ ) {
		float	vec[8];
		const idDrawVert *dv;

		dv = &uTris->verts[i];

		vec[0] = dv->xyz[0];
		vec[1] = dv->xyz[1];
		vec[2] = dv->xyz[2];
		vec[3] = dv->st[0];
		vec[4] = dv->st[1];
		vec[5] = dv->normal[0];
		vec[6] = dv->normal[1];
		vec[7] = dv->normal[2];
		Write1DMatrix( procFile, 8, vec );

		if ( ++col == 3 ) {
			col = 0;
			procFile->WriteFloatString( "\n" );
		}
	}
	if ( col != 0 ) {
		procFile->WriteFloatString( "\n" );
	}

	// indexes
	col = 0;
	for ( i = 0 ; i < uTris->numIndexes ; i++ ) {
		procFile->WriteFloatString( "%i ", uTris->indexes[i] );

		if ( ++col == 18 ) {
			col = 0;
			procFile->WriteFloatString( "\n" );
		}
	}
	if ( col != 0 ) {
		procFile->WriteFloatString( "\n" );
	}
}


/*
====================
WriteShadowTriangles

Writes text verts and indexes to procfile
====================
*/
static void WriteShadowTriangles( const srfTriangles_t *tri ) {
	int			col;
	int			i;

	// emit this chain
	procFile->WriteFloatString( "/* numVerts = */ %i /* noCaps = */ %i /* noFrontCaps = */ %i /* numIndexes = */ %i /* planeBits = */ %i\n", 
		tri->numVerts, tri->numShadowIndexesNoCaps, tri->numShadowIndexesNoFrontCaps, tri->numIndexes, tri->shadowCapPlaneBits );

	// verts
	col = 0;
	for ( i = 0 ; i < tri->numVerts ; i++ ) {
		Write1DMatrix( procFile, 3, &tri->shadowVertexes[i].xyz[0] );

		if ( ++col == 5 ) {
			col = 0;
			procFile->WriteFloatString( "\n" );
		}
	}
	if ( col != 0 ) {
		procFile->WriteFloatString( "\n" );
	}

	// indexes
	col = 0;
	for ( i = 0 ; i < tri->numIndexes ; i++ ) {
		procFile->WriteFloatString( "%i ", tri->indexes[i] );

		if ( ++col == 18 ) {
			col = 0;
			procFile->WriteFloatString( "\n" );
		}
	}
	if ( col != 0 ) {
		procFile->WriteFloatString( "\n" );
	}
}


/*
====================
WriteOutputSurfaces
====================
*/
static void WriteOutputSurfaces( int entityNum, int areaNum ) {
	mapTri_t	*ambient, *copy;
	int			surfaceNum;
	int			numSurfaces;
	idMapEntity	*entity;
	uArea_t		*area;
	optimizeGroup_t	*group;
	srfTriangles_t	*uTri;
	idList<optimizeGroup_t *>	groups;
	idList<int>					nextCompatible;


	area = &dmapGlobals.uEntities[entityNum].areas[areaNum];
	entity = dmapGlobals.uEntities[entityNum].mapEntity;

	numSurfaces = CountUniqueShaders( area->groups );


	if ( entityNum == 0 ) {
// jmarshall - quake 4 proc format
		procFile->WriteFloatString( "model { /* name = */ \"_area%i\" /* numSurfaces = */ %i /* sky */ 0\n\n", 
			areaNum, numSurfaces );
// jmarshall end
	} else {
		const char *name;

		entity->epairs.GetString( "name", "", &name );
		if ( !name[0] ) {
			common->Error( "Entity %i has surfaces, but no name key", entityNum );
		}
// jmarshall - quake 4 proc format
		procFile->WriteFloatString( "model { /* name = */ \"%s\" /* numSurfaces = */ %i /* sky */ 0\n\n", 
			name, numSurfaces );
// jmarshall end
	}

	SurfaceBuckets( area->groups, groups, nextCompatible );

	surfaceNum = 0;
	for ( int g = 0 ; g < groups.Num() ; g++ ) {
		group = groups[g];
		if ( group->surfaceEmited ) {
			continue;
		}

		// combine all groups compatible with this one
		// usually several optimizeGroup_t can be combined into a single
		// surface, even though they couldn't be merged together to save
		// vertexes because they had different planes, texture coordinates, or lights.
		// Different mergeGroups will stay in separate surfaces.
		// The Quake 4 .proc carries no per-light interaction surfaces, so only
		// the ambient triangles are gathered.
		ambient = NULL;
		mapTri_t **ambientTail = &ambient;

		for ( int step = g ; step != -1 ; step = nextCompatible[step] ) {
			optimizeGroup_t *groupStep = groups[step];
			if ( groupStep->surfaceEmited ) {
				continue;
			}

			// copy it out to the ambient list
			copy = CopyTriList( groupStep->triList );
			*ambientTail = copy;
			while ( *ambientTail ) {
				ambientTail = &(*ambientTail)->next;
			}
			groupStep->surfaceEmited = true;
		}

		if ( !ambient ) {
			continue;
		}

		if ( surfaceNum >= numSurfaces ) {
			common->Error( "WriteOutputSurfaces: surfaceNum >= numSurfaces" );
		}

		procFile->WriteFloatString( "/* surface %i */ { ", surfaceNum );
		surfaceNum++;
		procFile->WriteFloatString( "\"%s\" ", ambient->material->GetName() );

		uTri = ShareMapTriVerts( ambient );
		FreeTriList( ambient );

		CleanupUTriangles( uTri );
		WriteUTriangles( uTri );
		R_FreeStaticTriSurf( uTri );

		procFile->WriteFloatString( "}\n\n" );
	}

	procFile->WriteFloatString( "}\n\n" );
}

/*
===============
WriteNode_r

===============
*/
static void WriteNode_r( node_t *node ) {
	int		child[2];
	int		i;
	idPlane	*plane;

	if ( node->planenum == PLANENUM_LEAF ) {
		// we shouldn't get here unless the entire world
		// was a single leaf
		procFile->WriteFloatString( "/* node 0 */ ( 0 0 0 0 ) -1 -1\n" );
		return;
	}

	for ( i = 0 ; i < 2 ; i++ ) {
		if ( node->children[i]->planenum == PLANENUM_LEAF ) {
			child[i] = -1 - node->children[i]->area;
		} else {
			child[i] = node->children[i]->nodeNumber;
		}
	}

	plane = &dmapGlobals.mapPlanes[node->planenum];

	procFile->WriteFloatString( "/* node %i */ ", node->nodeNumber  );
	Write1DMatrix( procFile, 4, plane->ToFloatPtr() );
	procFile->WriteFloatString( "%i %i\n", child[0], child[1] );

	if ( child[0] > 0 ) {
		WriteNode_r( node->children[0] );
	}
	if ( child[1] > 0 ) {
		WriteNode_r( node->children[1] );
	}
}

static int NumberNodes_r( node_t *node, int nextNumber ) {
	if ( node->planenum == PLANENUM_LEAF ) {
		return nextNumber;
	}
	node->nodeNumber = nextNumber;
	nextNumber++;
	nextNumber = NumberNodes_r( node->children[0], nextNumber );
	nextNumber = NumberNodes_r( node->children[1], nextNumber );

	return nextNumber;
}

/*
====================
WriteOutputNodes
====================
*/
static void WriteOutputNodes( node_t *node ) {
	int		numNodes;

	// prune unneeded nodes and count
	PruneNodes_r( node );
	numNodes = NumberNodes_r( node, 0 );

	// output
	procFile->WriteFloatString( "nodes { /* numNodes = */ %i\n\n", numNodes );
	procFile->WriteFloatString( "/* node format is: ( planeVector ) positiveChild negativeChild */\n" );
	procFile->WriteFloatString( "/* a child number of 0 is an opaque, solid area */\n" );
	procFile->WriteFloatString( "/* negative child numbers are areas: (-1-child) */\n" );

	WriteNode_r( node );

	procFile->WriteFloatString( "}\n\n" );
}

/*
====================
WriteOutputPortals
====================
*/
static void WriteOutputPortals( uEntity_t *e ) {
	int			i, j;
	interAreaPortal_t	*iap;
	idWinding			*w;

	procFile->WriteFloatString( "interAreaPortals { /* numAreas = */ %i /* numIAP = */ %i\n\n", 
		e->numAreas, numInterAreaPortals );
	procFile->WriteFloatString( "/* interAreaPortal format is: numPoints positiveSideArea negativeSideArea ( point ) ... ( point ) [( fadeImage distanceNear distanceFar )] */\n" );
	for ( i = 0 ; i < numInterAreaPortals ; i++ ) {
		iap = &interAreaPortals[i];
		w = iap->side->winding;
		procFile->WriteFloatString("/* iap %i */ %i %i %i ", i, w->GetNumPoints(), iap->area0, iap->area1 );
		for ( j = 0 ; j < w->GetNumPoints() ; j++ ) {
			Write1DMatrix( procFile, 3, (*w)[j].ToFloatPtr() );
		}

		if ( iap->portalImageName.Length() > 0 ) {
			procFile->WriteFloatString( "( \"%s\" %.2f %.2f )", iap->portalImageName.c_str(), iap->cullNear, iap->cullFar );
		} else if ( iap->cullNear < 262144.0f || iap->cullFar < 262144.0f ) {
			procFile->WriteFloatString( "( \"_black\" %.2f %.2f )", iap->cullNear, iap->cullFar );
		}

		procFile->WriteFloatString("\n" );
	}

	procFile->WriteFloatString( "}\n\n" );
}


/*
====================
WriteOutputEntity
====================
*/
static void WriteOutputEntity( int entityNum ) {
	int		i;
	uEntity_t *e;

	e = &dmapGlobals.uEntities[entityNum];

	if ( entityNum != 0 ) {
		// entities may have enclosed, empty areas that we don't need to write out
		if ( e->numAreas > 1 ) {
			e->numAreas = 1;
		}
	}

	for ( i = 0 ; i < e->numAreas ; i++ ) {
		WriteOutputSurfaces( entityNum, i );
	}

	// we will completely skip the portals and nodes if it is a single area
	if ( entityNum == 0 && e->numAreas > 1 ) {
		// output the area portals
		WriteOutputPortals( e );

		// output the nodes
		WriteOutputNodes( e->tree->headnode );
	}
}


/*
====================
WriteOutputFile
====================
*/
void WriteOutputFile( void ) {
	int				i;
	uEntity_t		*entity;
	idStr			qpath;

	// write the file
	common->Printf( "----- WriteOutputFile -----\n" );

	sprintf( qpath, "%s." PROC_FILE_EXT, dmapGlobals.mapFileBase );

	// _D3XP used fs_cdpath; com_compilerOutput can redirect it (AASFile.cpp)
	extern const char *Compiler_OutputBasePath( void );
	procFile = fileSystem->OpenFileWrite( qpath, Compiler_OutputBasePath() );
	if ( !procFile ) {
		common->Error( "Error opening %s", qpath.c_str() );
	}

	// dmap writes beside the engine rather than beside the .map it read, so
	// report where the output actually landed instead of leaving map authors
	// to hunt for it
	common->Printf( "writing %s\n", procFile->GetFullPath() );
// jmarshall - Quake 4 proc support
	procFile->WriteFloatString( "%s \"%s\"\n\n", PROC_FILE_ID, PROC_FILEVERSION);

	procFile->WriteFloatString( "%u\n", dmapGlobals.dmapFile->GetGeometryCRC() );
// jmarshall end

	// write the entity models and information, writing entities first
	for ( i=dmapGlobals.num_entities - 1 ; i >= 0 ; i-- ) {
		entity = &dmapGlobals.uEntities[i];
	
		if ( !entity->primitives ) {
			continue;
		}

		WriteOutputEntity( i );
	}

	// write the shadow volumes
	for ( i = 0 ; i < dmapGlobals.mapLights.Num() ; i++ ) {
		mapLight_t	*light = dmapGlobals.mapLights[i];
		if ( !light->shadowTris ) {
			continue;
		}

		procFile->WriteFloatString( "shadowModel { /* name = */ \"_prelight_%s\"\n\n", light->name );
		WriteShadowTriangles( light->shadowTris );
		procFile->WriteFloatString( "}\n\n" );

		R_FreeStaticTriSurf( light->shadowTris );
		light->shadowTris = NULL;
	}

	fileSystem->CloseFile( procFile );
}
