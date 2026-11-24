//----------------------------------------------------------------
// MatchAuthentication.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_AUTHENTICATION_STANDALONE_TEST )
	#include "MatchAuthentication.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchAuthentication.h"
#endif

#include <limits.h>
#include <string.h>

static_assert( MP_REFEREE_AUTH_NONCE_BYTES >= 16,
	"referee challenges require at least 128 bits of adapter-supplied nonce" );
static_assert( MP_REFEREE_AUTH_VERIFIER_BYTES == 32,
	"SHA-256 verifier size is a wire contract" );
static_assert( MP_REFEREE_AUTH_PROOF_BYTES == 32,
	"HMAC-SHA-256 proof size is a wire contract" );
static_assert( MP_REFEREE_AUTH_MAX_SLOTS == 32,
	"authentication records share the competitive connection-slot ceiling" );

namespace {

static const uint8_t REFEREE_VERIFIER_DOMAIN[] =
	"openQ4/referee/verifier/v1";
static const uint8_t REFEREE_CHALLENGE_DOMAIN[] =
	"openQ4/referee/challenge/v1";

static const uint8_t REFEREE_CHALLENGE_MAGIC[ 4 ] = {
	'O', 'Q', 'R', 'A'
};

// Used only to keep the proof-computation path uniform when an adapter calls
// VerifyProof before installing either a real verifier or its random dummy.
// It can never authenticate because credentialInstalled remains false.
static const uint8_t REFEREE_UNAVAILABLE_VERIFIER[ MP_REFEREE_AUTH_VERIFIER_BYTES ] = {
	0x48, 0x2d, 0x71, 0x1f, 0xb7, 0x93, 0x52, 0xc0,
	0x0a, 0xe1, 0x67, 0xd4, 0x3b, 0x8c, 0x2f, 0x95,
	0x60, 0x14, 0xad, 0xee, 0x39, 0x76, 0x83, 0x0b,
	0xc5, 0x9a, 0xf8, 0x42, 0x1d, 0x6e, 0xb0, 0x37
};

struct mpSha256Context {
	uint32_t state[ 8 ];
	uint64_t totalBytes;
	uint8_t block[ 64 ];
	size_t blockBytes;
};

struct mpHmacSha256Prepared {
	mpSha256Context inner;
	mpSha256Context outer;
};

static uint32_t RotateRight( uint32_t value, unsigned int bits ) {
	return ( value >> bits ) | ( value << ( 32u - bits ) );
}

static uint32_t LoadBigEndian32( const uint8_t *input ) {
	return ( static_cast<uint32_t>( input[ 0 ] ) << 24 ) |
		( static_cast<uint32_t>( input[ 1 ] ) << 16 ) |
		( static_cast<uint32_t>( input[ 2 ] ) << 8 ) |
		static_cast<uint32_t>( input[ 3 ] );
}

static uint64_t LoadBigEndian64( const uint8_t *input ) {
	return ( static_cast<uint64_t>( LoadBigEndian32( input ) ) << 32 ) |
		static_cast<uint64_t>( LoadBigEndian32( input + 4 ) );
}

static void StoreBigEndian16( uint8_t *output, uint16_t value ) {
	output[ 0 ] = static_cast<uint8_t>( value >> 8 );
	output[ 1 ] = static_cast<uint8_t>( value );
}

static void StoreBigEndian32( uint8_t *output, uint32_t value ) {
	output[ 0 ] = static_cast<uint8_t>( value >> 24 );
	output[ 1 ] = static_cast<uint8_t>( value >> 16 );
	output[ 2 ] = static_cast<uint8_t>( value >> 8 );
	output[ 3 ] = static_cast<uint8_t>( value );
}

static void StoreBigEndian64( uint8_t *output, uint64_t value ) {
	StoreBigEndian32( output, static_cast<uint32_t>( value >> 32 ) );
	StoreBigEndian32( output + 4, static_cast<uint32_t>( value ) );
}

static void Sha256Transform( mpSha256Context &context, const uint8_t block[ 64 ] ) {
	static const uint32_t roundConstants[ 64 ] = {
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

	uint32_t schedule[ 64 ];
	for ( int index = 0; index < 16; ++index ) {
		schedule[ index ] = LoadBigEndian32( block + index * 4 );
	}
	for ( int index = 16; index < 64; ++index ) {
		const uint32_t s0 = RotateRight( schedule[ index - 15 ], 7 ) ^
			RotateRight( schedule[ index - 15 ], 18 ) ^
			( schedule[ index - 15 ] >> 3 );
		const uint32_t s1 = RotateRight( schedule[ index - 2 ], 17 ) ^
			RotateRight( schedule[ index - 2 ], 19 ) ^
			( schedule[ index - 2 ] >> 10 );
		schedule[ index ] = schedule[ index - 16 ] + s0 +
			schedule[ index - 7 ] + s1;
	}

	uint32_t a = context.state[ 0 ];
	uint32_t b = context.state[ 1 ];
	uint32_t c = context.state[ 2 ];
	uint32_t d = context.state[ 3 ];
	uint32_t e = context.state[ 4 ];
	uint32_t f = context.state[ 5 ];
	uint32_t g = context.state[ 6 ];
	uint32_t h = context.state[ 7 ];

	for ( int index = 0; index < 64; ++index ) {
		const uint32_t sum1 = RotateRight( e, 6 ) ^ RotateRight( e, 11 ) ^
			RotateRight( e, 25 );
		const uint32_t choose = ( e & f ) ^ ( ( ~e ) & g );
		const uint32_t temporary1 = h + sum1 + choose +
			roundConstants[ index ] + schedule[ index ];
		const uint32_t sum0 = RotateRight( a, 2 ) ^ RotateRight( a, 13 ) ^
			RotateRight( a, 22 );
		const uint32_t majority = ( a & b ) ^ ( a & c ) ^ ( b & c );
		const uint32_t temporary2 = sum0 + majority;

		h = g;
		g = f;
		f = e;
		e = d + temporary1;
		d = c;
		c = b;
		b = a;
		a = temporary1 + temporary2;
	}

	context.state[ 0 ] += a;
	context.state[ 1 ] += b;
	context.state[ 2 ] += c;
	context.state[ 3 ] += d;
	context.state[ 4 ] += e;
	context.state[ 5 ] += f;
	context.state[ 6 ] += g;
	context.state[ 7 ] += h;
	MPRefereeAuthSecureZero( schedule, sizeof( schedule ) );
}

static void Sha256Initialize( mpSha256Context &context ) {
	context.state[ 0 ] = 0x6a09e667u;
	context.state[ 1 ] = 0xbb67ae85u;
	context.state[ 2 ] = 0x3c6ef372u;
	context.state[ 3 ] = 0xa54ff53au;
	context.state[ 4 ] = 0x510e527fu;
	context.state[ 5 ] = 0x9b05688cu;
	context.state[ 6 ] = 0x1f83d9abu;
	context.state[ 7 ] = 0x5be0cd19u;
	context.totalBytes = 0;
	context.blockBytes = 0;
	memset( context.block, 0, sizeof( context.block ) );
}

static void Sha256Update( mpSha256Context &context, const void *inputMemory,
	size_t inputBytes ) {
	const uint8_t *input = static_cast<const uint8_t *>( inputMemory );
	while ( inputBytes > 0 ) {
		const size_t available = sizeof( context.block ) - context.blockBytes;
		const size_t consumed = inputBytes < available ? inputBytes : available;
		memcpy( context.block + context.blockBytes, input, consumed );
		context.blockBytes += consumed;
		context.totalBytes += static_cast<uint64_t>( consumed );
		input += consumed;
		inputBytes -= consumed;
		if ( context.blockBytes == sizeof( context.block ) ) {
			Sha256Transform( context, context.block );
			context.blockBytes = 0;
		}
	}
}

static void Sha256Finalize( mpSha256Context &context, uint8_t output[ 32 ] ) {
	const uint64_t totalBits = context.totalBytes * 8u;
	context.block[ context.blockBytes++ ] = 0x80u;
	if ( context.blockBytes > 56 ) {
		memset( context.block + context.blockBytes, 0,
			sizeof( context.block ) - context.blockBytes );
		Sha256Transform( context, context.block );
		context.blockBytes = 0;
	}
	memset( context.block + context.blockBytes, 0, 56 - context.blockBytes );
	StoreBigEndian64( context.block + 56, totalBits );
	Sha256Transform( context, context.block );
	for ( int index = 0; index < 8; ++index ) {
		StoreBigEndian32( output + index * 4, context.state[ index ] );
	}
	MPRefereeAuthSecureZero( &context, sizeof( context ) );
}

static void Sha256( const void *input, size_t inputBytes, uint8_t output[ 32 ] ) {
	mpSha256Context context;
	Sha256Initialize( context );
	Sha256Update( context, input, inputBytes );
	Sha256Finalize( context, output );
}

static void HmacSha256Prepare( const void *keyMemory, size_t keyBytes,
	mpHmacSha256Prepared &prepared ) {
	const uint8_t *key = static_cast<const uint8_t *>( keyMemory );
	uint8_t normalizedKey[ 64 ];
	uint8_t innerPad[ 64 ];
	uint8_t outerPad[ 64 ];
	memset( normalizedKey, 0, sizeof( normalizedKey ) );
	if ( keyBytes > sizeof( normalizedKey ) ) {
		Sha256( key, keyBytes, normalizedKey );
	} else if ( keyBytes > 0 ) {
		memcpy( normalizedKey, key, keyBytes );
	}
	for ( int index = 0; index < 64; ++index ) {
		innerPad[ index ] = static_cast<uint8_t>( normalizedKey[ index ] ^ 0x36u );
		outerPad[ index ] = static_cast<uint8_t>( normalizedKey[ index ] ^ 0x5cu );
	}

	Sha256Initialize( prepared.inner );
	Sha256Update( prepared.inner, innerPad, sizeof( innerPad ) );
	Sha256Initialize( prepared.outer );
	Sha256Update( prepared.outer, outerPad, sizeof( outerPad ) );

	MPRefereeAuthSecureZero( normalizedKey, sizeof( normalizedKey ) );
	MPRefereeAuthSecureZero( innerPad, sizeof( innerPad ) );
	MPRefereeAuthSecureZero( outerPad, sizeof( outerPad ) );

}

static void HmacSha256Prepared( const mpHmacSha256Prepared &prepared,
	const void *message, size_t messageBytes, uint8_t output[ 32 ] ) {
	uint8_t innerDigest[ 32 ];
	mpSha256Context context = prepared.inner;
	Sha256Update( context, message, messageBytes );
	Sha256Finalize( context, innerDigest );
	context = prepared.outer;
	Sha256Update( context, innerDigest, sizeof( innerDigest ) );
	Sha256Finalize( context, output );
	MPRefereeAuthSecureZero( innerDigest, sizeof( innerDigest ) );
}

static void HmacSha256( const void *keyMemory, size_t keyBytes,
	const void *message, size_t messageBytes, uint8_t output[ 32 ] ) {
	mpHmacSha256Prepared prepared;
	HmacSha256Prepare( keyMemory, keyBytes, prepared );
	HmacSha256Prepared( prepared, message, messageBytes, output );
	MPRefereeAuthSecureZero( &prepared, sizeof( prepared ) );
}

static bool IsAllZero( const uint8_t *bytes, size_t count ) {
	uint8_t aggregate = 0;
	for ( size_t index = 0; index < count; ++index ) {
		aggregate = static_cast<uint8_t>( aggregate | bytes[ index ] );
	}
	return aggregate == 0;
}

static bool ConstantTimeEqual( const uint8_t *left, const uint8_t *right,
	size_t count ) {
	volatile uint8_t difference = 0;
	for ( size_t index = 0; index < count; ++index ) {
		difference = static_cast<uint8_t>( difference | ( left[ index ] ^ right[ index ] ) );
	}
	return difference == 0;
}

static bool BindingIsStructurallyValid( const mpRefereeAuthBinding &binding ) {
	return binding.sessionId != 0 && binding.participantSequence != 0 &&
		binding.slot >= 0 && binding.slot < MP_REFEREE_AUTH_MAX_SLOTS &&
		binding.slotGeneration != 0;
}

static bool SameBinding( const mpRefereeAuthBinding &left,
	const mpRefereeAuthBinding &right ) {
	return left.sessionId == right.sessionId &&
		left.participantSequence == right.participantSequence &&
		left.slot == right.slot &&
		left.slotGeneration == right.slotGeneration;
}

static bool ChallengeIsStructurallyValid( const mpRefereeAuthChallenge &challenge ) {
	return challenge.wireVersion == MP_REFEREE_AUTH_WIRE_VERSION &&
		challenge.algorithm == MP_REFEREE_AUTH_ALGORITHM_PBKDF2_HMAC_SHA256 &&
		challenge.iterationCount == MP_REFEREE_AUTH_PBKDF2_ITERATIONS &&
		BindingIsStructurallyValid( challenge.binding ) &&
		challenge.challengeGeneration != 0 &&
		challenge.expiresAtEngineMsec >= 0 &&
		!IsAllZero( challenge.salt.bytes, sizeof( challenge.salt.bytes ) ) &&
		!IsAllZero( challenge.nonce.bytes, sizeof( challenge.nonce.bytes ) );
}

static bool AddEngineDuration( int64_t engineTimeMsec, int64_t durationMsec,
	int64_t &result ) {
	if ( engineTimeMsec < 0 || durationMsec < 0 ||
		engineTimeMsec > INT64_MAX - durationMsec ) {
		return false;
	}
	result = engineTimeMsec + durationMsec;
	return true;
}

static void EncodeChallengeUnchecked( const mpRefereeAuthChallenge &challenge,
	uint8_t output[ MP_REFEREE_AUTH_CHALLENGE_WIRE_BYTES ] ) {
	size_t offset = 0;
	memcpy( output + offset, REFEREE_CHALLENGE_MAGIC,
		sizeof( REFEREE_CHALLENGE_MAGIC ) );
	offset += sizeof( REFEREE_CHALLENGE_MAGIC );
	StoreBigEndian16( output + offset, challenge.wireVersion );
	offset += 2;
	StoreBigEndian16( output + offset, challenge.algorithm );
	offset += 2;
	StoreBigEndian32( output + offset, challenge.iterationCount );
	offset += 4;
	StoreBigEndian64( output + offset, challenge.binding.sessionId );
	offset += 8;
	StoreBigEndian32( output + offset, challenge.binding.participantSequence );
	offset += 4;
	StoreBigEndian32( output + offset, challenge.binding.slotGeneration );
	offset += 4;
	StoreBigEndian32( output + offset, static_cast<uint32_t>( challenge.binding.slot ) );
	offset += 4;
	StoreBigEndian64( output + offset, challenge.challengeGeneration );
	offset += 8;
	StoreBigEndian64( output + offset,
		static_cast<uint64_t>( challenge.expiresAtEngineMsec ) );
	offset += 8;
	memcpy( output + offset, challenge.salt.bytes, sizeof( challenge.salt.bytes ) );
	offset += sizeof( challenge.salt.bytes );
	memcpy( output + offset, challenge.nonce.bytes, sizeof( challenge.nonce.bytes ) );
	offset += sizeof( challenge.nonce.bytes );
	(void)offset;
}

static void BuildProofUnchecked( const mpRefereeAuthChallenge &challenge,
	const uint8_t verifier[ MP_REFEREE_AUTH_VERIFIER_BYTES ],
	mpRefereeAuthProof &proof ) {
	uint8_t message[ sizeof( REFEREE_CHALLENGE_DOMAIN ) +
		MP_REFEREE_AUTH_CHALLENGE_WIRE_BYTES ];
	const size_t domainBytes = sizeof( REFEREE_CHALLENGE_DOMAIN ) - 1;
	memcpy( message, REFEREE_CHALLENGE_DOMAIN, domainBytes );
	message[ domainBytes ] = 0;
	EncodeChallengeUnchecked( challenge, message + domainBytes + 1 );
	HmacSha256( verifier, MP_REFEREE_AUTH_VERIFIER_BYTES, message,
		domainBytes + 1 + MP_REFEREE_AUTH_CHALLENGE_WIRE_BYTES, proof.bytes );
	MPRefereeAuthSecureZero( message, sizeof( message ) );
}

static uint8_t HexValue( char value, uint8_t &valid ) {
	if ( value >= '0' && value <= '9' ) {
		return static_cast<uint8_t>( value - '0' );
	}
	if ( value >= 'a' && value <= 'f' ) {
		return static_cast<uint8_t>( value - 'a' + 10 );
	}
	if ( value >= 'A' && value <= 'F' ) {
		return static_cast<uint8_t>( value - 'A' + 10 );
	}
	valid = 0;
	return 0;
}

} // namespace

void mpRefereeAuthChallenge::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
}

