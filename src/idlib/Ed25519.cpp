/*
===========================================================================

openQ4 Ed25519 signature verification
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

The field and group arithmetic follows TweetNaCl by Daniel J. Bernstein,
Bernard van Gastel, Wesley Janssen, Tanja Lange, Peter Schwabe and Sjaak
Smetsers, which its authors placed in the public domain. The constants were
recomputed from their definitions (RFC 8032, FIPS 180-4).

===========================================================================
*/

#include "Ed25519.h"

#include <cstring>
#include <vector>

namespace idCrypto {

namespace {

/*
===============================================================================

	SHA-512

===============================================================================
*/

static constexpr std::uint64_t SHA512_K[ 80 ] = {
	0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
	0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
	0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
	0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
	0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
	0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
	0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
	0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
	0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
	0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
	0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
	0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
	0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
	0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
	0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
	0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
	0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
	0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
	0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
	0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull
};

static inline std::uint64_t RotateRight64( std::uint64_t value, unsigned int count ) {
	return ( value >> count ) | ( value << ( 64u - count ) );
}

static inline std::uint64_t ReadBigEndian64( const std::uint8_t *bytes ) {
	std::uint64_t value = 0;
	for ( int index = 0; index < 8; ++index ) {
		value = ( value << 8 ) | bytes[ index ];
	}
	return value;
}

static void SHA512Block( std::uint64_t state[ 8 ], const std::uint8_t block[ 128 ] ) {
	std::uint64_t w[ 80 ];
	for ( int index = 0; index < 16; ++index ) {
		w[ index ] = ReadBigEndian64( block + index * 8 );
	}
	for ( int index = 16; index < 80; ++index ) {
		const std::uint64_t s0 = RotateRight64( w[ index - 15 ], 1 ) ^ RotateRight64( w[ index - 15 ], 8 ) ^ ( w[ index - 15 ] >> 7 );
		const std::uint64_t s1 = RotateRight64( w[ index - 2 ], 19 ) ^ RotateRight64( w[ index - 2 ], 61 ) ^ ( w[ index - 2 ] >> 6 );
		w[ index ] = w[ index - 16 ] + s0 + w[ index - 7 ] + s1;
	}
	std::uint64_t a = state[ 0 ], b = state[ 1 ], c = state[ 2 ], d = state[ 3 ];
	std::uint64_t e = state[ 4 ], f = state[ 5 ], g = state[ 6 ], h = state[ 7 ];
	for ( int index = 0; index < 80; ++index ) {
		const std::uint64_t s1 = RotateRight64( e, 14 ) ^ RotateRight64( e, 18 ) ^ RotateRight64( e, 41 );
		const std::uint64_t choose = ( e & f ) ^ ( ~e & g );
		const std::uint64_t t1 = h + s1 + choose + SHA512_K[ index ] + w[ index ];
		const std::uint64_t s0 = RotateRight64( a, 28 ) ^ RotateRight64( a, 34 ) ^ RotateRight64( a, 39 );
		const std::uint64_t majority = ( a & b ) ^ ( a & c ) ^ ( b & c );
		const std::uint64_t t2 = s0 + majority;
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}
	state[ 0 ] += a; state[ 1 ] += b; state[ 2 ] += c; state[ 3 ] += d;
	state[ 4 ] += e; state[ 5 ] += f; state[ 6 ] += g; state[ 7 ] += h;
}

/*
===============================================================================

	Field arithmetic modulo 2^255 - 19, sixteen 16-bit limbs held in int64

===============================================================================
*/

typedef std::int64_t gf[ 16 ];

static const gf GF0 = { 0 };
static const gf GF1 = { 1 };
// d = -121665 / 121666
static const gf D = { 0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203 };
static const gf D2 = { 0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406 };
// sqrt( -1 )
static const gf SQRTM1 = { 0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83 };
// the base point ( x even, y = 4/5 )
static const gf BASE_X = { 0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169 };
static const gf BASE_Y = { 0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666 };

// the group order L = 2^252 + 27742317777372353535851937790883648493
static const std::int64_t ORDER[ 32 ] = {
	0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10
};

static void Set( gf r, const gf a ) {
	for ( int i = 0; i < 16; ++i ) {
		r[ i ] = a[ i ];
	}
}

static void Carry( gf o ) {
	for ( int i = 0; i < 16; ++i ) {
		o[ i ] += ( std::int64_t( 1 ) << 16 );
		const std::int64_t c = o[ i ] >> 16;
		o[ ( i + 1 ) * ( i < 15 ) ] += c - 1 + 37 * ( c - 1 ) * ( i == 15 );
		o[ i ] -= c * 65536;
	}
}

static void Select( gf p, gf q, int b ) {
	const std::int64_t mask = ~( std::int64_t( b ) - 1 );
	for ( int i = 0; i < 16; ++i ) {
		const std::int64_t t = mask & ( p[ i ] ^ q[ i ] );
		p[ i ] ^= t;
		q[ i ] ^= t;
	}
}

static void Pack( std::uint8_t o[ 32 ], const gf n ) {
	gf m, t;
	Set( t, n );
	Carry( t );
	Carry( t );
	Carry( t );
	for ( int pass = 0; pass < 2; ++pass ) {
		m[ 0 ] = t[ 0 ] - 0xffed;
		for ( int i = 1; i < 15; ++i ) {
			m[ i ] = t[ i ] - 0xffff - ( ( m[ i - 1 ] >> 16 ) & 1 );
			m[ i - 1 ] &= 0xffff;
		}
		m[ 15 ] = t[ 15 ] - 0x7fff - ( ( m[ 14 ] >> 16 ) & 1 );
		const int b = static_cast<int>( ( m[ 15 ] >> 16 ) & 1 );
		m[ 14 ] &= 0xffff;
		Select( t, m, 1 - b );
	}
	for ( int i = 0; i < 16; ++i ) {
		o[ 2 * i ] = static_cast<std::uint8_t>( t[ i ] & 0xff );
		o[ 2 * i + 1 ] = static_cast<std::uint8_t>( ( t[ i ] >> 8 ) & 0xff );
	}
}

static bool Equal32( const std::uint8_t *a, const std::uint8_t *b ) {
	unsigned int difference = 0;
	for ( int i = 0; i < 32; ++i ) {
		difference |= static_cast<unsigned int>( a[ i ] ^ b[ i ] );
	}
	return difference == 0;
}

static bool NotEqual( const gf a, const gf b ) {
	std::uint8_t c[ 32 ], d[ 32 ];
	Pack( c, a );
	Pack( d, b );
	return !Equal32( c, d );
}

static int Parity( const gf a ) {
	std::uint8_t d[ 32 ];
	Pack( d, a );
	return d[ 0 ] & 1;
}

static void Unpack( gf o, const std::uint8_t n[ 32 ] ) {
	for ( int i = 0; i < 16; ++i ) {
		o[ i ] = n[ 2 * i ] + ( std::int64_t( n[ 2 * i + 1 ] ) << 8 );
	}
	o[ 15 ] &= 0x7fff;
}

static void Add( gf o, const gf a, const gf b ) {
	for ( int i = 0; i < 16; ++i ) {
		o[ i ] = a[ i ] + b[ i ];
	}
}

static void Subtract( gf o, const gf a, const gf b ) {
	for ( int i = 0; i < 16; ++i ) {
		o[ i ] = a[ i ] - b[ i ];
	}
}

static void Multiply( gf o, const gf a, const gf b ) {
	std::int64_t t[ 31 ] = { 0 };
	for ( int i = 0; i < 16; ++i ) {
		for ( int j = 0; j < 16; ++j ) {
			t[ i + j ] += a[ i ] * b[ j ];
		}
	}
	for ( int i = 0; i < 15; ++i ) {
		t[ i ] += 38 * t[ i + 16 ];
	}
	for ( int i = 0; i < 16; ++i ) {
		o[ i ] = t[ i ];
	}
	Carry( o );
	Carry( o );
}

static void Square( gf o, const gf a ) {
	Multiply( o, a, a );
}

static void Pow2523( gf o, const gf i ) {
	gf c;
	Set( c, i );
	for ( int a = 250; a >= 0; --a ) {
		Square( c, c );
		if ( a != 1 ) {
			Multiply( c, c, i );
		}
	}
	Set( o, c );
}

static void Invert( gf o, const gf i ) {
	gf c;
	Set( c, i );
	for ( int a = 253; a >= 0; --a ) {
		Square( c, c );
		if ( a != 2 && a != 4 ) {
			Multiply( c, c, i );
		}
	}
	Set( o, c );
}

/*
===============================================================================

	Extended twisted Edwards points ( X, Y, Z, T )

===============================================================================
*/

static void PointAdd( gf p[ 4 ], gf q[ 4 ] ) {
	gf a, b, c, d, t, e, f, g, h;
	Subtract( a, p[ 1 ], p[ 0 ] );
	Subtract( t, q[ 1 ], q[ 0 ] );
	Multiply( a, a, t );
	Add( b, p[ 0 ], p[ 1 ] );
	Add( t, q[ 0 ], q[ 1 ] );
	Multiply( b, b, t );
	Multiply( c, p[ 3 ], q[ 3 ] );
	Multiply( c, c, D2 );
	Multiply( d, p[ 2 ], q[ 2 ] );
	Add( d, d, d );
	Subtract( e, b, a );
	Subtract( f, d, c );
	Add( g, d, c );
	Add( h, b, a );
	Multiply( p[ 0 ], e, f );
	Multiply( p[ 1 ], h, g );
	Multiply( p[ 2 ], g, f );
	Multiply( p[ 3 ], e, h );
}

static void PointSwap( gf p[ 4 ], gf q[ 4 ], int b ) {
	for ( int i = 0; i < 4; ++i ) {
		Select( p[ i ], q[ i ], b );
	}
}

static void PointPack( std::uint8_t r[ 32 ], gf p[ 4 ] ) {
	gf tx, ty, zi;
	Invert( zi, p[ 2 ] );
	Multiply( tx, p[ 0 ], zi );
	Multiply( ty, p[ 1 ], zi );
	Pack( r, ty );
	r[ 31 ] ^= static_cast<std::uint8_t>( Parity( tx ) << 7 );
}

static void ScalarMultiply( gf p[ 4 ], gf q[ 4 ], const std::uint8_t s[ 32 ] ) {
	Set( p[ 0 ], GF0 );
	Set( p[ 1 ], GF1 );
	Set( p[ 2 ], GF1 );
	Set( p[ 3 ], GF0 );
	for ( int i = 255; i >= 0; --i ) {
		const int b = ( s[ i / 8 ] >> ( i & 7 ) ) & 1;
		PointSwap( p, q, b );
		PointAdd( q, p );
		PointAdd( p, p );
		PointSwap( p, q, b );
	}
}

static void ScalarMultiplyBase( gf p[ 4 ], const std::uint8_t s[ 32 ] ) {
	gf q[ 4 ];
	Set( q[ 0 ], BASE_X );
	Set( q[ 1 ], BASE_Y );
	Set( q[ 2 ], GF1 );
	Multiply( q[ 3 ], BASE_X, BASE_Y );
	ScalarMultiply( p, q, s );
}

// Decodes a point and negates it, as verification needs -A.
static bool UnpackNegated( gf r[ 4 ], const std::uint8_t p[ 32 ] ) {
	gf t, check, num, den, den2, den4, den6;
	Set( r[ 2 ], GF1 );
	Unpack( r[ 1 ], p );
	Square( num, r[ 1 ] );
	Multiply( den, num, D );
	Subtract( num, num, r[ 2 ] );
	Add( den, r[ 2 ], den );

	Square( den2, den );
	Square( den4, den2 );
	Multiply( den6, den4, den2 );
	Multiply( t, den6, num );
	Multiply( t, t, den );

	Pow2523( t, t );
	Multiply( t, t, num );
	Multiply( t, t, den );
	Multiply( t, t, den );
	Multiply( r[ 0 ], t, den );

	Square( check, r[ 0 ] );
	Multiply( check, check, den );
	if ( NotEqual( check, num ) ) {
		Multiply( r[ 0 ], r[ 0 ], SQRTM1 );
	}
	Square( check, r[ 0 ] );
	Multiply( check, check, den );
	if ( NotEqual( check, num ) ) {
		return false;
	}
	if ( Parity( r[ 0 ] ) == ( p[ 31 ] >> 7 ) ) {
		Subtract( r[ 0 ], GF0, r[ 0 ] );
	}
	Multiply( r[ 3 ], r[ 0 ], r[ 1 ] );
	return true;
}

// Reduces a 512-bit little-endian value modulo L into 32 bytes.
static void ReduceModOrder( std::uint8_t r[ 32 ], std::int64_t x[ 64 ] ) {
	std::int64_t carry;
	for ( int i = 63; i >= 32; --i ) {
		carry = 0;
		int j;
		for ( j = i - 32; j < i - 12; ++j ) {
			x[ j ] += carry - 16 * x[ i ] * ORDER[ j - ( i - 32 ) ];
			carry = ( x[ j ] + 128 ) >> 8;
			x[ j ] -= carry * 256;
		}
		x[ j ] += carry;
		x[ i ] = 0;
	}
	carry = 0;
	for ( int j = 0; j < 32; ++j ) {
		x[ j ] += carry - ( x[ 31 ] >> 4 ) * ORDER[ j ];
		carry = x[ j ] >> 8;
		x[ j ] &= 255;
	}
	for ( int j = 0; j < 32; ++j ) {
		x[ j ] -= carry * ORDER[ j ];
	}
	for ( int i = 0; i < 32; ++i ) {
		x[ i + 1 ] += x[ i ] >> 8;
		r[ i ] = static_cast<std::uint8_t>( x[ i ] & 255 );
	}
}

// True when the little-endian scalar is below L (RFC 8032 section 5.1.7).
static bool ScalarIsCanonical( const std::uint8_t s[ 32 ] ) {
	for ( int i = 31; i >= 0; --i ) {
		if ( s[ i ] < ORDER[ i ] ) {
			return true;
		}
		if ( s[ i ] > ORDER[ i ] ) {
			return false;
		}
	}
	return false;	// equal to L
}

} // namespace

void SHA512( const void *data, std::size_t dataBytes,
		std::uint8_t digest[ SHA512_DIGEST_BYTES ] ) {
	std::uint64_t state[ 8 ] = {
		0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
		0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull
	};
	const std::uint8_t *cursor = static_cast<const std::uint8_t *>( data );
	std::size_t remaining = dataBytes;
	while ( remaining >= 128 ) {
		SHA512Block( state, cursor );
		cursor += 128;
		remaining -= 128;
	}
	std::uint8_t tail[ 256 ] = { 0 };
	if ( remaining != 0 ) {
		std::memcpy( tail, cursor, remaining );
	}
	tail[ remaining ] = 0x80u;
	const std::size_t tailBytes = remaining < 112 ? 128 : 256;
	// The message length in bits as a 128-bit big-endian value; sizes here
	// never reach 2^61 bytes, so the high half stays zero.
	const std::uint64_t totalBits = static_cast<std::uint64_t>( dataBytes ) * 8u;
	for ( int index = 0; index < 8; ++index ) {
		tail[ tailBytes - 1 - index ] = static_cast<std::uint8_t>( totalBits >> ( index * 8 ) );
	}
	SHA512Block( state, tail );
	if ( tailBytes == 256 ) {
		SHA512Block( state, tail + 128 );
	}
	for ( int index = 0; index < 8; ++index ) {
		for ( int byte = 0; byte < 8; ++byte ) {
			digest[ index * 8 + byte ] = static_cast<std::uint8_t>( state[ index ] >> ( 56 - byte * 8 ) );
		}
	}
}

bool Ed25519Verify( const std::uint8_t signature[ ED25519_SIGNATURE_BYTES ],
		const void *message, std::size_t messageBytes,
		const std::uint8_t publicKey[ ED25519_PUBLIC_KEY_BYTES ] ) {
	if ( signature == nullptr || publicKey == nullptr || ( message == nullptr && messageBytes != 0 ) ) {
		return false;
	}
	if ( !ScalarIsCanonical( signature + 32 ) ) {
		return false;
	}
	gf p[ 4 ], q[ 4 ];
	if ( !UnpackNegated( q, publicKey ) ) {
		return false;
	}

	// h = SHA-512( R || A || M ) mod L
	std::vector<std::uint8_t> hashed( 64 + messageBytes );
	std::memcpy( hashed.data(), signature, 32 );
	std::memcpy( hashed.data() + 32, publicKey, 32 );
	if ( messageBytes != 0 ) {
		std::memcpy( hashed.data() + 64, message, messageBytes );
	}
	std::uint8_t digest[ SHA512_DIGEST_BYTES ];
	SHA512( hashed.data(), hashed.size(), digest );
	std::int64_t wide[ 64 ];
	for ( int i = 0; i < 64; ++i ) {
		wide[ i ] = digest[ i ];
	}
	std::uint8_t h[ 32 ];
	ReduceModOrder( h, wide );

	// [S]B - [h]A must encode to R.
	ScalarMultiply( p, q, h );
	ScalarMultiplyBase( q, signature + 32 );
	PointAdd( p, q );
	std::uint8_t encoded[ 32 ];
	PointPack( encoded, p );
	return Equal32( signature, encoded );
}

void SecureWipe( void *data, std::size_t bytes ) {
	volatile std::uint8_t *cursor = static_cast<volatile std::uint8_t *>( data );
	while ( bytes-- != 0 ) {
		*cursor++ = 0;
	}
}

namespace {

// The secret scalar and the nonce prefix: SHA-512 of the seed, its first half
// clamped (RFC 8032 section 5.1.5).
static void ExpandSeed( std::uint8_t expanded[ SHA512_DIGEST_BYTES ], const std::uint8_t seed[ ED25519_SEED_BYTES ] ) {
	SHA512( seed, ED25519_SEED_BYTES, expanded );
	expanded[ 0 ] &= 248;
	expanded[ 31 ] &= 127;
	expanded[ 31 ] |= 64;
}

static void PublicKeyFromExpanded( std::uint8_t publicKey[ ED25519_PUBLIC_KEY_BYTES ], const std::uint8_t expanded[ SHA512_DIGEST_BYTES ] ) {
	gf p[ 4 ];
	ScalarMultiplyBase( p, expanded );
	PointPack( publicKey, p );
	SecureWipe( p, sizeof( p ) );
}

// SHA-512 of the parts, reduced modulo L.
static void HashModOrder( std::uint8_t out[ 32 ], const std::uint8_t *first, std::size_t firstBytes,
		const std::uint8_t *second, std::size_t secondBytes, const void *message, std::size_t messageBytes ) {
	std::vector<std::uint8_t> hashed( firstBytes + secondBytes + messageBytes );
	if ( firstBytes != 0 ) {
		std::memcpy( hashed.data(), first, firstBytes );
	}
	if ( secondBytes != 0 ) {
		std::memcpy( hashed.data() + firstBytes, second, secondBytes );
	}
	if ( messageBytes != 0 ) {
		std::memcpy( hashed.data() + firstBytes + secondBytes, message, messageBytes );
	}
	std::uint8_t digest[ SHA512_DIGEST_BYTES ];
	SHA512( hashed.data(), hashed.size(), digest );
	SecureWipe( hashed.data(), hashed.size() );
	std::int64_t wide[ 64 ];
	for ( int i = 0; i < 64; ++i ) {
		wide[ i ] = digest[ i ];
	}
	ReduceModOrder( out, wide );
	SecureWipe( digest, sizeof( digest ) );
	SecureWipe( wide, sizeof( wide ) );
}

} // namespace

void Ed25519PublicKey( const std::uint8_t seed[ ED25519_SEED_BYTES ],
		std::uint8_t publicKey[ ED25519_PUBLIC_KEY_BYTES ] ) {
	std::uint8_t expanded[ SHA512_DIGEST_BYTES ];
	ExpandSeed( expanded, seed );
	PublicKeyFromExpanded( publicKey, expanded );
	SecureWipe( expanded, sizeof( expanded ) );
}

void Ed25519Sign( std::uint8_t signature[ ED25519_SIGNATURE_BYTES ],
		const void *message, std::size_t messageBytes,
		const std::uint8_t seed[ ED25519_SEED_BYTES ] ) {
	std::uint8_t expanded[ SHA512_DIGEST_BYTES ];
	std::uint8_t publicKey[ ED25519_PUBLIC_KEY_BYTES ];
	ExpandSeed( expanded, seed );
	PublicKeyFromExpanded( publicKey, expanded );

	// r = SHA-512( prefix || M ) mod L; R = [r]B
	std::uint8_t r[ 32 ];
	HashModOrder( r, expanded + 32, 32, nullptr, 0, message, messageBytes );
	gf p[ 4 ];
	ScalarMultiplyBase( p, r );
	PointPack( signature, p );

	// h = SHA-512( R || A || M ) mod L; S = ( r + h * a ) mod L
	std::uint8_t h[ 32 ];
	HashModOrder( h, signature, 32, publicKey, ED25519_PUBLIC_KEY_BYTES, message, messageBytes );
	std::int64_t x[ 64 ] = { 0 };
	for ( int i = 0; i < 32; ++i ) {
		x[ i ] = r[ i ];
	}
	for ( int i = 0; i < 32; ++i ) {
		for ( int j = 0; j < 32; ++j ) {
			x[ i + j ] += std::int64_t( h[ i ] ) * expanded[ j ];
		}
	}
	ReduceModOrder( signature + 32, x );

	SecureWipe( expanded, sizeof( expanded ) );
	SecureWipe( r, sizeof( r ) );
	SecureWipe( p, sizeof( p ) );
	SecureWipe( x, sizeof( x ) );
}

} // namespace idCrypto
