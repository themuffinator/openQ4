/*
 * openQ4 player identity core (src/game/mp/Identity.h, roadmap MP-17)
 * compiled alone: per-server keys, the signed proof and what it binds, the
 * fingerprint, ghost codes, the reconnect store and the Elo ratings. Built and
 * run by tools/tests/mp_identity.py.
 */
#include "../../../src/idlib/Ed25519.cpp"
#include "../../../src/game/mp/Identity.h"

#include <cstdio>
#include <cstdlib>

static int failures = 0;

static void Check( bool condition, const char *what ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAIL: %s\n", what );
		++failures;
	}
}

static bool Near( double a, double b, double tolerance = 1e-6 ) {
	return a - b < tolerance && b - a < tolerance;
}

static void CheckProofs() {
	unsigned char seed[ MP_ID_SEED_BYTES ], serverA[ MP_ID_SERVER_ID_BYTES ], serverB[ MP_ID_SERVER_ID_BYTES ], nonce[ MP_ID_NONCE_BYTES ];
	for ( int i = 0; i < MP_ID_SEED_BYTES; i++ ) seed[ i ] = static_cast<unsigned char>( 200 - i * 3 );
	for ( int i = 0; i < MP_ID_SERVER_ID_BYTES; i++ ) { serverA[ i ] = static_cast<unsigned char>( i ); serverB[ i ] = static_cast<unsigned char>( i ); }
	serverB[ 15 ] ^= 1;
	for ( int i = 0; i < MP_ID_NONCE_BYTES; i++ ) nonce[ i ] = static_cast<unsigned char>( i * 7 + 1 );

	unsigned char keyA[ MP_ID_PUBLIC_KEY_BYTES ], keyB[ MP_ID_PUBLIC_KEY_BYTES ], keyA2[ MP_ID_PUBLIC_KEY_BYTES ];
	unsigned char signature[ MP_ID_SIGNATURE_BYTES ], signatureB[ MP_ID_SIGNATURE_BYTES ], signature2[ MP_ID_SIGNATURE_BYTES ];
	MPIdSignProof( seed, serverA, nonce, 3, 77, keyA, signature );
	MPIdSignProof( seed, serverB, nonce, 3, 77, keyB, signatureB );
	MPIdSignProof( seed, serverA, nonce, 3, 77, keyA2, signature2 );
	Check( memcmp( keyA, keyA2, sizeof( keyA ) ) == 0, "a server sees the same key every time" );
	Check( memcmp( signature, signature2, sizeof( signature ) ) == 0, "signing is deterministic" );
	Check( memcmp( keyA, keyB, sizeof( keyA ) ) != 0, "two servers see unrelated keys" );
	Check( MPIdVerifyProof( serverA, nonce, 3, 77, keyA, signature ), "the proof verifies" );
	Check( !MPIdVerifyProof( serverB, nonce, 3, 77, keyA, signature ), "a proof for one server does not verify on another" );
	Check( !MPIdVerifyProof( serverA, nonce, 4, 77, keyA, signature ), "a proof for one slot does not verify for another" );
	Check( !MPIdVerifyProof( serverA, nonce, 3, 78, keyA, signature ), "a proof for one connection does not verify for the next" );
	unsigned char otherNonce[ MP_ID_NONCE_BYTES ];
	memcpy( otherNonce, nonce, sizeof( nonce ) );
	otherNonce[ 9 ] ^= 0x80;
	Check( !MPIdVerifyProof( serverA, otherNonce, 3, 77, keyA, signature ), "a replayed proof does not answer a new nonce" );
	Check( !MPIdVerifyProof( serverA, nonce, 3, 77, keyB, signature ), "a proof does not verify under another key" );

	unsigned char seed2[ MP_ID_SEED_BYTES ];
	memcpy( seed2, seed, sizeof( seed ) );
	seed2[ 0 ] ^= 1;
	unsigned char keyReset[ MP_ID_PUBLIC_KEY_BYTES ];
	MPIdSignProof( seed2, serverA, nonce, 3, 77, keyReset, signature2 );
	Check( memcmp( keyA, keyReset, sizeof( keyA ) ) != 0, "a reset seed gives every server a new key" );

	char fingerprint[ MP_ID_FINGERPRINT_TEXT_BYTES ], parsed[ MP_ID_FINGERPRINT_TEXT_BYTES ];
	MPIdFingerprint( keyA, fingerprint );
	Check( strlen( fingerprint ) == 16, "a fingerprint is sixteen hex digits" );
	for ( const char *c = fingerprint; *c; c++ ) Check( ( *c >= '0' && *c <= '9' ) || ( *c >= 'a' && *c <= 'f' ), "a fingerprint is lower-case hex" );
	char upper[ MP_ID_FINGERPRINT_TEXT_BYTES ];
	for ( int i = 0; i < 17; i++ ) upper[ i ] = static_cast<char>( fingerprint[ i ] >= 'a' ? fingerprint[ i ] - 32 : fingerprint[ i ] );
	Check( MPIdParseFingerprint( upper, parsed ) && strcmp( parsed, fingerprint ) == 0, "a fingerprint typed in capitals reads back" );
	Check( !MPIdParseFingerprint( "0123456789abcde", parsed ), "fifteen digits are no fingerprint" );
	Check( !MPIdParseFingerprint( "0123456789abcdeg", parsed ), "a letter past f is no fingerprint" );
	Check( !MPIdParseFingerprint( "0123456789abcdef0", parsed ), "seventeen digits are no fingerprint" );

	unsigned char decoded[ MP_ID_SERVER_ID_BYTES ];
	char serverText[ MP_ID_SERVER_ID_TEXT_BYTES ];
	MPIdHexEncode( serverA, MP_ID_SERVER_ID_BYTES, serverText );
	Check( MPIdHexDecode( serverText, decoded, MP_ID_SERVER_ID_BYTES ) && memcmp( decoded, serverA, sizeof( decoded ) ) == 0, "a server id round-trips as hex" );
	Check( !MPIdHexDecode( "00", decoded, MP_ID_SERVER_ID_BYTES ), "a short server id is refused" );
}

