// Compile the production dmap T-junction splitter against a minimal geometry
// shim. No renderer or proprietary map assets required.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning( disable : 4244 4305 )	// the production snapping narrows double to int and float
#endif
// the production splitter keeps id's unused third-corner pointer
#if defined( __clang__ )
#if __has_warning( "-Wunused-but-set-variable" )
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#endif
#elif defined( __GNUC__ )
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

template<class T> static T Square( T x ) { return x * x; }

class idVec3 {
public:
	float x = 0, y = 0, z = 0;
	idVec3() = default;
	idVec3( float a, float b, float c ) : x( a ), y( b ), z( c ) {}
	float operator[]( int index ) const { return ( &x )[index]; }
	float &operator[]( int index ) { return ( &x )[index]; }
	idVec3 operator-( const idVec3 &a ) const { return idVec3( x - a.x, y - a.y, z - a.z ); }
	idVec3 Cross( const idVec3 &a ) const { return idVec3( y * a.z - z * a.y, z * a.x - x * a.z, x * a.y - y * a.x ); }
	float LengthSqr() const { return x * x + y * y + z * z; }
	float Length() const { return std::sqrt( LengthSqr() ); }
	// matches idVec3::Normalize, including Raven's zero-length early out
	float Normalize() {
		const float sqrLength = LengthSqr();
		if ( !sqrLength ) {
			return 0.0f;
		}
		const float invLength = 1.0f / std::sqrt( sqrLength );
		x *= invLength;
		y *= invLength;
		z *= invLength;
		return invLength * sqrLength;
	}
};
struct idVec2 {
	float v[2] = {};
	float operator[]( int index ) const { return v[index]; }
	float &operator[]( int index ) { return v[index]; }
};
struct idDrawVert {
	idVec3 xyz;
	idVec2 st;
	idVec3 normal;
};
class idPlane {
public:
	float a = 0, b = 0, c = 0, d = 0;
	float operator[]( int index ) const { return ( &a )[index]; }
	// matches idPlane::FromPoints with fixDegenerate
	bool FromPoints( const idVec3 &p1, const idVec3 &p2, const idVec3 &p3 ) {
		idVec3 normal = ( p1 - p2 ).Cross( p3 - p2 );
		const float length = normal.Normalize();
		if ( normal.x == 0.0f && normal.y == 0.0f ) {
			normal.z = normal.z > 0.0f ? 1.0f : -1.0f;
		} else if ( normal.x == 0.0f && normal.z == 0.0f ) {
			normal.y = normal.y > 0.0f ? 1.0f : -1.0f;
		} else if ( normal.y == 0.0f && normal.z == 0.0f ) {
			normal.x = normal.x > 0.0f ? 1.0f : -1.0f;
		}
		a = normal.x;
		b = normal.y;
		c = normal.z;
		if ( length == 0.0f ) {
			return false;
		}
		d = -( a * p2.x + b * p2.y + c * p2.z );
		return true;
	}
};
class idBounds {
public:
	idVec3 b[2];
	void Clear() {
		b[0] = idVec3( 1e30f, 1e30f, 1e30f );
		b[1] = idVec3( -1e30f, -1e30f, -1e30f );
	}
	void AddPoint( const idVec3 &v ) {
		for ( int i = 0; i < 3; i++ ) {
			b[0][i] = v[i] < b[0][i] ? v[i] : b[0][i];
			b[1][i] = v[i] > b[1][i] ? v[i] : b[1][i];
		}
	}
	idVec3 &operator[]( int index ) { return b[index]; }
	const idVec3 &operator[]( int index ) const { return b[index]; }
};
#define DotProduct( a, b )			( ( a )[0] * ( b )[0] + ( a )[1] * ( b )[1] + ( a )[2] * ( b )[2] )
#define VectorSubtract( a, b, c )	( ( c )[0] = ( a )[0] - ( b )[0], ( c )[1] = ( a )[1] - ( b )[1], ( c )[2] = ( a )[2] - ( b )[2] )
#define VectorMA( v, s, b, o )		( ( o )[0] = ( v )[0] + ( b )[0] * ( s ), ( o )[1] = ( v )[1] + ( b )[1] * ( s ), ( o )[2] = ( v )[2] + ( b )[2] * ( s ) )
#define VectorCopy( a, b )			( ( b )[0] = ( a )[0], ( b )[1] = ( a )[1], ( b )[2] = ( a )[2] )

struct hashVert_s;
struct mapTri_t {
	mapTri_t *next = nullptr;
	const struct hashVert_s *hashVert[3] = {};
	idDrawVert v[3];
};
struct idMaterial {
	bool IsDiscrete() const { return false; }
	bool NoTFix() const { return false; }
};
struct optimizeGroup_t {
	optimizeGroup_t *nextGroup = nullptr;
	mapTri_t *triList = nullptr;
	const idMaterial *material = nullptr;
};

