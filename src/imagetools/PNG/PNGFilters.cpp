// Copyright (C) 2026 DarkMatter Productions
//

#include "../../idlib/precompiled.h"
#include "PNGFilters.h"

#include <stdlib.h>
#include <string.h>

#if !defined( PNG_FILTERS_NO_SIMD ) && ( defined( __SSE2__ ) || defined( _M_X64 ) || ( defined( _M_IX86_FP ) && _M_IX86_FP >= 2 ) )
#define PNG_FILTERS_SSE2 1
#include <emmintrin.h>
#endif

/*
===============================================================================

	Unfiltering

	Sub, Average and Paeth depend on the reconstructed pixel to the left, so
	they run one pixel at a time; templating on the pixel size lets the
	compiler keep that pixel in registers. With SSE2 the 3- and 4-byte cases
	(8-bit RGB and RGBA, the common texture formats) predict a whole pixel
	per step in 16-bit lanes.

===============================================================================
*/

static inline uint8_t PaethPredict( int a, int b, int c ) {
	const int pa = abs( b - c );
	const int pb = abs( a - c );
	const int pc = abs( a + b - 2 * c );
	if ( pa <= pb && pa <= pc ) {
		return (uint8_t)a;
	}
	return (uint8_t)( pb <= pc ? b : c );
}

static void UnfilterUp( uint8_t *dst, const uint8_t *src, const uint8_t *prior, size_t n ) {
	for ( size_t i = 0; i < n; i++ ) {
		dst[i] = (uint8_t)( src[i] + prior[i] );
	}
}

template< int BPP >
static void UnfilterSub( uint8_t *dst, const uint8_t *src, size_t n ) {
	const size_t first = n < (size_t)BPP ? n : (size_t)BPP;
	for ( size_t i = 0; i < first; i++ ) {
		dst[i] = src[i];
	}
	for ( size_t i = BPP; i < n; i++ ) {
		dst[i] = (uint8_t)( src[i] + dst[i - BPP] );
	}
}

template< int BPP >
static void UnfilterAverage( uint8_t *dst, const uint8_t *src, const uint8_t *prior, size_t n ) {
	const size_t first = n < (size_t)BPP ? n : (size_t)BPP;
	for ( size_t i = 0; i < first; i++ ) {
		dst[i] = (uint8_t)( src[i] + ( prior[i] >> 1 ) );
	}
	for ( size_t i = BPP; i < n; i++ ) {
		dst[i] = (uint8_t)( src[i] + ( ( dst[i - BPP] + prior[i] ) >> 1 ) );
	}
}

template< int BPP >
static void UnfilterPaeth( uint8_t *dst, const uint8_t *src, const uint8_t *prior, size_t n ) {
	const size_t first = n < (size_t)BPP ? n : (size_t)BPP;
	for ( size_t i = 0; i < first; i++ ) {
		dst[i] = (uint8_t)( src[i] + prior[i] );
	}
	for ( size_t i = BPP; i < n; i++ ) {
		dst[i] = (uint8_t)( src[i] + PaethPredict( dst[i - BPP], prior[i], prior[i - BPP] ) );
	}
}

#if defined( PNG_FILTERS_SSE2 )

/*
Pixels load as 32-bit words (the last pixel of a 3-byte row loads exactly, so
the row is never overread) and store exactly BPP bytes. With 3-byte pixels the
loaded word's fourth lane is the next pixel's first byte; the lanes are
independent, so it only produces a lane nobody stores. Moving 3 bytes through
a 32-bit temporary, or storing 4 bytes in place, would make the next load miss
store forwarding on every pixel.
*/
template< bool WIDE, int BPP >
static inline __m128i LoadStep( const uint8_t *p ) {
	int32_t value = 0;
	memcpy( &value, p, WIDE ? 4 : BPP );
	return _mm_cvtsi32_si128( value );
}

template< bool WIDE, int BPP >
static inline void StoreStep( uint8_t *p, __m128i v ) {
	const int32_t value = _mm_cvtsi128_si32( v );
	memcpy( p, &value, BPP );
}

static inline bool StepFits( bool wide, size_t i, size_t n ) {
	return wide ? i + 4 <= n : i < n;
}

