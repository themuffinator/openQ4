#ifndef __INTERACTIONCULLBITS_H__
#define __INTERACTIONCULLBITS_H__

/*
===============================================================================

	openQ4: light-interaction cull bits in one pass over the vertices.

	R_CalcInteractionCullBits tests a surface against every light-frustum plane
	that crosses its bounds, one idSIMD_SSE2::Dot + CmpLT pass per plane, so an
	animated character inside a light's corner was read up to six times for
	every light, every frame. This kernel reads each vertex once and evaluates
	the same planes with the same arithmetic in the same order as those two
	SSE2 routines (four vertices transposed, ( ( x*a + y*b ) + z*c ) + d, the
	remainder in scalar), so every bit is identical. Only x64, where scalar
	float arithmetic is SSE as well; R_CalcInteractionCullBits uses it only
	while the SSE2 SIMD processor is the active one.

	Header-only and templated on the vertex and plane types so that
	tools/tests/native/InteractionCullBitsTest.cpp can compare it bit for bit
	with the per-plane passes.

===============================================================================
*/

#if defined( _M_X64 ) || defined( __x86_64__ )

#include <emmintrin.h>
#include <string.h>

#define OPENQ4_CULLBITS_ONE_PASS 1

// cullBits[v] gets bit i set where plane i (not in skipMask) has the vertex
// closer than epsilon; planes in skipMask contribute nothing, as when their
// per-plane pass is skipped.
template< class vertex_t, class plane_t >
static inline void R_CullBitsOnePass( unsigned char *cullBits, const plane_t *planes, const int skipMask,
		const vertex_t *verts, const int numVerts, const float epsilon ) {
	__m128 cx[6], cy[6], cz[6], cd[6];
	__m128i bits[6];
	int planeNums[6];
	int numPlanes = 0;

	for ( int i = 0; i < 6; i++ ) {
		if ( skipMask & ( 1 << i ) ) {
			continue;
		}
		cx[numPlanes] = _mm_set1_ps( planes[i][0] );
		cy[numPlanes] = _mm_set1_ps( planes[i][1] );
		cz[numPlanes] = _mm_set1_ps( planes[i][2] );
		cd[numPlanes] = _mm_set1_ps( planes[i][3] );
		bits[numPlanes] = _mm_set1_epi32( 1 << i );
		planeNums[numPlanes] = i;
		numPlanes++;
	}

	const __m128 eps = _mm_set1_ps( epsilon );
	int v = 0;
	for ( ; v + 4 <= numVerts; v += 4 ) {
		__m128 v0 = _mm_loadu_ps( verts[v+0].xyz.ToFloatPtr() );
		__m128 v1 = _mm_loadu_ps( verts[v+1].xyz.ToFloatPtr() );
		__m128 v2 = _mm_loadu_ps( verts[v+2].xyz.ToFloatPtr() );
		__m128 v3 = _mm_loadu_ps( verts[v+3].xyz.ToFloatPtr() );
		_MM_TRANSPOSE4_PS( v0, v1, v2, v3 );
		__m128i acc = _mm_setzero_si128();
		for ( int p = 0; p < numPlanes; p++ ) {
			const __m128 d = _mm_add_ps( _mm_add_ps( _mm_add_ps(
				_mm_mul_ps( cx[p], v0 ), _mm_mul_ps( cy[p], v1 ) ), _mm_mul_ps( cz[p], v2 ) ), cd[p] );
			acc = _mm_or_si128( acc, _mm_and_si128( _mm_castps_si128( _mm_cmplt_ps( d, eps ) ), bits[p] ) );
		}
		// each lane holds at most 63
		acc = _mm_packs_epi32( acc, acc );
		acc = _mm_packus_epi16( acc, acc );
		const int packed = _mm_cvtsi128_si32( acc );
		memcpy( cullBits + v, &packed, 4 );
	}
	for ( ; v < numVerts; v++ ) {
		unsigned char b = 0;
		for ( int p = 0; p < numPlanes; p++ ) {
			const plane_t &plane = planes[ planeNums[p] ];
			const float d = plane.Normal() * verts[v].xyz + plane[3];
			b |= (unsigned char)( ( d < epsilon ) << planeNums[p] );
		}
		cullBits[v] = b;
	}
}

#endif

#endif /* !__INTERACTIONCULLBITS_H__ */