void MPRefereeAuthSecureZero( void *memory, size_t bytes ) {
	volatile uint8_t *cursor = static_cast<volatile uint8_t *>( memory );
	while ( bytes > 0 ) {
		*cursor++ = 0;
		--bytes;
	}
}

bool MPRefereeAuthDeriveVerifier( const void *passwordBytes, size_t passwordLength,
	const mpRefereeAuthSalt &salt, mpRefereeAuthVerifier &verifier ) {
	MPRefereeAuthSecureZero( verifier.bytes, sizeof( verifier.bytes ) );
	if ( passwordBytes == NULL || passwordLength == 0 ||
		passwordLength > MP_REFEREE_AUTH_MAX_PASSWORD_BYTES ||
		IsAllZero( salt.bytes, sizeof( salt.bytes ) ) ) {
		return false;
	}

	uint8_t pbkdfSalt[ sizeof( REFEREE_VERIFIER_DOMAIN ) +
		MP_REFEREE_AUTH_SALT_BYTES ];
	const size_t domainBytes = sizeof( REFEREE_VERIFIER_DOMAIN ) - 1;
	memcpy( pbkdfSalt, REFEREE_VERIFIER_DOMAIN, domainBytes );
	pbkdfSalt[ domainBytes ] = 0;
	memcpy( pbkdfSalt + domainBytes + 1, salt.bytes, sizeof( salt.bytes ) );
	const size_t pbkdfSaltBytes = domainBytes + 1 + sizeof( salt.bytes );

	uint8_t firstMessage[ sizeof( pbkdfSalt ) + 4 ];
	memcpy( firstMessage, pbkdfSalt, pbkdfSaltBytes );
	StoreBigEndian32( firstMessage + pbkdfSaltBytes, 1 );
	uint8_t iteration[ 32 ];
	uint8_t aggregate[ 32 ];
	mpHmacSha256Prepared preparedPassword;
	HmacSha256Prepare( passwordBytes, passwordLength, preparedPassword );
	HmacSha256Prepared( preparedPassword, firstMessage,
		pbkdfSaltBytes + 4, iteration );
	memcpy( aggregate, iteration, sizeof( aggregate ) );
	for ( uint32_t round = 1; round < MP_REFEREE_AUTH_PBKDF2_ITERATIONS; ++round ) {
		uint8_t next[ 32 ];
		HmacSha256Prepared( preparedPassword, iteration,
			sizeof( iteration ), next );
		memcpy( iteration, next, sizeof( iteration ) );
		for ( int index = 0; index < 32; ++index ) {
			aggregate[ index ] = static_cast<uint8_t>( aggregate[ index ] ^ next[ index ] );
		}
		MPRefereeAuthSecureZero( next, sizeof( next ) );
	}
	memcpy( verifier.bytes, aggregate, sizeof( verifier.bytes ) );

	MPRefereeAuthSecureZero( pbkdfSalt, sizeof( pbkdfSalt ) );
	MPRefereeAuthSecureZero( firstMessage, sizeof( firstMessage ) );
	MPRefereeAuthSecureZero( iteration, sizeof( iteration ) );
	MPRefereeAuthSecureZero( aggregate, sizeof( aggregate ) );
	MPRefereeAuthSecureZero( &preparedPassword, sizeof( preparedPassword ) );
	return true;
}

