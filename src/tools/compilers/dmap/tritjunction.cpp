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

/*

  T junction fixing never creates more xyz points, but
  new vertexes will be created when different surfaces
  cause a fix

  The vertex cleaning accomplishes two goals: removing extranious low order
  bits to avoid numbers like 1.000001233, and grouping nearby vertexes
  together.  Straight truncation accomplishes the first foal, but two vertexes
  only a tiny epsilon apart could still be spread to different snap points.
  To avoid this, we allow the merge test to group points together that
  snapped to neighboring integer coordinates.

  Snaping verts can drag some triangles backwards or collapse them to points,
  which will cause them to be removed.
  

  When snapping to ints, a point can move a maximum of sqrt(3)/2 distance
  Two points that were an epsilon apart can then become sqrt(3) apart

  A case that causes recursive overflow with point to triangle fixing:

               A
	C            D
	           B

  Triangle ABC tests against point D and splits into triangles ADC and DBC
  Triangle DBC then tests against point A again and splits into ABC and ADB
  infinite recursive loop


  For a given source triangle
	init the no-check list to hold the three triangle hashVerts

  recursiveFixTriAgainstHash

  recursiveFixTriAgainstHashVert_r
	if hashVert is on the no-check list
		exit
	if the hashVert should split the triangle
		add to the no-check list
		recursiveFixTriAgainstHash(a)
		recursiveFixTriAgainstHash(b)

*/

#define	SNAP_FRACTIONS	32
//#define	SNAP_FRACTIONS	8
//#define	SNAP_FRACTIONS	1

#define	VERTEX_EPSILON	( 1.0 / SNAP_FRACTIONS )

#define	COLINEAR_EPSILON	( 1.8 * VERTEX_EPSILON )

#define	HASH_BINS	16

typedef struct hashVert_s {
	struct hashVert_s	*next;
	idVec3				v;
	int					iv[3];
	int					block[3];		// hash bin
	int					blockSeq;		// creation order within the bin
	int					cell[3];		// TJUNC_CELL_SIZE grid cell
} hashVert_t;

static idBounds	hashBounds;
static idVec3	hashScale;
static hashVert_t	*hashVerts[HASH_BINS][HASH_BINS][HASH_BINS];
static int		numHashVerts, numTotalVerts;
static int		hashIntMins[3], hashIntScale[3];

/*
  Each of the 16x16x16 hash bins spans a sixteenth of the map on every axis.
  Dense geometry, such as heavily subdivided patches, puts most of a map's
  vertexes into a few bins, and both the vertex merge and the T-junction pass
  scanned whole bins: the compile time grew with the square of those vertexes
  (issue #209). Two indexes answer the same questions without scanning:

  - hashVertCells finds the vertex at an exact snapped coordinate, so a merge
    looks at the 27 neighboring coordinates instead of the whole bin.
  - hashVertGrid buckets vertexes by TJUNC_CELL_SIZE cells, so a triangle only
    visits the vertexes near it.

  The bins, their newest-first chains and every result are unchanged; see
  GetHashVert and FixTriangleAgainstHash for why the shortcuts are exact.
*/
#define	TJUNC_CELL_SIZE		16.0f
#define	TJUNC_MAX_GRID_CELLS	64

static idList<hashVert_t *>	hashVertList;		// creation order
static idHashIndex			hashVertCells;
static idHashIndex			hashVertGrid;
static bool					hashVertGridBuilt;
static int					hashBinCounts[HASH_BINS][HASH_BINS][HASH_BINS];

static int TJuncKey( int x, int y, int z ) {
	// unsigned arithmetic: the products wrap by design
	const unsigned int key = static_cast<unsigned int>( x ) * 73856093u ^ static_cast<unsigned int>( y ) * 19349663u
		^ static_cast<unsigned int>( z ) * 83492791u;
	return static_cast<int>( key & 0x7fffffffu );
}

static int HashBin( int iv, int axis ) {
	int block = ( iv - hashIntMins[axis] ) / hashIntScale[axis];
	if ( block < 0 ) {
		block = 0;
	} else if ( block >= HASH_BINS ) {
		block = HASH_BINS - 1;
	}
	return block;
}

