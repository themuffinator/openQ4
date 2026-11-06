// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Quake 4 AAS tactical feature generation.
//
// Quake 4's aas32 files carry cover, lean, corner, pinch and vantage positions
// that rvAASTacticalSensor uses to pick cover and attack positions. Raven
// generated them after reachability, clustering and optimization, from the
// boundary edges of reachable floor faces. This pass reproduces the placement
// rules of the retail generator (Toolsx86.dll, Quake 4 1.4.2) as recorded in
// docs/dev/aas-compiler.md, so a recompiled map gets features that match the
// shipped files instead of none.
//
// Outline:
//  1. Hand-placed info_tactical_feature entities become features directly.
//  2. Areas are flooded from every entity that may use this AAS type.
//  3. Each floor-face edge of a flooded area that no other floor face of the
//     area shares, and that no walk reachability crosses, becomes a wall edge.
//     Walk-off-ledge drops of 20-60 units that land beside it are attached.
//  4. Wall edges are chained by shared vertices and merged into straight walls.
//  5. Each wall is split into height segments; segments long enough for the
//     AI's bounding box receive cover, lean, look-over and corner features,
//     each confirmed with collision traces against the world model.
//
// Two retail behaviors are deliberately not reproduced:
//  - Retail indexed features through a 1024-bucket hash keyed on area number
//    and never compared the stored area, so every area also listed the
//    features of every area congruent to it modulo 1024 (every shipped aas32
//    with more than 1024 areas shows this). Each area here lists only its own.
//  - Retail sized its visited sets for 32000 areas and edges; these are
//    dynamic.

#include "AASBuild_local.h"
#include "../../../cm/CollisionModel.h"

namespace {

const float TACTICAL_LEDGE_MIN_HEIGHT		= 20.0f;	// low walls a ledge drop can see over
const float TACTICAL_LEDGE_MAX_HEIGHT		= 60.0f;
const float TACTICAL_EDGE_NEAR				= 3.0f;		// ledge landing, collinearity and segment gap tolerance
const float TACTICAL_VERTEX_MATCH			= 2.0f;		// 2D join tolerance between areas
const float TACTICAL_FULL_HEIGHT			= 72.0f;	// height of a wall with no ledge
const float TACTICAL_HEADROOM				= 48.0f;
const float TACTICAL_STEP_HEIGHT_DELTA		= 10.0f;	// a neighbor this much lower exposes a lean
const float TACTICAL_CORNER_LONG			= 100.0f;
const float TACTICAL_CORNER_MEDIUM			= 48.0f;
const float TACTICAL_CORNER_SHORT			= 20.0f;
const float TACTICAL_CORNER_TURN_COS		= 0.2f;
const float TACTICAL_CORNER_STRAIGHT_COS	= 0.5f;
const float TACTICAL_PARALLEL_COS			= 0.9f;

const int TACTICAL_PLACE_MASK				= CONTENTS_SOLID | CONTENTS_NOTACTICALFEATURES;
const int TACTICAL_SIGHT_MASK				= CONTENTS_SOLID | CONTENTS_SIGHTCLIP;

struct tacticalEdge_t {
	int		vertexNum[2];		// oriented by the sign of the edge index
	int		edgeNum;			// signed edge index entry
	int		areaNum;
	int		order;				// creation order, a deterministic sort tie-break
	int		firstLedge;			// span in idTacticalBuilder::edgeLedges
	int		numLedges;
	int		next;				// edge that starts where this one ends
	int		prev;
	int		wall;
};

struct tacticalSegment_t {
	float	start;				// distance along the wall
	float	end;
	float	height;
};

struct tacticalWall_t {
	idVec3	start;
	idVec3	end;
	idVec3	dir;				// direction of the wall's first edge
	float	length;				// accumulated along dir
	idList<idReachability *>	ledges;
	idList<tacticalSegment_t>	segments;
	int		firstEdge;
	int		lastEdge;
	int		prevWall;			// wall that ends where this one starts
	int		nextWall;			// wall that starts where this one ends
};

float Orient2D( const idVec3 &a, const idVec3 &b, const idVec3 &c ) {
	return ( b.y * a.x - b.x * a.y ) + ( b.x * c.y - b.y * c.x ) + ( a.y * c.x - a.x * c.y );
}

float Distance2D( const idVec3 &a, const idVec3 &b ) {
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	return idMath::Sqrt( dx * dx + dy * dy );
}

// Distance from p to the segment ab; t receives the unclamped projection.
float PointSegmentDistance( const idVec3 &p, const idVec3 &a, const idVec3 &b, float &t ) {
	const idVec3 ab = b - a;
	const float lengthSqr = ab.LengthSqr();
	t = lengthSqr > 0.0f ? ( ab * ( p - a ) ) / lengthSqr : 0.0f;
	if ( t < 0.0f ) {
		return ( p - a ).Length();
	}
	if ( t > 1.0f ) {
		return ( p - b ).Length();
	}
	return ( p - ( a + ab * t ) ).Length();
}

short TruncateToShort( float value ) {
	return static_cast<short>( static_cast<int>( value ) );
}

byte TruncateToByte( float value ) {
	return static_cast<byte>( static_cast<int>( value ) );
}

class idTacticalBuilder {
public:
							idTacticalBuilder( idAASFileLocal *file, const idMapFile *mapFile, idCollisionModel *world );
	int						Generate( void );

private:
	idAASFileLocal *		file;
	const idMapFile *		mapFile;
	idCollisionModel *		world;
	const idAASSettings &	settings;

