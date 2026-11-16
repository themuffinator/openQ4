/*
===========================================================================

openQ4 cryptographic hash primitives
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#ifndef __CRYPTOHASH_H__
#define __CRYPTOHASH_H__

#include <cstddef>
#include <cstdint>

namespace idCrypto {

static constexpr std::size_t SHA256_DIGEST_BYTES = 32;
static constexpr std::size_t SHA256_BLOCK_BYTES = 64;

// These routines implement the algorithms specified by FIPS 180-4, RFC 2104,
// and RFC 8018. They deliberately have no dependency on engine globals so the
// exact primitives can be exercised by the native safety test target.
void SHA256( const void *data, std::size_t dataBytes,
	std::uint8_t digest[ SHA256_DIGEST_BYTES ] );
// The same digest through the portable compression only. SHA256 itself uses
// the processor's SHA instructions when it has them; tests compare the two.
void SHA256Portable( const void *data, std::size_t dataBytes,
	std::uint8_t digest[ SHA256_DIGEST_BYTES ] );
// "x86 SHA extensions", "Armv8 SHA-256 instructions" or "portable": which
// compression SHA256 runs on this machine.
const char *SHA256Implementation( void );

// The same digest over data that arrives in pieces, such as a download or a
// file too large to hold in memory.
class SHA256Stream {
public:
				SHA256Stream( void );
				~SHA256Stream( void );
				SHA256Stream( const SHA256Stream & ) = delete;
	SHA256Stream &	operator=( const SHA256Stream & ) = delete;

	void		Update( const void *data, std::size_t dataBytes );
	// Writes the digest; the stream starts over afterwards.
	void		Final( std::uint8_t digest[ SHA256_DIGEST_BYTES ] );

private:
	alignas( 8 ) std::uint8_t	context[ 128 ];
};

void HMACSHA256( const void *key, std::size_t keyBytes,
	const void *data, std::size_t dataBytes,
	std::uint8_t digest[ SHA256_DIGEST_BYTES ] );

// openQ4 only needs one SHA-256-sized PBKDF2 block. Keeping the interface
// bounded prevents accidental unbounded work or counter-wrap mistakes.
bool PBKDF2HMACSHA256( const void *password, std::size_t passwordBytes,
	const void *salt, std::size_t saltBytes, std::uint32_t iterations,
	void *output, std::size_t outputBytes );

bool ConstantTimeEquals( const void *left, const void *right, std::size_t bytes );
void SecureZero( void *memory, std::size_t bytes );

} // namespace idCrypto

#endif /* !__CRYPTOHASH_H__ */