template< bool WIDE, int BPP >
static inline size_t SubSteps( uint8_t *dst, const uint8_t *src, size_t i, size_t n, __m128i &a ) {
	for ( ; StepFits( WIDE, i, n ); i += BPP ) {
		a = _mm_add_epi8( LoadStep<WIDE, BPP>( src + i ), a );
		StoreStep<WIDE, BPP>( dst + i, a );
	}
	return i;
}

template< int BPP >
static void UnfilterSubSSE2( uint8_t *dst, const uint8_t *src, size_t n ) {
	__m128i a = _mm_setzero_si128();
	const size_t i = SubSteps<true, BPP>( dst, src, 0, n, a );
	SubSteps<false, BPP>( dst, src, i, n, a );
}

template< bool WIDE, int BPP >
static inline size_t AverageSteps( uint8_t *dst, const uint8_t *src, const uint8_t *prior, size_t i, size_t n, __m128i &a ) {
	const __m128i ones = _mm_set1_epi8( 1 );
	for ( ; StepFits( WIDE, i, n ); i += BPP ) {
		const __m128i b = LoadStep<WIDE, BPP>( prior + i );
		// floor( ( a + b ) / 2 ): pavgb rounds up, so take back the odd bit
		const __m128i average = _mm_sub_epi8( _mm_avg_epu8( a, b ), _mm_and_si128( _mm_xor_si128( a, b ), ones ) );
		a = _mm_add_epi8( LoadStep<WIDE, BPP>( src + i ), average );
		StoreStep<WIDE, BPP>( dst + i, a );
	}
	return i;
}

template< int BPP >
static void UnfilterAverageSSE2( uint8_t *dst, const uint8_t *src, const uint8_t *prior, size_t n ) {
	__m128i a = _mm_setzero_si128();
	const size_t i = AverageSteps<true, BPP>( dst, src, prior, 0, n, a );
	AverageSteps<false, BPP>( dst, src, prior, i, n, a );
}

static inline __m128i Abs16( __m128i v ) {
	return _mm_max_epi16( v, _mm_sub_epi16( _mm_setzero_si128(), v ) );
}

static inline __m128i Select( __m128i mask, __m128i ifSet, __m128i ifClear ) {
	return _mm_or_si128( _mm_and_si128( mask, ifSet ), _mm_andnot_si128( mask, ifClear ) );
}

template< bool WIDE, int BPP >
static inline size_t PaethSteps( uint8_t *dst, const uint8_t *src, const uint8_t *prior, size_t i, size_t n, __m128i &a, __m128i &c ) {
	const __m128i zero = _mm_setzero_si128();
	const __m128i lowByte = _mm_set1_epi16( 0xFF );
	for ( ; StepFits( WIDE, i, n ); i += BPP ) {
		const __m128i b = _mm_unpacklo_epi8( LoadStep<WIDE, BPP>( prior + i ), zero );
		const __m128i x = _mm_unpacklo_epi8( LoadStep<WIDE, BPP>( src + i ), zero );
		// pa = |b - c|, pb = |a - c|, pc = |a + b - 2c|
		const __m128i bc = _mm_sub_epi16( b, c );
		const __m128i ac = _mm_sub_epi16( a, c );
		const __m128i pa = Abs16( bc );
		const __m128i pb = Abs16( ac );
		const __m128i pc = Abs16( _mm_add_epi16( bc, ac ) );
		const __m128i smallest = _mm_min_epi16( pc, _mm_min_epi16( pa, pb ) );
		// ties prefer a, then b, then c
		__m128i predicted = Select( _mm_cmpeq_epi16( pb, smallest ), b, c );
		predicted = Select( _mm_cmpeq_epi16( pa, smallest ), a, predicted );
		a = _mm_and_si128( _mm_add_epi16( x, predicted ), lowByte );
		StoreStep<WIDE, BPP>( dst + i, _mm_packus_epi16( a, a ) );
		c = b;
	}
	return i;
}

template< int BPP >
static void UnfilterPaethSSE2( uint8_t *dst, const uint8_t *src, const uint8_t *prior, size_t n ) {
	__m128i a = _mm_setzero_si128();		// left, reconstructed
	__m128i c = _mm_setzero_si128();		// upper left
	const size_t i = PaethSteps<true, BPP>( dst, src, prior, 0, n, a, c );
	PaethSteps<false, BPP>( dst, src, prior, i, n, a, c );
}

#endif