bool MPRefereeAuthBuildProofFromPassword( const mpRefereeAuthChallenge &challenge,
	const void *passwordBytes, size_t passwordLength, mpRefereeAuthProof &proof ) {
	MPRefereeAuthSecureZero( proof.bytes, sizeof( proof.bytes ) );
	if ( !ChallengeIsStructurallyValid( challenge ) ) {
		return false;
	}
	mpRefereeAuthVerifier verifier;
	if ( !MPRefereeAuthDeriveVerifier( passwordBytes, passwordLength,
		challenge.salt, verifier ) ) {
		return false;
	}
	BuildProofUnchecked( challenge, verifier.bytes, proof );
	MPRefereeAuthSecureZero( verifier.bytes, sizeof( verifier.bytes ) );
	return true;
}

bool MPRefereeAuthBuildProofFromVerifier( const mpRefereeAuthChallenge &challenge,
	const mpRefereeAuthVerifier &verifier, mpRefereeAuthProof &proof ) {
	MPRefereeAuthSecureZero( proof.bytes, sizeof( proof.bytes ) );
	if ( !ChallengeIsStructurallyValid( challenge ) ||
		IsAllZero( verifier.bytes, sizeof( verifier.bytes ) ) ) {
		return false;
	}
	BuildProofUnchecked( challenge, verifier.bytes, proof );
	return true;
}

