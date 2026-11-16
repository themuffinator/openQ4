/*
===========================================================================

openQ4 cryptographic hash primitives
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This is an original implementation of the published FIPS 180-4 SHA-256,
RFC 2104 HMAC, and RFC 8018 PBKDF2 specifications. No source code from the
Quake 4 SDK game module or another cryptographic implementation is used here.

===========================================================================
*/

#include "CryptoHash.h"

#include <cstring>

#if defined( _M_X64 ) || defined( __x86_64__ )
#define IDCRYPTO_SHA_X86 1
#include <immintrin.h>
#if defined( _MSC_VER ) && !defined( __clang__ )
#include <intrin.h>
#define IDCRYPTO_SHA_X86_TARGET
#else
#include <cpuid.h>
#define IDCRYPTO_SHA_X86_TARGET __attribute__(( target( "sha,sse4.1,ssse3" ) ))
#endif
#else
#define IDCRYPTO_SHA_X86 0
#endif

// AArch64 uses the ARMv8 SHA-256 instructions only where the compilation
// baseline guarantees them (every Apple Silicon target does). A generic
// armv8-a build (Linux, Android, Windows) keeps the portable compression.
#if defined( __aarch64__ ) && ( defined( __ARM_FEATURE_SHA2 ) || defined( __ARM_FEATURE_CRYPTO ) )
#define IDCRYPTO_SHA_ARM64 1
#include <arm_neon.h>
#else
#define IDCRYPTO_SHA_ARM64 0
#endif