static int TJuncCell( float v ) {
	return static_cast<int>( floor( v / TJUNC_CELL_SIZE ) );
}

// a power of two near the expected number of vertexes; the optimizer hashes
// each small group separately, so a fixed large table would dominate
static int TJuncHashSize( int expected ) {
	int size = 256;
	while ( size < expected && size < ( 1 << 20 ) ) {
		size <<= 1;
	}
	return size;
}

static void ClearHashVertIndexes( int expectedVerts ) {
	memset( hashBinCounts, 0, sizeof( hashBinCounts ) );
	hashVertList.SetGranularity( 1024 );
	hashVertList.Clear();
	hashVertCells.Clear( TJuncHashSize( expectedVerts ), TJuncHashSize( expectedVerts ) );
	hashVertGrid.Free();
	hashVertGridBuilt = false;
}

static void BuildHashVertGrid( void ) {
	hashVertGrid.Clear( TJuncHashSize( hashVertList.Num() ), hashVertList.Num() > 0 ? hashVertList.Num() : 1 );
	for ( int i = 0 ; i < hashVertList.Num() ; i++ ) {
		const hashVert_t *hv = hashVertList[i];
		hashVertGrid.Add( TJuncKey( hv->cell[0], hv->cell[1], hv->cell[2] ), i );
	}
	hashVertGridBuilt = true;
}

/*
===============
GetHashVert

Also modifies the original vert to the snapped value
===============
*/
struct hashVert_s	*GetHashVert( idVec3 &v ) {
	int		iv[3];
	int		block[3];
	int		i;
	hashVert_t	*hv;

	numTotalVerts++;

	// snap the vert to integral values
	for ( i = 0 ; i < 3 ; i++ ) {
		iv[i] = floor( ( v[i] + 0.5/SNAP_FRACTIONS ) * SNAP_FRACTIONS );
		block[i] = HashBin( iv[i], i );
	}

	// see if a vertex near enough already exists: one in the same bin whose
	// snapped coordinates are all within one step. The bin's chain is newest
	// first and returned the first match, so take the newest candidate. No two
	// vertexes share a snapped coordinate, so each neighbor holds at most one.
	// this could still fail to find a near neighbor right at the hash block boundary
	int best = -1;
	for ( int dx = -1 ; dx <= 1 ; dx++ ) {
		for ( int dy = -1 ; dy <= 1 ; dy++ ) {
			for ( int dz = -1 ; dz <= 1 ; dz++ ) {
				const int c[3] = { iv[0] + dx, iv[1] + dy, iv[2] + dz };
				if ( HashBin( c[0], 0 ) != block[0] || HashBin( c[1], 1 ) != block[1] || HashBin( c[2], 2 ) != block[2] ) {
					continue;
				}
				for ( int index = hashVertCells.First( TJuncKey( c[0], c[1], c[2] ) ) ; index != -1 ; index = hashVertCells.Next( index ) ) {
					const hashVert_t *candidate = hashVertList[index];
					if ( candidate->iv[0] == c[0] && candidate->iv[1] == c[1] && candidate->iv[2] == c[2] ) {
						if ( index > best ) {
							best = index;
						}
						break;
					}
				}
			}
		}
	}
	if ( best >= 0 ) {
		hv = hashVertList[best];
		VectorCopy( hv->v, v );
		return hv;
	}

	// create a new one 
	hv = (hashVert_t *)Mem_Alloc( sizeof( *hv ) );

	hv->next = hashVerts[block[0]][block[1]][block[2]];
	hashVerts[block[0]][block[1]][block[2]] = hv;

	hv->iv[0] = iv[0];
	hv->iv[1] = iv[1];
	hv->iv[2] = iv[2];

	hv->v[0] = (float)iv[0] / SNAP_FRACTIONS;
	hv->v[1] = (float)iv[1] / SNAP_FRACTIONS;
	hv->v[2] = (float)iv[2] / SNAP_FRACTIONS;

	for ( i = 0 ; i < 3 ; i++ ) {
		hv->block[i] = block[i];
		hv->cell[i] = TJuncCell( hv->v[i] );
	}
	hv->blockSeq = hashBinCounts[block[0]][block[1]][block[2]]++;