bool MPRefereeAuthEncodeChallenge( const mpRefereeAuthChallenge &challenge,
	uint8_t *output, size_t outputBytes ) {
	if ( output == NULL || outputBytes != MP_REFEREE_AUTH_CHALLENGE_WIRE_BYTES ||
		!ChallengeIsStructurallyValid( challenge ) ) {
		return false;
	}
	uint8_t encoded[ MP_REFEREE_AUTH_CHALLENGE_WIRE_BYTES ];
	EncodeChallengeUnchecked( challenge, encoded );
	memcpy( output, encoded, sizeof( encoded ) );
	MPRefereeAuthSecureZero( encoded, sizeof( encoded ) );
	return true;
}

bool MPRefereeAuthDecodeChallenge( const uint8_t *input, size_t inputBytes,
	mpRefereeAuthChallenge &challenge ) {
	challenge.Clear();
	if ( input == NULL || inputBytes != MP_REFEREE_AUTH_CHALLENGE_WIRE_BYTES ||
		!ConstantTimeEqual( input, REFEREE_CHALLENGE_MAGIC,
			sizeof( REFEREE_CHALLENGE_MAGIC ) ) ) {
		return false;
	}

	mpRefereeAuthChallenge decoded;
	decoded.Clear();
	size_t offset = sizeof( REFEREE_CHALLENGE_MAGIC );
	decoded.wireVersion = static_cast<uint16_t>(
		( static_cast<uint16_t>( input[ offset ] ) << 8 ) | input[ offset + 1 ] );
	offset += 2;
	decoded.algorithm = static_cast<uint16_t>(
		( static_cast<uint16_t>( input[ offset ] ) << 8 ) | input[ offset + 1 ] );
	offset += 2;
	decoded.iterationCount = LoadBigEndian32( input + offset );
	offset += 4;
	decoded.binding.sessionId = LoadBigEndian64( input + offset );
	offset += 8;
	decoded.binding.participantSequence = LoadBigEndian32( input + offset );
	offset += 4;
	decoded.binding.slotGeneration = LoadBigEndian32( input + offset );
	offset += 4;
	const uint32_t wireSlot = LoadBigEndian32( input + offset );
	offset += 4;
	if ( wireSlot >= static_cast<uint32_t>( MP_REFEREE_AUTH_MAX_SLOTS ) ) {
		return false;
	}
	decoded.binding.slot = static_cast<int>( wireSlot );
	decoded.challengeGeneration = LoadBigEndian64( input + offset );
	offset += 8;
	const uint64_t wireExpiry = LoadBigEndian64( input + offset );
	offset += 8;
	if ( wireExpiry > static_cast<uint64_t>( INT64_MAX ) ) {
		return false;
	}
	decoded.expiresAtEngineMsec = static_cast<int64_t>( wireExpiry );
	memcpy( decoded.salt.bytes, input + offset, sizeof( decoded.salt.bytes ) );
	offset += sizeof( decoded.salt.bytes );
	memcpy( decoded.nonce.bytes, input + offset, sizeof( decoded.nonce.bytes ) );
	offset += sizeof( decoded.nonce.bytes );
	(void)offset;

	if ( !ChallengeIsStructurallyValid( decoded ) ) {
		decoded.Clear();
		return false;
	}
	challenge = decoded;
	decoded.Clear();
	return true;
}