template< int BPP >
static bool UnfilterRowBPP( uint8_t filter, uint8_t *dst, const uint8_t *src, const uint8_t *prior, size_t n ) {
	switch ( filter ) {
	case PNG_FILTER_NONE:
		if ( dst != src ) {
			memcpy( dst, src, n );
		}
		return true;
	case PNG_FILTER_SUB:
#if defined( PNG_FILTERS_SSE2 )
		if ( BPP == 3 || BPP == 4 ) {
			UnfilterSubSSE2<BPP == 3 ? 3 : 4>( dst, src, n );
			return true;
		}
#endif
		UnfilterSub<BPP>( dst, src, n );
		return true;
	case PNG_FILTER_UP:
		UnfilterUp( dst, src, prior, n );
		return true;
	case PNG_FILTER_AVERAGE:
#if defined( PNG_FILTERS_SSE2 )
		if ( BPP == 3 || BPP == 4 ) {
			UnfilterAverageSSE2<BPP == 3 ? 3 : 4>( dst, src, prior, n );
			return true;
		}
#endif
		UnfilterAverage<BPP>( dst, src, prior, n );
		return true;
	case PNG_FILTER_PAETH:
#if defined( PNG_FILTERS_SSE2 )
		if ( BPP == 3 || BPP == 4 ) {
			UnfilterPaethSSE2<BPP == 3 ? 3 : 4>( dst, src, prior, n );
			return true;
		}
#endif
		UnfilterPaeth<BPP>( dst, src, prior, n );
		return true;
	}
	return false;
}

bool PNG_UnfilterRow( uint8_t filter, uint8_t *dst, const uint8_t *src, const uint8_t *prior, size_t rowBytes, uint32_t bytesPerPixel ) {
	switch ( bytesPerPixel ) {
	case 1:	return UnfilterRowBPP<1>( filter, dst, src, prior, rowBytes );
	case 2:	return UnfilterRowBPP<2>( filter, dst, src, prior, rowBytes );
	case 3:	return UnfilterRowBPP<3>( filter, dst, src, prior, rowBytes );
	case 4:	return UnfilterRowBPP<4>( filter, dst, src, prior, rowBytes );
	case 6:	return UnfilterRowBPP<6>( filter, dst, src, prior, rowBytes );
	case 8:	return UnfilterRowBPP<8>( filter, dst, src, prior, rowBytes );
	}
	return false;
}

/*
===============================================================================

	Filtering (encoder)

	Encoding has no serial dependency (every predictor reads the source rows),
	so with SSE2 each filter runs 16 bytes at a time; the filtered row's
	signed-magnitude sum comes from min( v, -v ) and psadbw.

===============================================================================
*/

static inline uint32_t SignedMagnitude( uint8_t value ) {
	return value < 128 ? value : 256u - value;
}

static uint64_t SumSignedMagnitudes( const uint8_t *p, size_t n ) {
	uint64_t sum = 0;
	size_t i = 0;
#if defined( PNG_FILTERS_SSE2 )
	const __m128i zero = _mm_setzero_si128();
	__m128i total = zero;
	for ( ; i + 16 <= n; i += 16 ) {
		const __m128i v = _mm_loadu_si128( (const __m128i *)( p + i ) );
		total = _mm_add_epi64( total, _mm_sad_epu8( _mm_min_epu8( v, _mm_sub_epi8( zero, v ) ), zero ) );
	}
	sum = (uint64_t)(uint32_t)_mm_cvtsi128_si32( total ) + (uint64_t)(uint32_t)_mm_cvtsi128_si32( _mm_srli_si128( total, 8 ) );
#endif
	for ( ; i < n; i++ ) {
		sum += SignedMagnitude( p[i] );
	}
	return sum;
}

#if defined( PNG_FILTERS_SSE2 )
static inline __m128i LoadBytes( const uint8_t *p ) {
	return _mm_loadu_si128( (const __m128i *)p );
}

// Paeth predictor for eight 16-bit lanes
static inline __m128i PaethLanes( __m128i a, __m128i b, __m128i c ) {
	const __m128i bc = _mm_sub_epi16( b, c );
	const __m128i ac = _mm_sub_epi16( a, c );
	const __m128i pa = Abs16( bc );
	const __m128i pb = Abs16( ac );
	const __m128i pc = Abs16( _mm_add_epi16( bc, ac ) );
	const __m128i smallest = _mm_min_epi16( pc, _mm_min_epi16( pa, pb ) );
	const __m128i predicted = Select( _mm_cmpeq_epi16( pb, smallest ), b, c );
	return Select( _mm_cmpeq_epi16( pa, smallest ), a, predicted );
}
#endif