	const int index = hashVertList.Append( hv );
	hashVertCells.Add( TJuncKey( iv[0], iv[1], iv[2] ), index );
	hashVertGridBuilt = false;

	VectorCopy( hv->v, v );

	numHashVerts++;

	return hv;
}


/*
==================
HashBlocksForTri

Returns an inclusive bounding box of hash
bins that should hold the triangle
==================
*/
static void HashBlocksForTri( const mapTri_t *tri, int blocks[2][3] ) {
	idBounds	bounds;
	int			i;

	bounds.Clear();
	bounds.AddPoint( tri->v[0].xyz );
	bounds.AddPoint( tri->v[1].xyz );
	bounds.AddPoint( tri->v[2].xyz );

	// add a 1.0 slop margin on each side
	for ( i = 0 ; i < 3 ; i++ ) {
		blocks[0][i] = ( bounds[0][i] - 1.0 - hashBounds[0][i] ) / hashScale[i];
		if ( blocks[0][i] < 0 ) {
			blocks[0][i] = 0;
		} else if ( blocks[0][i] >= HASH_BINS ) {
			blocks[0][i] = HASH_BINS - 1;
		}

		blocks[1][i] = ( bounds[1][i] + 1.0 - hashBounds[0][i] ) / hashScale[i];
		if ( blocks[1][i] < 0 ) {
			blocks[1][i] = 0;
		} else if ( blocks[1][i] >= HASH_BINS ) {
			blocks[1][i] = HASH_BINS - 1;
		}
	}
}


/*
=================
HashTriangles

Removes triangles that are degenerated or flipped backwards
=================
*/
void HashTriangles( optimizeGroup_t *groupList ) {
	mapTri_t	*a;
	int			vert;
	int			i;
	optimizeGroup_t	*group;

	// clear the hash tables
	memset( hashVerts, 0, sizeof( hashVerts ) );

	numHashVerts = 0;
	numTotalVerts = 0;

	// bound all the triangles to determine the bucket size
	int numTris = 0;
	hashBounds.Clear();
	for ( group = groupList ; group ; group = group->nextGroup ) {
		for ( a = group->triList ; a ; a = a->next ) {
			numTris++;
			hashBounds.AddPoint( a->v[0].xyz );
			hashBounds.AddPoint( a->v[1].xyz );
			hashBounds.AddPoint( a->v[2].xyz );
		}
	}

	// spread the bounds so it will never have a zero size
	for ( i = 0 ; i < 3 ; i++ ) {
		hashBounds[0][i] = floor( hashBounds[0][i] - 1 );
		hashBounds[1][i] = ceil( hashBounds[1][i] + 1 );
		hashIntMins[i] = hashBounds[0][i] * SNAP_FRACTIONS;

		hashScale[i] = ( hashBounds[1][i] - hashBounds[0][i] ) / HASH_BINS;
		hashIntScale[i] = hashScale[i] * SNAP_FRACTIONS;
		if ( hashIntScale[i] < 1 ) {
			hashIntScale[i] = 1;
		}
	}

	ClearHashVertIndexes( numTris );

	// add all the points to the hash buckets
	for ( group = groupList ; group ; group = group->nextGroup ) {
		// don't create tjunctions against discrete/no-t-fix surfaces (blood decals, etc)
		if ( group->material != NULL && ( group->material->IsDiscrete() || group->material->NoTFix() ) ) {
			continue;
		}
		for ( a = group->triList ; a ; a = a->next ) {
			for ( vert = 0 ; vert < 3 ; vert++ ) {
				a->hashVert[vert] = GetHashVert( a->v[vert].xyz );
			}
		}
	}
}

