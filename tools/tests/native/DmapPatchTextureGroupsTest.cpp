// Compile the production dmap texture grouping (which optimize groups a curve,
// model or brush-side triangle list may join) against minimal geometry types.
// No renderer or proprietary map assets required.
//
// Built with LEGACY_GROUPING, the id fit and comparison replace production as a
// control: the road seam case must then be accepted, proving the fixture
// reproduces issue #209's welded seam.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

struct idMath {
	static float Fabs( float value ) { return std::fabs( value ); }
};
struct idVec3 {
	float x = 0, y = 0, z = 0;
	idVec3() = default;
	idVec3( float x_, float y_, float z_ ) : x( x_ ), y( y_ ), z( z_ ) {}
	float &operator[]( int i ) { return ( &x )[i]; }
	const float &operator[]( int i ) const { return ( &x )[i]; }
};
struct idVec4 {
	float x = 0, y = 0, z = 0, w = 0;
	float &operator[]( int i ) { return ( &x )[i]; }
	const float &operator[]( int i ) const { return ( &x )[i]; }
};
struct idVec2 {
	float x = 0, y = 0;
	float &operator[]( int i ) { return ( &x )[i]; }
	const float &operator[]( int i ) const { return ( &x )[i]; }
};
struct idDrawVert {
	idVec3 xyz;
	idVec2 st;
};
struct mapTri_t {
	mapTri_t *next = nullptr;
	idDrawVert v[3];
};
struct textureVectors_t {
	idVec4 v[2];
};
struct optimizeGroup_t {
	textureVectors_t texVec;
	int texVecSource = 0;
};

// @CONSTANTS@

// @TEXVECSMATCH@

#ifdef LEGACY_GROUPING
// id's fit: normalized directions, and the t offset taken along the s direction
static int TexVecForTri( textureVectors_t *texVec, const mapTri_t *tri ) {
	const idDrawVert *a = &tri->v[0], *b = &tri->v[1], *c = &tri->v[2];
	float d0[5] = { b->xyz[0] - a->xyz[0], b->xyz[1] - a->xyz[1], b->xyz[2] - a->xyz[2], b->st[0] - a->st[0], b->st[1] - a->st[1] };
	float d1[5] = { c->xyz[0] - a->xyz[0], c->xyz[1] - a->xyz[1], c->xyz[2] - a->xyz[2], c->st[0] - a->st[0], c->st[1] - a->st[1] };
	const float inva = 1.0f / ( d0[3] * d1[4] - d0[4] * d1[3] );
	float s[3], t[3];
	for ( int j = 0 ; j < 3 ; j++ ) {
		s[j] = ( d0[j] * d1[4] - d0[4] * d1[j] ) * inva;
		t[j] = ( d0[3] * d1[j] - d0[j] * d1[3] ) * inva;
	}
	const float sl = std::sqrt( s[0] * s[0] + s[1] * s[1] + s[2] * s[2] );
	const float tl = std::sqrt( t[0] * t[0] + t[1] * t[1] + t[2] * t[2] );
	for ( int j = 0 ; j < 3 ; j++ ) {
		texVec->v[0][j] = s[j] / sl;
		texVec->v[1][j] = t[j] / tl;
	}
	const float dot = a->xyz[0] * texVec->v[0][0] + a->xyz[1] * texVec->v[0][1] + a->xyz[2] * texVec->v[0][2];
	texVec->v[0][3] = dot - a->st[0];
	texVec->v[1][3] = dot - a->st[1];
	return TEXVEC_FITTED;
}
static bool GroupAcceptsTris( const optimizeGroup_t *group, const mapTri_t *, const textureVectors_t *texVec, int ) {
	return TexVecsMatch( texVec, &group->texVec );
}
#else
// @PRODUCTION@
#endif

static int failures = 0;

static void Check( bool condition, const char *what ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAIL: %s\n", what );
		failures++;
	}
}

static mapTri_t Tri( float x0, float y0, float z0, float s0, float t0,
					 float x1, float y1, float z1, float s1, float t1,
					 float x2, float y2, float z2, float s2, float t2 ) {
	mapTri_t tri;
	tri.v[0].xyz = idVec3( x0, y0, z0 ); tri.v[0].st[0] = s0; tri.v[0].st[1] = t0;
	tri.v[1].xyz = idVec3( x1, y1, z1 ); tri.v[1].st[0] = s1; tri.v[1].st[1] = t1;
	tri.v[2].xyz = idVec3( x2, y2, z2 ); tri.v[2].st[0] = s2; tri.v[2].st[1] = t2;
	return tri;
}

