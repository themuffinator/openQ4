// Compile the production silhouette edge builder and the real idHashIndex
// against a minimal geometry shim. No renderer or proprietary assets required.
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifdef INT_MAX
#undef INT_MAX	// idMath::INT_MAX is a member
#endif
// the production builder keeps id's unused tally of shared edges
#if defined( __clang__ )
#if __has_warning( "-Wunused-but-set-variable" )
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#endif
#elif defined( __GNUC__ )
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#define ID_INLINE inline
#define MEM_SCOPED_TAG( var, tag )

template<class T> static T Max( T a, T b ) { return a > b ? a : b; }

class idMath {
public:
	static const int INT_MAX = 0x7fffffff;
	static bool IsPowerOfTwo( int x ) { return x > 0 && ( x & ( x - 1 ) ) == 0; }
	static int CeilPowerOfTwo( int x ) {
		int p = 1;
		while ( p < x ) {
			p <<= 1;
		}
		return p;
	}
	static int ClampInt( int min, int max, int value ) { return value < min ? min : ( value > max ? max : value ); }
};

class idVec3 {
public:
	float x = 0, y = 0, z = 0;
	float operator[]( int index ) const { return ( &x )[index]; }
};
class idStr {
public:
	static int Hash( const char * ) { return 0; }
	static int IHash( const char * ) { return 0; }
};

// @HASHINDEX@

typedef int glIndex_t;
typedef struct silEdge_s {
	glIndex_t p1, p2;
	glIndex_t v1, v2;
} silEdge_t;
struct idDrawVert {
	idVec3 xyz;
};
class idPlane {
public:
	bool FromPoints( const idVec3 &, const idVec3 &, const idVec3 & ) { return true; }
	float Distance( const idVec3 & ) const { return 1.0f; }
};
struct srfTriangles_t {
	int numIndexes = 0;
	glIndex_t *silIndexes = nullptr;
	idDrawVert *verts = nullptr;
	bool perfectHull = false;
	int numSilEdges = 0;
	silEdge_t *silEdges = nullptr;
};

static int liveStaticAllocs;
static void *R_StaticAlloc( size_t bytes ) {
	void *buf = std::malloc( bytes );
	if ( !buf && bytes != 0 ) {
		throw std::runtime_error( "R_StaticAlloc failed" );
	}
	if ( buf ) {
		liveStaticAllocs++;
	}
	return buf;
}
static void R_StaticFree( void *data ) {
	if ( data ) {
		liveStaticAllocs--;
	}
	std::free( data );
}

static struct {
	silEdge_t *Alloc( int num ) { return new silEdge_t[num > 0 ? num : 1]; }
} triSilEdgeAllocator;

struct renderGeoHooks_t {
	bool ( *reportSilEdgeWarnings )( void ) = nullptr;
};
static const renderGeoHooks_t &RenderGeo_GetHooks() {
	static const renderGeoHooks_t hooks;
	return hooks;
}

static int edgeLimitWarnings;
static struct {
	void DWarning( const char *fmt, ... ) {
		if ( std::strcmp( fmt, "MAX_SIL_EDGES" ) == 0 ) {
			edgeLimitWarnings++;
		}
	}
} commonShim, *common = &commonShim;

// @PRODUCTION_STATE@

// @PRODUCTION_CAPACITY@

// @PRODUCTION_EDGES@

static void InitScratch() {
#ifdef OPENQ4_FIXED_SIL_EDGE_SCRATCH
	silEdges = (silEdge_t *)R_StaticAlloc( MAX_SIL_EDGES * sizeof( silEdges[0] ) );
#else
	R_SetSilEdgeCapacity( MIN_SIL_EDGES );
#endif
}

static void Require( bool condition, const std::string &message ) {
	if ( !condition ) {
		throw std::runtime_error( message );
	}
}

typedef std::tuple<int, int, int, int> edgeKey_t;

// R_DefineEdge's matching rule on its own: a directed edge pairs with the newest
// unpaired reverse edge, and anything else becomes a new edge
struct referenceEdges_t {
	std::vector<edgeKey_t> edges;
	int duplicated = 0;
	int tripled = 0;
	bool perfectHull = true;
};