/*
=================
FreeTJunctionHash

The optimizer may add some more crossing verts
after t junction processing
=================
*/
void FreeTJunctionHash( void ) {
	int			i, j, k;
	hashVert_t	*hv, *next;

	for ( i = 0 ; i < HASH_BINS ; i++ ) {
		for ( j = 0 ; j < HASH_BINS ; j++ ) {
			for ( k = 0 ; k < HASH_BINS ; k++ ) {
				for ( hv = hashVerts[i][j][k] ; hv ; hv = next ) {
					next = hv->next;
					Mem_Free( hv );
				}
			}
		}
	}
	memset( hashVerts, 0, sizeof( hashVerts ) );
	memset( hashBinCounts, 0, sizeof( hashBinCounts ) );
	hashVertList.Clear();
	hashVertCells.Free();
	hashVertGrid.Free();
	hashVertGridBuilt = false;
}


/*
==================
TriangleIsSliver

True when every corner lies within COLINEAR_EPSILON of the line through the
longest edge, the tolerance FixTriangleAgainstHashVert uses to put a point on
an edge. Such a triangle is nowhere wider than that tolerance, so it can hide
no visible crack, and its edges overlap along the line: both halves of a split
still span it, so every further hashVert on the line splits both again.
Heavily subdivided curves multiplied single slivers into millions of
triangles this way (issue #181).
==================
*/
static bool TriangleIsSliver( const mapTri_t *a ) {
	idVec3		edge;
	float		lengthSqr, longestSqr;
	float		twiceArea;
	int			i;

	longestSqr = 0;
	for ( i = 0 ; i < 3 ; i++ ) {
		VectorSubtract( a->v[(i+1)%3].xyz, a->v[i].xyz, edge );
		lengthSqr = edge.LengthSqr();
		if ( lengthSqr > longestSqr ) {
			longestSqr = lengthSqr;
		}
	}
	twiceArea = ( a->v[1].xyz - a->v[0].xyz ).Cross( a->v[2].xyz - a->v[0].xyz ).Length();

	// the height above the longest edge is twiceArea / sqrt( longestSqr )
	return twiceArea * twiceArea <= Square( COLINEAR_EPSILON ) * longestSqr;
}

/*
==================
FixTriangleAgainstHashVert

Returns a list of two new mapTri if the hashVert is
on an edge of the given mapTri, otherwise returns NULL.
==================
*/
static mapTri_t *FixTriangleAgainstHashVert( const mapTri_t *a, const hashVert_t *hv ) {
	int			i;
	const idDrawVert	*v1, *v2, *v3;
	idDrawVert	split;
	idVec3		dir;
	float		len;
	float		frac;
	mapTri_t	*new1, *new2;
	idVec3		temp;
	float		d, off;
	const idVec3 *v;
	idPlane		plane1, plane2;

	v = &hv->v;

	// if the triangle already has this hashVert as a vert,
	// it can't be split by it
	if ( a->hashVert[0] == hv || a->hashVert[1] == hv || a->hashVert[2] == hv ) {
		return NULL;
	}

	// we probably should find the edge that the vertex is closest to.
	// it is possible to be < 1 unit away from multiple
	// edges, but we only want to split by one of them
	for ( i = 0 ; i < 3 ; i++ ) {
		v1 = &a->v[i];
		v2 = &a->v[(i+1)%3];
		v3 = &a->v[(i+2)%3];
		VectorSubtract( v2->xyz, v1->xyz, dir );
		len = dir.Normalize();

		// if it is close to one of the edge vertexes, skip it
		VectorSubtract( *v, v1->xyz, temp );
		d = DotProduct( temp, dir );
		if ( d <= 0 || d >= len ) {
			continue;
		}

		// make sure it is on the line
		VectorMA( v1->xyz, d, dir, temp );
		VectorSubtract( temp, *v, temp );
		off = temp.Length();
		if ( off <= -COLINEAR_EPSILON || off >= COLINEAR_EPSILON ) {
			continue;
		}

		// a sliver has no area to fix, and splitting it only makes
		// more slivers that overlap along the same line
		if ( TriangleIsSliver( a ) ) {
			return NULL;
		}

		// take the x/y/z from the splitter,
		// but interpolate everything else from the original tri
		VectorCopy( *v, split.xyz );
		frac = d / len;
		split.st[0] = v1->st[0] + frac * ( v2->st[0] - v1->st[0] );
		split.st[1] = v1->st[1] + frac * ( v2->st[1] - v1->st[1] );
		split.normal[0] = v1->normal[0] + frac * ( v2->normal[0] - v1->normal[0] );
		split.normal[1] = v1->normal[1] + frac * ( v2->normal[1] - v1->normal[1] );
		split.normal[2] = v1->normal[2] + frac * ( v2->normal[2] - v1->normal[2] );
		split.normal.Normalize();

		// split the tri
		new1 = CopyMapTri( a );
		new1->v[(i+1)%3] = split;
		new1->hashVert[(i+1)%3] = hv;
		new1->next = NULL;

		new2 = CopyMapTri( a );
		new2->v[i] = split;
		new2->hashVert[i] = hv;
		new2->next = new1;

		plane1.FromPoints( new1->hashVert[0]->v, new1->hashVert[1]->v, new1->hashVert[2]->v );
		plane2.FromPoints( new2->hashVert[0]->v, new2->hashVert[1]->v, new2->hashVert[2]->v );

		d = DotProduct( plane1, plane2 );

		// if the two split triangle's normals don't face the same way,
		// it should not be split
		if ( d <= 0 ) {
			FreeTriList( new2 );
			continue;
		}

		return new2;
	}


	return NULL;
}



