//----------------------------------------------------------------
// MatchAuthentication.h
//
// Dependency-neutral referee challenge/response authentication.  The core
// owns only a password-derived verifier; plaintext credentials, randomness,
// transport, logging, cvars and role assignment remain adapter concerns.
//----------------------------------------------------------------

#ifndef __MP_MATCH_AUTHENTICATION_H__
#define __MP_MATCH_AUTHENTICATION_H__

#include <stddef.h>
#include <stdint.h>

static const uint16_t MP_REFEREE_AUTH_WIRE_VERSION = 1;
static const uint16_t MP_REFEREE_AUTH_ALGORITHM_PBKDF2_HMAC_SHA256 = 1;
static const uint32_t MP_REFEREE_AUTH_PBKDF2_ITERATIONS = 600000;

static const int MP_REFEREE_AUTH_MAX_SLOTS = 32;
static const int MP_REFEREE_AUTH_SALT_BYTES = 16;
static const int MP_REFEREE_AUTH_NONCE_BYTES = 32;
static const int MP_REFEREE_AUTH_VERIFIER_BYTES = 32;
static const int MP_REFEREE_AUTH_PROOF_BYTES = 32;
static const int MP_REFEREE_AUTH_PROOF_HEX_BYTES = 64;
static const int MP_REFEREE_AUTH_CHALLENGE_WIRE_BYTES = 96;
static const int MP_REFEREE_AUTH_MAX_PASSWORD_BYTES = 1024;

static const int64_t MP_REFEREE_AUTH_CHALLENGE_LIFETIME_MSEC = 15 * 1000;
static const int64_t MP_REFEREE_AUTH_FAILURE_WINDOW_MSEC = 60 * 1000;
static const int64_t MP_REFEREE_AUTH_LOCKOUT_MSEC = 60 * 1000;
static const int64_t MP_REFEREE_AUTH_MIN_CHALLENGE_INTERVAL_MSEC = 250;
static const uint32_t MP_REFEREE_AUTH_FAILURES_BEFORE_LOCKOUT = 5;

/*
===============================================================================

	Cryptographic values and canonical wire challenge

	The verifier is PBKDF2-HMAC-SHA-256(password,
	"openQ4/referee/verifier/v1" || 0x00 || salt, 600000, 32).
	A proof is HMAC-SHA-256(verifier,
	"openQ4/referee/challenge/v1" || 0x00 || canonicalChallenge).

	The fixed iteration count and both domain separators are protocol contract,
	not configurable tuning values.  A future change requires a new wire version
	and algorithm id.  Password bytes are opaque and may contain zeroes.

===============================================================================
*/

struct mpRefereeAuthSalt {
	uint8_t bytes[ MP_REFEREE_AUTH_SALT_BYTES ];
};

struct mpRefereeAuthNonce {
	uint8_t bytes[ MP_REFEREE_AUTH_NONCE_BYTES ];
};

struct mpRefereeAuthVerifier {
	uint8_t bytes[ MP_REFEREE_AUTH_VERIFIER_BYTES ];
};

struct mpRefereeAuthProof {
	uint8_t bytes[ MP_REFEREE_AUTH_PROOF_BYTES ];
};

struct mpRefereeAuthBinding {
	uint64_t sessionId;
	uint32_t participantSequence;
	int slot;
	uint32_t slotGeneration;
};

struct mpRefereeAuthChallenge {
	uint16_t wireVersion;
	uint16_t algorithm;
	uint32_t iterationCount;
	mpRefereeAuthBinding binding;
	uint64_t challengeGeneration;
	int64_t expiresAtEngineMsec;
	mpRefereeAuthSalt salt;
	mpRefereeAuthNonce nonce;

	void Clear( void );
};

// Clears through a volatile access path.  Adapters should use this for their
// own transient credential buffers once derivation is complete.
void MPRefereeAuthSecureZero( void *memory, size_t bytes );

bool MPRefereeAuthDeriveVerifier( const void *passwordBytes, size_t passwordLength,
	const mpRefereeAuthSalt &salt, mpRefereeAuthVerifier &verifier );

bool MPRefereeAuthBuildProofFromPassword( const mpRefereeAuthChallenge &challenge,
	const void *passwordBytes, size_t passwordLength, mpRefereeAuthProof &proof );

bool MPRefereeAuthBuildProofFromVerifier( const mpRefereeAuthChallenge &challenge,
	const mpRefereeAuthVerifier &verifier, mpRefereeAuthProof &proof );