static void CheckGhostCodes() {
	unsigned char random[ MP_ID_GHOST_CODE_LENGTH ] = { 0, 20, 21, 255, 128, 7 };
	char code[ MP_ID_GHOST_CODE_LENGTH + 1 ], parsed[ MP_ID_GHOST_CODE_LENGTH + 1 ];
	MPIdGhostCode( random, code );
	Check( strlen( code ) == 6, "a ghost code is six letters" );
	for ( const char *c = code; *c; c++ ) Check( strchr( MP_ID_GHOST_ALPHABET, *c ) != 0, "a ghost code uses the plain alphabet" );
	char lower[ 7 ];
	for ( int i = 0; i < 7; i++ ) lower[ i ] = static_cast<char>( code[ i ] ? code[ i ] + 32 : 0 );
	Check( MPIdParseGhostCode( lower, parsed ) && strcmp( parsed, code ) == 0, "a ghost code typed in lower case reads back" );
	Check( !MPIdParseGhostCode( "ABCDE", parsed ), "five letters are no ghost code" );
	Check( !MPIdParseGhostCode( "ABCDEI", parsed ), "a misreadable letter is no ghost code" );
	Check( !MPIdParseGhostCode( "ABC1EF", parsed ), "a digit is no ghost code" );
}

static mpIdReturn_t Entry( const char *identity, const char *ghost, int team, int score, int stamp, mpIdTime_t expires ) {
	mpIdReturn_t entry;
	memset( &entry, 0, sizeof( entry ) );
	snprintf( entry.identity, sizeof( entry.identity ), "%s", identity );
	snprintf( entry.ghost, sizeof( entry.ghost ), "%s", ghost );
	entry.team = team;
	entry.score = score;
	entry.matchStamp = stamp;
	entry.expires = expires;
	return entry;
}