/*
==================
FixListAgainstHashVert

One pass of the T-junction fix: every triangle in the list is tested against
the hashVert, and the list is rebuilt front to back, so it comes out reversed
with each split triangle's two halves in place.
==================
*/
static mapTri_t *FixListAgainstHashVert( mapTri_t *fixed, const hashVert_t *hv ) {
	mapTri_t	*test, *next, *a;

	test = fixed;
	fixed = NULL;
	for ( ; test ; test = next ) {
		next = test->next;
		a = FixTriangleAgainstHashVert( test, hv );
		if ( a ) {
			// cut into two triangles
			a->next->next = fixed;
			fixed = a;
			FreeTri( test );
		} else {
			test->next = fixed;
			fixed = test;
		}
	}
	return fixed;
}

static mapTri_t *ReverseTriList( mapTri_t *list ) {
	mapTri_t	*reversed = NULL, *next;

	for ( ; list ; list = next ) {
		next = list->next;
		list->next = reversed;
		reversed = list;
	}
	return reversed;
}

/*
==================
TJuncListReach

The box a hashVert must be inside to split any triangle in the list: it splits
only from within COLINEAR_EPSILON of an edge.
==================
*/
static idBounds TJuncListReach( const mapTri_t *list ) {
	idBounds	bounds;

	bounds.Clear();
	for ( ; list ; list = list->next ) {
		bounds.AddPoint( list->v[0].xyz );
		bounds.AddPoint( list->v[1].xyz );
		bounds.AddPoint( list->v[2].xyz );
	}
	bounds.ExpandSelf( COLINEAR_EPSILON + 0.01f );
	return bounds;
}

typedef struct {
	const hashVert_t *	hv;
	int					pass;		// position in the bin traversal
} tjuncCandidate_t;

static int SortTJuncCandidates( const tjuncCandidate_t *a, const tjuncCandidate_t *b ) {
	return a->pass - b->pass;
}