bool MPRefereeAuthProofToHex( const mpRefereeAuthProof &proof,
	char *output, size_t outputBytes ) {
	static const char digits[] = "0123456789abcdef";
	if ( output == NULL || outputBytes != MP_REFEREE_AUTH_PROOF_HEX_BYTES + 1 ) {
		return false;
	}
	for ( int index = 0; index < MP_REFEREE_AUTH_PROOF_BYTES; ++index ) {
		output[ index * 2 ] = digits[ proof.bytes[ index ] >> 4 ];
		output[ index * 2 + 1 ] = digits[ proof.bytes[ index ] & 15u ];
	}
	output[ MP_REFEREE_AUTH_PROOF_HEX_BYTES ] = '\0';
	return true;
}

bool MPRefereeAuthProofFromHex( const char *input, size_t inputBytes,
	mpRefereeAuthProof &proof ) {
	MPRefereeAuthSecureZero( proof.bytes, sizeof( proof.bytes ) );
	if ( input == NULL || inputBytes != MP_REFEREE_AUTH_PROOF_HEX_BYTES ) {
		return false;
	}
	uint8_t decoded[ MP_REFEREE_AUTH_PROOF_BYTES ];
	uint8_t valid = 1;
	for ( int index = 0; index < MP_REFEREE_AUTH_PROOF_BYTES; ++index ) {
		const uint8_t high = HexValue( input[ index * 2 ], valid );
		const uint8_t low = HexValue( input[ index * 2 + 1 ], valid );
		decoded[ index ] = static_cast<uint8_t>( ( high << 4 ) | low );
	}
	if ( valid == 0 ) {
		MPRefereeAuthSecureZero( decoded, sizeof( decoded ) );
		return false;
	}
	memcpy( proof.bytes, decoded, sizeof( proof.bytes ) );
	MPRefereeAuthSecureZero( decoded, sizeof( decoded ) );
	return true;
}

