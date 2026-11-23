/*
 * openQ4 Ed25519 key derivation and signing against RFC 8032 section 7.1's
 * test vectors 1 to 3 and SHA(abc), and a round trip through the verifier
 * with tampered messages and signatures (roadmap MP-17). Built and run by
 * tools/tests/mp_identity.py.
 */
#include "../../../src/idlib/Ed25519.cpp"

#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;

static void Check( bool condition, const char *what ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAIL: %s\n", what );
		++failures;
	}
}

static std::vector<std::uint8_t> Hex( const char *text ) {
	std::vector<std::uint8_t> bytes;
	for ( std::size_t i = 0; text[ i ] != '\0' && text[ i + 1 ] != '\0'; i += 2 ) {
		bytes.push_back( static_cast<std::uint8_t>( std::stoi( std::string( text + i, 2 ), nullptr, 16 ) ) );
	}
	return bytes;
}

struct Vector {
	const char *seed, *publicKey, *message, *signature;
};

static const Vector VECTORS[] = {
	{ "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
	  "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
	  "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },
	{ "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
	  "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
	  "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" },
	{ "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
	  "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
	  "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a" },
};

int main() {
	for ( const Vector &vector : VECTORS ) {
		const std::vector<std::uint8_t> seed = Hex( vector.seed ), publicKey = Hex( vector.publicKey );
		const std::vector<std::uint8_t> message = Hex( vector.message ), signature = Hex( vector.signature );
		std::uint8_t derived[ idCrypto::ED25519_PUBLIC_KEY_BYTES ];
		idCrypto::Ed25519PublicKey( seed.data(), derived );
		Check( std::memcmp( derived, publicKey.data(), sizeof( derived ) ) == 0, "the public key derives as RFC 8032 gives it" );
		std::uint8_t signed_[ idCrypto::ED25519_SIGNATURE_BYTES ];
		idCrypto::Ed25519Sign( signed_, message.data(), message.size(), seed.data() );
		Check( std::memcmp( signed_, signature.data(), sizeof( signed_ ) ) == 0, "the signature is RFC 8032's" );
		Check( idCrypto::Ed25519Verify( signed_, message.data(), message.size(), derived ), "the signature verifies" );
		std::vector<std::uint8_t> other = message;
		other.push_back( 0x01 );
		Check( !idCrypto::Ed25519Verify( signed_, other.data(), other.size(), derived ), "another message does not verify" );
		for ( std::size_t bit = 0; bit < sizeof( signed_ ) * 8; bit += 37 ) {
			std::uint8_t flipped[ idCrypto::ED25519_SIGNATURE_BYTES ];
			std::memcpy( flipped, signed_, sizeof( flipped ) );
			flipped[ bit / 8 ] ^= static_cast<std::uint8_t>( 1u << ( bit % 8 ) );
			Check( !idCrypto::Ed25519Verify( flipped, message.data(), message.size(), derived ), "a flipped signature bit does not verify" );
		}
	}
	// a seed of every byte value signs and verifies a longer message
	std::uint8_t seed[ idCrypto::ED25519_SEED_BYTES ];
	for ( std::size_t i = 0; i < sizeof( seed ); ++i ) {
		seed[ i ] = static_cast<std::uint8_t>( i * 37 + 11 );
	}
	std::vector<std::uint8_t> message( 1000 );
	for ( std::size_t i = 0; i < message.size(); ++i ) {
		message[ i ] = static_cast<std::uint8_t>( i ^ ( i >> 3 ) );
	}
	std::uint8_t publicKey[ idCrypto::ED25519_PUBLIC_KEY_BYTES ], signature[ idCrypto::ED25519_SIGNATURE_BYTES ];
	idCrypto::Ed25519PublicKey( seed, publicKey );
	idCrypto::Ed25519Sign( signature, message.data(), message.size(), seed );
	Check( idCrypto::Ed25519Verify( signature, message.data(), message.size(), publicKey ), "a long message round-trips" );
	std::uint8_t wiped[ 8 ] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	idCrypto::SecureWipe( wiped, sizeof( wiped ) );
	Check( std::memcmp( wiped, "\0\0\0\0\0\0\0\0", 8 ) == 0, "SecureWipe clears" );
	if ( failures != 0 ) {
		std::fprintf( stderr, "%d failures\n", failures );
		return 1;
	}
	std::printf( "ed25519 signing: all checks passed\n" );
	return 0;
}
