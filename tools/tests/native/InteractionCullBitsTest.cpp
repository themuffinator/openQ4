// Compares R_CullBitsOnePass (src/rendergeo/InteractionCullBits.h) bit for bit
// with the per-plane passes it replaces: idSIMD_SSE2::Dot( float *, idPlane,
// idDrawVert * ) followed by CmpLT( byte *, bitNum, ... ) for each plane not in
// the surface's front bits. interaction_cull_bits_test.py pastes the engine's
// own Dot and CmpLT code in at the three @...@ markers.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <emmintrin.h>
#include <vector>

#define RESTRICT
#define ID_INLINE inline
typedef unsigned char byte;

struct idVec3 {
	float x, y, z;
	const float *ToFloatPtr( void ) const { return &x; }
	float operator*( const idVec3 &a ) const { return x * a.x + y * a.y + z * a.z; }
};
struct idVec2 { float x, y; };
struct idPlane {
	float a, b, c, d;
	float operator[]( int i ) const { return ( &a )[i]; }
	const idVec3 &Normal( void ) const { return *reinterpret_cast<const idVec3 *>( &a ); }
};
// The engine's layout: xyz first, 64 bytes.
struct idDrawVert {
	idVec3 xyz;
	byte color[4];
	idVec3 normal;
	byte color2[4];
	idVec3 tangents[2];
	idVec2 st;
};
static_assert( sizeof( idDrawVert ) == 64, "idDrawVert layout" );

static void ReferenceDot( float * RESTRICT dst, const idPlane &constant, const idDrawVert * RESTRICT src, const int count ) {
/*@DOT@*/
}

/*@PACK@*/

static void ReferenceCmpLT( byte *dst, const byte bitNum, const float *src0, const float constant, const int count ) {
/*@CMPLT@*/
}

#include "../../../src/rendergeo/InteractionCullBits.h"

static uint32_t rng = 0x12345678u;
static float Rand( float lo, float hi ) {
	rng = rng * 1664525u + 1013904223u;
	return lo + ( hi - lo ) * ( ( rng >> 8 ) * ( 1.0f / 16777216.0f ) );
}

int main( void ) {
	// LIGHT_CLIP_EPSILON (0.1f) and a value that is not a short binary fraction
	const float epsilons[] = { 0.1f, 1.0f / 3.0f };
	long cases = 0, compared = 0;
	for ( int round = 0; round < 4000; round++ ) {
		const int numVerts = round % 53;
		std::vector<idDrawVert> verts( numVerts + 1 );
		idPlane planes[6];
		for ( int i = 0; i < 6; i++ ) {
			planes[i].a = Rand( -1, 1 ); planes[i].b = Rand( -1, 1 ); planes[i].c = Rand( -1, 1 );
			planes[i].d = Rand( -300, 300 );
		}
		for ( int v = 0; v < numVerts + 1; v++ ) {
			memset( &verts[v], 0x5a, sizeof( idDrawVert ) );	// garbage beyond xyz, as in the engine
			verts[v].xyz.x = Rand( -400, 400 ); verts[v].xyz.y = Rand( -400, 400 ); verts[v].xyz.z = Rand( -400, 400 );
			// put some vertices on a plane, within a rounding step of the epsilon
			if ( ( v % 5 ) == 0 && numVerts > 0 ) {
				const idPlane &p = planes[v % 6];
				const float n2 = p.a * p.a + p.b * p.b + p.c * p.c;
				const float s = ( epsilons[round & 1] - ( p.Normal() * verts[v].xyz + p.d ) ) / n2;
				verts[v].xyz.x += s * p.a; verts[v].xyz.y += s * p.b; verts[v].xyz.z += s * p.c;
			}
		}
		for ( int skip = 0; skip < 64; skip += ( round % 7 ) + 1 ) {
			const float eps = epsilons[round & 1];
			std::vector<byte> expected( numVerts + 16, 0 ), actual( numVerts + 16, 0xcc );
			std::vector<float> side( numVerts + 4 );
			for ( int i = 0; i < 6; i++ ) {
				if ( skip & ( 1 << i ) ) {
					continue;
				}
				ReferenceDot( side.data(), planes[i], verts.data(), numVerts );
				ReferenceCmpLT( expected.data(), (byte)i, side.data(), eps, numVerts );
			}
			R_CullBitsOnePass( actual.data(), planes, skip, verts.data(), numVerts, eps );
			cases++;
			for ( int v = 0; v < numVerts; v++ ) {
				compared++;
				if ( expected[v] != actual[v] ) {
					printf( "MISMATCH round=%d numVerts=%d skip=%d vert=%d expected=%d actual=%d\n",
						round, numVerts, skip, v, expected[v], actual[v] );
					return 1;
				}
			}
			if ( actual[numVerts] != 0xcc ) {
				printf( "OVERRUN round=%d numVerts=%d\n", round, numVerts );
				return 1;
			}
		}
	}
	printf( "interaction cull bits: %ld cases, %ld vertices identical\n", cases, compared );
	return 0;
}