mpRefereeAuthenticationService::mpRefereeAuthenticationService( void ) :
	currentSessionId( 0 ),
	lastChallengeGeneration( 0 ),
	lastObservedEngineTime( -1 ),
	credentialInstalled( false ) {
	memset( &credentialSalt, 0, sizeof( credentialSalt ) );
	memset( &credentialVerifier, 0, sizeof( credentialVerifier ) );
	ClearChallengesAndRateState();
}

mpRefereeAuthenticationService::~mpRefereeAuthenticationService( void ) {
	ClearCredential();
	MPRefereeAuthSecureZero( slots, sizeof( slots ) );
	currentSessionId = 0;
	lastChallengeGeneration = 0;
	lastObservedEngineTime = -1;
}

bool mpRefereeAuthenticationService::InstallCredentialVerifier(
	const mpRefereeAuthSalt &salt, const mpRefereeAuthVerifier &verifier ) {
	if ( IsAllZero( salt.bytes, sizeof( salt.bytes ) ) ||
		IsAllZero( verifier.bytes, sizeof( verifier.bytes ) ) ) {
		return false;
	}
	MPRefereeAuthSecureZero( credentialSalt.bytes, sizeof( credentialSalt.bytes ) );
	MPRefereeAuthSecureZero( credentialVerifier.bytes, sizeof( credentialVerifier.bytes ) );
	memcpy( credentialSalt.bytes, salt.bytes, sizeof( credentialSalt.bytes ) );
	memcpy( credentialVerifier.bytes, verifier.bytes, sizeof( credentialVerifier.bytes ) );
	credentialInstalled = true;
	ClearChallengesAndRateState();
	return true;
}

bool mpRefereeAuthenticationService::InstallCredentialFromPassword(
	const void *passwordBytes, size_t passwordLength, const mpRefereeAuthSalt &salt ) {
	mpRefereeAuthVerifier derivedVerifier;
	if ( !MPRefereeAuthDeriveVerifier( passwordBytes, passwordLength,
		salt, derivedVerifier ) ) {
		return false;
	}
	const bool installed = InstallCredentialVerifier( salt, derivedVerifier );
	MPRefereeAuthSecureZero( derivedVerifier.bytes, sizeof( derivedVerifier.bytes ) );
	return installed;
}

void mpRefereeAuthenticationService::ClearCredential( void ) {
	credentialInstalled = false;
	MPRefereeAuthSecureZero( credentialSalt.bytes, sizeof( credentialSalt.bytes ) );
	MPRefereeAuthSecureZero( credentialVerifier.bytes, sizeof( credentialVerifier.bytes ) );
	ClearChallengesAndRateState();
}

bool mpRefereeAuthenticationService::BeginSession( uint64_t sessionId,
	int64_t engineTimeMsec ) {
	if ( sessionId == 0 || engineTimeMsec < 0 || sessionId == currentSessionId ) {
		return false;
	}
	currentSessionId = sessionId;
	lastObservedEngineTime = engineTimeMsec;
	ClearChallengesAndRateState();
	return true;
}