static referenceEdges_t ReferenceEdges( const std::vector<int> &indexes ) {
	referenceEdges_t out;
	const int numTris = (int)indexes.size() / 3;
	std::vector<silEdge_t> edges;
	std::map<std::pair<int, int>, std::vector<int>> byPair;
	for ( int t = 0; t < numTris; t++ ) {
		for ( int e = 0; e < 3; e++ ) {
			const int v1 = indexes[t * 3 + e];
			const int v2 = indexes[t * 3 + ( e + 1 ) % 3];
			if ( v1 == v2 ) {
				continue;
			}
			std::vector<int> &list = byPair[std::make_pair( std::min( v1, v2 ), std::max( v1, v2 ) )];
			bool matched = false;
			for ( auto it = list.rbegin(); it != list.rend(); ++it ) {
				silEdge_t &other = edges[*it];
				if ( other.v1 == v1 && other.v2 == v2 ) {
					out.duplicated++;
					continue;
				}
				if ( other.p2 != numTris ) {
					out.tripled++;
					continue;
				}
				other.p2 = t;
				matched = true;
				break;
			}
			if ( !matched ) {
				list.push_back( (int)edges.size() );
				edges.push_back( silEdge_t{ t, numTris, v1, v2 } );
			}
		}
	}
	for ( const silEdge_t &edge : edges ) {
		out.edges.emplace_back( edge.p1, edge.p2, edge.v1, edge.v2 );
		if ( edge.p2 == numTris ) {
			out.perfectHull = false;
		}
	}
	std::sort( out.edges.begin(), out.edges.end() );
	return out;
}

struct builtEdges_t {
	std::vector<edgeKey_t> edges;
	bool perfectHull = false;
};

static builtEdges_t BuildEdges( std::vector<int> indexes ) {
	std::vector<idDrawVert> verts( 1 );
	srfTriangles_t tri;
	tri.numIndexes = (int)indexes.size();
	tri.silIndexes = indexes.data();
	tri.verts = verts.data();
	edgeLimitWarnings = 0;
	R_IdentifySilEdges( &tri, true );
	builtEdges_t out;
	out.perfectHull = tri.perfectHull;
	for ( int i = 0; i < tri.numSilEdges; i++ ) {
		const silEdge_t &edge = tri.silEdges[i];
		out.edges.emplace_back( edge.p1, edge.p2, edge.v1, edge.v2 );
	}
	// the builder sorts by plane, and qsort may order equal planes either way
	std::sort( out.edges.begin(), out.edges.end() );
	delete[] tri.silEdges;
	return out;
}

static void RequireMatchesReference( const std::vector<int> &indexes, const char *name ) {
	const referenceEdges_t expected = ReferenceEdges( indexes );
	const builtEdges_t built = BuildEdges( indexes );
	Require( edgeLimitWarnings == 0, std::string( name ) + ": hit the silhouette edge limit" );
	Require( built.edges.size() == expected.edges.size(),
		std::string( name ) + ": " + std::to_string( built.edges.size() ) + " sil edges, expected "
		+ std::to_string( expected.edges.size() ) );
	Require( built.edges == expected.edges, std::string( name ) + ": sil edges differ from the reference" );
	Require( c_duplicatedEdges == expected.duplicated && c_tripledEdges == expected.tripled,
		std::string( name ) + ": duplicated/tripled edge counts differ from the reference" );
	Require( built.perfectHull == expected.perfectHull, std::string( name ) + ": perfectHull differs" );
}

// cols x rows quads, each split into two triangles; wrapping joins opposite
// borders into a torus, which has no dangling edges
static std::vector<int> Grid( int cols, int rows, bool wrap ) {
	const int vertCols = wrap ? cols : cols + 1;
	const int vertRows = wrap ? rows : rows + 1;
	auto vert = [&]( int c, int r ) { return ( r % vertRows ) * vertCols + ( c % vertCols ); };
	std::vector<int> indexes;
	for ( int r = 0; r < rows; r++ ) {
		for ( int c = 0; c < cols; c++ ) {
			const int a = vert( c, r ), b = vert( c + 1, r ), d = vert( c, r + 1 ), e = vert( c + 1, r + 1 );
			indexes.insert( indexes.end(), { a, b, e, a, e, d } );
		}
	}
	return indexes;
}

