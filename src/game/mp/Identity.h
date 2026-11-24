//----------------------------------------------------------------
// Identity.h
//
// openQ4 player identity (roadmap MP-17), with no engine dependency.
//
// A player's game keeps one random 32-byte seed in its save folder and never
// sends it. For each server it derives a separate Ed25519 key from that seed
// and the server's own random id (si_identityId), so two servers see two
// unrelated keys and cannot link the same player; the user decided on
// 23 November 2026 that this identity is on unless the player turns it off.
// The server proves the player holds the key with a one-time challenge: a
// nonce bound to the server's id and the player's slot, signed with the key.
// The public key's fingerprint, sixteen hex digits, is how the server knows
// the player again: in status, in identity bans, when they reconnect, and in
// its local ranking table.
//
// Also here: the Elo ratings of that table, and the store that gives a
// returning player their team and score back (by identity, or by the ghost
// code the server told them when they joined).
//
// tools/tests/native/MpIdentityTest.cpp compiles this alone.
//----------------------------------------------------------------

#ifndef __GAME_MP_IDENTITY_H__
#define __GAME_MP_IDENTITY_H__

#include <math.h>
#include <string.h>

#include "../../idlib/Ed25519.h"

typedef long long mpIdTime_t;		// seconds since 1970-01-01 UTC, as mpModTime_t

static const int MP_ID_SEED_BYTES = 32;
static const int MP_ID_SERVER_ID_BYTES = 16;
static const int MP_ID_NONCE_BYTES = 32;
static const int MP_ID_PUBLIC_KEY_BYTES = 32;
static const int MP_ID_SIGNATURE_BYTES = 64;
static const int MP_ID_FINGERPRINT_BYTES = 8;
static const int MP_ID_FINGERPRINT_TEXT_BYTES = MP_ID_FINGERPRINT_BYTES * 2 + 1;
static const int MP_ID_SERVER_ID_TEXT_BYTES = MP_ID_SERVER_ID_BYTES * 2 + 1;
static const int MP_ID_GHOST_CODE_LENGTH = 6;		// letters a player types: "ghost KTRWQM"
static const int MP_ID_KEY_TEXT_BYTES = 24;			// "id:<16 hex>" or "ghost:<6 letters>"

// Domain separation: the seed derivation and the signed proof can never be
// confused with each other or with another protocol's use of the same key.
#define MP_ID_SEED_DOMAIN		"openQ4 identity seed v1"
#define MP_ID_PROOF_DOMAIN		"openQ4 identity proof v1"
static const int MP_ID_SEED_DOMAIN_BYTES = sizeof( MP_ID_SEED_DOMAIN );		// the nul included
static const int MP_ID_PROOF_DOMAIN_BYTES = sizeof( MP_ID_PROOF_DOMAIN );
static const int MP_ID_PROOF_MESSAGE_BYTES = MP_ID_PROOF_DOMAIN_BYTES + MP_ID_SERVER_ID_BYTES + MP_ID_NONCE_BYTES + 4 + 4;

//----------------------------------------------------------------
// hex text
//----------------------------------------------------------------

inline void MPIdHexEncode( const unsigned char *bytes, int count, char *out ) {
	static const char digits[] = "0123456789abcdef";
	for ( int i = 0; i < count; i++ ) {
		out[ i * 2 ] = digits[ bytes[ i ] >> 4 ];
		out[ i * 2 + 1 ] = digits[ bytes[ i ] & 15 ];
	}
	out[ count * 2 ] = '\0';
}

inline int MPIdHexDigit( char c ) {
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return c - 'a' + 10;
	}
	if ( c >= 'A' && c <= 'F' ) {
		return c - 'A' + 10;
	}
	return -1;
}

// Exactly count bytes of hex digits, nothing before or after.
inline bool MPIdHexDecode( const char *text, unsigned char *out, int count ) {
	if ( text == 0 || (int)strlen( text ) != count * 2 ) {
		return false;
	}
	for ( int i = 0; i < count; i++ ) {
		const int high = MPIdHexDigit( text[ i * 2 ] ), low = MPIdHexDigit( text[ i * 2 + 1 ] );
		if ( high < 0 || low < 0 ) {
			return false;
		}
		out[ i ] = static_cast<unsigned char>( high * 16 + low );
	}
	return true;
}

//----------------------------------------------------------------
// keys, proofs and fingerprints
//----------------------------------------------------------------