mpRefereeAuthChallengeResult_t mpRefereeAuthenticationService::IssueChallenge(
	const mpRefereeAuthBinding &binding, int64_t engineTimeMsec,
	const mpRefereeAuthNonce &adapterRandomNonce,
	mpRefereeAuthChallenge &challenge ) {
	challenge.Clear();
	if ( !BindingIsCurrentAndValid( binding ) || !ObserveEngineTime( engineTimeMsec ) ||
		!credentialInstalled ||
		IsAllZero( adapterRandomNonce.bytes, sizeof( adapterRandomNonce.bytes ) ) ) {
		return MP_REFEREE_AUTH_CHALLENGE_REJECTED;
	}

	SlotRecord &record = slots[ binding.slot ];
	if ( record.lockedUntil > engineTimeMsec ) {
		return MP_REFEREE_AUTH_CHALLENGE_THROTTLED;
	}
	if ( record.lockedUntil >= 0 && record.lockedUntil <= engineTimeMsec ) {
		record.lockedUntil = -1;
		record.failedAttempts = 0;
		record.failureWindowStartedAt = -1;
	}
	if ( record.failureWindowStartedAt >= 0 &&
		engineTimeMsec - record.failureWindowStartedAt >=
			MP_REFEREE_AUTH_FAILURE_WINDOW_MSEC ) {
		record.failedAttempts = 0;
		record.failureWindowStartedAt = -1;
	}
	if ( record.lastChallengeIssuedAt >= 0 &&
		engineTimeMsec - record.lastChallengeIssuedAt <
			MP_REFEREE_AUTH_MIN_CHALLENGE_INTERVAL_MSEC ) {
		return MP_REFEREE_AUTH_CHALLENGE_THROTTLED;
	}
	if ( lastChallengeGeneration == UINT64_MAX ) {
		return MP_REFEREE_AUTH_CHALLENGE_REJECTED;
	}
	int64_t expiry = 0;
	if ( !AddEngineDuration( engineTimeMsec,
		MP_REFEREE_AUTH_CHALLENGE_LIFETIME_MSEC, expiry ) ) {
		return MP_REFEREE_AUTH_CHALLENGE_REJECTED;
	}

	challenge.wireVersion = MP_REFEREE_AUTH_WIRE_VERSION;
	challenge.algorithm = MP_REFEREE_AUTH_ALGORITHM_PBKDF2_HMAC_SHA256;
	challenge.iterationCount = MP_REFEREE_AUTH_PBKDF2_ITERATIONS;
	challenge.binding = binding;
	challenge.challengeGeneration = ++lastChallengeGeneration;
	challenge.expiresAtEngineMsec = expiry;
	challenge.salt = credentialSalt;
	challenge.nonce = adapterRandomNonce;

	record.challenge.Clear();
	record.challenge = challenge;
	record.active = true;
	record.lastChallengeIssuedAt = engineTimeMsec;
	return MP_REFEREE_AUTH_CHALLENGE_ISSUED;
}

mpRefereeAuthVerifyResult_t mpRefereeAuthenticationService::VerifyProof(
	const mpRefereeAuthBinding &trustedBinding, int64_t engineTimeMsec,
	uint64_t challengeGeneration, const mpRefereeAuthProof &proof ) {
	const bool slotInRange = trustedBinding.slot >= 0 &&
		trustedBinding.slot < MP_REFEREE_AUTH_MAX_SLOTS;
	SlotRecord *record = slotInRange ? &slots[ trustedBinding.slot ] : NULL;

	mpRefereeAuthChallenge proofChallenge;
	proofChallenge.Clear();
	if ( record != NULL ) {
		proofChallenge = record->challenge;
	} else {
		proofChallenge.wireVersion = MP_REFEREE_AUTH_WIRE_VERSION;
		proofChallenge.algorithm = MP_REFEREE_AUTH_ALGORITHM_PBKDF2_HMAC_SHA256;
		proofChallenge.iterationCount = MP_REFEREE_AUTH_PBKDF2_ITERATIONS;
		proofChallenge.binding.sessionId = trustedBinding.sessionId != 0 ?
			trustedBinding.sessionId : 1;
		proofChallenge.binding.participantSequence =
			trustedBinding.participantSequence != 0 ?
			trustedBinding.participantSequence : 1;
		proofChallenge.binding.slot = 0;
		proofChallenge.binding.slotGeneration = trustedBinding.slotGeneration != 0 ?
			trustedBinding.slotGeneration : 1;
		proofChallenge.challengeGeneration = challengeGeneration != 0 ?
			challengeGeneration : 1;
		proofChallenge.expiresAtEngineMsec = engineTimeMsec >= 0 ? engineTimeMsec : 0;
		memset( proofChallenge.salt.bytes, 0x5a,
			sizeof( proofChallenge.salt.bytes ) );
		memset( proofChallenge.nonce.bytes, 0xa5,
			sizeof( proofChallenge.nonce.bytes ) );
	}

	mpRefereeAuthProof expectedProof;
	BuildProofUnchecked( proofChallenge,
		credentialInstalled ? credentialVerifier.bytes : REFEREE_UNAVAILABLE_VERIFIER,
		expectedProof );
	const bool proofMatches = ConstantTimeEqual( expectedProof.bytes, proof.bytes,
		sizeof( expectedProof.bytes ) );
	MPRefereeAuthSecureZero( expectedProof.bytes, sizeof( expectedProof.bytes ) );
	proofChallenge.Clear();

	const bool timeAccepted = ObserveEngineTime( engineTimeMsec );
	const bool bindingAccepted = BindingIsCurrentAndValid( trustedBinding );
	const bool challengeMatches = record != NULL && record->active &&
		SameBinding( record->challenge.binding, trustedBinding ) &&
		record->challenge.challengeGeneration == challengeGeneration;
	const bool wasLocked = record != NULL && record->lockedUntil > engineTimeMsec;
	const bool unexpired = challengeMatches && engineTimeMsec >= 0 &&
		engineTimeMsec < record->challenge.expiresAtEngineMsec;

	// A correctly addressed challenge is consumed before any proof decision.
	// This makes success, failure and expiry equally single-use.
	if ( challengeMatches ) {
		record->active = false;
	}
	if ( record != NULL && record->active &&
		!SameBinding( record->challenge.binding, trustedBinding ) ) {
		record->active = false;
	}

	if ( wasLocked ) {
		return MP_REFEREE_AUTH_VERIFY_THROTTLED;
	}
	if ( timeAccepted && bindingAccepted && challengeMatches && unexpired &&
		credentialInstalled && proofMatches ) {
		record->failedAttempts = 0;
		record->failureWindowStartedAt = -1;
		record->lockedUntil = -1;
		return MP_REFEREE_AUTH_VERIFY_AUTHENTICATED;
	}
	if ( timeAccepted && bindingAccepted && challengeMatches && unexpired &&
		record != NULL ) {
		RegisterFailure( *record, engineTimeMsec );
	}
	return MP_REFEREE_AUTH_VERIFY_REJECTED;
}