static optimizeGroup_t GroupFor( const mapTri_t &tri ) {
	optimizeGroup_t group;
	group.texVecSource = TexVecForTri( &group.texVec, &tri );
	return group;
}

static bool Joins( const optimizeGroup_t &group, const mapTri_t &tri ) {
	textureVectors_t texVec;
	const int source = TexVecForTri( &texVec, &tri );
	return GroupAcceptsTris( &group, &tri, &texVec, source );
}

// walker's ramp road: consecutive flat patches on one plane, each running t 0
// to 1, so the shared edge at y = 4096 is t = 1 on one patch and 0 on the next
static void RoadSeam() {
	const mapTri_t upper = Tri( -448, 4352, -1952, 0.0f, 1.0f, -512, 4352, -1952, 0.25f, 1.0f, -512, 4096, -2080, 0.25f, 0.0f );
	const mapTri_t lower = Tri( -448, 4096, -2080, 0.0f, 1.0f, -512, 4096, -2080, 0.25f, 1.0f, -512, 3840, -2208, 0.25f, 0.0f );
	const optimizeGroup_t group = GroupFor( upper );
	Check( !Joins( group, lower ), "seam: the next patch, a whole repeat apart, must not join the group" );
#ifndef LEGACY_GROUPING
	const mapTri_t upperOther = Tri( -448, 4096, -2080, 0.0f, 0.0f, -448, 4352, -1952, 0.0f, 1.0f, -512, 4096, -2080, 0.25f, 0.0f );
	Check( group.texVecSource == TEXVEC_FITTED, "a curve triangle has a fitted mapping" );
	Check( Joins( group, upperOther ), "the rest of the same patch joins the group" );
#endif
}

#ifndef LEGACY_GROUPING
static void BrushSides() {
	// brush sides keep id's comparison: the vectors and offsets, nothing else
	optimizeGroup_t group;
	group.texVecSource = TEXVEC_SIDE;
	group.texVec.v[0] = { 1.0f / 256, 0, 0, 0.25f };
	group.texVec.v[1] = { 0, 1.0f / 256, 0, 0.5f };
	const mapTri_t distant = Tri( 9000, 9000, 0, 0, 0, 9100, 9000, 0, 0, 0, 9000, 9100, 0, 0, 0 );
	textureVectors_t side = group.texVec;
	Check( GroupAcceptsTris( &group, &distant, &side, TEXVEC_SIDE ), "identical side projections share a group" );
	side.v[1][3] += 0.01f;
	Check( !GroupAcceptsTris( &group, &distant, &side, TEXVEC_SIDE ), "side offsets 0.01 apart do not" );

	// curves never share a group with brush sides, even on the same projection
	auto onSide = []( float x, float y ) {
		return Tri( x, y, 0, x / 256 + 0.25f, y / 256 + 0.5f,
					x + 64, y, 0, ( x + 64 ) / 256 + 0.25f, y / 256 + 0.5f,
					x, y + 64, 0, x / 256 + 0.25f, ( y + 64 ) / 256 + 0.5f );
	};
	Check( !Joins( group, onSide( 128, 64 ) ), "a curve triangle never joins a brush side's group" );
	const optimizeGroup_t curve = GroupFor( onSide( 128, 64 ) );
	const mapTri_t sideTri = onSide( 512, 512 );
	textureVectors_t projection = group.texVec;
	Check( !GroupAcceptsTris( &curve, &sideTri, &projection, TEXVEC_SIDE ), "a brush side never joins a curve group" );
	Check( Joins( curve, onSide( 512, 512 ) ), "a curve triangle on the same mapping joins the curve group" );
}

static void Degenerate() {
	const mapTri_t flat = Tri( 0, 0, 0, 0, 0, 64, 0, 0, 1, 0, 128, 0, 0, 2, 0 );
	textureVectors_t texVec;
	Check( TexVecForTri( &texVec, &flat ) == TEXVEC_NONE, "a zero-area triangle has no fit" );
	const optimizeGroup_t none = GroupFor( flat );
	const mapTri_t normal = Tri( 0, 0, 0, 0, 0, 64, 0, 0, 1, 0, 0, 64, 0, 0, 1 );
	Check( !Joins( none, normal ), "nothing joins a group without a fit" );
	Check( !Joins( GroupFor( normal ), flat ), "a triangle without a fit joins nothing" );

	// coordinates that do not vary still fit, and only the same constant joins
	const mapTri_t constant = Tri( 0, 0, 0, 0.5f, 0.5f, 64, 0, 0, 0.5f, 0.5f, 0, 64, 0, 0.5f, 0.5f );
	Check( TexVecForTri( &texVec, &constant ) == TEXVEC_FITTED, "constant coordinates fit" );
	const optimizeGroup_t group = GroupFor( constant );
	Check( Joins( group, Tri( 64, 64, 0, 0.5f, 0.5f, 64, 0, 0, 0.5f, 0.5f, 0, 64, 0, 0.5f, 0.5f ) ), "the same constant joins" );
	Check( !Joins( group, Tri( 64, 64, 0, 1.5f, 0.5f, 64, 0, 0, 1.5f, 0.5f, 0, 64, 0, 1.5f, 0.5f ) ), "a constant a repeat off does not" );
}