// The seed the player's key for one server is derived from:
// SHA-512( domain || seed || server id ), its first 32 bytes.
inline void MPIdServerSeed( const unsigned char seed[ MP_ID_SEED_BYTES ], const unsigned char serverId[ MP_ID_SERVER_ID_BYTES ],
		unsigned char out[ MP_ID_SEED_BYTES ] ) {
	unsigned char buffer[ MP_ID_SEED_DOMAIN_BYTES + MP_ID_SEED_BYTES + MP_ID_SERVER_ID_BYTES ];
	memcpy( buffer, MP_ID_SEED_DOMAIN, MP_ID_SEED_DOMAIN_BYTES );
	memcpy( buffer + MP_ID_SEED_DOMAIN_BYTES, seed, MP_ID_SEED_BYTES );
	memcpy( buffer + MP_ID_SEED_DOMAIN_BYTES + MP_ID_SEED_BYTES, serverId, MP_ID_SERVER_ID_BYTES );
	unsigned char digest[ idCrypto::SHA512_DIGEST_BYTES ];
	idCrypto::SHA512( buffer, sizeof( buffer ), digest );
	memcpy( out, digest, MP_ID_SEED_BYTES );
	idCrypto::SecureWipe( buffer, sizeof( buffer ) );
	idCrypto::SecureWipe( digest, sizeof( digest ) );
}

inline void MPIdPutUint32( unsigned char *out, unsigned int value ) {
	out[ 0 ] = static_cast<unsigned char>( value );
	out[ 1 ] = static_cast<unsigned char>( value >> 8 );
	out[ 2 ] = static_cast<unsigned char>( value >> 16 );
	out[ 3 ] = static_cast<unsigned char>( value >> 24 );
}

// What the player signs: the domain, the server's id, the challenge's nonce,
// the slot it was issued to and that connection's number, so a proof answers
// one challenge on one server for one connection and nothing else.
inline void MPIdProofMessage( const unsigned char serverId[ MP_ID_SERVER_ID_BYTES ], const unsigned char nonce[ MP_ID_NONCE_BYTES ],
		int slot, unsigned int connection, unsigned char out[ MP_ID_PROOF_MESSAGE_BYTES ] ) {
	memcpy( out, MP_ID_PROOF_DOMAIN, MP_ID_PROOF_DOMAIN_BYTES );
	memcpy( out + MP_ID_PROOF_DOMAIN_BYTES, serverId, MP_ID_SERVER_ID_BYTES );
	memcpy( out + MP_ID_PROOF_DOMAIN_BYTES + MP_ID_SERVER_ID_BYTES, nonce, MP_ID_NONCE_BYTES );
	MPIdPutUint32( out + MP_ID_PROOF_DOMAIN_BYTES + MP_ID_SERVER_ID_BYTES + MP_ID_NONCE_BYTES, static_cast<unsigned int>( slot ) );
	MPIdPutUint32( out + MP_ID_PROOF_DOMAIN_BYTES + MP_ID_SERVER_ID_BYTES + MP_ID_NONCE_BYTES + 4, connection );
}

// The player's side: the key for this server, and its signature over the proof.
inline void MPIdSignProof( const unsigned char seed[ MP_ID_SEED_BYTES ], const unsigned char serverId[ MP_ID_SERVER_ID_BYTES ],
		const unsigned char nonce[ MP_ID_NONCE_BYTES ], int slot, unsigned int connection,
		unsigned char publicKey[ MP_ID_PUBLIC_KEY_BYTES ], unsigned char signature[ MP_ID_SIGNATURE_BYTES ] ) {
	unsigned char serverSeed[ MP_ID_SEED_BYTES ];
	unsigned char message[ MP_ID_PROOF_MESSAGE_BYTES ];
	MPIdServerSeed( seed, serverId, serverSeed );
	MPIdProofMessage( serverId, nonce, slot, connection, message );
	idCrypto::Ed25519PublicKey( serverSeed, publicKey );
	idCrypto::Ed25519Sign( signature, message, sizeof( message ), serverSeed );
	idCrypto::SecureWipe( serverSeed, sizeof( serverSeed ) );
}

// The server's side.
inline bool MPIdVerifyProof( const unsigned char serverId[ MP_ID_SERVER_ID_BYTES ], const unsigned char nonce[ MP_ID_NONCE_BYTES ],
		int slot, unsigned int connection, const unsigned char publicKey[ MP_ID_PUBLIC_KEY_BYTES ],
		const unsigned char signature[ MP_ID_SIGNATURE_BYTES ] ) {
	unsigned char message[ MP_ID_PROOF_MESSAGE_BYTES ];
	MPIdProofMessage( serverId, nonce, slot, connection, message );
	return idCrypto::Ed25519Verify( signature, message, sizeof( message ), publicKey );
}