static int liveTris = 0, liveHashVerts = 0;
static void *Mem_Alloc( size_t size ) {
	liveHashVerts++;
	return std::malloc( size );
}
static void Mem_Free( void *pointer ) {
	liveHashVerts--;
	std::free( pointer );
}
static mapTri_t *CopyMapTri( const mapTri_t *tri ) {
	liveTris++;
	return new mapTri_t( *tri );
}
static void FreeTri( mapTri_t *tri ) {
	liveTris--;
	delete tri;
}
static void FreeTriList( mapTri_t *tri ) {
	while ( tri ) {
		mapTri_t *next = tri->next;
		FreeTri( tri );
		tri = next;
	}
}

// @PRODUCTION@

static void Require( bool condition, const std::string &message ) {
	if ( !condition ) {
		throw std::runtime_error( message );
	}
}

static float TriArea( const mapTri_t *tri ) {
	return 0.5f * ( tri->v[1].xyz - tri->v[0].xyz ).Cross( tri->v[2].xyz - tri->v[0].xyz ).Length();
}

static void AddTri( optimizeGroup_t &group, const idVec3 &a, const idVec3 &b, const idVec3 &c ) {
	mapTri_t blank;
	mapTri_t *tri = CopyMapTri( &blank );
	tri->v[0].xyz = a;
	tri->v[1].xyz = b;
	tri->v[2].xyz = c;
	tri->next = group.triList;
	group.triList = tri;
}

// a strip of triangles below y = 0 whose vertexes sit every `step` units along x
static void AddNeighbourStrip( optimizeGroup_t &group, float length, float step ) {
	for ( float x = 0; x < length; x += step ) {
		AddTri( group, idVec3( x, 0, 0 ), idVec3( x, -16, 0 ), idVec3( x + step, 0, 0 ) );
	}
}

struct FixResult {
	int count = 0;
	float area = 0;
};

// hashes the target triangle together with its neighbours and fixes it
static FixResult FixOne( const idVec3 &a, const idVec3 &b, const idVec3 &c, float length, float step ) {
	optimizeGroup_t target, neighbours;
	AddTri( target, a, b, c );
	AddNeighbourStrip( neighbours, length, step );
	target.nextGroup = &neighbours;
	HashTriangles( &target );

	FixResult result;
	mapTri_t *fixed = FixTriangleAgainstHash( target.triList );
	for ( mapTri_t *tri = fixed; tri; tri = tri->next ) {
		result.count++;
		result.area += TriArea( tri );
	}
	FreeTriList( fixed );
	FreeTriList( target.triList );
	FreeTriList( neighbours.triList );
	FreeTJunctionHash();
	Require( liveTris == 0 && liveHashVerts == 0, "the fixer leaked triangles or hash vertexes" );
	return result;
}

static void TestRealTriangleStillSplits() {
	const FixResult result = FixOne( idVec3( 0, 0, 0 ), idVec3( 64, 0, 0 ), idVec3( 0, 64, 0 ), 64, 32 );
	Require( result.count == 2, "a real triangle must still split at its T-junction, got " + std::to_string( result.count ) );
	Require( std::fabs( result.area - 2048.0f ) < 0.01f, "splitting must preserve the triangle's area" );
}

static void TestThinTriangleSplitsAtEveryVertex() {
	// 8 units high over a 512-unit edge is thin but not a sliver
	const FixResult result = FixOne( idVec3( 0, 0, 0 ), idVec3( 512, 0, 0 ), idVec3( 0, 8, 0 ), 512, 16 );
	Require( result.count == 32, "a thin triangle must split at all 31 edge vertexes, got " + std::to_string( result.count ) );
	Require( std::fabs( result.area - 2048.0f ) < 0.01f, "splitting must preserve the thin triangle's area" );
}

static void TestSliverDoesNotMultiply() {
	// the old splitter turned each of these into thousands of overlapping slivers
	const idVec3 collinear[] = { idVec3( 256, 0, 0 ), idVec3( 256, 1.0f / 32, 0 ), idVec3( 100, 0.0f, 0 ) };
	for ( const idVec3 &corner : collinear ) {
		const FixResult result = FixOne( idVec3( 0, 0, 0 ), idVec3( 512, 0, 0 ), corner, 512, 32 );
		Require( result.count == 1, "a sliver must pass through unsplit, got " + std::to_string( result.count ) + " triangles" );
	}
}

int main() {
	try {
		TestRealTriangleStillSplits();
		TestThinTriangleSplitsAtEveryVertex();
		TestSliverDoesNotMultiply();
	} catch ( const std::exception &error ) {
		std::fprintf( stderr, "FAIL: %s\n", error.what() );
		return 1;
	}
	std::printf( "dmap T-junction sliver tests passed\n" );
	return 0;
}
