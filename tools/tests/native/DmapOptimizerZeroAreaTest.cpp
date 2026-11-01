// Compile the production dmap optimizer's triangle regeneration, with the
// idlib plane, normal and area code it relies on, against minimal geometry
// types. No renderer or proprietary map assets required.
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

#define ID_INLINE inline
struct idMath {
	static float InvSqrt( float value ) { return 1.0f / std::sqrt( value ); }
	static float Fabs( float value ) { return std::fabs( value ); }
};
struct idVec3 {
	float x = 0, y = 0, z = 0;
	idVec3() = default;
	idVec3( float x_, float y_, float z_ ) : x( x_ ), y( y_ ), z( z_ ) {}
	float &operator[]( int i ) { return ( &x )[i]; }
	const float &operator[]( int i ) const { return ( &x )[i]; }
	idVec3 operator-( const idVec3 &b ) const { return idVec3( x - b.x, y - b.y, z - b.z ); }
	float operator*( const idVec3 &b ) const { return x * b.x + y * b.y + z * b.z; }
	idVec3 Cross( const idVec3 &b ) const { return idVec3( y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x ); }
	float Length() const { return std::sqrt( x * x + y * y + z * z ); }
	float Normalize( void );
	bool FixDegenerateNormal( void );
};
// @IDVEC3@
struct idPlane {
	float a = 0, b = 0, c = 0, d = 0;
	idPlane() = default;
	idPlane( float a_, float b_, float c_, float d_ ) : a( a_ ), b( b_ ), c( c_ ), d( d_ ) {}
	idVec3 &Normal() { return *reinterpret_cast<idVec3 *>( &a ); }
	float Normalize( bool fixDegenerate = true );
	bool FixDegenerateNormal( void );
	bool FromPoints( const idVec3 &p1, const idVec3 &p2, const idVec3 &p3, bool fixDegenerate = true );
};
// @IDPLANE@
struct idWinding {
	static float TriangleArea( const idVec3 &a, const idVec3 &b, const idVec3 &c );
};
// @WINDING@

struct idMaterial;
struct idDrawVert {
	idVec3 xyz;
};
struct mapTri_t {
	mapTri_t *next = nullptr;
	const idMaterial *material = nullptr;
	void *mergeGroup = nullptr;
	idDrawVert v[3];
};
struct optVertex_t {
	idDrawVert v;
};
struct optTri_t {
	optTri_t *next = nullptr;
	idVec3 midpoint;
	optVertex_t *v[3] = { nullptr, nullptr, nullptr };
	bool filled = true;
};
struct optimizeGroup_t {
	int planeNum = 0;
	const idMaterial *material = nullptr;
	void *mergeGroup = nullptr;
	mapTri_t *regeneratedTris = nullptr;
};
struct optIsland_t {
	optimizeGroup_t *group = nullptr;
	optTri_t *tris = nullptr;
};
static struct {
	std::vector<idPlane> mapPlanes;
	bool verbose = false;
} dmapGlobals;
static int backwardsWarnings = 0;
static struct TestCommon {
	void Printf( const char *format, ... ) {
		if ( std::strstr( format, "backwards triangle" ) ) { backwardsWarnings++; }
	}
} testCommon, *common = &testCommon;

static int liveTris = 0, liveOptTris = 0;
static mapTri_t *AllocTri( void ) {
	liveTris++;
	return new mapTri_t;
}
static void FreeTri( mapTri_t *tri ) {
	liveTris--;
	delete tri;
}
static void Mem_Free( optTri_t *opt ) {
	liveOptTris--;
	delete opt;
}

// @TRITOOLS@

// @PRODUCTION@