static void TestSmallSurfaceMatchesReference() {
	// a closed cube with a dangling fin, a degenerate triangle and a
	// duplicated face, so every R_DefineEdge branch runs
	std::vector<int> indexes = {
		0, 1, 2, 0, 2, 3,  4, 6, 5, 4, 7, 6,  0, 4, 5, 0, 5, 1,
		1, 5, 6, 1, 6, 2,  2, 6, 7, 2, 7, 3,  3, 7, 4, 3, 4, 0,
		0, 1, 8,  9, 9, 2,  0, 1, 2,
	};
	RequireMatchesReference( indexes, "small surface" );
	RequireMatchesReference( Grid( 8, 8, true ), "small torus" );
}

static void TestLargeOpenGridKeepsEveryEdge() {
	// 80,000 triangles and 120,400 edges, of which 800 are on the border
	const std::vector<int> indexes = Grid( 200, 200, false );
	const builtEdges_t built = BuildEdges( indexes );
	Require( edgeLimitWarnings == 0, "large grid: hit the silhouette edge limit" );
	Require( built.edges.size() == 120400, "large grid: " + std::to_string( built.edges.size() )
		+ " sil edges, expected 120400" );
	int dangling = 0;
	for ( const edgeKey_t &edge : built.edges ) {
		dangling += std::get<1>( edge ) == 80000;
	}
	Require( dangling == 800, "large grid: " + std::to_string( dangling ) + " border edges, expected 800" );
	Require( !built.perfectHull, "large grid: an open surface reported a perfect hull" );
	RequireMatchesReference( indexes, "large grid" );
}

static void TestLargeClosedHullIsPerfect() {
	const std::vector<int> indexes = Grid( 200, 200, true );
	const builtEdges_t built = BuildEdges( indexes );
	Require( built.edges.size() == 120000, "large torus: " + std::to_string( built.edges.size() )
		+ " sil edges, expected 120000" );
	Require( built.perfectHull, "large torus: a closed surface lost its perfect hull" );
	RequireMatchesReference( indexes, "large torus" );
}

static void TestLargeTangledSurfaceMatchesReference() {
	// random triangles over a small vertex pool give long runs of duplicated
	// and tripled edges well past the old limit
	std::vector<int> indexes;
	unsigned int seed = 181;
	auto next = [&]() { seed = seed * 1664525u + 1013904223u; return (int)( ( seed >> 8 ) % 6000u ); };
	for ( int t = 0; t < 60000; t++ ) {
		indexes.insert( indexes.end(), { next(), next(), next() } );
	}
	RequireMatchesReference( indexes, "tangled surface" );
}

static void TestScratchReturnsToRestingSize() {
#ifndef OPENQ4_FIXED_SIL_EDGE_SCRATCH
	R_SetSilEdgeCapacity( 240000 );
	Require( silEdgeHash.GetHashSize() == 4096 && silEdgeHash.GetIndexSize() == 240000,
		"capacity: the hash did not grow with the scratch" );
	R_SetSilEdgeCapacity( MIN_SIL_EDGES );
	BuildEdges( Grid( 200, 200, false ) );
	Require( maxSilEdges == MIN_SIL_EDGES, "capacity: the scratch stayed at a large surface's size" );
	Require( silEdgeHash.GetHashSize() == SILEDGE_HASH_SIZE && silEdgeHash.GetIndexSize() == MIN_SIL_EDGES,
		"capacity: the hash stayed at a large surface's size" );
	Require( liveStaticAllocs == 1, "capacity: " + std::to_string( liveStaticAllocs ) + " scratch buffers live" );
	RequireMatchesReference( Grid( 8, 8, false ), "small grid after a large surface" );
#endif
}

int main() {
	try {
		InitScratch();
		TestSmallSurfaceMatchesReference();
		TestLargeOpenGridKeepsEveryEdge();
		TestLargeClosedHullIsPerfect();
		TestLargeTangledSurfaceMatchesReference();
		TestScratchReturnsToRestingSize();
		R_StaticFree( silEdges );
		silEdges = nullptr;
		silEdgeHash.Free();
		Require( liveStaticAllocs == 0, "the scratch leaked" );
	} catch ( const std::exception &error ) {
		std::fprintf( stderr, "FAIL: %s\n", error.what() );
		return 1;
	}
	std::printf( "silhouette edge capacity tests passed\n" );
	return 0;
}
