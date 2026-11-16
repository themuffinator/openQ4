/*
===========================================================================

openQ4 Ed25519 signature verification
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

===========================================================================
*/

#ifndef __ED25519_H__
#define __ED25519_H__

#include <cstddef>
#include <cstdint>

namespace idCrypto {

static constexpr std::size_t SHA512_DIGEST_BYTES = 64;
static constexpr std::size_t ED25519_PUBLIC_KEY_BYTES = 32;
static constexpr std::size_t ED25519_SIGNATURE_BYTES = 64;

// FIPS 180-4 SHA-512, which Ed25519 is defined over.
void SHA512( const void *data, std::size_t dataBytes,
	std::uint8_t digest[ SHA512_DIGEST_BYTES ] );

// RFC 8032 Ed25519 (pure, no context) verification. Rejects a signature whose
// scalar is not reduced (S >= L) and a public key that does not decode to a
// curve point. Verification only: openQ4 never holds a signing key, so there
// is no secret data here and no constant-time requirement beyond the final
// comparison. The arithmetic follows TweetNaCl (Bernstein, van Gastel,
// Janssen, Lange, Schwabe, Smetsers; public domain).
bool Ed25519Verify( const std::uint8_t signature[ ED25519_SIGNATURE_BYTES ],
	const void *message, std::size_t messageBytes,
	const std::uint8_t publicKey[ ED25519_PUBLIC_KEY_BYTES ] );

} // namespace idCrypto

#endif /* !__ED25519_H__ */