/*
==================
FixTriangleAgainstHash

Potentially splits a triangle into a list of triangles based on tjunctions

Every hashVert in the bins the triangle overlaps is one pass over the growing
list, in bin order and newest first within a bin. A hashVert outside the list's
reach cannot split anything, so its pass only reverses the list. The passes are
replayed with those reduced to a parity flag, applied before the next real pass
and at the end, which gives the same triangles in the same order. The hashVerts
that can matter come from the grid around the triangle, or for a triangle too
large for that, from scanning the bins with the reach test.
==================
*/
static mapTri_t	*FixTriangleAgainstHash( const mapTri_t *tri ) {
	mapTri_t		*fixed;
	int				blocks[2][3];
	int				i, j, k;
	hashVert_t		*hv;
	static int		passBase[HASH_BINS][HASH_BINS][HASH_BINS];

	// if this triangle is degenerate after point snapping,
	// do nothing (this shouldn't happen, because they should
	// be removed as they are hashed)
	if ( tri->hashVert[0] == tri->hashVert[1]
		|| tri->hashVert[0] == tri->hashVert[2]
		|| tri->hashVert[1] == tri->hashVert[2] ) {
		return NULL;
	}

	fixed = CopyMapTri( tri );
	fixed->next = NULL;

	HashBlocksForTri( tri, blocks );

	int numPasses = 0;
	for ( i = blocks[0][0] ; i <= blocks[1][0] ; i++ ) {
		for ( j = blocks[0][1] ; j <= blocks[1][1] ; j++ ) {
			for ( k = blocks[0][2] ; k <= blocks[1][2] ; k++ ) {
				passBase[i][j][k] = numPasses;
				numPasses += hashBinCounts[i][j][k];
			}
		}
	}

	idBounds reach = TJuncListReach( fixed );
	bool reversed = false;
	int lastPass = -1;

	// the grid answers for the region around the triangle; a split that grows
	// the list past it falls back to scanning the remaining passes
	idBounds region = reach;
	region.ExpandSelf( 2.0f );
	int cellMins[3], cellMaxs[3], numCells = 1;
	for ( i = 0 ; i < 3 ; i++ ) {
		cellMins[i] = TJuncCell( region[0][i] );
		cellMaxs[i] = TJuncCell( region[1][i] );
		numCells *= cellMaxs[i] - cellMins[i] + 1;
	}

	bool scan = true;
	if ( numCells <= TJUNC_MAX_GRID_CELLS && numCells * 4 < numPasses ) {
		if ( !hashVertGridBuilt ) {
			BuildHashVertGrid();
		}
		static idList<tjuncCandidate_t> candidates;
		candidates.SetNum( 0, false );
		for ( int x = cellMins[0] ; x <= cellMaxs[0] ; x++ ) {
			for ( int y = cellMins[1] ; y <= cellMaxs[1] ; y++ ) {
				for ( int z = cellMins[2] ; z <= cellMaxs[2] ; z++ ) {
					for ( int index = hashVertGrid.First( TJuncKey( x, y, z ) ) ; index != -1 ; index = hashVertGrid.Next( index ) ) {
						const hashVert_t *c = hashVertList[index];
						if ( c->cell[0] != x || c->cell[1] != y || c->cell[2] != z ) {
							continue;
						}
						if ( c->block[0] < blocks[0][0] || c->block[0] > blocks[1][0]
							|| c->block[1] < blocks[0][1] || c->block[1] > blocks[1][1]
							|| c->block[2] < blocks[0][2] || c->block[2] > blocks[1][2] ) {
							continue;
						}
						tjuncCandidate_t &candidate = candidates.Alloc();
						candidate.hv = c;
						candidate.pass = passBase[c->block[0]][c->block[1]][c->block[2]]
							+ hashBinCounts[c->block[0]][c->block[1]][c->block[2]] - 1 - c->blockSeq;
					}
				}
			}
		}
		candidates.Sort( SortTJuncCandidates );

		scan = false;
		for ( i = 0 ; i < candidates.Num() ; i++ ) {
			const tjuncCandidate_t &candidate = candidates[i];
			reversed ^= ( ( candidate.pass - lastPass - 1 ) & 1 ) != 0;
			lastPass = candidate.pass;
			if ( !reach.ContainsPoint( candidate.hv->v ) ) {
				reversed = !reversed;
				continue;
			}
			if ( reversed ) {
				fixed = ReverseTriList( fixed );
				reversed = false;
			}
			fixed = FixListAgainstHashVert( fixed, candidate.hv );
			reach = TJuncListReach( fixed );
			if ( !region.ContainsPoint( reach[0] ) || !region.ContainsPoint( reach[1] ) ) {
				scan = true;
				break;
			}
		}
	}

	if ( scan ) {
		int pass = 0;
		for ( i = blocks[0][0] ; i <= blocks[1][0] ; i++ ) {
			for ( j = blocks[0][1] ; j <= blocks[1][1] ; j++ ) {
				for ( k = blocks[0][2] ; k <= blocks[1][2] ; k++ ) {
					for ( hv = hashVerts[i][j][k] ; hv ; hv = hv->next, pass++ ) {
						if ( pass <= lastPass ) {
							continue;
						}
						lastPass = pass;
						if ( !reach.ContainsPoint( hv->v ) ) {
							reversed = !reversed;
							continue;
						}
						if ( reversed ) {
							fixed = ReverseTriList( fixed );
							reversed = false;
						}
						fixed = FixListAgainstHashVert( fixed, hv );
						reach = TJuncListReach( fixed );
					}
				}
			}
		}
	}

	// the passes after the last one that could split
	reversed ^= ( ( numPasses - lastPass - 1 ) & 1 ) != 0;
	if ( reversed ) {
		fixed = ReverseTriList( fixed );
	}

	return fixed;
}