void mpRefereeAuthenticationService::InvalidateSlot( int slot ) {
	if ( slot < 0 || slot >= MP_REFEREE_AUTH_MAX_SLOTS ) {
		return;
	}
	slots[ slot ].challenge.Clear();
	slots[ slot ].active = false;
	// Throttle state intentionally survives a reconnect/slot-generation change.
}

int64_t mpRefereeAuthenticationService::RetryAfterMsec( int slot,
	int64_t engineTimeMsec ) const {
	if ( slot < 0 || slot >= MP_REFEREE_AUTH_MAX_SLOTS || engineTimeMsec < 0 ) {
		return 0;
	}
	const SlotRecord &record = slots[ slot ];
	int64_t retry = record.lockedUntil > engineTimeMsec ?
		record.lockedUntil - engineTimeMsec : 0;
	if ( record.lastChallengeIssuedAt >= 0 &&
		engineTimeMsec >= record.lastChallengeIssuedAt ) {
		const int64_t issuanceElapsed = engineTimeMsec - record.lastChallengeIssuedAt;
		if ( issuanceElapsed < MP_REFEREE_AUTH_MIN_CHALLENGE_INTERVAL_MSEC ) {
			const int64_t issuanceRetry =
				MP_REFEREE_AUTH_MIN_CHALLENGE_INTERVAL_MSEC - issuanceElapsed;
			if ( issuanceRetry > retry ) {
				retry = issuanceRetry;
			}
		}
	}
	return retry;
}

uint64_t mpRefereeAuthenticationService::GetLastChallengeGeneration( void ) const {
	return lastChallengeGeneration;
}

bool mpRefereeAuthenticationService::ObserveEngineTime( int64_t engineTimeMsec ) {
	if ( currentSessionId == 0 || engineTimeMsec < 0 ||
		engineTimeMsec < lastObservedEngineTime ) {
		return false;
	}
	lastObservedEngineTime = engineTimeMsec;
	return true;
}

bool mpRefereeAuthenticationService::BindingIsCurrentAndValid(
	const mpRefereeAuthBinding &binding ) const {
	return BindingIsStructurallyValid( binding ) &&
		binding.sessionId == currentSessionId;
}

void mpRefereeAuthenticationService::ClearChallengesAndRateState( void ) {
	for ( int slot = 0; slot < MP_REFEREE_AUTH_MAX_SLOTS; ++slot ) {
		slots[ slot ].challenge.Clear();
		slots[ slot ].active = false;
		slots[ slot ].failedAttempts = 0;
		slots[ slot ].failureWindowStartedAt = -1;
		slots[ slot ].lockedUntil = -1;
		slots[ slot ].lastChallengeIssuedAt = -1;
	}
}

void mpRefereeAuthenticationService::RegisterFailure( SlotRecord &record,
	int64_t engineTimeMsec ) {
	if ( record.failureWindowStartedAt < 0 ||
		engineTimeMsec < record.failureWindowStartedAt ||
		engineTimeMsec - record.failureWindowStartedAt >=
			MP_REFEREE_AUTH_FAILURE_WINDOW_MSEC ) {
		record.failureWindowStartedAt = engineTimeMsec;
		record.failedAttempts = 0;
	}
	if ( record.failedAttempts < UINT32_MAX ) {
		++record.failedAttempts;
	}
	if ( record.failedAttempts >= MP_REFEREE_AUTH_FAILURES_BEFORE_LOCKOUT ) {
		int64_t lockoutExpiry = INT64_MAX;
		(void)AddEngineDuration( engineTimeMsec, MP_REFEREE_AUTH_LOCKOUT_MSEC,
			lockoutExpiry );
		record.lockedUntil = lockoutExpiry;
	}
}