// Sixteen hex digits: the first eight bytes of SHA-512( public key ).
inline void MPIdFingerprint( const unsigned char publicKey[ MP_ID_PUBLIC_KEY_BYTES ], char out[ MP_ID_FINGERPRINT_TEXT_BYTES ] ) {
	unsigned char digest[ idCrypto::SHA512_DIGEST_BYTES ];
	idCrypto::SHA512( publicKey, MP_ID_PUBLIC_KEY_BYTES, digest );
	MPIdHexEncode( digest, MP_ID_FINGERPRINT_BYTES, out );
}

// A fingerprint as an operator types it: sixteen hex digits, any case, read
// back in lower case.
inline bool MPIdParseFingerprint( const char *text, char out[ MP_ID_FINGERPRINT_TEXT_BYTES ] ) {
	unsigned char bytes[ MP_ID_FINGERPRINT_BYTES ];
	if ( !MPIdHexDecode( text, bytes, MP_ID_FINGERPRINT_BYTES ) ) {
		return false;
	}
	MPIdHexEncode( bytes, MP_ID_FINGERPRINT_BYTES, out );
	return true;
}

//----------------------------------------------------------------
// ghost codes: six letters a returning player types
//----------------------------------------------------------------

// Letters that are hard to misread or mistype: no I, L, O, U or V.
#define MP_ID_GHOST_ALPHABET	"ABCDEFGHJKMNPQRSTWXYZ"
static const int MP_ID_GHOST_ALPHABET_LENGTH = sizeof( MP_ID_GHOST_ALPHABET ) - 1;

inline void MPIdGhostCode( const unsigned char random[ MP_ID_GHOST_CODE_LENGTH ], char out[ MP_ID_GHOST_CODE_LENGTH + 1 ] ) {
	for ( int i = 0; i < MP_ID_GHOST_CODE_LENGTH; i++ ) {
		out[ i ] = MP_ID_GHOST_ALPHABET[ random[ i ] % MP_ID_GHOST_ALPHABET_LENGTH ];
	}
	out[ MP_ID_GHOST_CODE_LENGTH ] = '\0';
}

// A code as typed, any case, read back in capitals.
inline bool MPIdParseGhostCode( const char *text, char out[ MP_ID_GHOST_CODE_LENGTH + 1 ] ) {
	if ( text == 0 || (int)strlen( text ) != MP_ID_GHOST_CODE_LENGTH ) {
		return false;
	}
	for ( int i = 0; i < MP_ID_GHOST_CODE_LENGTH; i++ ) {
		char c = text[ i ];
		if ( c >= 'a' && c <= 'z' ) {
			c = static_cast<char>( c - 'a' + 'A' );
		}
		if ( strchr( MP_ID_GHOST_ALPHABET, c ) == 0 || c == '\0' ) {
			return false;
		}
		out[ i ] = c;
	}
	out[ MP_ID_GHOST_CODE_LENGTH ] = '\0';
	return true;
}

//----------------------------------------------------------------
// the reconnect store
//----------------------------------------------------------------

static const int MP_ID_RETURN_SLOTS = 64;

// What a player who left a casual match gets back when they return within
// the server's time: their team and their score.
typedef struct mpIdReturn_s {
	char		identity[ MP_ID_FINGERPRINT_TEXT_BYTES ];	// empty when they had none
	char		ghost[ MP_ID_GHOST_CODE_LENGTH + 1 ];
	int			team;
	int			score;
	int			teamScore;		// the player's share of team points (captures and the like)
	int			matchStamp;		// the match it belongs to: a new match forgets it
	mpIdTime_t	expires;
} mpIdReturn_t;

typedef struct mpIdReturnStore_s {
	mpIdReturn_t	entries[ MP_ID_RETURN_SLOTS ];
	int				count;
} mpIdReturnStore_t;

inline void MPIdReturnClear( mpIdReturnStore_t &store ) {
	memset( &store, 0, sizeof( store ) );
}

inline void MPIdReturnRemove( mpIdReturnStore_t &store, int index ) {
	if ( index < 0 || index >= store.count ) {
		return;
	}
	store.entries[ index ] = store.entries[ store.count - 1 ];
	store.count--;
}

// Drops what has expired or belongs to another match.
inline void MPIdReturnPrune( mpIdReturnStore_t &store, mpIdTime_t now, int matchStamp ) {
	for ( int i = store.count - 1; i >= 0; i-- ) {
		if ( store.entries[ i ].expires <= now || store.entries[ i ].matchStamp != matchStamp ) {
			MPIdReturnRemove( store, i );
		}
	}
}