	idList<tacticalEdge_t>	edges;
	idList<idReachability *> edgeLedges;
	idList<tacticalWall_t>	walls;
	idList<int>				areaFeatures;		// per area: last feature added, -1 if none
	idList<int>				featureChain;		// per feature: previous feature of the same area

	float					Radius( float scale ) const { return settings.boundingBoxes[0][1][0] * scale; }
	int						ReachableArea( const idVec3 &origin ) const;
	void					AddFeature( const idVec3 &origin, const idVec3 &normal, int flags, float height, int areaNum );

	void					AddPlacedFeature( const idMapEntity *entity );
	void					FloodAreas( idList<int> &stack, idList<bool> &areaVisited );
	bool					FloorEdgeShared( const aasArea_t &area, int skipFace, int edgeNum ) const;
	bool					EdgeHasWalkReach( const aasArea_t &area, int edgeNum ) const;
	void					AddEdgeIfWall( const aasArea_t &area, int areaNum, int edgeIndexValue );
	void					OrientedEdge( int edgeIndexValue, int vertexNum[2] ) const;
	void					LinkEdges( void );
	int						FindEdgeStartingAt( int vertexNum ) const;
	int						FindEdgeContinuingIn( const tacticalEdge_t &edge ) const;
	bool					AddEdgeToWall( int edgeIndex, int wallIndex, bool atEnd );
	void					BuildWalls( void );

	bool					EdgeExtendsLedge( tacticalSegment_t &segment, int edgeNum, const tacticalWall_t &wall, int &startVertex, int &endVertex ) const;
	bool					LedgeSegment( tacticalSegment_t &segment, const idReachability *reach, const tacticalWall_t &wall ) const;
	void					BuildSegments( tacticalWall_t &wall );
	bool					IsCorner( const tacticalWall_t &wall, bool atStart ) const;
	void					PlaceFeature( const tacticalWall_t &wall, float height, float t, int flags );
	void					SegmentWall( tacticalWall_t &wall );
	void					BuildAreaIndex( void );