namespace idCrypto {
namespace {

typedef void ( *sha256TransformBlocks_t )( std::uint32_t state[ 8 ],
	const std::uint8_t *blocks, std::size_t blockCount );

struct sha256Context_t {
	std::uint32_t state[ 8 ];
	std::uint64_t totalBytes;
	std::uint8_t buffer[ SHA256_BLOCK_BYTES ];
	std::size_t bufferedBytes;
	sha256TransformBlocks_t transform;
};

static constexpr std::uint32_t SHA256_ROUND_CONSTANTS[ 64 ] = {
	0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
	0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
	0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
	0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
	0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
	0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
	0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
	0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
	0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
	0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
	0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
	0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
	0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
	0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
	0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
	0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static inline std::uint32_t RotateRight( std::uint32_t value, unsigned int count ) {
	return ( value >> count ) | ( value << ( 32u - count ) );
}

static inline std::uint32_t ReadBigEndian32( const std::uint8_t *bytes ) {
	return ( static_cast<std::uint32_t>( bytes[ 0 ] ) << 24 ) |
		( static_cast<std::uint32_t>( bytes[ 1 ] ) << 16 ) |
		( static_cast<std::uint32_t>( bytes[ 2 ] ) << 8 ) |
		static_cast<std::uint32_t>( bytes[ 3 ] );
}

static inline void WriteBigEndian32( std::uint8_t *bytes, std::uint32_t value ) {
	bytes[ 0 ] = static_cast<std::uint8_t>( value >> 24 );
	bytes[ 1 ] = static_cast<std::uint8_t>( value >> 16 );
	bytes[ 2 ] = static_cast<std::uint8_t>( value >> 8 );
	bytes[ 3 ] = static_cast<std::uint8_t>( value );
}

// Portable FIPS 180-4 compression of whole blocks. The schedule is scrubbed
// once per run rather than once per block: callers hash long buffers (images,
// generated caches), where a per-block scrub cost about a fifth of the time.
static void SHA256TransformBlocksPortable( std::uint32_t state[ 8 ],
		const std::uint8_t *blocks, std::size_t blockCount ) {
	std::uint32_t schedule[ 64 ];
	for ( ; blockCount != 0; --blockCount, blocks += SHA256_BLOCK_BYTES ) {
		const std::uint8_t *block = blocks;
		for ( int index = 0; index < 16; ++index ) {
			schedule[ index ] = ReadBigEndian32( block + index * 4 );
		}
		for ( int index = 16; index < 64; ++index ) {
			const std::uint32_t sigma0 = RotateRight( schedule[ index - 15 ], 7 ) ^
				RotateRight( schedule[ index - 15 ], 18 ) ^ ( schedule[ index - 15 ] >> 3 );
			const std::uint32_t sigma1 = RotateRight( schedule[ index - 2 ], 17 ) ^
				RotateRight( schedule[ index - 2 ], 19 ) ^ ( schedule[ index - 2 ] >> 10 );
			schedule[ index ] = schedule[ index - 16 ] + sigma0 +
				schedule[ index - 7 ] + sigma1;
		}

		std::uint32_t a = state[ 0 ];
		std::uint32_t b = state[ 1 ];
		std::uint32_t c = state[ 2 ];
		std::uint32_t d = state[ 3 ];
		std::uint32_t e = state[ 4 ];
		std::uint32_t f = state[ 5 ];
		std::uint32_t g = state[ 6 ];
		std::uint32_t h = state[ 7 ];

		for ( int index = 0; index < 64; ++index ) {
			const std::uint32_t sum1 = RotateRight( e, 6 ) ^ RotateRight( e, 11 ) ^ RotateRight( e, 25 );
			const std::uint32_t choose = ( e & f ) ^ ( ( ~e ) & g );
			const std::uint32_t temp1 = h + sum1 + choose +
				SHA256_ROUND_CONSTANTS[ index ] + schedule[ index ];
			const std::uint32_t sum0 = RotateRight( a, 2 ) ^ RotateRight( a, 13 ) ^ RotateRight( a, 22 );
			const std::uint32_t majority = ( a & b ) ^ ( a & c ) ^ ( b & c );
			const std::uint32_t temp2 = sum0 + majority;

			h = g;
			g = f;
			f = e;
			e = d + temp1;
			d = c;
			c = b;
			b = a;
			a = temp1 + temp2;
		}

		state[ 0 ] += a;
		state[ 1 ] += b;
		state[ 2 ] += c;
		state[ 3 ] += d;
		state[ 4 ] += e;
		state[ 5 ] += f;
		state[ 6 ] += g;
		state[ 7 ] += h;
	}
	idCrypto::SecureZero( schedule, sizeof( schedule ) );
}

#if IDCRYPTO_SHA_X86
/*
The x86 SHA extensions (Intel Goldmont/Ice Lake and later, every AMD Zen) run
two rounds per SHA256RNDS2 and derive four schedule words per
SHA256MSG1/SHA256MSG2 pair, as specified in the Intel 64 and IA-32
Architectures Software Developer's Manual. The state travels as the two
register halves the round instruction expects: ABEF and CDGH, each with its
first word in the most significant lane.
*/
IDCRYPTO_SHA_X86_TARGET
static void SHA256TransformBlocksX86( std::uint32_t state[ 8 ],
		const std::uint8_t *blocks, std::size_t blockCount ) {
	// big-endian message words, one 32-bit lane each
	const __m128i byteSwap = _mm_set_epi64x( 0x0c0d0e0f08090a0bLL, 0x0405060700010203LL );

	__m128i dcba = _mm_loadu_si128( reinterpret_cast<const __m128i *>( state ) );
	__m128i hgfe = _mm_loadu_si128( reinterpret_cast<const __m128i *>( state + 4 ) );
	const __m128i cdab = _mm_shuffle_epi32( dcba, 0xB1 );
	const __m128i efgh = _mm_shuffle_epi32( hgfe, 0x1B );
	__m128i abef = _mm_alignr_epi8( cdab, efgh, 8 );
	__m128i cdgh = _mm_blend_epi16( efgh, cdab, 0xF0 );

	for ( ; blockCount != 0; --blockCount, blocks += SHA256_BLOCK_BYTES ) {
		const __m128i abefSaved = abef;
		const __m128i cdghSaved = cdgh;
		// words[ group & 3 ] holds schedule words 4 * group .. 4 * group + 3
		__m128i words[ 4 ];
		for ( int group = 0; group < 16; ++group ) {
			__m128i next;
			if ( group < 4 ) {
				next = _mm_shuffle_epi8( _mm_loadu_si128(
					reinterpret_cast<const __m128i *>( blocks + group * 16 ) ), byteSwap );
			} else {
				// W[t] = sigma1(W[t-2]) + W[t-7] + sigma0(W[t-15]) + W[t-16]
				const __m128i oldest = words[ group & 3 ];			// W[t-16..t-13]
				const __m128i older = words[ ( group + 1 ) & 3 ];	// W[t-12..t-9]
				const __m128i newer = words[ ( group + 2 ) & 3 ];	// W[t-8..t-5]
				const __m128i newest = words[ ( group + 3 ) & 3 ];	// W[t-4..t-1]
				__m128i partial = _mm_sha256msg1_epu32( oldest, older );
				partial = _mm_add_epi32( partial, _mm_alignr_epi8( newest, newer, 4 ) );
				next = _mm_sha256msg2_epu32( partial, newest );
			}
			words[ group & 3 ] = next;
			__m128i roundInput = _mm_add_epi32( next, _mm_loadu_si128(
				reinterpret_cast<const __m128i *>( SHA256_ROUND_CONSTANTS + group * 4 ) ) );
			cdgh = _mm_sha256rnds2_epu32( cdgh, abef, roundInput );
			roundInput = _mm_shuffle_epi32( roundInput, 0x0E );
			abef = _mm_sha256rnds2_epu32( abef, cdgh, roundInput );
		}
		abef = _mm_add_epi32( abef, abefSaved );
		cdgh = _mm_add_epi32( cdgh, cdghSaved );
	}

	const __m128i feba = _mm_shuffle_epi32( abef, 0x1B );
	const __m128i dchg = _mm_shuffle_epi32( cdgh, 0xB1 );
	dcba = _mm_blend_epi16( feba, dchg, 0xF0 );
	hgfe = _mm_alignr_epi8( dchg, feba, 8 );
	_mm_storeu_si128( reinterpret_cast<__m128i *>( state ), dcba );
	_mm_storeu_si128( reinterpret_cast<__m128i *>( state + 4 ), hgfe );
}

static bool SHA256DetectX86( void ) {
#if defined( _MSC_VER ) && !defined( __clang__ )
	int registers[ 4 ] = {};
	__cpuid( registers, 0 );
	if ( registers[ 0 ] < 7 ) {
		return false;
	}
	__cpuid( registers, 1 );
	const bool ssse3 = ( registers[ 2 ] & ( 1 << 9 ) ) != 0;
	const bool sse41 = ( registers[ 2 ] & ( 1 << 19 ) ) != 0;
	__cpuidex( registers, 7, 0 );
	const bool sha = ( registers[ 1 ] & ( 1 << 29 ) ) != 0;
#else
	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
	if ( __get_cpuid_max( 0, nullptr ) < 7 ) {
		return false;
	}
	__cpuid( 1, eax, ebx, ecx, edx );
	const bool ssse3 = ( ecx & ( 1u << 9 ) ) != 0;
	const bool sse41 = ( ecx & ( 1u << 19 ) ) != 0;
	__cpuid_count( 7, 0, eax, ebx, ecx, edx );
	const bool sha = ( ebx & ( 1u << 29 ) ) != 0;
#endif
	return ssse3 && sse41 && sha;
}
#endif

#if IDCRYPTO_SHA_ARM64
// SHA256H/SHA256H2 run four rounds on the ABCD/EFGH halves of the state, and
// SHA256SU0/SHA256SU1 derive four schedule words (Arm Architecture Reference
// Manual, Armv8 Cryptographic Extension).
static void SHA256TransformBlocksArm64( std::uint32_t state[ 8 ],
		const std::uint8_t *blocks, std::size_t blockCount ) {
	uint32x4_t abcd = vld1q_u32( state );
	uint32x4_t efgh = vld1q_u32( state + 4 );
	for ( ; blockCount != 0; --blockCount, blocks += SHA256_BLOCK_BYTES ) {
		const uint32x4_t abcdSaved = abcd;
		const uint32x4_t efghSaved = efgh;
		// words[ group & 3 ] holds schedule words 4 * group .. 4 * group + 3
		uint32x4_t words[ 4 ];
		for ( int group = 0; group < 16; ++group ) {
			uint32x4_t next;
			if ( group < 4 ) {
				next = vreinterpretq_u32_u8( vrev32q_u8( vld1q_u8( blocks + group * 16 ) ) );
			} else {
				next = vsha256su1q_u32(
					vsha256su0q_u32( words[ group & 3 ], words[ ( group + 1 ) & 3 ] ),
					words[ ( group + 2 ) & 3 ], words[ ( group + 3 ) & 3 ] );
			}
			words[ group & 3 ] = next;
			const uint32x4_t roundInput = vaddq_u32( next, vld1q_u32( SHA256_ROUND_CONSTANTS + group * 4 ) );
			const uint32x4_t abcdBefore = abcd;
			abcd = vsha256hq_u32( abcd, efgh, roundInput );
			efgh = vsha256h2q_u32( efgh, abcdBefore, roundInput );
		}
		abcd = vaddq_u32( abcd, abcdSaved );
		efgh = vaddq_u32( efgh, efghSaved );
	}
	vst1q_u32( state, abcd );
	vst1q_u32( state + 4, efgh );
}
#endif

static bool SHA256Accelerated( void ) {
#if IDCRYPTO_SHA_X86
	static const bool available = SHA256DetectX86();
	return available;
#elif IDCRYPTO_SHA_ARM64
	return true;
#else
	return false;
#endif
}

static void SHA256TransformBlocks( std::uint32_t state[ 8 ],
		const std::uint8_t *blocks, std::size_t blockCount ) {
#if IDCRYPTO_SHA_X86
	if ( SHA256Accelerated() ) {
		SHA256TransformBlocksX86( state, blocks, blockCount );
		return;
	}
#elif IDCRYPTO_SHA_ARM64
	SHA256TransformBlocksArm64( state, blocks, blockCount );
	return;
#endif
	SHA256TransformBlocksPortable( state, blocks, blockCount );
}

static void SHA256Init( sha256Context_t &context,
		sha256TransformBlocks_t transform = SHA256TransformBlocks ) {
	context.transform = transform;
	context.state[ 0 ] = 0x6a09e667u;
	context.state[ 1 ] = 0xbb67ae85u;
	context.state[ 2 ] = 0x3c6ef372u;
	context.state[ 3 ] = 0xa54ff53au;
	context.state[ 4 ] = 0x510e527fu;
	context.state[ 5 ] = 0x9b05688cu;
	context.state[ 6 ] = 0x1f83d9abu;
	context.state[ 7 ] = 0x5be0cd19u;
	context.totalBytes = 0;
	context.bufferedBytes = 0;
	std::memset( context.buffer, 0, sizeof( context.buffer ) );
}

static void SHA256Update( sha256Context_t &context, const void *data, std::size_t dataBytes ) {
	const std::uint8_t *cursor = static_cast<const std::uint8_t *>( data );
	context.totalBytes += static_cast<std::uint64_t>( dataBytes );

	if ( context.bufferedBytes != 0 ) {
		const std::size_t wanted = SHA256_BLOCK_BYTES - context.bufferedBytes;
		const std::size_t copied = dataBytes < wanted ? dataBytes : wanted;
		if ( copied != 0 ) {
			std::memcpy( context.buffer + context.bufferedBytes, cursor, copied );
			context.bufferedBytes += copied;
			cursor += copied;
			dataBytes -= copied;
		}
		if ( context.bufferedBytes == SHA256_BLOCK_BYTES ) {
			context.transform( context.state, context.buffer, 1 );
			context.bufferedBytes = 0;
		}
	}

	const std::size_t wholeBlocks = dataBytes / SHA256_BLOCK_BYTES;
	if ( wholeBlocks != 0 ) {
		context.transform( context.state, cursor, wholeBlocks );
		cursor += wholeBlocks * SHA256_BLOCK_BYTES;
		dataBytes -= wholeBlocks * SHA256_BLOCK_BYTES;
	}
	if ( dataBytes != 0 ) {
		std::memcpy( context.buffer, cursor, dataBytes );
		context.bufferedBytes = dataBytes;
	}
}

static void SHA256Final( sha256Context_t &context,
		std::uint8_t digest[ SHA256_DIGEST_BYTES ] ) {
	const std::uint64_t totalBits = context.totalBytes * 8u;
	context.buffer[ context.bufferedBytes++ ] = 0x80u;
	if ( context.bufferedBytes > 56 ) {
		std::memset( context.buffer + context.bufferedBytes, 0,
			SHA256_BLOCK_BYTES - context.bufferedBytes );
		context.transform( context.state, context.buffer, 1 );
		context.bufferedBytes = 0;
	}
	std::memset( context.buffer + context.bufferedBytes, 0, 56 - context.bufferedBytes );
	for ( int index = 0; index < 8; ++index ) {
		context.buffer[ 56 + index ] = static_cast<std::uint8_t>( totalBits >> ( 56 - index * 8 ) );
	}
	context.transform( context.state, context.buffer, 1 );
	for ( int index = 0; index < 8; ++index ) {
		WriteBigEndian32( digest + index * 4, context.state[ index ] );
	}
	SecureZero( &context, sizeof( context ) );
}

struct hmacSHA256Prepared_t {
	sha256Context_t inner;
	sha256Context_t outer;
};

static void HMACPrepare( const void *key, std::size_t keyBytes,
		hmacSHA256Prepared_t &prepared ) {
	std::uint8_t normalizedKey[ SHA256_BLOCK_BYTES ] = {};
	if ( keyBytes > SHA256_BLOCK_BYTES ) {
		SHA256( key, keyBytes, normalizedKey );
	} else if ( keyBytes != 0 ) {
		std::memcpy( normalizedKey, key, keyBytes );
	}

	std::uint8_t innerPad[ SHA256_BLOCK_BYTES ];
	std::uint8_t outerPad[ SHA256_BLOCK_BYTES ];
	for ( std::size_t index = 0; index < SHA256_BLOCK_BYTES; ++index ) {
		innerPad[ index ] = normalizedKey[ index ] ^ 0x36u;
		outerPad[ index ] = normalizedKey[ index ] ^ 0x5cu;
	}
	SHA256Init( prepared.inner );
	SHA256Update( prepared.inner, innerPad, sizeof( innerPad ) );
	SHA256Init( prepared.outer );
	SHA256Update( prepared.outer, outerPad, sizeof( outerPad ) );
	SecureZero( normalizedKey, sizeof( normalizedKey ) );
	SecureZero( innerPad, sizeof( innerPad ) );
	SecureZero( outerPad, sizeof( outerPad ) );
}

static void HMACFinish( const hmacSHA256Prepared_t &prepared,
		const void *data, std::size_t dataBytes,
		std::uint8_t digest[ SHA256_DIGEST_BYTES ] ) {
	sha256Context_t inner = prepared.inner;
	sha256Context_t outer = prepared.outer;
	std::uint8_t innerDigest[ SHA256_DIGEST_BYTES ];
	SHA256Update( inner, data, dataBytes );
	SHA256Final( inner, innerDigest );
	SHA256Update( outer, innerDigest, sizeof( innerDigest ) );
	SHA256Final( outer, digest );
	SecureZero( innerDigest, sizeof( innerDigest ) );
}

} // namespace

void SHA256( const void *data, std::size_t dataBytes,
		std::uint8_t digest[ SHA256_DIGEST_BYTES ] ) {
	sha256Context_t context;
	SHA256Init( context );
	if ( dataBytes != 0 ) {
		SHA256Update( context, data, dataBytes );
	}
	SHA256Final( context, digest );
}

static_assert( sizeof( sha256Context_t ) <= 128, "SHA256Stream storage is too small" );
static_assert( alignof( sha256Context_t ) <= 8, "SHA256Stream storage is under-aligned" );

SHA256Stream::SHA256Stream( void ) {
	SHA256Init( *reinterpret_cast<sha256Context_t *>( context ) );
}

SHA256Stream::~SHA256Stream( void ) {
	SecureZero( context, sizeof( context ) );
}

void SHA256Stream::Update( const void *data, std::size_t dataBytes ) {
	if ( dataBytes != 0 ) {
		SHA256Update( *reinterpret_cast<sha256Context_t *>( context ), data, dataBytes );
	}
}

void SHA256Stream::Final( std::uint8_t digest[ SHA256_DIGEST_BYTES ] ) {
	sha256Context_t &state = *reinterpret_cast<sha256Context_t *>( context );
	SHA256Final( state, digest );
	SHA256Init( state );
}

void SHA256Portable( const void *data, std::size_t dataBytes,
		std::uint8_t digest[ SHA256_DIGEST_BYTES ] ) {
	sha256Context_t context;
	SHA256Init( context, SHA256TransformBlocksPortable );
	if ( dataBytes != 0 ) {
		SHA256Update( context, data, dataBytes );
	}
	SHA256Final( context, digest );
}

const char *SHA256Implementation( void ) {
	if ( !SHA256Accelerated() ) {
		return "portable";
	}
#if IDCRYPTO_SHA_ARM64
	return "Armv8 SHA-256 instructions";
#else
	return "x86 SHA extensions";
#endif
}

void HMACSHA256( const void *key, std::size_t keyBytes,
		const void *data, std::size_t dataBytes,
		std::uint8_t digest[ SHA256_DIGEST_BYTES ] ) {
	hmacSHA256Prepared_t prepared;
	HMACPrepare( key, keyBytes, prepared );
	HMACFinish( prepared, data, dataBytes, digest );
	SecureZero( &prepared, sizeof( prepared ) );
}

bool PBKDF2HMACSHA256( const void *password, std::size_t passwordBytes,
		const void *salt, std::size_t saltBytes, std::uint32_t iterations,
		void *output, std::size_t outputBytes ) {
	if ( ( passwordBytes != 0 && password == nullptr ) ||
		( saltBytes != 0 && salt == nullptr ) || output == nullptr ||
		outputBytes == 0 || outputBytes > SHA256_DIGEST_BYTES || iterations == 0 ) {
		return false;
	}

	hmacSHA256Prepared_t prepared;
	HMACPrepare( password, passwordBytes, prepared );
	sha256Context_t firstInner = prepared.inner;
	const std::uint8_t blockIndex[ 4 ] = { 0, 0, 0, 1 };
	std::uint8_t iteration[ SHA256_DIGEST_BYTES ];
	std::uint8_t aggregate[ SHA256_DIGEST_BYTES ];

	if ( saltBytes != 0 ) {
		SHA256Update( firstInner, salt, saltBytes );
	}
	SHA256Update( firstInner, blockIndex, sizeof( blockIndex ) );
	std::uint8_t firstDigest[ SHA256_DIGEST_BYTES ];
	SHA256Final( firstInner, firstDigest );
	sha256Context_t firstOuter = prepared.outer;
	SHA256Update( firstOuter, firstDigest, sizeof( firstDigest ) );
	SHA256Final( firstOuter, iteration );
	std::memcpy( aggregate, iteration, sizeof( aggregate ) );
	SecureZero( firstDigest, sizeof( firstDigest ) );

	for ( std::uint32_t round = 1; round < iterations; ++round ) {
		std::uint8_t next[ SHA256_DIGEST_BYTES ];
		HMACFinish( prepared, iteration, sizeof( iteration ), next );
		for ( std::size_t index = 0; index < sizeof( aggregate ); ++index ) {
			aggregate[ index ] ^= next[ index ];
		}
		std::memcpy( iteration, next, sizeof( iteration ) );
		SecureZero( next, sizeof( next ) );
	}

	std::memcpy( output, aggregate, outputBytes );
	SecureZero( iteration, sizeof( iteration ) );
	SecureZero( aggregate, sizeof( aggregate ) );
	SecureZero( &prepared, sizeof( prepared ) );
	return true;
}

bool ConstantTimeEquals( const void *left, const void *right, std::size_t bytes ) {
	if ( bytes == 0 ) {
		return true;
	}
	if ( left == nullptr || right == nullptr ) {
		return false;
	}
	const std::uint8_t *leftBytes = static_cast<const std::uint8_t *>( left );
	const std::uint8_t *rightBytes = static_cast<const std::uint8_t *>( right );
	volatile std::uint8_t difference = 0;
	for ( std::size_t index = 0; index < bytes; ++index ) {
		difference |= leftBytes[ index ] ^ rightBytes[ index ];
	}
	return difference == 0;
}

void SecureZero( void *memory, std::size_t bytes ) {
	volatile std::uint8_t *cursor = static_cast<volatile std::uint8_t *>( memory );
	while ( bytes-- != 0 ) {
		*cursor++ = 0;
	}
}

} // namespace idCrypto