void PNG_FilterRow( int filter, uint8_t *dst, const uint8_t *row, const uint8_t *prior, size_t rowBytes, uint32_t bytesPerPixel ) {
	const size_t bpp = bytesPerPixel;
	const size_t first = rowBytes < bpp ? rowBytes : bpp;
	size_t i;
	switch ( filter ) {
	case PNG_FILTER_SUB:
		for ( i = 0; i < first; i++ ) {
			dst[i] = row[i];
		}
#if defined( PNG_FILTERS_SSE2 )
		for ( ; i + 16 <= rowBytes; i += 16 ) {
			_mm_storeu_si128( (__m128i *)( dst + i ), _mm_sub_epi8( LoadBytes( row + i ), LoadBytes( row + i - bpp ) ) );
		}
#endif
		for ( ; i < rowBytes; i++ ) {
			dst[i] = (uint8_t)( row[i] - row[i - bpp] );
		}
		return;
	case PNG_FILTER_UP:
		i = 0;
#if defined( PNG_FILTERS_SSE2 )
		for ( ; i + 16 <= rowBytes; i += 16 ) {
			_mm_storeu_si128( (__m128i *)( dst + i ), _mm_sub_epi8( LoadBytes( row + i ), LoadBytes( prior + i ) ) );
		}
#endif
		for ( ; i < rowBytes; i++ ) {
			dst[i] = (uint8_t)( row[i] - prior[i] );
		}
		return;
	case PNG_FILTER_AVERAGE:
		for ( i = 0; i < first; i++ ) {
			dst[i] = (uint8_t)( row[i] - ( prior[i] >> 1 ) );
		}
#if defined( PNG_FILTERS_SSE2 )
		{
			const __m128i ones = _mm_set1_epi8( 1 );
			for ( ; i + 16 <= rowBytes; i += 16 ) {
				const __m128i a = LoadBytes( row + i - bpp );
				const __m128i b = LoadBytes( prior + i );
				const __m128i average = _mm_sub_epi8( _mm_avg_epu8( a, b ), _mm_and_si128( _mm_xor_si128( a, b ), ones ) );
				_mm_storeu_si128( (__m128i *)( dst + i ), _mm_sub_epi8( LoadBytes( row + i ), average ) );
			}
		}
#endif
		for ( ; i < rowBytes; i++ ) {
			dst[i] = (uint8_t)( row[i] - ( ( row[i - bpp] + prior[i] ) >> 1 ) );
		}
		return;
	case PNG_FILTER_PAETH:
		for ( i = 0; i < first; i++ ) {
			dst[i] = (uint8_t)( row[i] - prior[i] );
		}
#if defined( PNG_FILTERS_SSE2 )
		{
			const __m128i zero = _mm_setzero_si128();
			for ( ; i + 16 <= rowBytes; i += 16 ) {
				const __m128i a = LoadBytes( row + i - bpp );
				const __m128i b = LoadBytes( prior + i );
				const __m128i c = LoadBytes( prior + i - bpp );
				const __m128i low = PaethLanes( _mm_unpacklo_epi8( a, zero ), _mm_unpacklo_epi8( b, zero ), _mm_unpacklo_epi8( c, zero ) );
				const __m128i high = PaethLanes( _mm_unpackhi_epi8( a, zero ), _mm_unpackhi_epi8( b, zero ), _mm_unpackhi_epi8( c, zero ) );
				_mm_storeu_si128( (__m128i *)( dst + i ), _mm_sub_epi8( LoadBytes( row + i ), _mm_packus_epi16( low, high ) ) );
			}
		}
#endif
		for ( ; i < rowBytes; i++ ) {
			dst[i] = (uint8_t)( row[i] - PaethPredict( row[i - bpp], prior[i], prior[i - bpp] ) );
		}
		return;
	default:
		memcpy( dst, row, rowBytes );
		return;
	}
}

uint64_t PNG_FilterRowSum( const uint8_t *filtered, size_t rowBytes ) {
	return SumSignedMagnitudes( filtered, rowBytes );
}