	bool					TraceClear( const idVec3 &start, const idVec3 &end, int contentMask ) const;
	const tacticalWall_t *	Wall( int index ) const { return index >= 0 ? &walls[index] : NULL; }
};

idTacticalBuilder::idTacticalBuilder( idAASFileLocal *file, const idMapFile *mapFile, idCollisionModel *world ) :
	file( file ),
	mapFile( mapFile ),
	world( world ),
	settings( file->GetSettings() ) {
}

int idTacticalBuilder::ReachableArea( const idVec3 &origin ) const {
	return file->PointReachableAreaNum( origin, settings.boundingBoxes[0], AREA_REACHABLE_WALK, TFL_INVALID );
}

void idTacticalBuilder::AddFeature( const idVec3 &origin, const idVec3 &normal, int flags, float height, int areaNum ) {
	aasFeature_t feature;

	feature.x = TruncateToShort( origin.x );
	feature.y = TruncateToShort( origin.y );
	feature.z = TruncateToShort( origin.z );
	feature.flags = static_cast<unsigned short>( flags );
	feature.normalx = TruncateToByte( ( normal.x + 1.0f ) * 127.0f );
	feature.normaly = TruncateToByte( ( normal.y + 1.0f ) * 127.0f );
	feature.height = TruncateToByte( height );
	feature.weight = 0;

	const int featureNum = file->AppendFeature( feature );
	featureChain.AssureSize( featureNum + 1, -1 );
	if ( areaNum > 0 && areaNum < areaFeatures.Num() ) {
		featureChain[featureNum] = areaFeatures[areaNum];
		areaFeatures[areaNum] = featureNum;
	} else {
		featureChain[featureNum] = -1;
	}
}

bool idTacticalBuilder::TraceClear( const idVec3 &start, const idVec3 &end, int contentMask ) const {
	trace_t trace;
	collisionModelManager->Translation( &trace, start, end, NULL, mat3_identity, contentMask, world, vec3_origin, mat3_identity );
	return trace.fraction >= 1.0f;
}

/*
Hand-placed features. The flag keys overlap: corner_left and lean_left both
imply cover with a left lean, and a height inside the low-wall band adds the
look-over attack.
*/
void idTacticalBuilder::AddPlacedFeature( const idMapEntity *entity ) {
	const idDict &args = entity->epairs;
	const idVec3 origin = args.GetVector( "origin", "0 0 0" );
	const float angle = args.GetFloat( "angle", "0" );
	const float height = args.GetFloat( "height", "72" );
	int flags = args.GetInt( "flags", "0" );

	if ( args.GetInt( "vantage", "0" ) ) {
		flags |= FEATURE_VANTAGE;
	}
	if ( args.GetInt( "pinch", "0" ) ) {
		flags |= FEATURE_PINCH;
	}
	if ( args.GetInt( "cover", "0" ) ) {
		flags |= FEATURE_COVER;
	}
	if ( args.GetInt( "lean_left", "0" ) ) {
		flags |= FEATURE_COVER | FEATURE_LOOK_LEFT;
	}
	if ( args.GetInt( "corner_left", "0" ) ) {
		flags |= FEATURE_COVER | FEATURE_LOOK_LEFT | FEATURE_CORNER_LEFT;
	}
	if ( args.GetInt( "lean_right", "0" ) ) {
		flags |= FEATURE_COVER | FEATURE_LOOK_RIGHT;
	}
	if ( args.GetInt( "corner_right", "0" ) ) {
		flags |= FEATURE_COVER | FEATURE_LOOK_RIGHT | FEATURE_CORNER_RIGHT;
	}
	if ( height > TACTICAL_LEDGE_MIN_HEIGHT && height < TACTICAL_LEDGE_MAX_HEIGHT ) {
		flags |= FEATURE_COVER | FEATURE_LOOK_OVER;
	}

	const int areaNum = ReachableArea( origin );
	if ( !areaNum ) {
		common->Warning( "info_tactical_feature '%s' at (%s) is outside the %s navigation",
			args.GetString( "name" ), origin.ToString( 0 ), settings.fileExtension.c_str() );
	}
	AddFeature( origin, idAngles( 0.0f, angle, 0.0f ).ToForward(), flags, height, areaNum );
}

void idTacticalBuilder::OrientedEdge( int edgeIndexValue, int vertexNum[2] ) const {
	const aasEdge_t &edge = file->GetEdge( abs( edgeIndexValue ) );
	const int flip = edgeIndexValue < 0 ? 1 : 0;
	vertexNum[0] = edge.vertexNum[flip];
	vertexNum[1] = edge.vertexNum[1 - flip];
}

bool idTacticalBuilder::FloorEdgeShared( const aasArea_t &area, int skipFace, int edgeNum ) const {
	for ( int i = 0; i < area.numFaces; i++ ) {
		if ( i == skipFace ) {
			continue;
		}
		const aasFace_t &face = file->GetFace( abs( file->GetFaceIndex( area.firstFace + i ) ) );
		if ( !( face.flags & FACE_FLOOR ) ) {
			continue;
		}
		for ( int j = 0; j < face.numEdges; j++ ) {
			if ( abs( file->GetEdgeIndex( face.firstEdge + j ) ) == edgeNum ) {
				return true;
			}
		}
	}
	return false;
}

bool idTacticalBuilder::EdgeHasWalkReach( const aasArea_t &area, int edgeNum ) const {
	for ( const idReachability *reach = area.reach; reach; reach = reach->next ) {
		if ( ( reach->travelType & TFL_WALK ) && !( reach->travelType & ( TFL_BARRIERJUMP | TFL_JUMP ) ) && reach->edgeNum == edgeNum ) {
			return true;
		}
	}
	return false;
}

/*
A boundary edge of a floor face becomes a wall edge. Walk-off-ledge drops into
this area whose source lies on the edge's wall side, and whose landing point is
next to the edge, mark the wall as low enough to look over.
*/
void idTacticalBuilder::AddEdgeIfWall( const aasArea_t &area, int areaNum, int edgeIndexValue ) {
	tacticalEdge_t edge;
	idList<idReachability *> ledges;

	OrientedEdge( edgeIndexValue, edge.vertexNum );
	edge.edgeNum = edgeIndexValue;
	edge.areaNum = areaNum;
	edge.order = edges.Num();
	edge.next = edge.prev = edge.wall = -1;

	const idVec3 &a = file->GetVertex( edge.vertexNum[0] );
	const idVec3 &b = file->GetVertex( edge.vertexNum[1] );
	float maxHeight = 0.0f;

	for ( idReachability *reach = area.rev_reach; reach; reach = reach->rev_next ) {
		if ( !( reach->travelType & TFL_WALKOFFLEDGE ) ) {
			continue;
		}
		const float height = reach->start.z - reach->end.z;
		if ( height <= TACTICAL_LEDGE_MIN_HEIGHT || height >= TACTICAL_LEDGE_MAX_HEIGHT ) {
			continue;
		}
		if ( Orient2D( a, b, file->GetArea( reach->fromAreaNum ).center ) <= 0.0f ) {
			continue;
		}
		float t;
		if ( PointSegmentDistance( reach->end, a, b, t ) >= TACTICAL_EDGE_NEAR ) {
			continue;
		}
		maxHeight = Max( maxHeight, height );
		ledges.AddUnique( reach );
	}

	// Retail keeps this rejection although the band above makes it unreachable.
	if ( maxHeight != 0.0f && maxHeight <= TACTICAL_LEDGE_MIN_HEIGHT ) {
		return;
	}

	edge.firstLedge = edgeLedges.Num();
	edge.numLedges = ledges.Num();
	edgeLedges.Append( ledges );
	edges.Append( edge );
}

void idTacticalBuilder::FloodAreas( idList<int> &stack, idList<bool> &areaVisited ) {
	idList<bool> edgeVisited;
	edgeVisited.AssureSize( file->GetNumEdges(), false );

	while ( stack.Num() ) {
		const int areaNum = stack[stack.Num() - 1];
		stack.RemoveIndex( stack.Num() - 1 );

		const aasArea_t &area = file->GetArea( areaNum );
		for ( const idReachability *reach = area.reach; reach; reach = reach->next ) {
			if ( reach->toAreaNum > 0 && reach->toAreaNum < areaVisited.Num() && !areaVisited[reach->toAreaNum] ) {
				areaVisited[reach->toAreaNum] = true;
				stack.Append( reach->toAreaNum );
			}
		}

		if ( area.flags & AREA_LEDGE ) {
			continue;
		}

		for ( int i = 0; i < area.numFaces; i++ ) {
			const aasFace_t &face = file->GetFace( abs( file->GetFaceIndex( area.firstFace + i ) ) );
			if ( !( face.flags & FACE_FLOOR ) ) {
				continue;
			}
			for ( int j = 0; j < face.numEdges; j++ ) {
				const int edgeIndexValue = file->GetEdgeIndex( face.firstEdge + j );
				const int edgeNum = abs( edgeIndexValue );
				if ( edgeNum <= 0 || edgeNum >= edgeVisited.Num() || edgeVisited[edgeNum] ) {
					continue;
				}
				if ( FloorEdgeShared( area, i, edgeNum ) || EdgeHasWalkReach( area, edgeNum ) ) {
					continue;
				}
				edgeVisited[edgeNum] = true;
				AddEdgeIfWall( area, areaNum, edgeIndexValue );
			}
		}
	}
}

int idTacticalBuilder::FindEdgeStartingAt( int vertexNum ) const {
	int low = 0;
	int high = edges.Num();
	while ( low < high ) {
		const int mid = ( low + high ) / 2;
		if ( edges[mid].vertexNum[0] < vertexNum ) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}
	return ( low < edges.Num() && edges[low].vertexNum[0] == vertexNum ) ? low : -1;
}

/*
Adjacent areas do not share vertices, so a wall that continues into a walkable
neighbor is joined by matching the end point in 2D against that neighbor's floor
edges.
*/
int idTacticalBuilder::FindEdgeContinuingIn( const tacticalEdge_t &edge ) const {
	const idVec3 &end = file->GetVertex( edge.vertexNum[1] );
	const aasArea_t &area = file->GetArea( edge.areaNum );

	for ( const idReachability *reach = area.reach; reach; reach = reach->next ) {
		if ( !( reach->travelType & TFL_WALK ) ) {
			continue;
		}
		const aasArea_t &neighbor = file->GetArea( reach->toAreaNum );
		for ( int i = 0; i < neighbor.numFaces; i++ ) {
			const aasFace_t &face = file->GetFace( abs( file->GetFaceIndex( neighbor.firstFace + i ) ) );
			if ( !( face.flags & FACE_FLOOR ) ) {
				continue;
			}
			for ( int j = 0; j < face.numEdges; j++ ) {
				const int edgeIndexValue = file->GetEdgeIndex( face.firstEdge + j );
				int vertexNum[2];
				OrientedEdge( edgeIndexValue, vertexNum );
				const idVec3 &start = file->GetVertex( vertexNum[0] );
				if ( idMath::Fabs( start.x - end.x ) < TACTICAL_VERTEX_MATCH && idMath::Fabs( start.y - end.y ) < TACTICAL_VERTEX_MATCH ) {
					return FindEdgeStartingAt( vertexNum[0] );
				}
			}
		}
	}
	return -1;
}

int SortEdgesByStartVertex( const tacticalEdge_t *a, const tacticalEdge_t *b ) {
	if ( a->vertexNum[0] != b->vertexNum[0] ) {
		return a->vertexNum[0] < b->vertexNum[0] ? -1 : 1;
	}
	return a->order - b->order;
}

void idTacticalBuilder::LinkEdges( void ) {
	edges.Sort( SortEdgesByStartVertex );

	for ( int i = 0; i < edges.Num(); i++ ) {
		int next = FindEdgeStartingAt( edges[i].vertexNum[1] );
		if ( next < 0 ) {
			next = FindEdgeContinuingIn( edges[i] );
		}
		edges[i].next = next;
		if ( next >= 0 ) {
			edges[next].prev = i;
		}
	}
}

/*
Starts a wall with an edge, or extends a wall with an edge that stays within
TACTICAL_EDGE_NEAR of its line. The wall keeps its first edge's direction.
*/
bool idTacticalBuilder::AddEdgeToWall( int edgeIndex, int wallIndex, bool atEnd ) {
	tacticalEdge_t &edge = edges[edgeIndex];
	if ( edge.wall >= 0 ) {
		return false;
	}

	int vertexNum[2];
	OrientedEdge( edge.edgeNum, vertexNum );
	const idVec3 start = file->GetVertex( vertexNum[0] );
	const idVec3 end = file->GetVertex( vertexNum[1] );

	if ( wallIndex < 0 ) {
		tacticalWall_t &wall = walls.Alloc();
		wall.start = start;
		wall.end = end;
		wall.dir = end - start;
		wall.length = wall.dir.Normalize();
		for ( int i = 0; i < edge.numLedges; i++ ) {
			wall.ledges.Append( edgeLedges[edge.firstLedge + i] );
		}
		wall.firstEdge = wall.lastEdge = edgeIndex;
		wall.prevWall = wall.nextWall = -1;
		edge.wall = walls.Num() - 1;
		return true;
	}

	tacticalWall_t &wall = walls[wallIndex];
	const float along = wall.dir * ( end - start );
	if ( ( end - ( start + wall.dir * along ) ).Length() > TACTICAL_EDGE_NEAR ) {
		return false;
	}

	wall.length += along;
	if ( atEnd ) {
		wall.end = end;
		wall.lastEdge = edgeIndex;
	} else {
		wall.start = start;
		wall.firstEdge = edgeIndex;
	}
	for ( int i = 0; i < edge.numLedges; i++ ) {
		wall.ledges.AddUnique( edgeLedges[edge.firstLedge + i] );
	}
	edge.wall = wallIndex;
	return true;
}

void idTacticalBuilder::BuildWalls( void ) {
	for ( int i = 0; i < edges.Num(); i++ ) {
		if ( !AddEdgeToWall( i, -1, false ) ) {
			continue;
		}
		const int wallIndex = edges[i].wall;
		for ( int next = edges[i].next; next >= 0 && AddEdgeToWall( next, wallIndex, true ); next = edges[next].next ) {
		}
		for ( int prev = edges[i].prev; prev >= 0 && AddEdgeToWall( prev, wallIndex, false ); prev = edges[prev].prev ) {
		}
	}

	for ( int i = 0; i < walls.Num(); i++ ) {
		const int next = edges[walls[i].lastEdge].next;
		const int prev = edges[walls[i].firstEdge].prev;
		walls[i].nextWall = next >= 0 ? edges[next].wall : -1;
		walls[i].prevWall = prev >= 0 ? edges[prev].wall : -1;
	}
}

/*
Grows a ledge's extent along the wall with an AAS edge parallel to it. Edges
beyond the first must touch the extent found so far.
*/
bool idTacticalBuilder::EdgeExtendsLedge( tacticalSegment_t &segment, int edgeNum, const tacticalWall_t &wall, int &startVertex, int &endVertex ) const {
	if ( edgeNum < 1 || edgeNum >= file->GetNumEdges() ) {
		return false;
	}

	int vertexNum[2];
	OrientedEdge( edgeNum, vertexNum );
	if ( startVertex != -1 && endVertex != -1 &&
		vertexNum[0] != startVertex && vertexNum[1] != startVertex &&
		vertexNum[0] != endVertex && vertexNum[1] != endVertex ) {
		return false;
	}

	const idVec3 &a = file->GetVertex( vertexNum[0] );
	const idVec3 &b = file->GetVertex( vertexNum[1] );
	idVec3 dir = b - a;
	dir.Normalize();
	if ( idMath::Fabs( dir * wall.dir ) < TACTICAL_PARALLEL_COS ) {
		return false;
	}

	float ta, tb;
	PointSegmentDistance( a, wall.start, wall.end, ta );
	PointSegmentDistance( b, wall.start, wall.end, tb );
	if ( ta <= 0.0f && tb <= 0.0f ) {
		return false;
	}
	if ( ta >= 1.0f && tb >= 1.0f ) {
		return false;
	}

	float sa = ta > 0.0f ? ( ta < 1.0f ? Distance2D( a, wall.start ) : wall.length ) : 0.0f;
	float sb = tb > 0.0f ? ( tb < 1.0f ? Distance2D( b, wall.start ) : wall.length ) : 0.0f;
	int va = vertexNum[0];
	int vb = vertexNum[1];
	if ( sb < sa ) {
		const float s = sa;
		sa = sb;
		sb = s;
		const int v = va;
		va = vb;
		vb = v;
	}

	if ( startVertex == -1 || sa < segment.start ) {
		startVertex = va;
		segment.start = sa;
	}
	if ( endVertex == -1 || sb > segment.end ) {
		endVertex = vb;
		segment.end = sb;
	}
	return true;
}

/*
Measures how far a ledge runs along the wall by walking the AAS edges numbered
next to the reachability's edge; the BSP store numbers a ledge's edges in order.
*/
bool idTacticalBuilder::LedgeSegment( tacticalSegment_t &segment, const idReachability *reach, const tacticalWall_t &wall ) const {
	int startVertex = -1;
	int endVertex = -1;

	segment.start = segment.end = 0.0f;
	segment.height = reach->start.z - reach->end.z;

	const int edgeNum = abs( reach->edgeNum );
	for ( int e = edgeNum; EdgeExtendsLedge( segment, e, wall, startVertex, endVertex ); e++ ) {
	}
	for ( int e = edgeNum - 1; EdgeExtendsLedge( segment, e, wall, startVertex, endVertex ); e-- ) {
	}
	return startVertex != -1 && endVertex != -1;
}

idVec3 tacticalSortOrigin;

int SortLedgesByDistance( idReachability * const *a, idReachability * const *b ) {
	const float da = ( tacticalSortOrigin - ( *a )->end ).LengthSqr();
	const float db = ( tacticalSortOrigin - ( *b )->end ).LengthSqr();
	if ( da < db ) {
		return -1;
	}
	return da > db ? 1 : 0;
}

/*
Splits a wall into height segments: low runs measured from ledge drops, and
full-height runs between and around them. Segment ends then move one bounding
box radius away from taller neighbors, and toward the joint by the turn's sharpness
where the wall meets another wall.
*/
void idTacticalBuilder::BuildSegments( tacticalWall_t &wall ) {
	tacticalSortOrigin = wall.start;
	wall.ledges.Sort( SortLedgesByDistance );

	float prevEnd = 0.0f;
	float prevHeight = 0.0f;
	for ( int i = 0; i < wall.ledges.Num(); i++ ) {
		tacticalSegment_t segment;
		if ( !LedgeSegment( segment, wall.ledges[i], wall ) ) {
			continue;
		}
		if ( !wall.segments.Num() || idMath::Fabs( segment.height - prevHeight ) >= TACTICAL_EDGE_NEAR || segment.start - prevEnd >= TACTICAL_EDGE_NEAR ) {
			if ( segment.start - prevEnd > TACTICAL_EDGE_NEAR ) {
				tacticalSegment_t gap = { prevEnd, segment.start, TACTICAL_FULL_HEIGHT };
				wall.segments.Append( gap );
			}
			wall.segments.Append( segment );
		} else if ( prevEnd < segment.end ) {
			tacticalSegment_t &last = wall.segments[wall.segments.Num() - 1];
			last.end = segment.end;
			last.height = segment.height;
		}
		prevEnd = segment.end;
		prevHeight = segment.height;
	}

	const float wallLength = Distance2D( wall.start, wall.end );
	if ( wallLength - prevEnd > TACTICAL_EDGE_NEAR ) {
		tacticalSegment_t tail = { prevEnd, wallLength, TACTICAL_FULL_HEIGHT };
		wall.segments.Append( tail );
	}

	const float r = Radius( 1.0f );
	for ( int i = 0; i < wall.segments.Num(); i++ ) {
		tacticalSegment_t &segment = wall.segments[i];
		if ( i == 0 ) {
			const tacticalWall_t *prevWall = Wall( wall.prevWall );
			if ( prevWall ) {
				const float adjust = ( 1.0f - Max( 0.0f, wall.dir * prevWall->dir ) ) * r;
				segment.start += Orient2D( wall.start, wall.end, prevWall->start ) <= 0.0f ? -adjust : adjust;
			}
		} else {
			segment.start += segment.height < wall.segments[i - 1].height ? -r : r;
		}
		if ( i == wall.segments.Num() - 1 ) {
			const tacticalWall_t *nextWall = Wall( wall.nextWall );
			if ( nextWall ) {
				const float adjust = ( 1.0f - Max( 0.0f, wall.dir * nextWall->dir ) ) * r;
				segment.end += Orient2D( wall.start, wall.end, nextWall->end ) <= 0.0f ? adjust : -adjust;
			}
		} else {
			segment.end -= segment.height < wall.segments[i + 1].height ? -r : r;
		}
	}
}

/*
Whether the wall's start (atStart) or end turns a corner the AI can lean
around: the neighboring walls fold back past this wall's line within a short
distance and end up roughly perpendicular to it.
*/
bool idTacticalBuilder::IsCorner( const tacticalWall_t &wall, bool atStart ) const {
	const tacticalWall_t *w1 = Wall( atStart ? wall.prevWall : wall.nextWall );
	const tacticalWall_t *w2 = w1 ? Wall( atStart ? w1->prevWall : w1->nextWall ) : NULL;
	const tacticalWall_t *w3 = w2 ? Wall( atStart ? w2->prevWall : w2->nextWall ) : NULL;

	if ( !w1 ) {
		return false;
	}
	if ( Orient2D( atStart ? w1->start : w1->end, wall.start, wall.end ) <= 0.0f ) {
		return false;
	}
	if ( w1->length > TACTICAL_CORNER_LONG ) {
		return wall.dir * w1->dir < TACTICAL_CORNER_TURN_COS;
	}
	if ( !w2 ) {
		return w1->length > TACTICAL_CORNER_MEDIUM && wall.dir * w1->dir < TACTICAL_CORNER_TURN_COS;
	}

	const idVec3 &p2 = atStart ? w2->start : w2->end;
	if ( Orient2D( p2, wall.start, wall.end ) <= 0.0f ) {
		return false;
	}
	if ( w1->length > TACTICAL_CORNER_SHORT && wall.dir * w1->dir > TACTICAL_CORNER_STRAIGHT_COS ) {
		return false;
	}
	if ( wall.dir * w2->dir < TACTICAL_CORNER_TURN_COS ) {
		return true;
	}
	if ( Orient2D( p2, w1->start, w1->end ) > 0.0f ) {
		return w1->dir * w2->dir < TACTICAL_CORNER_TURN_COS;
	}
	if ( !w3 ) {
		return false;
	}
	if ( w3->length + w2->length + w1->length < TACTICAL_CORNER_MEDIUM ) {
		return false;
	}
	if ( w2->length + w1->length > TACTICAL_CORNER_SHORT ) {
		return false;
	}
	if ( Orient2D( atStart ? w3->start : w3->end, w2->start, w2->end ) <= 0.0f ) {
		return false;
	}
	return wall.dir * w3->dir < TACTICAL_CORNER_TURN_COS;
}

/*
Places a feature 't' units along the wall, one unit off its face. The position
needs headroom; cover needs the wall to block a third of the way up, and each
attack option needs a clear line over or around it.
*/
void idTacticalBuilder::PlaceFeature( const tacticalWall_t &wall, float height, float t, int flags ) {
	const idVec3 &gravityDir = settings.gravityDir;
	const float r = Radius( 1.0f );
	const idVec3 normal( -wall.dir.y, wall.dir.x, 0.0f );
	const idVec3 origin = wall.start + wall.dir * t - normal;

	const int areaNum = ReachableArea( origin );
	if ( !areaNum ) {
		return;
	}

	const idVec3 above = origin - gravityDir * ( settings.maxStepHeight + 1.0f );
	if ( collisionModelManager->Contents( above, NULL, mat3_identity, 0, world, vec3_origin, mat3_identity ) & TACTICAL_PLACE_MASK ) {
		return;
	}
	if ( !TraceClear( above, origin - gravityDir * TACTICAL_HEADROOM, TACTICAL_PLACE_MASK ) ) {
		return;
	}

	if ( flags & FEATURE_COVER ) {
		const idVec3 low = origin - gravityDir * ( height * 0.333f );
		if ( TraceClear( low, low + normal * ( r * 3.0f ), TACTICAL_SIGHT_MASK ) ) {
			return;
		}

		const idVec3 eye = origin - gravityDir * ( height + TACTICAL_STEP_HEIGHT_DELTA );
		const idVec3 reach = normal * ( r * 7.0f );
		if ( ( flags & FEATURE_LOOK_OVER ) && !TraceClear( eye, eye + reach, TACTICAL_SIGHT_MASK ) ) {
			flags &= ~FEATURE_LOOK_OVER;
		}
		if ( ( flags & FEATURE_LOOK_LEFT ) && !( flags & FEATURE_CORNER_LEFT ) ) {
			const idVec3 lean = eye - wall.dir * ( r + 5.0f );
			if ( !TraceClear( lean, lean + reach, TACTICAL_SIGHT_MASK ) ) {
				flags &= ~FEATURE_LOOK_LEFT;
			}
		}
		if ( ( flags & FEATURE_LOOK_RIGHT ) && !( flags & FEATURE_CORNER_RIGHT ) ) {
			const idVec3 lean = eye + wall.dir * ( r + 5.0f );
			if ( !TraceClear( lean, lean + reach, TACTICAL_SIGHT_MASK ) ) {
				flags &= ~FEATURE_LOOK_RIGHT;
			}
		}
		if ( !( flags & ( FEATURE_LOOK_LEFT | FEATURE_LOOK_RIGHT | FEATURE_LOOK_OVER ) ) ) {
			return;
		}
	}

	if ( flags ) {
		AddFeature( origin, normal, flags, height, areaNum );
	}
}

/*
Chooses features for each segment long enough for the AI: corners and steps
down to a lower neighbor allow leaning, low segments allow looking over. Short
segments get one central feature; longer ones get leans near the ends and evenly
spaced look-over positions between them.
*/
void idTacticalBuilder::SegmentWall( tacticalWall_t &wall ) {
	BuildSegments( wall );

	const float r = Radius( 1.0f );
	const tacticalSegment_t *prev = NULL;
	for ( int i = 0; i < wall.segments.Num(); i++ ) {
		const tacticalSegment_t &segment = wall.segments[i];
		const tacticalSegment_t *next = i < wall.segments.Num() - 1 ? &wall.segments[i + 1] : NULL;
		float start = segment.start;
		float end = segment.end;
		const float length = end - start;
		const float height = segment.height;

		if ( length < r * 1.6f ) {
			prev = &segment;
			continue;
		}

		int flags = 0;
		if ( !prev && IsCorner( wall, true ) ) {
			flags = FEATURE_CORNER_LEFT;
		}
		if ( !next && IsCorner( wall, false ) ) {
			flags |= FEATURE_CORNER_RIGHT;
		}
		if ( ( prev && height - prev->height > TACTICAL_STEP_HEIGHT_DELTA ) || ( flags & FEATURE_CORNER_LEFT ) ) {
			flags |= FEATURE_LOOK_LEFT;
		}
		if ( ( next && height - next->height > TACTICAL_STEP_HEIGHT_DELTA ) || ( flags & FEATURE_CORNER_RIGHT ) ) {
			flags |= FEATURE_LOOK_RIGHT;
		}
		if ( height < TACTICAL_LEDGE_MAX_HEIGHT ) {
			flags |= FEATURE_LOOK_OVER;
		}
		if ( !flags ) {
			continue;		// retail keeps the previous segment as 'prev' here
		}
		prev = &segment;

		const int leftFlags = ( flags & ( FEATURE_COVER | FEATURE_LOOK_LEFT | FEATURE_LOOK_OVER | FEATURE_CORNER_LEFT ) ) | FEATURE_COVER;
		const int rightFlags = ( flags & ( FEATURE_COVER | FEATURE_LOOK_RIGHT | FEATURE_LOOK_OVER | FEATURE_CORNER_RIGHT ) ) | FEATURE_COVER;
		const bool leans = ( flags & ( FEATURE_LOOK_LEFT | FEATURE_LOOK_RIGHT ) ) != 0;

		if ( length <= r * 3.0f || ( length <= r * 4.5f && !leans ) ) {
			PlaceFeature( wall, height, ( start + end ) * 0.5f, flags | FEATURE_COVER );
			continue;
		}
		if ( length <= r * 4.5f ) {
			if ( flags & FEATURE_LOOK_LEFT ) {
				PlaceFeature( wall, height, start + r, leftFlags );
			}
			if ( flags & FEATURE_LOOK_RIGHT ) {
				PlaceFeature( wall, height, end - r, rightFlags );
			}
			continue;
		}

		if ( flags & FEATURE_LOOK_LEFT ) {
			PlaceFeature( wall, height, start + r, leftFlags );
			start += r * 2.0f;
		}
		if ( flags & FEATURE_LOOK_RIGHT ) {
			PlaceFeature( wall, height, end - r, rightFlags );
			end -= r * 2.0f;
		}
		if ( !( flags & FEATURE_LOOK_OVER ) ) {
			continue;
		}

		const int overFlags = FEATURE_COVER | FEATURE_LOOK_OVER;
		const float span = end - start;
		if ( span >= r * 7.0f ) {
			PlaceFeature( wall, height, start + span * 0.25f, overFlags );
			PlaceFeature( wall, height, start + span * 0.5f, overFlags );
			PlaceFeature( wall, height, start + span * 0.75f, overFlags );
		} else if ( span >= r * 4.5f ) {
			PlaceFeature( wall, height, start + span * 0.33f, overFlags );
			PlaceFeature( wall, height, start + span * 0.66f, overFlags );
		} else if ( span >= r * 3.0f ) {
			PlaceFeature( wall, height, start + span * 0.5f, overFlags );
		}
	}
}

/*
Each area lists its own features, most recently added first, which is the order
retail's per-area chains produced.
*/
void idTacticalBuilder::BuildAreaIndex( void ) {
	// areas address their span of the index with 16-bit fields
	const int maxIndexes = 0xffff;

	for ( int areaNum = 1; areaNum < file->GetNumAreas(); areaNum++ ) {
		aasArea_t &area = file->GetArea( areaNum );
		area.numFeatures = 0;
		area.firstFeature = 0;

		int count = 0;
		for ( int featureNum = areaFeatures[areaNum]; featureNum >= 0; featureNum = featureChain[featureNum] ) {
			count++;
		}
		if ( file->GetNumFeatureIndexes() + count > maxIndexes ) {
			common->Warning( "%s: more than %d tactical feature references; area %d and later list none",
				settings.fileExtension.c_str(), maxIndexes, areaNum );
			return;
		}

		for ( int featureNum = areaFeatures[areaNum]; featureNum >= 0; featureNum = featureChain[featureNum] ) {
			const int index = file->AppendFeatureIndex( featureNum );
			if ( !area.numFeatures ) {
				area.firstFeature = static_cast<unsigned short>( index );
			}
			area.numFeatures++;
		}
	}
}

int idTacticalBuilder::Generate( void ) {
	file->ClearTactical();

	const int numAreas = file->GetNumAreas();
	if ( numAreas < 2 ) {
		return 0;
	}
	areaFeatures.AssureSize( numAreas, -1 );

	idList<int> stack;
	idList<bool> areaVisited;
	areaVisited.AssureSize( numAreas, false );

	for ( int i = 0; i < mapFile->GetNumEntities(); i++ ) {
		const idMapEntity *entity = mapFile->GetEntity( i );
		const char *classname = entity->epairs.GetString( "classname" );
		if ( !idStr::Icmp( classname, "info_tactical_feature" ) ) {
			AddPlacedFeature( entity );
			continue;
		}
		idVec3 origin;
		if ( !settings.ValidEntity( classname ) || !entity->epairs.GetVector( "origin", "", origin ) ) {
			continue;
		}
		const int areaNum = ReachableArea( origin );
		if ( areaNum > 0 && !areaVisited[areaNum] ) {
			areaVisited[areaNum] = true;
			stack.Append( areaNum );
		}
	}

	FloodAreas( stack, areaVisited );
	LinkEdges();
	BuildWalls();
	for ( int i = 0; i < walls.Num(); i++ ) {
		SegmentWall( walls[i] );
	}
	BuildAreaIndex();

	return file->GetNumFeatures();
}

} // namespace