// Keeps a leaving player's place. A newer entry for the same identity or
// ghost code replaces the older; when the store is full, the entry nearest
// its expiry gives way.
inline void MPIdReturnSave( mpIdReturnStore_t &store, const mpIdReturn_t &entry, mpIdTime_t now ) {
	MPIdReturnPrune( store, now, entry.matchStamp );
	for ( int i = store.count - 1; i >= 0; i-- ) {
		const mpIdReturn_t &old = store.entries[ i ];
		if ( ( entry.identity[ 0 ] != '\0' && strcmp( old.identity, entry.identity ) == 0 ) ||
			 ( entry.ghost[ 0 ] != '\0' && strcmp( old.ghost, entry.ghost ) == 0 ) ) {
			MPIdReturnRemove( store, i );
		}
	}
	if ( store.count == MP_ID_RETURN_SLOTS ) {
		int oldest = 0;
		for ( int i = 1; i < store.count; i++ ) {
			if ( store.entries[ i ].expires < store.entries[ oldest ].expires ) {
				oldest = i;
			}
		}
		MPIdReturnRemove( store, oldest );
	}
	store.entries[ store.count++ ] = entry;
}

// Takes a returning player's entry out of the store, by identity or by ghost
// code: one claim each, so a code cannot be used twice.
inline bool MPIdReturnClaim( mpIdReturnStore_t &store, const char *identity, const char *ghost, mpIdTime_t now, int matchStamp,
		mpIdReturn_t &out ) {
	MPIdReturnPrune( store, now, matchStamp );
	for ( int i = 0; i < store.count; i++ ) {
		const mpIdReturn_t &entry = store.entries[ i ];
		const bool byIdentity = identity != 0 && identity[ 0 ] != '\0' && strcmp( entry.identity, identity ) == 0;
		const bool byGhost = ghost != 0 && ghost[ 0 ] != '\0' && strcmp( entry.ghost, ghost ) == 0;
		if ( byIdentity || byGhost ) {
			out = entry;
			MPIdReturnRemove( store, i );
			return true;
		}
	}
	return false;
}

//----------------------------------------------------------------
// Elo ratings
//----------------------------------------------------------------

static const int MP_RATING_INITIAL = 1500;
static const int MP_RATING_PROVISIONAL_MATCHES = 20;	// Muff Mode: K 48 for a player's first 20 rated matches
static const int MP_RATING_K_PROVISIONAL = 48;
static const int MP_RATING_K = 32;

inline double MPRatingExpected( double rating, double opponent ) {
	return 1.0 / ( 1.0 + pow( 10.0, ( opponent - rating ) / 400.0 ) );
}

inline int MPRatingK( int ratedMatches ) {
	return ratedMatches < MP_RATING_PROVISIONAL_MATCHES ? MP_RATING_K_PROVISIONAL : MP_RATING_K;
}

// One result: score is 1 for a win, 0.5 for a draw and 0 for a loss.
inline double MPRatingChange( double rating, double opponent, double score, int ratedMatches ) {
	return MPRatingK( ratedMatches ) * ( score - MPRatingExpected( rating, opponent ) );
}

// A free-for-all: every player against every other by final place (a higher
// score beats a lower; equal scores draw), the K shared across the opponents
// so a big match moves a rating no more than a duel does. ratings, scores and
// ratedMatches are count long; changes receives each player's change.
inline void MPRatingFreeForAll( const double *ratings, const int *scores, const int *ratedMatches, int count, double *changes ) {
	for ( int i = 0; i < count; i++ ) {
		changes[ i ] = 0.0;
		if ( count < 2 ) {
			continue;
		}
		double sum = 0.0;
		for ( int j = 0; j < count; j++ ) {
			if ( j == i ) {
				continue;
			}
			const double result = scores[ i ] > scores[ j ] ? 1.0 : ( scores[ i ] == scores[ j ] ? 0.5 : 0.0 );
			sum += result - MPRatingExpected( ratings[ i ], ratings[ j ] );
		}
		changes[ i ] = MPRatingK( ratedMatches[ i ] ) * sum / ( count - 1 );
	}
}

// Two teams: each player's change is their own K times the team result
// against the other team's average rating.
inline double MPRatingTeamChange( double teamAverage, double otherAverage, double score, int ratedMatches ) {
	return MPRatingChange( teamAverage, otherAverage, score, ratedMatches );
}

#endif	// __GAME_MP_IDENTITY_H__