// random planes and mappings: fragments of one mapping always share a group,
// even small ones far from the origin like a 10x10 subdivided patch; the same
// mapping shifted by a whole repeat never does
static void Random() {
	std::mt19937 rng( 209 );
	std::uniform_real_distribution<float> unit( -1.0f, 1.0f );
	int accepted = 0, rejected = 0;
	for ( int trial = 0 ; trial < 4000 ; trial++ ) {
		double n[3] = { unit( rng ), unit( rng ), unit( rng ) };
		double len = std::sqrt( n[0] * n[0] + n[1] * n[1] + n[2] * n[2] );
		if ( len < 0.1 ) {
			continue;
		}
		for ( double &c : n ) {
			c /= len;
		}
		// two in-plane axes
		double u[3] = { 1, 0, 0 };
		if ( std::fabs( n[0] ) > 0.9 ) {
			u[0] = 0; u[1] = 1;
		}
		const double un = u[0] * n[0] + u[1] * n[1] + u[2] * n[2];
		for ( int j = 0 ; j < 3 ; j++ ) {
			u[j] -= un * n[j];
		}
		len = std::sqrt( u[0] * u[0] + u[1] * u[1] + u[2] * u[2] );
		for ( double &c : u ) {
			c /= len;
		}
		const double w[3] = { n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0] };
		const double origin[3] = { 8000.0 * unit( rng ), 8000.0 * unit( rng ), 2000.0 * unit( rng ) };
		const double scale = trial % 2 ? 1.0 / 128 : 1.0 / 512;
		const double map[2][2] = { { scale * unit( rng ), scale * unit( rng ) }, { scale * unit( rng ), scale * unit( rng ) } };
		const double offset[2] = { unit( rng ), unit( rng ) };
		if ( std::fabs( map[0][0] * map[1][1] - map[0][1] * map[1][0] ) < scale * scale * 0.05 ) {
			continue;
		}
		const double size = trial % 3 == 0 ? 2.0 : 64.0;
		auto tri = [&]( double a, double b, double shift ) {
			mapTri_t t;
			const double corners[3][2] = { { a, b }, { a + size, b }, { a, b + size } };
			for ( int k = 0 ; k < 3 ; k++ ) {
				const double pu = corners[k][0], pw = corners[k][1];
				for ( int j = 0 ; j < 3 ; j++ ) {
					t.v[k].xyz[j] = float( origin[j] + pu * u[j] + pw * w[j] );
				}
				// coordinates from the float positions, as a patch's would be
				double fu = 0, fw = 0;
				for ( int j = 0 ; j < 3 ; j++ ) {
					fu += ( t.v[k].xyz[j] - origin[j] ) * u[j];
					fw += ( t.v[k].xyz[j] - origin[j] ) * w[j];
				}
				for ( int i = 0 ; i < 2 ; i++ ) {
					t.v[k].st[i] = float( map[i][0] * fu + map[i][1] * fw + offset[i] + ( i == 1 ? shift : 0.0 ) );
				}
			}
			return t;
		};
		const optimizeGroup_t group = GroupFor( tri( 0, 0, 0 ) );
		const double reach = 40 * size;
		const bool same = Joins( group, tri( reach * unit( rng ), reach * unit( rng ), 0 ) );
		const bool shifted = Joins( group, tri( reach * unit( rng ), reach * unit( rng ), 1 ) );
		if ( !same ) {
			std::fprintf( stderr, "trial %d: size %g scale %g rejected a fragment of the same mapping\n", trial, size, scale );
		}
		Check( same, "random: a fragment of the same mapping joins" );
		Check( !shifted, "random: the mapping a repeat off does not join" );
		accepted += same;
		rejected += !shifted;
	}
	std::printf( "dmap_patch_texture_groups: %d fragments joined, %d shifted fragments refused\n", accepted, rejected );
}
#endif

int main() {
	RoadSeam();
#ifndef LEGACY_GROUPING
	BrushSides();
	Degenerate();
	Random();
#endif
	if ( failures ) {
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "dmap_patch_texture_groups: road seam, brush sides, degenerate fits and random mappings passed\n" );
	return 0;
}