bool MPRefereeAuthEncodeChallenge( const mpRefereeAuthChallenge &challenge,
	uint8_t *output, size_t outputBytes );

bool MPRefereeAuthDecodeChallenge( const uint8_t *input, size_t inputBytes,
	mpRefereeAuthChallenge &challenge );

bool MPRefereeAuthProofToHex( const mpRefereeAuthProof &proof,
	char *output, size_t outputBytes );

bool MPRefereeAuthProofFromHex( const char *input, size_t inputBytes,
	mpRefereeAuthProof &proof );

typedef enum {
	MP_REFEREE_AUTH_CHALLENGE_REJECTED = 0,
	MP_REFEREE_AUTH_CHALLENGE_ISSUED,
	MP_REFEREE_AUTH_CHALLENGE_THROTTLED
} mpRefereeAuthChallengeResult_t;

// REJECTED deliberately covers a wrong proof, missing credential, expired or
// stale challenge, and stale identity.  The service never reveals which test
// failed.  THROTTLED is solely a retry policy result.
typedef enum {
	MP_REFEREE_AUTH_VERIFY_REJECTED = 0,
	MP_REFEREE_AUTH_VERIFY_AUTHENTICATED,
	MP_REFEREE_AUTH_VERIFY_THROTTLED
} mpRefereeAuthVerifyResult_t;

/*
===============================================================================

	Bounded authentication service

	The adapter MUST supply each nonce from a cryptographically secure random
	source; the fixed 32-byte input exceeds the required 128 bits.  This class
	can reject an all-zero nonce but cannot measure source entropy.

	InstallCredentialFromPassword receives plaintext only for the duration of
	the call and retains solely the 32-byte verifier.  The caller owns and must
	wipe its source buffer.  An adapter which intentionally disables referee
	authentication should install an adapter-random salt and verifier instead of
	exposing a distinct "no password" response over the network.

===============================================================================
*/

class mpRefereeAuthenticationService {
public:
					mpRefereeAuthenticationService( void );
					~mpRefereeAuthenticationService( void );

	bool			InstallCredentialVerifier( const mpRefereeAuthSalt &salt,
						const mpRefereeAuthVerifier &verifier );
	bool			InstallCredentialFromPassword( const void *passwordBytes,
						size_t passwordLength, const mpRefereeAuthSalt &salt );
	void			ClearCredential( void );

	// A new non-zero session invalidates outstanding challenges and rate state,
	// but never rolls the service-wide challenge generation backwards.
	bool			BeginSession( uint64_t sessionId, int64_t engineTimeMsec );

	mpRefereeAuthChallengeResult_t IssueChallenge(
						const mpRefereeAuthBinding &binding,
						int64_t engineTimeMsec,
						const mpRefereeAuthNonce &adapterRandomNonce,
						mpRefereeAuthChallenge &challenge );

	mpRefereeAuthVerifyResult_t VerifyProof(
						const mpRefereeAuthBinding &trustedBinding,
						int64_t engineTimeMsec,
						uint64_t challengeGeneration,
						const mpRefereeAuthProof &proof );

	void			InvalidateSlot( int slot );
	int64_t		RetryAfterMsec( int slot, int64_t engineTimeMsec ) const;
	uint64_t	GetLastChallengeGeneration( void ) const;

private:
					mpRefereeAuthenticationService(
						const mpRefereeAuthenticationService &other ) = delete;
	mpRefereeAuthenticationService &operator=(
						const mpRefereeAuthenticationService &other ) = delete;

	struct SlotRecord {
		mpRefereeAuthChallenge challenge;
		bool		active;
		uint32_t	failedAttempts;
		int64_t		failureWindowStartedAt;
		int64_t		lockedUntil;
		int64_t		lastChallengeIssuedAt;
	};

	bool			ObserveEngineTime( int64_t engineTimeMsec );
	bool			BindingIsCurrentAndValid( const mpRefereeAuthBinding &binding ) const;
	void			ClearChallengesAndRateState( void );
	void			RegisterFailure( SlotRecord &record, int64_t engineTimeMsec );

	uint64_t	currentSessionId;
	uint64_t	lastChallengeGeneration;
	int64_t		lastObservedEngineTime;
	bool			credentialInstalled;
	mpRefereeAuthSalt credentialSalt;
	mpRefereeAuthVerifier credentialVerifier;
	SlotRecord	slots[ MP_REFEREE_AUTH_MAX_SLOTS ];
};

#endif // __MP_MATCH_AUTHENTICATION_H__
