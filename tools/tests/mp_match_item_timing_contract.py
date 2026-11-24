#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Static and hostile native contracts for authoritative item timing."""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from game_regression_suite import discover as discover_game_regressions


ROOT = Path(__file__).resolve().parents[2]
MATCH_DIR = ROOT / "src/game/mp/match"
HEADER = MATCH_DIR / "MatchItemTiming.h"
SOURCE = MATCH_DIR / "MatchItemTiming.cpp"
SESSION_SOURCE = MATCH_DIR / "MatchSession.cpp"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def static_contracts(header: str, source: str) -> None:
    combined = header + source
    for token in (
        "MP_MATCH_ITEM_TIMING_SCHEMA_VERSION = 1",
        "MP_MATCH_ITEM_TIMING_MAX_OBSERVATIONS =\n\tMP_MATCH_VIEW_MAX_ITEM_TIMINGS",
        "MP_MATCH_ITEM_TIMING_TOKEN_BYTES =\n\tMP_MATCH_VIEW_ITEM_TOKEN_BYTES",
        "class mpMatchItemTimingRegistry",
        "BeginMap",
        "Observe",
        "FindObservation",
        "ProjectCandidate",
        "ValidateInvariants",
        "MPMatchItemTimingSemanticToken",
        "MPMatchItemTimingIsAdapterToken",
    ):
        require(combined, token, "bounded item-timing API")

    for token in (
        "MP_MATCH_ITEM_TIMING_REASON_CLOCK_REGRESSION",
        "MP_MATCH_ITEM_TIMING_REASON_CLOCK_OVERFLOW",
        "MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_SOURCE",
        "MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_TOKEN",
        "MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_OBSERVATION",
        "MP_MATCH_ITEM_TIMING_REASON_STALE_REVISION",
        "MP_MATCH_ITEM_TIMING_REASON_REVISION_EXHAUSTED",
        "MP_MATCH_ITEM_TIMING_REASON_CAPACITY",
    ):
        require(header, token, "explicit mutation failure vocabulary")

    semantic_tokens = {
        "QUAD_DAMAGE": "quad",
        "HASTE": "haste",
        "REGENERATION": "regeneration",
        "INVISIBILITY": "invisibility",
        "MEGA_HEALTH": "mega_health",
        "LARGE_ARMOR": "large_armor",
        "SMALL_ARMOR": "small_armor",
    }
    for kind, token in semantic_tokens.items():
        require(
            source,
            f'MP_MATCH_ITEM_TIMING_KIND_{kind}: return "{token}"',
            "allowlisted semantic item mapping",
        )
    require(source, "IsCanonicalAdapterToken", "canonical adapter-token validator")
    for token in (
        "value >= 'a' && value <= 'z'",
        "value >= '0' && value <= '9'",
        "IsAdapterSeparator",
        "previousSeparator",
        "!IsSemanticToken( token )",
    ):
        require(source, token, "adapter-token validation")

    for forbidden in (
        "idEntity",
        "idItem",
        "idDict",
        "idCVar",
        "idBitMsg",
        "idFile",
        "gameLocal",
        "idMultiplayerGame",
        "cmdSystem",
        "networkSystem",
        "std::vector",
        "std::string",
        "idList<",
        "MatchSeriesRecovery.h",
    ):
        if forbidden in combined:
            raise AssertionError(f"item timing core contains dependency {forbidden!r}")
    for pattern in (
        r"\bnew\b",
        r"\bdelete\b",
        r"\bmalloc\s*\(",
        r"\bcalloc\s*\(",
        r"\brealloc\s*\(",
        r"\bfree\s*\(",
    ):
        if re.search(pattern, combined):
            raise AssertionError(f"item timing core allocates via {pattern!r}")

    if len(re.findall(r"\+\+revision", source)) != 1:
        raise AssertionError("registry revision must have one increment owner")
    require(
        source,
        "ObservationStateEqual( observations[ sourceIndex ], candidate )",
        "idempotent exact observation replay",
    )
    require(
        source,
        "expectedRevision != revision",
        "compare-and-swap mutation guard",
    )
    require(
        source,
        "INT64_MAX - MP_MATCH_DISCLOSURE_MAX_ITEM_TIMING_DELAY_MSEC",
        "worst-case holdback overflow rejection",
    )
    project = source[source.index(
        "mpMatchDisclosureItemResult_t mpMatchItemTimingRegistry::ProjectCandidate"
    ) : source.index("bool mpMatchItemTimingRegistry::ValidateInvariants")]
    require(
        project,
        "MPMatchDisclosureSetItemTimingCandidate( policy, audience",
        "canonical disclosure projection",
    )
    if ".SetItemTiming(" in source:
        raise AssertionError("registry bypasses the disclosure-policy projection gate")
    require(
        header,
        "This is the sole observer-candidate projection route.",
        "projection ownership contract",
    )
    require(source, "MP_MATCH_ITEM_TIMING_STANDALONE_TEST", "standalone seam")

    listing = subprocess.run(
        [
            shutil.which("python") or shutil.which("python3") or "python",
            str(ROOT / "tools/build/list_sources.py"),
            str(ROOT / "src"),
            "game",
            "game/gamesys/Callbacks.cpp",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    if "game/mp/match/MatchItemTiming.cpp" not in {
        line.strip() for line in listing
    }:
        raise AssertionError("recursive MP source discovery omits MatchItemTiming.cpp")
    if "tools/tests/mp_match_item_timing_contract.py" not in discover_game_regressions():
        raise AssertionError("item timing contract is absent from the game regression suite")


HARNESS = r'''
#include "game/mp/match/MatchItemTiming.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK( value ) do { if ( !( value ) ) return __LINE__; } while ( 0 )

static int disclosureCalls = 0;
static mpMatchViewAudience_t capturedAudience = MP_MATCH_VIEW_AUDIENCE_PUBLIC;
static int64_t capturedObserved = -1;
static int64_t capturedDeadline = -1;
static bool capturedAvailable = false;
static char capturedToken[ MP_MATCH_VIEW_ITEM_TOKEN_BYTES + 1 ];

void mpMatchViewObserverCandidate_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
}

mpMatchDisclosureItemResult_t MPMatchDisclosureSetItemTimingCandidate(
	const mpMatchDisclosurePolicy_t &policy,
	mpMatchViewAudience_t audience,
	mpMatchTime currentMatchTime, mpMatchTime observedAtMatchTime,
	const char *itemToken, mpMatchTime matchDeadline, bool available,
	mpMatchViewObserverCandidate_t &candidate,
	mpMatchTime *notBeforeMatchTime ) {
	++disclosureCalls;
	capturedAudience = audience;
	capturedObserved = observedAtMatchTime.Milliseconds();
	capturedDeadline = matchDeadline.Milliseconds();
	capturedAvailable = available;
	int tokenLength = 0;
	while ( tokenLength < MP_MATCH_VIEW_ITEM_TOKEN_BYTES &&
		itemToken[ tokenLength ] != '\0' ) {
		capturedToken[ tokenLength ] = itemToken[ tokenLength ];
		++tokenLength;
	}
	capturedToken[ tokenLength ] = '\0';
	candidate.Clear();
	if ( notBeforeMatchTime != 0 ) {
		*notBeforeMatchTime = mpMatchTime::FromMilliseconds( 0 );
	}
	if ( audience != MP_MATCH_VIEW_AUDIENCE_BROADCASTER &&
		audience != MP_MATCH_VIEW_AUDIENCE_REFEREE ) {
		return MP_MATCH_DISCLOSURE_ITEM_NOT_PERMITTED;
	}
	if ( ( audience == MP_MATCH_VIEW_AUDIENCE_BROADCASTER &&
			!policy.allowBroadcasterItemTiming ) ||
		( audience == MP_MATCH_VIEW_AUDIENCE_REFEREE &&
			!policy.allowRefereeItemTiming ) ) {
		return MP_MATCH_DISCLOSURE_ITEM_NOT_PERMITTED;
	}
	if ( !currentMatchTime.IsValid() || !observedAtMatchTime.IsValid() ||
		!matchDeadline.IsValid() || currentMatchTime < observedAtMatchTime ) {
		return MP_MATCH_DISCLOSURE_ITEM_CLOCK_REGRESSION;
	}
	if ( observedAtMatchTime.Milliseconds() >
		INT64_MAX - policy.itemTimingDelayMsec ) {
		return MP_MATCH_DISCLOSURE_ITEM_CLOCK_OVERFLOW;
	}
	const int64_t notBefore = observedAtMatchTime.Milliseconds() +
		policy.itemTimingDelayMsec;
	if ( notBeforeMatchTime != 0 ) {
		*notBeforeMatchTime = mpMatchTime::FromMilliseconds( notBefore );
	}
	if ( currentMatchTime.Milliseconds() < notBefore ) {
		return MP_MATCH_DISCLOSURE_ITEM_DELAYED;
	}
	candidate.authorization.audience = audience;
	candidate.authorization.audienceSide = MP_MATCH_VIEW_SIDE_NONE;
	candidate.kind = MP_MATCH_VIEW_OBSERVER_ITEM_TIMING;
	candidate.active = available;
	candidate.matchDeadlineMsec =
		static_cast<unsigned long long>( matchDeadline.Milliseconds() );
	candidate.tokenLength = static_cast<unsigned char>( tokenLength );
	memcpy( candidate.token, itemToken, static_cast<size_t>( tokenLength ) );
	candidate.token[ tokenLength ] = '\0';
	return MP_MATCH_DISCLOSURE_ITEM_READY;
}

static mpMatchDisclosurePolicy_t Policy( int64_t delayMsec ) {
	mpMatchDisclosurePolicy_t policy = {};
	policy.schemaVersion = MP_MATCH_DISCLOSURE_POLICY_VERSION;
	policy.allowLiveBroadcasterObservation = true;
	policy.allowBroadcasterItemTiming = true;
	policy.allowRefereeObservation = true;
	policy.allowRefereeItemTiming = true;
	policy.itemTimingDelayMsec = delayMsec;
	return policy;
}

static mpMatchItemTimingObservationInput Semantic( uint64_t sourceId,
	mpMatchItemTimingKind_t kind, int64_t observedMsec, int64_t deadlineMsec,
	bool available ) {
	mpMatchItemTimingObservationInput input = {};
	input.sourceId = sourceId;
	input.kind = kind;
	input.observedAtMatchTime = mpMatchTime::FromMilliseconds( observedMsec );
	input.matchDeadline = mpMatchTime::FromMilliseconds( deadlineMsec );
	input.available = available;
	return input;
}

static mpMatchItemTimingObservationInput Adapter( uint64_t sourceId,
	const char *token, int64_t observedMsec, int64_t deadlineMsec,
	bool available ) {
	mpMatchItemTimingObservationInput input = Semantic( sourceId,
		MP_MATCH_ITEM_TIMING_KIND_ADAPTER_TOKEN, observedMsec, deadlineMsec,
		available );
	input.adapterToken = token;
	return input;
}

static bool ChangedOnce( const mpMatchItemTimingMutationResult &result ) {
	return result.WasApplied() &&
		result.currentRevision == result.previousRevision + 1;
}

int main() {
	mpMatchItemTimingRegistry registry;
	CHECK( registry.ValidateInvariants() );
	CHECK( !registry.IsInitialized() && registry.GetRevision() == 0 );
	mpMatchItemTimingObservationInput beforeMap = Semantic( 1,
		MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE, 0, 100, false );
	CHECK( registry.Observe( beforeMap, 0 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_NOT_INITIALIZED );
	CHECK( registry.BeginMap( 0 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_INVALID_MAP_INSTANCE );
	CHECK( ChangedOnce( registry.BeginMap( 0x1122334455667788ULL ) ) );
	CHECK( registry.GetRevision() == 1 );
	CHECK( registry.BeginMap( 0x1122334455667788ULL ).code ==
		MP_MATCH_ITEM_TIMING_MUTATION_NO_CHANGE );
	CHECK( registry.BeginMap( 9 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_MAP_INSTANCE_CONFLICT );

	static const char *semanticTokens[] = {
		"quad", "haste", "regeneration", "invisibility", "mega_health",
		"large_armor", "small_armor"
	};
	for ( int index = 0; index < 7; ++index ) {
		CHECK( strcmp( MPMatchItemTimingSemanticToken(
			static_cast<mpMatchItemTimingKind_t>( index + 1 ) ),
			semanticTokens[ index ] ) == 0 );
	}
	CHECK( MPMatchItemTimingSemanticToken(
		MP_MATCH_ITEM_TIMING_KIND_INVALID ) == 0 );
	CHECK( MPMatchItemTimingSemanticToken(
		MP_MATCH_ITEM_TIMING_KIND_ADAPTER_TOKEN ) == 0 );
	CHECK( MPMatchItemTimingIsAdapterToken( "item_armor_large.spawn2" ) );
	static const char *invalidTokens[] = {
		"", "1armor", "Armor", "armor/room", "armor room", "armor__room",
		"armor..room", "armor-", "_armor", "quad", "mega_health",
		"\xc3\xa9"
	};
	for ( unsigned int index = 0;
		index < sizeof( invalidTokens ) / sizeof( invalidTokens[ 0 ] ); ++index ) {
		CHECK( !MPMatchItemTimingIsAdapterToken( invalidTokens[ index ] ) );
	}
	char overlongToken[ MP_MATCH_ITEM_TIMING_TOKEN_BYTES + 2 ];
	memset( overlongToken, 'a', sizeof( overlongToken ) );
	overlongToken[ sizeof( overlongToken ) - 1 ] = '\0';
	CHECK( !MPMatchItemTimingIsAdapterToken( overlongToken ) );

	mpMatchItemTimingObservationInput invalid = beforeMap;
	invalid.sourceId = 0;
	CHECK( registry.Observe( invalid, 1 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_INVALID_SOURCE_ID );
	invalid = beforeMap;
	invalid.kind = MP_MATCH_ITEM_TIMING_KIND_INVALID;
	CHECK( registry.Observe( invalid, 1 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_INVALID_KIND );
	invalid = beforeMap;
	invalid.adapterToken = "not_allowed_for_semantic";
	CHECK( registry.Observe( invalid, 1 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_INVALID_TOKEN );
	invalid = Adapter( 1, "Quad", 0, 100, false );
	CHECK( registry.Observe( invalid, 1 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_INVALID_TOKEN );
	invalid = Semantic( 1, MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE, -1, 100, false );
	CHECK( registry.Observe( invalid, 1 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_CLOCK_REGRESSION );
	invalid = Semantic( 1, MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE,
		INT64_MAX - MP_MATCH_DISCLOSURE_MAX_ITEM_TIMING_DELAY_MSEC + 1,
		INT64_MAX, false );
	CHECK( registry.Observe( invalid, 1 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_CLOCK_OVERFLOW );
	invalid = Semantic( 1, MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE, 100, 100, false );
	CHECK( registry.Observe( invalid, 1 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_INVALID_DEADLINE );
	invalid = Semantic( 1, MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE, 100, 101, true );
	CHECK( registry.Observe( invalid, 1 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_INVALID_DEADLINE );
	CHECK( registry.GetObservationCount() == 0 && registry.GetRevision() == 1 );

	mpMatchItemTimingObservationInput quad = Semantic( 11,
		MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE, 100, 500, false );
	CHECK( ChangedOnce( registry.Observe( quad, 1 ) ) );
	CHECK( registry.GetRevision() == 2 && registry.GetObservationCount() == 1 );
	CHECK( registry.Observe( quad, 1 ).code ==
		MP_MATCH_ITEM_TIMING_MUTATION_NO_CHANGE );
	CHECK( registry.GetRevision() == 2 );
	mpMatchItemTimingObservationInput quadAvailable = Semantic( 11,
		MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE, 200, 0, true );
	CHECK( registry.Observe( quadAvailable, 1 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_STALE_REVISION );
	mpMatchItemTimingObservationInput reusedSource = Semantic( 11,
		MP_MATCH_ITEM_TIMING_KIND_HASTE, 200, 0, true );
	CHECK( registry.Observe( reusedSource, 2 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_SOURCE );
	mpMatchItemTimingObservationInput sameClockConflict = Semantic( 11,
		MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE, 100, 600, false );
	CHECK( registry.Observe( sameClockConflict, 2 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_OBSERVATION );
	CHECK( ChangedOnce( registry.Observe( quadAvailable, 2 ) ) );
	CHECK( registry.GetRevision() == 3 );
	CHECK( registry.GetLastObservedMatchTime().Milliseconds() == 200 );
	CHECK( registry.FindObservation( 11 ) != 0 );
	CHECK( registry.FindObservation( 11 )->available );
	CHECK( registry.FindObservation( 11 )->firstRegistryRevision == 2 );
	CHECK( registry.FindObservation( 11 )->lastRegistryRevision == 3 );
	mpMatchItemTimingObservationInput regressed = Semantic( 11,
		MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE, 150, 700, false );
	CHECK( registry.Observe( regressed, 3 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_CLOCK_REGRESSION );

	mpMatchItemTimingObservationInput duplicateToken = Semantic( 12,
		MP_MATCH_ITEM_TIMING_KIND_QUAD_DAMAGE, 200, 0, true );
	CHECK( registry.Observe( duplicateToken, 3 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_TOKEN );
	mpMatchItemTimingObservationInput custom = Adapter( 12, "armor_room.a",
		200, 800, false );
	CHECK( ChangedOnce( registry.Observe( custom, 3 ) ) );
	CHECK( registry.GetRevision() == 4 );
	CHECK( registry.Observe( Adapter( 13, "armor_room.a", 200, 800, false ),
		4 ).reason == MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_TOKEN );
	CHECK( registry.Observe( Adapter( 12, "armor_room.b", 201, 801, false ),
		4 ).reason == MP_MATCH_ITEM_TIMING_REASON_DUPLICATE_SOURCE );
	CHECK( registry.Observe( Semantic( 13, MP_MATCH_ITEM_TIMING_KIND_HASTE,
		199, 700, false ), 4 ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_CLOCK_REGRESSION );
	CHECK( ChangedOnce( registry.Observe( Semantic( 13,
		MP_MATCH_ITEM_TIMING_KIND_HASTE, 201, 700, false ), 4 ) ) );
	CHECK( registry.ValidateInvariants() );

	mpMatchDisclosurePolicy_t policy = Policy( 100 );
	mpMatchViewObserverCandidate_t candidate;
	memset( &candidate, 0x7f, sizeof( candidate ) );
	mpMatchTime notBefore = mpMatchTime::FromMilliseconds( 999 );
	const int callsBeforeDelay = disclosureCalls;
	CHECK( registry.ProjectCandidate( 11, policy,
		MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
		mpMatchTime::FromMilliseconds( 299 ), candidate, &notBefore ) ==
		MP_MATCH_DISCLOSURE_ITEM_DELAYED );
	CHECK( disclosureCalls == callsBeforeDelay + 1 );
	CHECK( notBefore.Milliseconds() == 300 );
	CHECK( candidate.kind == 0 && candidate.tokenLength == 0 );
	CHECK( capturedAudience == MP_MATCH_VIEW_AUDIENCE_BROADCASTER );
	CHECK( capturedObserved == 200 && capturedDeadline == 0 && capturedAvailable );
	CHECK( strcmp( capturedToken, "quad" ) == 0 );
	CHECK( registry.ProjectCandidate( 11, policy,
		MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
		mpMatchTime::FromMilliseconds( 300 ), candidate, &notBefore ) ==
		MP_MATCH_DISCLOSURE_ITEM_READY );
	CHECK( candidate.kind == MP_MATCH_VIEW_OBSERVER_ITEM_TIMING );
	CHECK( candidate.authorization.audience ==
		MP_MATCH_VIEW_AUDIENCE_BROADCASTER );
	CHECK( candidate.active && candidate.matchDeadlineMsec == 0 );
	CHECK( strcmp( candidate.token, "quad" ) == 0 );
	CHECK( registry.ProjectCandidate( 11, policy,
		MP_MATCH_VIEW_AUDIENCE_REFEREE,
		mpMatchTime::FromMilliseconds( 300 ), candidate, 0 ) ==
		MP_MATCH_DISCLOSURE_ITEM_READY );
	CHECK( candidate.authorization.audience == MP_MATCH_VIEW_AUDIENCE_REFEREE );
	CHECK( registry.ProjectCandidate( 11, policy,
		MP_MATCH_VIEW_AUDIENCE_PUBLIC,
		mpMatchTime::FromMilliseconds( 300 ), candidate, 0 ) ==
		MP_MATCH_DISCLOSURE_ITEM_NOT_PERMITTED );
	policy.allowBroadcasterItemTiming = false;
	CHECK( registry.ProjectCandidate( 11, policy,
		MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
		mpMatchTime::FromMilliseconds( 300 ), candidate, 0 ) ==
		MP_MATCH_DISCLOSURE_ITEM_NOT_PERMITTED );
	policy.allowBroadcasterItemTiming = true;
	const int callsBeforeMissing = disclosureCalls;
	memset( &candidate, 0x7f, sizeof( candidate ) );
	notBefore = mpMatchTime::FromMilliseconds( 999 );
	CHECK( registry.ProjectCandidate( 9999, policy,
		MP_MATCH_VIEW_AUDIENCE_BROADCASTER,
		mpMatchTime::FromMilliseconds( 300 ), candidate, &notBefore ) ==
		MP_MATCH_DISCLOSURE_ITEM_REJECTED );
	CHECK( disclosureCalls == callsBeforeMissing );
	CHECK( candidate.tokenLength == 0 && notBefore.Milliseconds() == 0 );

	// Fill the exact recipient-view capacity with unique stable sources/tokens.
	for ( int index = registry.GetObservationCount();
		index < MP_MATCH_ITEM_TIMING_MAX_OBSERVATIONS; ++index ) {
		char token[ 32 ];
		snprintf( token, sizeof( token ), "custom_%d", index );
		const int64_t observed = 300 + index;
		CHECK( ChangedOnce( registry.Observe( Adapter( 1000 + index, token,
			observed, observed + 100, false ), registry.GetRevision() ) ) );
	}
	CHECK( registry.GetObservationCount() == MP_MATCH_ITEM_TIMING_MAX_OBSERVATIONS );
	CHECK( registry.Observe( Adapter( 9999, "capacity_extra", 1000, 1100,
		false ), registry.GetRevision() ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_CAPACITY );
	CHECK( registry.ValidateInvariants() );

	// The maximum safe observation supports the maximum configured holdback;
	// the next millisecond is rejected before it can reach disclosure arithmetic.
	mpMatchItemTimingRegistry boundary;
	CHECK( ChangedOnce( boundary.BeginMap( 77 ) ) );
	const int64_t safeObserved = INT64_MAX -
		MP_MATCH_DISCLOSURE_MAX_ITEM_TIMING_DELAY_MSEC;
	mpMatchItemTimingObservationInput safe = Semantic( 1,
		MP_MATCH_ITEM_TIMING_KIND_INVISIBILITY, safeObserved, safeObserved, true );
	CHECK( ChangedOnce( boundary.Observe( safe, 1 ) ) );
	mpMatchDisclosurePolicy_t maximumDelay = Policy(
		MP_MATCH_DISCLOSURE_MAX_ITEM_TIMING_DELAY_MSEC );
	CHECK( boundary.ProjectCandidate( 1, maximumDelay,
		MP_MATCH_VIEW_AUDIENCE_REFEREE,
		mpMatchTime::FromMilliseconds( INT64_MAX ), candidate, &notBefore ) ==
		MP_MATCH_DISCLOSURE_ITEM_READY );
	CHECK( notBefore.Milliseconds() == INT64_MAX );
	safe.sourceId = 2;
	safe.observedAtMatchTime = mpMatchTime::FromMilliseconds( safeObserved + 1 );
	safe.matchDeadline = safe.observedAtMatchTime;
	CHECK( boundary.Observe( safe, boundary.GetRevision() ).reason ==
		MP_MATCH_ITEM_TIMING_REASON_CLOCK_OVERFLOW );

	registry.Clear();
	CHECK( registry.ValidateInvariants() );
	CHECK( registry.GetObservationCount() == 0 &&
		registry.FindObservation( 11 ) == 0 );
	return 0;
}
'''


def executable_contract() -> None:
    compiler = next(
        (path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))),
        None,
    )
    if compiler is None:
        print("mp_match_item_timing_contract: native checks skipped (no C++ compiler)")
        return

    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="match-item-timing-", dir=ROOT / ".tmp") as temp:
        temp_dir = Path(temp)
        harness = temp_dir / "match_item_timing_contract.cpp"
        executable = temp_dir / "match_item_timing_contract.exe"
        harness.write_text(HARNESS, encoding="utf-8")
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DMP_MATCH_SESSION_STANDALONE_TEST",
            "-DMP_MATCH_ITEM_TIMING_STANDALONE_TEST",
            f"-I{ROOT / 'src'}",
            str(harness),
            str(SOURCE),
            str(SESSION_SOURCE),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if compiled.returncode != 0:
            raise AssertionError(
                "standalone item-timing contract did not compile:\n"
                + compiled.stdout
                + compiled.stderr
            )
        ran = subprocess.run([str(executable)], cwd=ROOT, capture_output=True)
        if ran.returncode != 0:
            raise AssertionError(
                f"item-timing invariant failed at harness line {ran.returncode}:\n"
                + ran.stdout.decode("utf-8", errors="replace")
                + ran.stderr.decode("utf-8", errors="replace")
            )


def main() -> None:
    static_contracts(read(HEADER), read(SOURCE))
    executable_contract()
    print("mp_match_item_timing_contract: PASS")


if __name__ == "__main__":
    main()