/*
============
AAS_GenerateTacticalFeatures

Replaces the file's tactical features. Loads the map's collision world for the
placement traces and releases it afterwards. Returns the number of features, or
-1 when the collision world is unavailable.
============
*/
int AAS_GenerateTacticalFeatures( idAASFileLocal *file, const idMapFile *mapFile ) {
	const int startTime = Sys_Milliseconds();

	common->Printf( "[Tactical Features]\n" );

	// A loaded game owns the collision manager's procedural clip models, which a
	// fresh world build replaces; dmap releases the game for the same reason.
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, "disconnect" );

	collisionModelManager->LoadMap( mapFile );
	idCollisionModel *world = collisionModelManager->LoadModel( mapFile->GetName(), WORLD_MODEL_NAME );
	if ( !world ) {
		collisionModelManager->FreeMap( mapFile->GetName() );
		file->ClearTactical();
		common->Warning( "no collision world for '%s'; %s has no tactical features", mapFile->GetName(), file->GetSettings().fileExtension.c_str() );
		return -1;
	}

	idTacticalBuilder builder( file, mapFile, world );
	const int numFeatures = builder.Generate();

	collisionModelManager->FreeModel( world );
	collisionModelManager->FreeMap( mapFile->GetName() );

	common->Printf( "%6d tactical features\n", numFeatures );
	common->Printf( "%6d msec to create tactical features\n", Sys_Milliseconds() - startTime );
	return numFeatures;
}