/*
==================
CountGroupListTris
==================
*/
int CountGroupListTris( const optimizeGroup_t *groupList ) {
	int		c;

	c = 0;
	for ( ; groupList ; groupList = groupList->nextGroup ) {
		c += CountTriList( groupList->triList );
	}

	return c;
}

/*
==================
FixAreaGroupsTjunctions
==================
*/
void	FixAreaGroupsTjunctions( optimizeGroup_t *groupList ) {
	const mapTri_t	*tri;
	mapTri_t		*newList;
	mapTri_t		*fixed;
	int				startCount, endCount;
	optimizeGroup_t	*group;

	if ( dmapGlobals.noTJunc ) {
		return;
	}

	if ( !groupList ) {
		return;
	}

	startCount = CountGroupListTris( groupList );

	if ( dmapGlobals.verbose ) {
		common->Printf( "----- FixAreaGroupsTjunctions -----\n" );
		common->Printf( "%6i triangles in\n", startCount );
	}

	HashTriangles( groupList );

	for ( group = groupList ; group ; group = group->nextGroup ) {
		// don't touch discrete surfaces
		if ( group->material != NULL && group->material->IsDiscrete() ) {
			continue;
		}

		newList = NULL;
		mapTri_t **tail = &newList;
		for ( tri = group->triList ; tri ; tri = tri->next ) {
			fixed = FixTriangleAgainstHash( tri );
			// Large split surfaces must not rescan the accumulated list for
			// every input triangle. Preserve the same output order with a tail.
			*tail = fixed;
			while ( *tail ) {
				tail = &(*tail)->next;
			}
		}
		FreeTriList( group->triList );
		group->triList = newList;
	}

	endCount = CountGroupListTris( groupList );
	if ( dmapGlobals.verbose ) {
		common->Printf( "%6i triangles out\n", endCount );
	}
}


/*
==================
FixEntityTjunctions
==================
*/
void	FixEntityTjunctions( uEntity_t *e ) {
	int		i;

	for ( i = 0 ; i < e->numAreas ; i++ ) {
		FixAreaGroupsTjunctions( e->areas[i].groups );
		FreeTJunctionHash();
	}
}