static void CheckReturnStore() {
	mpIdReturnStore_t store;
	MPIdReturnClear( store );
	MPIdReturnSave( store, Entry( "00112233aabbccdd", "ABCDEF", 1, 12, 5, 1300 ), 1000 );
	MPIdReturnSave( store, Entry( "", "GHJKMN", 0, 3, 5, 1300 ), 1000 );
	Check( store.count == 2, "two leaving players are kept" );
	mpIdReturn_t out;
	Check( !MPIdReturnClaim( store, "00112233aabbccdd", "", 1000, 6, out ) && store.count == 0, "a new match forgets the last one's places" );

	MPIdReturnSave( store, Entry( "00112233aabbccdd", "ABCDEF", 1, 12, 5, 1300 ), 1000 );
	MPIdReturnSave( store, Entry( "00112233aabbccdd", "PQRSTW", 0, 20, 5, 1400 ), 1100 );
	Check( store.count == 1, "a newer entry for the same identity replaces the older" );
	Check( !MPIdReturnClaim( store, "", "ABCDEF", 1100, 5, out ), "the replaced entry's ghost code is gone" );
	Check( MPIdReturnClaim( store, "00112233aabbccdd", "", 1100, 5, out ) && out.score == 20 && out.team == 0, "the identity claims its place back" );
	Check( !MPIdReturnClaim( store, "00112233aabbccdd", "", 1100, 5, out ), "a place is claimed once" );

	MPIdReturnSave( store, Entry( "", "GHJKMN", 1, 4, 5, 1300 ), 1000 );
	Check( !MPIdReturnClaim( store, "", "GHJKMN", 1300, 5, out ), "an expired place is gone" );
	MPIdReturnSave( store, Entry( "", "GHJKMN", 1, 4, 5, 1300 ), 1000 );
	Check( MPIdReturnClaim( store, "ffffffffffffffff", "GHJKMN", 1200, 5, out ) && out.score == 4, "a ghost code claims a place without an identity" );

	MPIdReturnClear( store );
	for ( int i = 0; i < MP_ID_RETURN_SLOTS + 5; i++ ) {
		char ghost[ 7 ];
		unsigned char random[ 6 ] = { static_cast<unsigned char>( i ), static_cast<unsigned char>( i / 21 ), 1, 2, 3, 4 };
		MPIdGhostCode( random, ghost );
		MPIdReturnSave( store, Entry( "", ghost, 0, i, 5, 2000 + i ), 1000 );
	}
	Check( store.count == MP_ID_RETURN_SLOTS, "a full store keeps its size" );
	bool newestKept = false, oldestGone = true;
	for ( int i = 0; i < store.count; i++ ) {
		newestKept |= store.entries[ i ].score == MP_ID_RETURN_SLOTS + 4;
		oldestGone &= store.entries[ i ].score != 0;
	}
	Check( newestKept && oldestGone, "a full store gives up the place nearest its expiry" );
}

static void CheckRatings() {
	Check( Near( MPRatingExpected( 1500, 1500 ), 0.5 ), "equal ratings expect a draw" );
	Check( Near( MPRatingExpected( 1600, 1400 ) + MPRatingExpected( 1400, 1600 ), 1.0 ), "both sides' expectations add to one" );
	Check( Near( MPRatingExpected( 1600, 1400 ), 0.759746926647958, 1e-9 ), "200 points ahead expects 76%" );
	Check( MPRatingK( 0 ) == 48 && MPRatingK( 19 ) == 48 && MPRatingK( 20 ) == 32, "K is 48 for the first 20 rated matches, then 32" );
	Check( Near( MPRatingChange( 1500, 1500, 1.0, 0 ), 24.0 ), "a new player's first win against an equal gains 24" );
	Check( Near( MPRatingChange( 1600, 1400, 1.0, 30 ), 32.0 * ( 1.0 - 0.759746926647958 ), 1e-9 ), "an expected win gains little" );
	Check( Near( MPRatingChange( 1400, 1600, 1.0, 30 ), 32.0 * 0.759746926647958, 1e-9 ), "an upset gains much" );

	// a known sequence: a new player wins three, then loses one, all against 1500
	double rating = MP_RATING_INITIAL;
	int played = 0;
	const double results[] = { 1, 1, 1, 0 };
	for ( double result : results ) {
		rating += MPRatingChange( rating, 1500, result, played++ );
	}
	Check( Near( rating, 1538.5798, 0.001 ), "the four-match fixture (three wins and a loss against 1500) ends at 1538.58" );

	const double ratings[] = { 1500, 1500, 1500, 1500 };
	const int scores[] = { 30, 20, 20, 5 };
	const int matches[] = { 30, 30, 30, 30 };
	double changes[ 4 ];
	MPRatingFreeForAll( ratings, scores, matches, 4, changes );
	Check( Near( changes[ 0 ] + changes[ 1 ] + changes[ 2 ] + changes[ 3 ], 0.0 ), "a free-for-all among equals moves no points in all" );
	Check( Near( changes[ 0 ], 16.0 ) && Near( changes[ 3 ], -16.0 ) && Near( changes[ 1 ], 0.0 ) && Near( changes[ 2 ], 0.0 ),
		"first gains a duel's win, last loses one, the tied middle stands still" );
	MPRatingFreeForAll( ratings, scores, matches, 1, changes );
	Check( Near( changes[ 0 ], 0.0 ), "a player alone is not rated" );
}

int main() {
	CheckProofs();
	CheckGhostCodes();
	CheckReturnStore();
	CheckRatings();
	if ( failures != 0 ) {
		std::fprintf( stderr, "%d failures\n", failures );
		return 1;
	}
	std::printf( "mp identity: all checks passed\n" );
	return 0;
}
