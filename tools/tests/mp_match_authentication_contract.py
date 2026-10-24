#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Hostile static and executable contracts for referee authentication."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATCH_DIR = ROOT / "src/mpgame/mp/match"
HEADER = MATCH_DIR / "MatchAuthentication.h"
SOURCE = MATCH_DIR / "MatchAuthentication.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def reject(text: str, token: str, context: str) -> None:
    if token in text:
        raise AssertionError(f"unexpected {token!r} in {context}")


def require_before(text: str, first: str, second: str, context: str) -> None:
    first_at = text.find(first)
    second_at = text.find(second)
    if first_at < 0 or second_at < 0 or first_at >= second_at:
        raise AssertionError(f"expected {first!r} before {second!r} in {context}")


def static_contracts(header: str, source: str) -> None:
    combined = header + source
    for token in (
        "MP_REFEREE_AUTH_PBKDF2_ITERATIONS = 600000",
        "MP_REFEREE_AUTH_MAX_SLOTS = 32",
        "MP_REFEREE_AUTH_SALT_BYTES = 16",
        "MP_REFEREE_AUTH_NONCE_BYTES = 32",
        "MP_REFEREE_AUTH_VERIFIER_BYTES = 32",
        "MP_REFEREE_AUTH_PROOF_BYTES = 32",
        "MP_REFEREE_AUTH_CHALLENGE_WIRE_BYTES = 96",
        "MP_REFEREE_AUTH_FAILURES_BEFORE_LOCKOUT = 5",
        "class mpRefereeAuthenticationService",
        "InstallCredentialFromPassword",
        "InstallCredentialVerifier",
        "BeginSession",
        "IssueChallenge",
        "VerifyProof",
        "InvalidateSlot",
        "RetryAfterMsec",
    ):
        require(combined, token, "bounded authentication API")

    for token in (
        "openQ4/referee/verifier/v1",
        "openQ4/referee/challenge/v1",
        "HmacSha256",
        "Sha256Transform",
        "MPRefereeAuthDeriveVerifier",
        "MPRefereeAuthBuildProofFromVerifier",
        "MPRefereeAuthEncodeChallenge",
        "MPRefereeAuthDecodeChallenge",
        "MPRefereeAuthProofToHex",
        "MPRefereeAuthProofFromHex",
    ):
        require(combined, token, "versioned cryptographic protocol")

    for token in (
        "sessionId",
        "participantSequence",
        "slotGeneration",
        "challengeGeneration",
        "expiresAtEngineMsec",
        "adapterRandomNonce",
    ):
        require(header, token, "challenge identity binding")

    # The service has no engine/network/filesystem dependency, dynamic storage,
    # background work, or convenience command/string path.
    for forbidden in (
        "idFileSystem",
        "fileSystem",
        "gameLocal",
        "idBitMsg",
        "sys_public",
        "precompiled.h\"\n#include",
        "std::string",
        "std::vector",
        "std::map",
        "operator new",
        "malloc(",
        "fopen(",
        "ofstream",
        "std::filesystem",
        "std::thread",
        "CreateThread",
        "system(",
        "popen(",
        "cmdSystem",
    ):
        reject(combined, forbidden, "dependency-neutral bounded core")

    # Plaintext is only a bounded pointer parameter.  Persistent class fields
    # are the salt and verifier; neither challenge nor proof contains it.
    private_fields = header[header.index("private:") :]
    for forbidden_field in (
        "passwordBytes;",
        "passwordLength;",
        "char password",
        "credentialText",
        "password[",
    ):
        reject(private_fields, forbidden_field, "persistent service state")
    require(private_fields, "mpRefereeAuthVerifier credentialVerifier",
            "derived-only persistent credential")
    reject(header[header.index("struct mpRefereeAuthChallenge") :
                  header.index("// Clears through")],
           "password", "wire-visible challenge values")

    # Comparison of the fixed proof length must remain constant work and the
    # expected proof must be erased before any outcome branch.
    require(source, "static bool ConstantTimeEqual", "proof comparison")
    require(source, "volatile uint8_t difference", "non-elidable comparison")
    verify = source[source.index(
        "mpRefereeAuthVerifyResult_t mpRefereeAuthenticationService::VerifyProof") :
        source.index("void mpRefereeAuthenticationService::InvalidateSlot")]
    reject(verify, "memcmp(", "proof verification")
    require_before(verify, "ConstantTimeEqual", "if ( wasLocked )",
                   "uniform proof computation")
    require_before(verify, "record->active = false",
                   "MP_REFEREE_AUTH_VERIFY_AUTHENTICATED",
                   "single-use challenge consumption")
    require(verify, "engineTimeMsec < record->challenge.expiresAtEngineMsec",
            "engine-clock expiry")

    # All sensitive derived values and resident credentials have explicit
    # erasure paths.  Caller-owned input is documented as an adapter duty.
    if source.count("MPRefereeAuthSecureZero") < 20:
        raise AssertionError("sensitive temporary erasure coverage regressed")
    require(header, "caller owns and must\n\twipe its source buffer",
            "adapter credential lifetime contract")
    require(source, "MPRefereeAuthSecureZero( credentialVerifier.bytes",
            "resident verifier erasure")
    require(source, "MPRefereeAuthSecureZero( derivedVerifier.bytes",
            "transient verifier erasure")
    require(source, "MPRefereeAuthSecureZero( expectedProof.bytes",
            "expected proof erasure")

    # There is intentionally no detailed public cause such as bad password,
    # no credential, expired proof, or stale binding.
    verify_enum = header[header.index("typedef enum {\n\tMP_REFEREE_AUTH_VERIFY") :
                         header.index("} mpRefereeAuthVerifyResult_t;")]
    for disclosure in ("BAD_PASSWORD", "NO_CREDENTIAL", "EXPIRED", "STALE", "INVALID_PROOF"):
        reject(verify_enum, disclosure, "credential-opaque result vocabulary")

    # Challenge generation is service-wide, advances exactly at accepted
    # issuance, and is deliberately not reset by BeginSession.
    begin_session = source[source.index(
        "bool mpRefereeAuthenticationService::BeginSession") :
        source.index("mpRefereeAuthChallengeResult_t")]
    reject(begin_session, "lastChallengeGeneration =", "session rollover")
    issue = source[source.index(
        "mpRefereeAuthChallengeResult_t mpRefereeAuthenticationService::IssueChallenge") :
        source.index("mpRefereeAuthVerifyResult_t")]
    if issue.count("++lastChallengeGeneration") != 1:
        raise AssertionError("challenge generation must advance exactly once per issuance")
    require(issue, "lastChallengeGeneration == UINT64_MAX", "generation exhaustion")

    # Core is picked up by the existing recursive source inventory without a
    # hand-maintained build-list edit.
    listing = subprocess.run(
        [
            "python",
            str(ROOT / "tools/build/list_sources.py"),
            str(ROOT / "src"),
            "mpgame",
            "mpgame/Callbacks.cpp",
            "mpgame/gamesys/Callbacks.cpp",
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    if "mpgame/mp/match/MatchAuthentication.cpp" not in {
        line.strip() for line in listing
    }:
        raise AssertionError("MatchAuthentication.cpp is absent from the MP source list")


HARNESS = r'''
#include "mpgame/mp/match/MatchAuthentication.h"

#include <string.h>

#define CHECK( value ) do { if ( !( value ) ) return __LINE__; } while ( 0 )

static void FillSalt( mpRefereeAuthSalt &salt ) {
	for ( int index = 0; index < MP_REFEREE_AUTH_SALT_BYTES; ++index ) {
		salt.bytes[ index ] = static_cast<uint8_t>( index + 1 );
	}
}

static void FillNonce( mpRefereeAuthNonce &nonce, int seed ) {
	for ( int index = 0; index < MP_REFEREE_AUTH_NONCE_BYTES; ++index ) {
		nonce.bytes[ index ] = static_cast<uint8_t>( seed + index + 1 );
	}
}

static bool AllZero( const uint8_t *bytes, int count ) {
	uint8_t aggregate = 0;
	for ( int index = 0; index < count; ++index ) aggregate |= bytes[ index ];
	return aggregate == 0;
}

static int KnownAnswerContract() {
	static const char expectedVerifier[] =
		"5affbe3b596f051001aa6f7bb89b03c87ce79479923ca420c5e423494d67c1a2";
	static const char expectedProof[] =
		"5584b3b1451f900d84aa8df045d1b1f5154df5a9c09298b8f828eb921c87aac8";
	static const uint8_t password[] = "correct horse battery staple";

	mpRefereeAuthSalt salt;
	FillSalt( salt );
	mpRefereeAuthVerifier verifier;
	CHECK( MPRefereeAuthDeriveVerifier( password, sizeof( password ) - 1,
		salt, verifier ) );
	mpRefereeAuthProof verifierAsProof;
	memcpy( verifierAsProof.bytes, verifier.bytes, sizeof( verifier.bytes ) );
	char verifierHex[ MP_REFEREE_AUTH_PROOF_HEX_BYTES + 1 ];
	CHECK( MPRefereeAuthProofToHex( verifierAsProof, verifierHex,
		sizeof( verifierHex ) ) );
	CHECK( strcmp( verifierHex, expectedVerifier ) == 0 );

	mpRefereeAuthChallenge challenge;
	challenge.Clear();
	challenge.wireVersion = MP_REFEREE_AUTH_WIRE_VERSION;
	challenge.algorithm = MP_REFEREE_AUTH_ALGORITHM_PBKDF2_HMAC_SHA256;
	challenge.iterationCount = MP_REFEREE_AUTH_PBKDF2_ITERATIONS;
	challenge.binding.sessionId = 0x0102030405060708ull;
	challenge.binding.participantSequence = 0x11223344u;
	challenge.binding.slotGeneration = 0x55667788u;
	challenge.binding.slot = 7;
	challenge.challengeGeneration = 0x1020304050607080ull;
	challenge.expiresAtEngineMsec = 123456789;
	challenge.salt = salt;
	for ( int index = 0; index < MP_REFEREE_AUTH_NONCE_BYTES; ++index ) {
		challenge.nonce.bytes[ index ] = static_cast<uint8_t>( index + 32 );
	}

	uint8_t wire[ MP_REFEREE_AUTH_CHALLENGE_WIRE_BYTES ];
	CHECK( MPRefereeAuthEncodeChallenge( challenge, wire, sizeof( wire ) ) );
	CHECK( wire[ 0 ] == 'O' && wire[ 1 ] == 'Q' && wire[ 2 ] == 'R' && wire[ 3 ] == 'A' );
	CHECK( wire[ 12 ] == 1 && wire[ 19 ] == 8 );
	CHECK( wire[ 24 ] == 0x55 && wire[ 27 ] == 0x88 );
	CHECK( wire[ 31 ] == 7 );
	CHECK( wire[ 39 ] == 0x80 );
	CHECK( wire[ 63 ] == 16 && wire[ 64 ] == 32 && wire[ 95 ] == 63 );

	mpRefereeAuthChallenge decoded;
	CHECK( MPRefereeAuthDecodeChallenge( wire, sizeof( wire ), decoded ) );
	CHECK( decoded.binding.sessionId == challenge.binding.sessionId );
	CHECK( decoded.binding.participantSequence == challenge.binding.participantSequence );
	CHECK( decoded.binding.slotGeneration == challenge.binding.slotGeneration );
	CHECK( decoded.binding.slot == challenge.binding.slot );
	CHECK( decoded.challengeGeneration == challenge.challengeGeneration );
	CHECK( decoded.expiresAtEngineMsec == challenge.expiresAtEngineMsec );

	mpRefereeAuthProof proofFromVerifier;
	mpRefereeAuthProof proofFromPassword;
	CHECK( MPRefereeAuthBuildProofFromVerifier( challenge, verifier,
		proofFromVerifier ) );
	CHECK( MPRefereeAuthBuildProofFromPassword( challenge, password,
		sizeof( password ) - 1, proofFromPassword ) );
	CHECK( memcmp( proofFromVerifier.bytes, proofFromPassword.bytes,
		sizeof( proofFromVerifier.bytes ) ) == 0 );
	char proofHex[ MP_REFEREE_AUTH_PROOF_HEX_BYTES + 1 ];
	CHECK( MPRefereeAuthProofToHex( proofFromVerifier, proofHex,
		sizeof( proofHex ) ) );
	CHECK( strcmp( proofHex, expectedProof ) == 0 );

	mpRefereeAuthProof decodedProof;
	CHECK( MPRefereeAuthProofFromHex( proofHex, MP_REFEREE_AUTH_PROOF_HEX_BYTES,
		decodedProof ) );
	CHECK( memcmp( decodedProof.bytes, proofFromVerifier.bytes,
		sizeof( decodedProof.bytes ) ) == 0 );
	proofHex[ 9 ] = 'Z';
	CHECK( !MPRefereeAuthProofFromHex( proofHex, MP_REFEREE_AUTH_PROOF_HEX_BYTES,
		decodedProof ) );
	CHECK( AllZero( decodedProof.bytes, sizeof( decodedProof.bytes ) ) );

	uint8_t sentinel[ MP_REFEREE_AUTH_CHALLENGE_WIRE_BYTES ];
	memset( sentinel, 0xcc, sizeof( sentinel ) );
	CHECK( !MPRefereeAuthEncodeChallenge( challenge, sentinel,
		sizeof( sentinel ) - 1 ) );
	CHECK( sentinel[ 0 ] == 0xcc && sentinel[ sizeof( sentinel ) - 1 ] == 0xcc );
	wire[ 7 ] ^= 1; // algorithm id
	CHECK( !MPRefereeAuthDecodeChallenge( wire, sizeof( wire ), decoded ) );
	CHECK( decoded.challengeGeneration == 0 );
	CHECK( !MPRefereeAuthDecodeChallenge( wire, sizeof( wire ) - 1, decoded ) );

	mpRefereeAuthVerifier rejectedVerifier;
	memset( rejectedVerifier.bytes, 0xcc, sizeof( rejectedVerifier.bytes ) );
	CHECK( !MPRefereeAuthDeriveVerifier( password,
		MP_REFEREE_AUTH_MAX_PASSWORD_BYTES + 1u, salt, rejectedVerifier ) );
	CHECK( AllZero( rejectedVerifier.bytes, sizeof( rejectedVerifier.bytes ) ) );
	return 0;
}

static int ServiceContract() {
	static const uint8_t password[] = "correct horse battery staple";
	mpRefereeAuthSalt salt;
	FillSalt( salt );
	mpRefereeAuthVerifier verifier;
	CHECK( MPRefereeAuthDeriveVerifier( password, sizeof( password ) - 1,
		salt, verifier ) );

	mpRefereeAuthenticationService service;
	CHECK( service.InstallCredentialVerifier( salt, verifier ) );
	CHECK( service.BeginSession( 77, 1000 ) );
	CHECK( !service.BeginSession( 77, 1000 ) );
	mpRefereeAuthBinding binding = { 77, 9, 3, 4 };
	mpRefereeAuthNonce nonce;
	FillNonce( nonce, 10 );
	mpRefereeAuthChallenge challenge;
	CHECK( service.IssueChallenge( binding, 1000, nonce, challenge ) ==
		MP_REFEREE_AUTH_CHALLENGE_ISSUED );
	CHECK( challenge.expiresAtEngineMsec ==
		1000 + MP_REFEREE_AUTH_CHALLENGE_LIFETIME_MSEC );
	const uint64_t firstGeneration = challenge.challengeGeneration;
	mpRefereeAuthProof proof;
	CHECK( MPRefereeAuthBuildProofFromVerifier( challenge, verifier, proof ) );
	CHECK( service.VerifyProof( binding, 1001, firstGeneration, proof ) ==
		MP_REFEREE_AUTH_VERIFY_AUTHENTICATED );
	CHECK( service.VerifyProof( binding, 1001, firstGeneration, proof ) ==
		MP_REFEREE_AUTH_VERIFY_REJECTED );

	// A newer challenge replaces the older one.  A stale generation cannot
	// consume the current challenge, while the current proof remains valid.
	FillNonce( nonce, 20 );
	CHECK( service.IssueChallenge( binding, 1250, nonce, challenge ) ==
		MP_REFEREE_AUTH_CHALLENGE_ISSUED );
	mpRefereeAuthProof staleProof;
	CHECK( MPRefereeAuthBuildProofFromVerifier( challenge, verifier, staleProof ) );
	const uint64_t staleGeneration = challenge.challengeGeneration;
	FillNonce( nonce, 30 );
	CHECK( service.IssueChallenge( binding, 1500, nonce, challenge ) ==
		MP_REFEREE_AUTH_CHALLENGE_ISSUED );
	CHECK( MPRefereeAuthBuildProofFromVerifier( challenge, verifier, proof ) );
	CHECK( service.VerifyProof( binding, 1501, staleGeneration, staleProof ) ==
		MP_REFEREE_AUTH_VERIFY_REJECTED );
	CHECK( service.VerifyProof( binding, 1502, challenge.challengeGeneration, proof ) ==
		MP_REFEREE_AUTH_VERIFY_AUTHENTICATED );

	// Every public binding component is authenticated.  A stale connection
	// identity invalidates rather than transferring an outstanding challenge.
	FillNonce( nonce, 40 );
	CHECK( service.IssueChallenge( binding, 1750, nonce, challenge ) ==
		MP_REFEREE_AUTH_CHALLENGE_ISSUED );
	CHECK( MPRefereeAuthBuildProofFromVerifier( challenge, verifier, proof ) );
	mpRefereeAuthBinding staleBinding = binding;
	++staleBinding.slotGeneration;
	CHECK( service.VerifyProof( staleBinding, 1751,
		challenge.challengeGeneration, proof ) == MP_REFEREE_AUTH_VERIFY_REJECTED );
	CHECK( service.VerifyProof( binding, 1752,
		challenge.challengeGeneration, proof ) == MP_REFEREE_AUTH_VERIFY_REJECTED );

	// Expiry is strict on authoritative engine time and still single-use.
	FillNonce( nonce, 50 );
	CHECK( service.IssueChallenge( binding, 2000, nonce, challenge ) ==
		MP_REFEREE_AUTH_CHALLENGE_ISSUED );
	CHECK( MPRefereeAuthBuildProofFromVerifier( challenge, verifier, proof ) );
	CHECK( service.VerifyProof( binding, challenge.expiresAtEngineMsec,
		challenge.challengeGeneration, proof ) == MP_REFEREE_AUTH_VERIFY_REJECTED );
	CHECK( service.IssueChallenge( binding, challenge.expiresAtEngineMsec - 1,
		nonce, challenge ) == MP_REFEREE_AUTH_CHALLENGE_REJECTED );

	// Five consumed bad proofs trigger a time-bounded per-slot lock.  The same
	// generic rejection is used for each bad credential attempt.
	int64_t attemptTime = 17250;
	int64_t lockoutStart = 0;
	for ( uint32_t attempt = 0;
		attempt < MP_REFEREE_AUTH_FAILURES_BEFORE_LOCKOUT; ++attempt ) {
		FillNonce( nonce, 60 + static_cast<int>( attempt ) );
		CHECK( service.IssueChallenge( binding, attemptTime, nonce, challenge ) ==
			MP_REFEREE_AUTH_CHALLENGE_ISSUED );
		CHECK( MPRefereeAuthBuildProofFromVerifier( challenge, verifier, proof ) );
		proof.bytes[ attempt % MP_REFEREE_AUTH_PROOF_BYTES ] ^= 0x80;
		CHECK( service.VerifyProof( binding, attemptTime + 1,
			challenge.challengeGeneration, proof ) == MP_REFEREE_AUTH_VERIFY_REJECTED );
		lockoutStart = attemptTime + 1;
		attemptTime += MP_REFEREE_AUTH_MIN_CHALLENGE_INTERVAL_MSEC;
	}
	CHECK( service.IssueChallenge( binding, attemptTime, nonce, challenge ) ==
		MP_REFEREE_AUTH_CHALLENGE_THROTTLED );
	CHECK( service.RetryAfterMsec( binding.slot, attemptTime ) > 0 );
	const int64_t lockoutEnd = lockoutStart + MP_REFEREE_AUTH_LOCKOUT_MSEC;
	FillNonce( nonce, 80 );
	CHECK( service.IssueChallenge( binding, lockoutEnd, nonce, challenge ) ==
		MP_REFEREE_AUTH_CHALLENGE_ISSUED );
	CHECK( MPRefereeAuthBuildProofFromVerifier( challenge, verifier, proof ) );
	CHECK( service.VerifyProof( binding, lockoutEnd + 1,
		challenge.challengeGeneration, proof ) == MP_REFEREE_AUTH_VERIFY_AUTHENTICATED );

	// Session rollover invalidates old state while generation remains monotonic.
	const uint64_t previousGeneration = service.GetLastChallengeGeneration();
	CHECK( service.BeginSession( 88, lockoutEnd + 2 ) );
	binding.sessionId = 88;
	FillNonce( nonce, 90 );
	CHECK( service.IssueChallenge( binding, lockoutEnd + 2, nonce, challenge ) ==
		MP_REFEREE_AUTH_CHALLENGE_ISSUED );
	CHECK( challenge.challengeGeneration > previousGeneration );

	// Missing credential and wrong proof share the same verification outcome.
	CHECK( MPRefereeAuthBuildProofFromVerifier( challenge, verifier, proof ) );
	service.ClearCredential();
	CHECK( service.VerifyProof( binding, lockoutEnd + 3,
		challenge.challengeGeneration, proof ) == MP_REFEREE_AUTH_VERIFY_REJECTED );

	// Invalid entropy/bounds never mint generations or touch an output challenge.
	CHECK( service.InstallCredentialVerifier( salt, verifier ) );
	CHECK( service.BeginSession( 99, lockoutEnd + 4 ) );
	binding.sessionId = 99;
	memset( nonce.bytes, 0, sizeof( nonce.bytes ) );
	const uint64_t generationBeforeReject = service.GetLastChallengeGeneration();
	CHECK( service.IssueChallenge( binding, lockoutEnd + 4, nonce, challenge ) ==
		MP_REFEREE_AUTH_CHALLENGE_REJECTED );
	CHECK( service.GetLastChallengeGeneration() == generationBeforeReject );
	CHECK( challenge.challengeGeneration == 0 );
	binding.slot = MP_REFEREE_AUTH_MAX_SLOTS;
	FillNonce( nonce, 100 );
	CHECK( service.IssueChallenge( binding, lockoutEnd + 4, nonce, challenge ) ==
		MP_REFEREE_AUTH_CHALLENGE_REJECTED );
	return 0;
}

int main() {
	const int knownAnswer = KnownAnswerContract();
	if ( knownAnswer != 0 ) return knownAnswer;
	return ServiceContract();
}
'''


def locate_msvc() -> tuple[Path, str] | None:
    if os.name != "nt":
        return None
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) / (
        "Microsoft Visual Studio/Installer/vswhere.exe"
    )
    if not vswhere.is_file():
        return None
    query = subprocess.run(
        [
            str(vswhere),
            "-latest",
            "-prerelease",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        capture_output=True,
        text=True,
    )
    install = query.stdout.strip()
    if not install:
        return None
    devcmd = Path(install) / "Common7/Tools/VsDevCmd.bat"
    return (devcmd, "x64") if devcmd.is_file() else None


def executable_contract() -> None:
    temp_root = ROOT / ".tmp"
    temp_root.mkdir(exist_ok=True)
    # The Windows ARM64 runners refuse to delete the just-run harness executable
    # (WinError 5) after the checks pass; a leftover file in .tmp must not turn
    # that pass into a failure.
    with tempfile.TemporaryDirectory(prefix="match-auth-", dir=temp_root, ignore_cleanup_errors=True) as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_authentication_contract.cpp"
        executable = temp_dir / "match_authentication_contract.exe"
        harness.write_text(HARNESS, encoding="utf-8")

        msvc = locate_msvc()
        if msvc is not None:
            devcmd, architecture = msvc
            compile_script = temp_dir / "compile_msvc.cmd"
            compile_script.write_text(
                "@echo off\n"
                f'call "{devcmd}" -arch={architecture} '
                f'-host_arch={architecture} >nul\n'
                "if errorlevel 1 exit /b %errorlevel%\n"
                "cl.exe /nologo /std:c++17 /EHsc /W4 "
                "/DMP_MATCH_AUTHENTICATION_STANDALONE_TEST "
                f'/I"{ROOT / "src"}" "{harness}" "{SOURCE}" '
                f'/Fe:"{executable}"\n',
                encoding="utf-8",
            )
            compiled = subprocess.run(
                [os.environ.get("ComSpec", "cmd.exe"), "/d", "/c",
                 str(compile_script)],
                cwd=temp_dir,
                text=True,
                capture_output=True,
            )
            compiler_name = "MSVC"
        else:
            compiler = next(
                (path for name in ("clang++", "g++", "c++")
                 if (path := shutil.which(name))),
                None,
            )
            if compiler is None:
                print("mp_match_authentication_contract: executable checks skipped "
                      "(no C++ compiler)")
                return
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-DMP_MATCH_AUTHENTICATION_STANDALONE_TEST",
                    f"-I{ROOT / 'src'}",
                    str(harness),
                    str(SOURCE),
                    "-o",
                    str(executable),
                ],
                cwd=temp_dir,
                text=True,
                capture_output=True,
            )
            compiler_name = Path(compiler).name
        if compiled.returncode != 0:
            raise AssertionError(
                f"standalone authentication contract did not compile with {compiler_name}:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run(
            [str(executable)], cwd=ROOT, text=True, capture_output=True, timeout=30
        )
        if ran.returncode != 0:
            raise AssertionError(
                "authentication executable invariant failed "
                f"(exit {ran.returncode}):\n" + ran.stdout + ran.stderr
            )
        print(f"mp_match_authentication_contract: executable PASS ({compiler_name})")


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    static_contracts(header, source)
    executable_contract()
    print("mp_match_authentication_contract: PASS")


if __name__ == "__main__":
    main()