/*
==================
FixGlobalTjunctions
==================
*/
void	FixGlobalTjunctions( uEntity_t *e ) {
	mapTri_t	*a;
	int			vert;
	int			i;
	optimizeGroup_t	*group;
	int			areaNum;

	common->Printf( "----- FixGlobalTjunctions -----\n" );

	// clear the hash tables
	memset( hashVerts, 0, sizeof( hashVerts ) );

	numHashVerts = 0;
	numTotalVerts = 0;

	// bound all the triangles to determine the bucket size
	int numTris = 0;
	hashBounds.Clear();
	for ( areaNum = 0 ; areaNum < e->numAreas ; areaNum++ ) {
		for ( group = e->areas[areaNum].groups ; group ; group = group->nextGroup ) {
			for ( a = group->triList ; a ; a = a->next ) {
				numTris++;
				hashBounds.AddPoint( a->v[0].xyz );
				hashBounds.AddPoint( a->v[1].xyz );
				hashBounds.AddPoint( a->v[2].xyz );
			}
		}
	}

	// spread the bounds so it will never have a zero size
	for ( i = 0 ; i < 3 ; i++ ) {
		hashBounds[0][i] = floor( hashBounds[0][i] - 1 );
		hashBounds[1][i] = ceil( hashBounds[1][i] + 1 );
		hashIntMins[i] = hashBounds[0][i] * SNAP_FRACTIONS;

		hashScale[i] = ( hashBounds[1][i] - hashBounds[0][i] ) / HASH_BINS;
		hashIntScale[i] = hashScale[i] * SNAP_FRACTIONS;
		if ( hashIntScale[i] < 1 ) {
			hashIntScale[i] = 1;
		}
	}

	ClearHashVertIndexes( numTris );

	// add all the points to the hash buckets
	for ( areaNum = 0 ; areaNum < e->numAreas ; areaNum++ ) {
		for ( group = e->areas[areaNum].groups ; group ; group = group->nextGroup ) {
			// don't touch discrete surfaces
			if ( group->material != NULL && group->material->IsDiscrete() ) {
				continue;
			}

			for ( a = group->triList ; a ; a = a->next ) {
				for ( vert = 0 ; vert < 3 ; vert++ ) {
					a->hashVert[vert] = GetHashVert( a->v[vert].xyz );
				}
			}
		}
	}

	// add all the func_static model vertexes to the hash buckets
	// optionally inline some of the func_static models
	if ( dmapGlobals.entityNum == 0 ) {
		for ( int eNum = 1 ; eNum < dmapGlobals.num_entities ; eNum++ ) {
			uEntity_t *entity = &dmapGlobals.uEntities[eNum];
			const char *className = entity->mapEntity->epairs.GetString( "classname" );
			if ( idStr::Icmp( className, "func_static" ) ) {
				continue;
			}
			const char *modelName = entity->mapEntity->epairs.GetString( "model" );
			if ( !modelName ) {
				continue;
			}
			if ( !strstr( modelName, ".lwo" ) && !strstr( modelName, ".ase" ) && !strstr( modelName, ".ma" ) ) {
				continue;
			}

			idRenderModel	*model = renderModelManager->FindModel( modelName );

//			common->Printf( "adding T junction verts for %s.\n", entity->mapEntity->epairs.GetString( "name" ) );

			idMat3	axis;
			// get the rotation matrix in either full form, or single angle form
			if ( !entity->mapEntity->epairs.GetMatrix( "rotation", "1 0 0 0 1 0 0 0 1", axis ) ) {
				float angle = entity->mapEntity->epairs.GetFloat( "angle" );
				if ( angle != 0.0f ) {
					axis = idAngles( 0.0f, angle, 0.0f ).ToMat3();
				} else {
					axis.Identity();
				}
			}		

			idVec3	origin = entity->mapEntity->epairs.GetVector( "origin" );

			for ( i = 0 ; i < model->NumSurfaces() ; i++ ) {
				const modelSurface_t *surface = model->Surface( i );
				const srfTriangles_t *tri = surface->geometry;

				mapTri_t	mapTri;
				memset( &mapTri, 0, sizeof( mapTri ) );
				mapTri.material = surface->shader;
				// don't let discretes (autosprites, etc) merge together
				if ( mapTri.material->IsDiscrete() ) {
					mapTri.mergeGroup = (void *)surface;
				}
				for ( int j = 0 ; j < tri->numVerts ; j += 3 ) {
					idVec3 v = tri->verts[j].xyz * axis + origin;
					GetHashVert( v );
				}
			}
		}
	}



	// now fix each area
	for ( areaNum = 0 ; areaNum < e->numAreas ; areaNum++ ) {
		for ( group = e->areas[areaNum].groups ; group ; group = group->nextGroup ) {
			// don't touch discrete surfaces
			if ( group->material != NULL && group->material->IsDiscrete() ) {
				continue;
			}

			mapTri_t *newList = NULL;
			mapTri_t **tail = &newList;
			for ( mapTri_t *tri = group->triList ; tri ; tri = tri->next ) {
				mapTri_t *fixed = FixTriangleAgainstHash( tri );
				*tail = fixed;
				while ( *tail ) {
					tail = &(*tail)->next;
				}
			}
			FreeTriList( group->triList );
			group->triList = newList;
		}
	}


	// done
	FreeTJunctionHash();
}