static void Require( bool condition, const char *message ) {
	if ( !condition ) { throw std::runtime_error( message ); }
}
// Regenerates the given triangles into a group on plane planeNum and returns
// how many the optimizer kept.
static int Regenerate( int planeNum, const std::vector<std::vector<idVec3>> &corners, bool filled = true ) {
	std::vector<optVertex_t> verts( corners.size() * 3 );
	optimizeGroup_t group;
	group.planeNum = planeNum;
	optIsland_t island;
	island.group = &group;
	for ( size_t i = 0; i < corners.size(); i++ ) {
		optTri_t *opt = new optTri_t;
		liveOptTris++;
		opt->filled = filled;
		for ( int j = 0; j < 3; j++ ) {
			verts[i * 3 + j].v.xyz = corners[i][j];
			opt->v[j] = &verts[i * 3 + j];
		}
		opt->next = island.tris;
		island.tris = opt;
	}
	RegenerateTriangles( &island );
	Require( island.tris == nullptr && liveOptTris == 0, "optimizer triangles were not freed" );
	int kept = 0;
	for ( mapTri_t *tri = group.regeneratedTris, *next; tri; tri = next ) {
		next = tri->next;
		kept++;
		FreeTri( tri );
	}
	Require( liveTris == 0, "regenerated triangles leaked" );
	return kept;
}
int main() {
	try {
		dmapGlobals.mapPlanes = { idPlane( 0, 0, 1, 0 ), idPlane( 0, 0, -1, 0 ), idPlane( 0.6f, 0, -0.8f, 0 ) };
		// Wound so that FromPoints() gives the normal of the plane they are on.
		const std::vector<idVec3> up = { { 0, 0, 0 }, { 0, 32, 0 }, { 32, 0, 0 } };
		const std::vector<idVec3> down = { { 0, 0, 0 }, { 32, 0, 0 }, { 0, 32, 0 } };
		const std::vector<idVec3> tilted = { { 0, 0, 0 }, { 32, 0, 24 }, { 0, 32, 0 } };

		// Facing triangles are kept, and backwards ones still dropped with a warning.
		Require( Regenerate( 0, { up } ) == 1 && Regenerate( 1, { down } ) == 1, "facing triangle was dropped" );
		Require( Regenerate( 2, { tilted } ) == 1, "triangle on a tilted plane was dropped" );
		backwardsWarnings = 0;
		Require( Regenerate( 0, { down } ) == 0 && Regenerate( 1, { up } ) == 0, "backwards triangle was kept" );
		Require( backwardsWarnings == 2, "backwards triangle no longer warns" );
		Require( Regenerate( 1, { down }, false ) == 0, "unfilled optimizer triangle was regenerated" );

		// A zero-area triangle has no plane. FromPoints() fixes its zero normal up
		// to (0, 0, -1), which passes the facing test on any downward plane; this is
		// how the line-shaped triangles in #181's hub2 reached T-junction repair.
		const std::vector<std::vector<idVec3>> lines = {
			{ { 4062.5625f, -2803.1875f, -392.15625f }, { 4062.5625f, -2444.8125f, -392.15625f }, { 4062.5625f, -2432.0f, -392.15625f } },
			{ { 0, 0, 8 }, { 10, 0, 8 }, { 20, 0, 8 } },
			{ { -64, 32, 8 }, { -60, 36, 8 }, { -16, 80, 8 } },
			{ { 5, 5, 5 }, { 5, 5, 5 }, { 5, 5, 5 } },
		};
		for ( int plane : { 0, 1, 2 } ) {
			for ( const auto &line : lines ) {
				Require( Regenerate( plane, { line } ) == 0, "optimizer kept a zero-area triangle" );
			}
		}
		Require( Regenerate( 1, { down, lines[0], down, lines[1] } ) == 2, "zero-area triangle displaced a facing one" );
		std::puts( "dmap_optimizer_zero_area: passed (facing kept, backwards dropped, zero-area dropped)" );
		return 0;
	} catch ( const std::exception &error ) {
		std::fprintf( stderr, "FAIL: %s\n", error.what() );
		return 1;
	}
}
