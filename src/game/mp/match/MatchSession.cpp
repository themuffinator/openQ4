//----------------------------------------------------------------
// MatchSession.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_SESSION_STANDALONE_TEST )
	#include "MatchSession.h"
	static const int MAX_CLIENTS = 32;
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchSession.h"
#endif

#include <limits.h>

static_assert( MAX_CLIENTS <= MP_MATCH_MAX_CONNECTION_SLOTS,
	"match-session protocol storage must cover every platform client slot" );

namespace {

struct mpLegalPhaseTransition {
	mpGameState_t			from;
	mpGameState_t			to;
	mpMatchTransitionReason_t reason;
};

// Keep this as data rather than branching spread through phase adapters.  The
// table is deliberately exhaustive and is verified by ValidateInvariants().
static const mpLegalPhaseTransition legalPhaseTransitions[] = {
	{ INACTIVE, WARMUP, MP_MATCH_TRANSITION_SESSION_INITIALIZED },

	{ WARMUP, COUNTDOWN, MP_MATCH_TRANSITION_READY_GATE },
	{ WARMUP, COUNTDOWN, MP_MATCH_TRANSITION_REFEREE_FORCE_READY },

	{ COUNTDOWN, WARMUP, MP_MATCH_TRANSITION_COUNTDOWN_ABORTED },
	{ COUNTDOWN, WARMUP, MP_MATCH_TRANSITION_RULES_INVALIDATED },
	{ COUNTDOWN, WARMUP, MP_MATCH_TRANSITION_ROSTER_INVALIDATED },
	{ COUNTDOWN, WARMUP, MP_MATCH_TRANSITION_REQUIRED_PLAYER_LOST },
	{ COUNTDOWN, GAMEON, MP_MATCH_TRANSITION_COUNTDOWN_COMPLETE },

	{ GAMEON, SUDDENDEATH, MP_MATCH_TRANSITION_REGULATION_TIE },
	{ GAMEON, GAMEREVIEW, MP_MATCH_TRANSITION_LIMIT_REACHED },
	{ GAMEON, GAMEREVIEW, MP_MATCH_TRANSITION_FORFEIT },
	{ GAMEON, GAMEREVIEW, MP_MATCH_TRANSITION_MATCH_ABORTED },

	{ SUDDENDEATH, GAMEREVIEW, MP_MATCH_TRANSITION_LIMIT_REACHED },
	{ SUDDENDEATH, GAMEREVIEW, MP_MATCH_TRANSITION_FORFEIT },
	{ SUDDENDEATH, GAMEREVIEW, MP_MATCH_TRANSITION_MATCH_ABORTED },

	{ GAMEREVIEW, NEXTGAME, MP_MATCH_TRANSITION_REVIEW_COMPLETE },
	{ GAMEREVIEW, NEXTGAME, MP_MATCH_TRANSITION_REFEREE_ADVANCE },

	{ NEXTGAME, WARMUP, MP_MATCH_TRANSITION_SAME_MAP_RESTART },
	{ NEXTGAME, WARMUP, MP_MATCH_TRANSITION_NEXT_MAP_READY },
	{ NEXTGAME, INACTIVE, MP_MATCH_TRANSITION_SESSION_END },

	{ WARMUP, INACTIVE, MP_MATCH_TRANSITION_MAP_SHUTDOWN },
	{ COUNTDOWN, INACTIVE, MP_MATCH_TRANSITION_MAP_SHUTDOWN },
	{ GAMEON, INACTIVE, MP_MATCH_TRANSITION_MAP_SHUTDOWN },
	{ SUDDENDEATH, INACTIVE, MP_MATCH_TRANSITION_MAP_SHUTDOWN },
	{ GAMEREVIEW, INACTIVE, MP_MATCH_TRANSITION_MAP_SHUTDOWN },
	{ NEXTGAME, INACTIVE, MP_MATCH_TRANSITION_MAP_SHUTDOWN },
	{ WARMUP, INACTIVE, MP_MATCH_TRANSITION_FATAL_RESET },
	{ COUNTDOWN, INACTIVE, MP_MATCH_TRANSITION_FATAL_RESET },
	{ GAMEON, INACTIVE, MP_MATCH_TRANSITION_FATAL_RESET },
	{ SUDDENDEATH, INACTIVE, MP_MATCH_TRANSITION_FATAL_RESET },
	{ GAMEREVIEW, INACTIVE, MP_MATCH_TRANSITION_FATAL_RESET },
	{ NEXTGAME, INACTIVE, MP_MATCH_TRANSITION_FATAL_RESET }
};

static const int numLegalPhaseTransitions =
	static_cast<int>( sizeof( legalPhaseTransitions ) / sizeof( legalPhaseTransitions[ 0 ] ) );

struct mpLegalRoundTransition {
	roundState_t				from;
	roundState_t				to;
	mpMatchRoundTransitionReason_t reason;
};

static const mpLegalRoundTransition legalRoundTransitions[] = {
	{ RS_INACTIVE, RS_COUNTDOWN, MP_MATCH_ROUND_TRANSITION_PARENT_ACTIVE },
	{ RS_COUNTDOWN, RS_ACTIVE, MP_MATCH_ROUND_TRANSITION_COUNTDOWN_COMPLETE },
	{ RS_ACTIVE, RS_COMPLETE, MP_MATCH_ROUND_TRANSITION_RESULT_COMMITTED },
	{ RS_COMPLETE, RS_COUNTDOWN, MP_MATCH_ROUND_TRANSITION_NEXT_ROUND },
	{ RS_COUNTDOWN, RS_INACTIVE, MP_MATCH_ROUND_TRANSITION_PARENT_LEFT_ACTIVE },
	{ RS_ACTIVE, RS_INACTIVE, MP_MATCH_ROUND_TRANSITION_PARENT_LEFT_ACTIVE },
	{ RS_COMPLETE, RS_INACTIVE, MP_MATCH_ROUND_TRANSITION_PARENT_LEFT_ACTIVE },
	{ RS_COUNTDOWN, RS_INACTIVE, MP_MATCH_ROUND_TRANSITION_SESSION_RESET },
	{ RS_ACTIVE, RS_INACTIVE, MP_MATCH_ROUND_TRANSITION_SESSION_RESET },
	{ RS_COMPLETE, RS_INACTIVE, MP_MATCH_ROUND_TRANSITION_SESSION_RESET }
};

static const int numLegalRoundTransitions =
	static_cast<int>( sizeof( legalRoundTransitions ) / sizeof( legalRoundTransitions[ 0 ] ) );

static bool IsValidPhase( mpGameState_t phase ) {
	return phase >= INACTIVE && phase < STATE_COUNT;
}

static bool IsValidTransitionReason( mpMatchTransitionReason_t reason ) {
	return reason > MP_MATCH_TRANSITION_NONE && reason < MP_MATCH_TRANSITION_REASON_COUNT;
}

static bool IsValidRoundState( roundState_t state ) {
	return state >= RS_INACTIVE && state < RS_STATE_COUNT;
}

static bool IsValidRoundTransitionReason( mpMatchRoundTransitionReason_t reason ) {
	return reason > MP_MATCH_ROUND_TRANSITION_NONE &&
		reason < MP_MATCH_ROUND_TRANSITION_REASON_COUNT;
}

static bool IsValidPauseReason( mpMatchPauseReason_t reason ) {
	return reason > MP_MATCH_PAUSE_REASON_NONE && reason < MP_MATCH_PAUSE_REASON_COUNT;
}

static bool IsValidTeamTimeoutReason( mpMatchPauseReason_t reason ) {
	return reason == MP_MATCH_PAUSE_REASON_TACTICAL ||
		reason == MP_MATCH_PAUSE_REASON_PLAYER_DISCONNECT;
}

static bool IsValidTechnicalPauseReason( mpMatchPauseReason_t reason ) {
	return reason == MP_MATCH_PAUSE_REASON_PLAYER_DISCONNECT ||
		reason == MP_MATCH_PAUSE_REASON_TECHNICAL_FAULT ||
		reason == MP_MATCH_PAUSE_REASON_SERVER_FAULT ||
		reason == MP_MATCH_PAUSE_REASON_REFEREE;
}

static uint32_t AllReadinessBlockerBits( void ) {
	return ( static_cast<uint32_t>( 1 ) << MP_MATCH_BLOCKER_COUNT ) - 1;
}

static_assert( MP_MATCH_BLOCKER_COUNT < 32,
	"readiness blockers must fit their stable 32-bit wire/evidence mask" );

static uint32_t ExternalReadinessBlockerBits( void ) {
	return MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_RULES_INVALID ) |
		MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_MAP_INVALID ) |
		MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_VETO_INCOMPLETE );
}

static uint32_t NonOverridableReadinessBlockerBits( void ) {
	return MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_RULES_NOT_FROZEN ) |
		MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_RULES_INVALID ) |
		MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_MAP_INVALID ) |
		MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_VETO_INCOMPLETE );
}

static bool AddNonNegativeMsec( int64_t base, int64_t delta, int64_t &result ) {
	if ( base < 0 || delta < 0 || base > INT64_MAX - delta ) {
		return false;
	}
	result = base + delta;
	return true;
}

} // namespace

/*
===============================================================================

	Strong clock-domain values

===============================================================================
*/

#define MP_DEFINE_MATCH_CLOCK_VALUE( typeName ) \
	typeName::typeName( void ) : msec( 0 ) {} \
	typeName::typeName( int64_t value ) : msec( value ) {} \
	typeName typeName::FromMilliseconds( int64_t value ) { return typeName( value ); } \
	int64_t typeName::Milliseconds( void ) const { return msec; } \
	bool typeName::IsValid( void ) const { return msec >= 0; } \
	bool typeName::operator==( const typeName &rhs ) const { return msec == rhs.msec; } \
	bool typeName::operator!=( const typeName &rhs ) const { return msec != rhs.msec; } \
	bool typeName::operator<( const typeName &rhs ) const { return msec < rhs.msec; } \
	bool typeName::operator<=( const typeName &rhs ) const { return msec <= rhs.msec; } \
	bool typeName::operator>( const typeName &rhs ) const { return msec > rhs.msec; } \
	bool typeName::operator>=( const typeName &rhs ) const { return msec >= rhs.msec; }

MP_DEFINE_MATCH_CLOCK_VALUE( mpMatchEngineTime )
MP_DEFINE_MATCH_CLOCK_VALUE( mpMatchTime )
MP_DEFINE_MATCH_CLOCK_VALUE( mpMatchHostTime )

#undef MP_DEFINE_MATCH_CLOCK_VALUE

/*
===============================================================================

	Identity, results and capability bundles

===============================================================================
*/

mpParticipantId::mpParticipantId( void ) : session( 0 ), sequence( 0 ) {
}

mpParticipantId::mpParticipantId( uint64_t sessionPart, uint32_t sequencePart ) :
	session( sessionPart ), sequence( sequencePart ) {
}

mpParticipantId mpParticipantId::Invalid( void ) {
	return mpParticipantId();
}

bool mpParticipantId::IsValid( void ) const {
	return session != 0 && sequence != 0;
}

uint64_t mpParticipantId::SessionPart( void ) const {
	return session;
}

uint32_t mpParticipantId::SequencePart( void ) const {
	return sequence;
}

bool mpParticipantId::operator==( const mpParticipantId &rhs ) const {
	return session == rhs.session && sequence == rhs.sequence;
}

bool mpParticipantId::operator!=( const mpParticipantId &rhs ) const {
	return !( *this == rhs );
}

bool mpMatchMutationResult::WasApplied( void ) const {
	return code == MP_MATCH_MUTATION_APPLIED;
}

bool mpMatchMutationResult::WasRejected( void ) const {
	return code == MP_MATCH_MUTATION_REJECTED;
}

mpMatchRoleMask_t MPMatchRoleBit( mpMatchRole_t role ) {
	if ( role <= MP_MATCH_ROLE_NONE || role >= MP_MATCH_ROLE_COUNT ) {
		return 0;
	}
	return static_cast<mpMatchRoleMask_t>( 1 ) << static_cast<int>( role );
}

mpMatchCapabilityMask_t MPMatchCapabilityBit( mpMatchCapability_t capability ) {
	if ( capability < 0 || capability >= MP_MATCH_CAP_COUNT ) {
		return 0;
	}
	return static_cast<mpMatchCapabilityMask_t>( 1 ) << static_cast<int>( capability );
}

mpMatchCapabilityMask_t MPMatchCapabilitiesForRole( mpMatchRole_t role ) {
	switch ( role ) {
		case MP_MATCH_ROLE_PLAYER:
			return MPMatchCapabilityBit( MP_MATCH_CAP_SELF_READY ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_SELF_TEAM_AND_QUEUE ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_ELIGIBLE_PROPOSAL );

		case MP_MATCH_ROLE_CAPTAIN:
			return MPMatchCapabilityBit( MP_MATCH_CAP_TEAM_READY ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_ELIGIBLE_PROPOSAL ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_OWN_TEAM_ROSTER ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_OWN_TEAM_LOCK ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_OWN_TEAM_TIMEOUT );

		case MP_MATCH_ROLE_COACH:
			return MPMatchCapabilityBit( MP_MATCH_CAP_TEAM_OBSERVATION ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_TEAM_COMMUNICATION );

		case MP_MATCH_ROLE_BROADCASTER:
			return MPMatchCapabilityBit( MP_MATCH_CAP_BROADCAST_OBSERVATION );

		case MP_MATCH_ROLE_REFEREE:
			return MPMatchCapabilityBit( MP_MATCH_CAP_PHASE_CONTROL ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_PAUSE_CONTROL ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_ROSTER_CONTROL ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_RULES_BOUNDARY ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_VETO_CONTROL ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_PROPOSAL_MODERATION );

		case MP_MATCH_ROLE_SERVER_OPERATOR:
			return MPMatchCapabilityBit( MP_MATCH_CAP_PHASE_CONTROL ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_PAUSE_CONTROL ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_ROSTER_CONTROL ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_RULES_BOUNDARY ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_VETO_CONTROL ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_PROPOSAL_MODERATION ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_SERVER_POLICY ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_AUTH_CREDENTIALS ) |
				MPMatchCapabilityBit( MP_MATCH_CAP_FILESYSTEM_OUTPUT );

		default:
			return 0;
	}
}

mpMatchCapabilityMask_t MPMatchCapabilitiesForRoles( mpMatchRoleMask_t roles ) {
	mpMatchCapabilityMask_t capabilities = 0;
	for ( int role = MP_MATCH_ROLE_PLAYER; role < MP_MATCH_ROLE_COUNT; ++role ) {
		if ( roles & MPMatchRoleBit( static_cast<mpMatchRole_t>( role ) ) ) {
			capabilities |= MPMatchCapabilitiesForRole( static_cast<mpMatchRole_t>( role ) );
		}
	}
	return capabilities;
}

bool MPMatchRoleMaskIsValid( mpMatchRoleMask_t roles, bool connectionScoped ) {
	const mpMatchRoleMask_t knownRoles =
		( static_cast<mpMatchRoleMask_t>( 1 ) << MP_MATCH_ROLE_COUNT ) - 2;
	if ( roles & ~knownRoles ) {
		return false;
	}
	const bool player = ( roles & MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) ) != 0;
	const bool captain = ( roles & MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN ) ) != 0;
	const bool coach = ( roles & MPMatchRoleBit( MP_MATCH_ROLE_COACH ) ) != 0;
	const bool broadcaster =
		( roles & MPMatchRoleBit( MP_MATCH_ROLE_BROADCASTER ) ) != 0;
	const bool referee = ( roles & MPMatchRoleBit( MP_MATCH_ROLE_REFEREE ) ) != 0;
	const bool serverOperator =
		( roles & MPMatchRoleBit( MP_MATCH_ROLE_SERVER_OPERATOR ) ) != 0;

	// Player/captain is the only intentionally composite principal.  Tactical
	// observer roles are exclusive so one connection can never receive two
	// disclosure audiences, and server-operator authority is never stored on a
	// network participant.
	if ( ( captain && !player ) || ( coach && ( player || captain ) ) ||
		( broadcaster && ( player || captain || coach || referee ) ) ||
		( referee && ( player || captain || coach || broadcaster ) ) ||
		( serverOperator && roles != MPMatchRoleBit( MP_MATCH_ROLE_SERVER_OPERATOR ) ) ||
		( connectionScoped && serverOperator ) ) {
		return false;
	}
	return true;
}

bool MPMatchRosterRoleIsValid( mpMatchRosterRole_t role ) {
	return role >= MP_MATCH_ROSTER_PLAYER && role < MP_MATCH_ROSTER_ROLE_COUNT;
}

bool MPMatchRosterRoleIsActive( mpMatchRosterRole_t role ) {
	return role == MP_MATCH_ROSTER_PLAYER || role == MP_MATCH_ROSTER_CAPTAIN;
}

mpMatchRoleMask_t MPMatchRosterPrincipalRoleMask( void ) {
	return MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) |
		MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN ) |
		MPMatchRoleBit( MP_MATCH_ROLE_COACH );
}

mpMatchRoleMask_t MPMatchPrincipalRolesForRosterRole(
		mpMatchRosterRole_t role ) {
	switch ( role ) {
		case MP_MATCH_ROSTER_PLAYER:
			return MPMatchRoleBit( MP_MATCH_ROLE_PLAYER );
		case MP_MATCH_ROSTER_CAPTAIN:
			return MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) |
				MPMatchRoleBit( MP_MATCH_ROLE_CAPTAIN );
		case MP_MATCH_ROSTER_COACH:
			return MPMatchRoleBit( MP_MATCH_ROLE_COACH );
		case MP_MATCH_ROSTER_SUBSTITUTE:
			return 0;
		default:
			return 0;
	}
}

mpMatchReadinessBlockerMask_t MPMatchReadinessBlockerBit( mpMatchReadinessBlocker_t blocker ) {
	if ( blocker < 0 || blocker >= MP_MATCH_BLOCKER_COUNT ) {
		return 0;
	}
	return static_cast<mpMatchReadinessBlockerMask_t>( 1 ) << static_cast<int>( blocker );
}

mpMatchReadinessPolicy::mpMatchReadinessPolicy( void ) :
	policy( MP_MATCH_READY_DISABLED ),
	botPolicy( MP_MATCH_BOTS_EXCLUDED ),
	teamMode( false ),
	minimumActiveHumans( 0 ),
	minimumActiveParticipants( 0 ),
	minimumActiveOnAnySide( 0 ),
	minimumActivePerRequiredSide( 0 ),
	readyThresholdBasisPoints( 10000 ),
	maximumActivePerSide( 0 ),
	requiredSideMask( 0 ),
	requireDeclaredRosterSeats( false ),
	allowRoundSideChanges( false ) {
}

bool mpMatchReadinessView::IsReady( void ) const {
	return blockers == 0;
}

/*
===============================================================================

	mpMatchSession setup and common mutation helpers

===============================================================================
*/

mpMatchSession::mpMatchSession( void ) {
	Reset( 1, mpMatchEngineTime::FromMilliseconds( 0 ) );
}

bool mpMatchSession::Reset( uint64_t newSessionId, mpMatchEngineTime initialEngineTime ) {
	if ( newSessionId == 0 || !initialEngineTime.IsValid() ) {
		return false;
	}

	sessionId = newSessionId;
	sessionRevision = 1;
	nextParticipantSequence = 1;
	phase = INACTIVE;

	lastTransition.from = INACTIVE;
	lastTransition.to = INACTIVE;
	lastTransition.reason = MP_MATCH_TRANSITION_NONE;
	lastTransition.authorizer = mpParticipantId::Invalid();
	lastTransition.forcedReadinessBlockers = 0;
	roundState = RS_INACTIVE;
	lastRoundTransition.from = RS_INACTIVE;
	lastRoundTransition.to = RS_INACTIVE;
	lastRoundTransition.reason = MP_MATCH_ROUND_TRANSITION_NONE;

	frozenRulesValid = false;
	frozenRulesRevision = 0;
	frozenRulesDigest = 0;
	regulationDurationMsec = 0;

	engineTime = initialEngineTime;
	matchTime = mpMatchTime::FromMilliseconds( 0 );
	livePeriodStartMatchTime = mpMatchTime::FromMilliseconds( 0 );
	ResetLivePeriod();
	ClearPause();

	for ( int slot = 0; slot < MP_MATCH_MAX_CONNECTION_SLOTS; ++slot ) {
		slotGenerations[ slot ] = 0;
		slotParticipantIndex[ slot ] = -1;
	}
	for ( int index = 0; index < MP_MATCH_MAX_PARTICIPANTS; ++index ) {
		ClearParticipantState( participants[ index ] );
	}

	readinessPolicy.policy = MP_MATCH_READY_DISABLED;
	readinessPolicy.botPolicy = MP_MATCH_BOTS_EXCLUDED;
	readinessPolicy.teamMode = false;
	readinessPolicy.minimumActiveHumans = 0;
	readinessPolicy.minimumActiveParticipants = 0;
	readinessPolicy.minimumActiveOnAnySide = 0;
	readinessPolicy.minimumActivePerRequiredSide = 0;
	readinessPolicy.readyThresholdBasisPoints = 10000;
	readinessPolicy.maximumActivePerSide = 0;
	readinessPolicy.requiredSideMask = 0;
	readinessPolicy.requireDeclaredRosterSeats = false;
	readinessPolicy.allowRoundSideChanges = false;
	externalReadinessBlockers = 0;
	ClearTeamReady();
	lastForcedReadinessBlockers = 0;

	for ( int seat = 0; seat < MP_MATCH_MAX_ROSTER_SEATS; ++seat ) {
		roster[ seat ].declared = false;
		roster[ seat ].required = false;
		roster[ seat ].side = MP_MATCH_SIDE_NONE;
		roster[ seat ].role = MP_MATCH_ROSTER_PLAYER;
		roster[ seat ].occupant = mpParticipantId::Invalid();
	}

	for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
		timeoutBudgets[ side ].configured = 0;
		timeoutBudgets[ side ].remaining = 0;
		timeoutBudgets[ side ].consumed = 0;
	}
	timeoutDurationMsec = 0;
	technicalPauseDurationMsec = 0;
	technicalPauseExpires = false;
	timeoutAllowedDuringCountdown = false;
	resumeCountdownMsec = 0;
	resumePolicy = MP_MATCH_RESUME_OWNER_OR_AUTHORITY;

	return ValidateInvariants();
}

uint64_t mpMatchSession::GetSessionId( void ) const {
	return sessionId;
}

uint64_t mpMatchSession::GetSessionRevision( void ) const {
	return sessionRevision;
}

mpGameState_t mpMatchSession::GetPhase( void ) const {
	return phase;
}

const mpMatchTransitionView &mpMatchSession::GetLastTransition( void ) const {
	return lastTransition;
}

bool mpMatchSession::IsExpectedRevision( uint64_t expectedRevision ) const {
	return expectedRevision == sessionRevision;
}

bool mpMatchSession::CanCommit( void ) const {
	return sessionRevision < UINT64_MAX;
}

mpMatchMutationResult mpMatchSession::Applied( uint64_t previousRevision ) {
	mpMatchMutationResult result;
	result.code = MP_MATCH_MUTATION_APPLIED;
	result.reason = MP_MATCH_REASON_NONE;
	result.previousRevision = previousRevision;
	++sessionRevision;
	result.currentRevision = sessionRevision;
	return result;
}

mpMatchMutationResult mpMatchSession::NoChange( mpMatchReason_t reason ) const {
	mpMatchMutationResult result;
	result.code = MP_MATCH_MUTATION_NO_CHANGE;
	result.reason = reason;
	result.previousRevision = sessionRevision;
	result.currentRevision = sessionRevision;
	return result;
}

mpMatchMutationResult mpMatchSession::Rejected( mpMatchReason_t reason ) const {
	mpMatchMutationResult result;
	result.code = MP_MATCH_MUTATION_REJECTED;
	result.reason = reason;
	result.previousRevision = sessionRevision;
	result.currentRevision = sessionRevision;
	return result;
}

/*
===============================================================================

	Lifecycle and frozen rules boundary

===============================================================================
*/

bool mpMatchSession::IsLegalPhaseTransition( mpGameState_t from, mpGameState_t to,
		mpMatchTransitionReason_t reason ) {
	if ( !IsValidPhase( from ) || !IsValidPhase( to ) || from == to || !IsValidTransitionReason( reason ) ) {
		return false;
	}
	for ( int index = 0; index < numLegalPhaseTransitions; ++index ) {
		const mpLegalPhaseTransition &entry = legalPhaseTransitions[ index ];
		if ( entry.from == from && entry.to == to && entry.reason == reason ) {
			return true;
		}
	}
	return false;
}

roundState_t mpMatchSession::GetRoundState( void ) const {
	return roundState;
}

const mpMatchRoundTransitionView &mpMatchSession::GetLastRoundTransition( void ) const {
	return lastRoundTransition;
}

bool mpMatchSession::IsLegalRoundTransition( roundState_t from, roundState_t to,
		mpMatchRoundTransitionReason_t reason ) {
	if ( !IsValidRoundState( from ) || !IsValidRoundState( to ) || from == to ||
		!IsValidRoundTransitionReason( reason ) ) {
		return false;
	}
	for ( int index = 0; index < numLegalRoundTransitions; ++index ) {
		const mpLegalRoundTransition &entry = legalRoundTransitions[ index ];
		if ( entry.from == from && entry.to == to && entry.reason == reason ) {
			return true;
		}
	}
	return false;
}

mpMatchMutationResult mpMatchSession::TransitionRound( roundState_t to,
		mpMatchRoundTransitionReason_t reason, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	const bool explicitRoundReset = to == RS_INACTIVE &&
		reason == MP_MATCH_ROUND_TRANSITION_SESSION_RESET;
	if ( ( !IsLivePhase() || pause.state != MP_MATCH_PAUSE_RUNNING ) &&
		!explicitRoundReset ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( !IsLegalRoundTransition( roundState, to, reason ) ||
		( to == RS_INACTIVE && !explicitRoundReset ) ) {
		// Parent normalization is atomic with its phase mutation; feature code may
		// not manufacture a second revision to leave active round play.
		return Rejected( MP_MATCH_REASON_ILLEGAL_ROUND_TRANSITION );
	}
	const uint64_t previousRevision = sessionRevision;
	lastRoundTransition.from = roundState;
	lastRoundTransition.to = to;
	lastRoundTransition.reason = reason;
	roundState = to;
	return Applied( previousRevision );
}

void mpMatchSession::NormalizeRoundForParentTransition( void ) {
	if ( roundState == RS_INACTIVE ) {
		return;
	}
	lastRoundTransition.from = roundState;
	lastRoundTransition.to = RS_INACTIVE;
	lastRoundTransition.reason = MP_MATCH_ROUND_TRANSITION_PARENT_LEFT_ACTIVE;
	roundState = RS_INACTIVE;
}

mpMatchMutationResult mpMatchSession::BeginCountdown( uint64_t rulesRevision,
		uint64_t rulesDigest, mpMatchTransitionReason_t reason,
		mpParticipantId authorizer, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != WARMUP ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( !IsLegalPhaseTransition( WARMUP, COUNTDOWN, reason ) ||
		( reason != MP_MATCH_TRANSITION_READY_GATE &&
			reason != MP_MATCH_TRANSITION_REFEREE_FORCE_READY ) ) {
		return Rejected( MP_MATCH_REASON_ILLEGAL_PHASE_TRANSITION );
	}
	if ( pause.state != MP_MATCH_PAUSE_RUNNING ) {
		return Rejected( MP_MATCH_REASON_PAUSE_ALREADY_ACTIVE );
	}
	if ( rulesRevision == 0 ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}

	// Evaluate the complete candidate boundary before touching the currently
	// published frozen identity.  The supplied non-zero rules revision satisfies
	// only RULES_NOT_FROZEN; every other blocker retains its normal force policy.
	mpMatchReadinessView readiness = EvaluateReadiness();
	readiness.blockers &= ~MPMatchReadinessBlockerBit(
		MP_MATCH_BLOCKER_RULES_NOT_FROZEN );
	if ( reason == MP_MATCH_TRANSITION_READY_GATE && !readiness.IsReady() ) {
		return Rejected( MP_MATCH_REASON_READINESS_BLOCKED );
	}
	if ( reason == MP_MATCH_TRANSITION_REFEREE_FORCE_READY &&
		( readiness.blockers & NonOverridableReadinessBlockerBits() ) ) {
		return Rejected( MP_MATCH_REASON_READINESS_BLOCKED );
	}

	const uint64_t previousRevision = sessionRevision;
	frozenRulesValid = true;
	frozenRulesRevision = rulesRevision;
	frozenRulesDigest = rulesDigest;
	phase = COUNTDOWN;
	lastTransition.from = WARMUP;
	lastTransition.to = COUNTDOWN;
	lastTransition.reason = reason;
	lastTransition.authorizer = authorizer;
	lastTransition.forcedReadinessBlockers =
		reason == MP_MATCH_TRANSITION_REFEREE_FORCE_READY ? readiness.blockers : 0;
	lastForcedReadinessBlockers = lastTransition.forcedReadinessBlockers;
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::TransitionPhase( mpGameState_t to,
		mpMatchTransitionReason_t reason, mpParticipantId authorizer,
		uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( !IsLegalPhaseTransition( phase, to, reason ) ) {
		return Rejected( MP_MATCH_REASON_ILLEGAL_PHASE_TRANSITION );
	}
	if ( pause.state != MP_MATCH_PAUSE_RUNNING ) {
		const bool pausedAbortOrForfeit = to == GAMEREVIEW &&
			( reason == MP_MATCH_TRANSITION_MATCH_ABORTED ||
				reason == MP_MATCH_TRANSITION_FORFEIT );
		const bool pausedCountdownCancellation = phase == COUNTDOWN && to == WARMUP;
		const bool sessionTeardown = to == INACTIVE;
		if ( !pausedAbortOrForfeit && !pausedCountdownCancellation && !sessionTeardown ) {
			return Rejected( MP_MATCH_REASON_PAUSE_ALREADY_ACTIVE );
		}
	}

	mpMatchReadinessView readiness = EvaluateReadiness();
	if ( to == COUNTDOWN ) {
		if ( !frozenRulesValid ) {
			return Rejected( MP_MATCH_REASON_RULES_NOT_FROZEN );
		}
		if ( reason == MP_MATCH_TRANSITION_READY_GATE && !readiness.IsReady() ) {
			return Rejected( MP_MATCH_REASON_READINESS_BLOCKED );
		}
		if ( reason == MP_MATCH_TRANSITION_REFEREE_FORCE_READY &&
			( readiness.blockers & NonOverridableReadinessBlockerBits() ) ) {
			return Rejected( MP_MATCH_REASON_READINESS_BLOCKED );
		}
	}
	if ( phase == COUNTDOWN && to == GAMEON && !frozenRulesValid ) {
		return Rejected( MP_MATCH_REASON_RULES_NOT_FROZEN );
	}
	mpMatchTime regulationDeadline = matchTime;
	if ( to == GAMEON && regulationDurationMsec > 0 ) {
		int64_t deadlineMsec = 0;
		if ( !AddNonNegativeMsec( matchTime.Milliseconds(), regulationDurationMsec,
				deadlineMsec ) ) {
			return Rejected( MP_MATCH_REASON_CLOCK_OVERFLOW );
		}
		regulationDeadline = mpMatchTime::FromMilliseconds( deadlineMsec );
	}

	const uint64_t previousRevision = sessionRevision;
	const mpGameState_t from = phase;
	phase = to;
	lastTransition.from = from;
	lastTransition.to = to;
	lastTransition.reason = reason;
	lastTransition.authorizer = authorizer;
	lastTransition.forcedReadinessBlockers =
		reason == MP_MATCH_TRANSITION_REFEREE_FORCE_READY ? readiness.blockers : 0;
	if ( reason == MP_MATCH_TRANSITION_REFEREE_FORCE_READY ) {
		lastForcedReadinessBlockers = readiness.blockers;
	} else if ( to == COUNTDOWN ) {
		lastForcedReadinessBlockers = 0;
	}

	if ( to == GAMEON ) {
		livePeriodStartMatchTime = matchTime;
		livePeriod.period = 0;
		livePeriod.start = matchTime;
		livePeriod.deadline = regulationDeadline;
		livePeriod.hasDeadline = regulationDurationMsec > 0;
	}
	if ( !IsLivePhase() ) {
		NormalizeRoundForParentTransition();
	}
	if ( to == WARMUP || to == GAMEREVIEW || to == INACTIVE ) {
		ClearPause();
	}
	if ( to == WARMUP || to == INACTIVE ) {
		ResetLivePeriod();
	}
	if ( from == NEXTGAME && to == WARMUP ) {
		frozenRulesValid = false;
		frozenRulesRevision = 0;
		frozenRulesDigest = 0;
		for ( int index = 0; index < MP_MATCH_MAX_PARTICIPANTS; ++index ) {
			if ( participants[ index ].connected ) {
				participants[ index ].ready = false;
			}
		}
		ClearTeamReady();
	}

	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::FreezeRules( uint64_t rulesRevision,
		uint64_t rulesDigest, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != INACTIVE && phase != WARMUP ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( rulesRevision == 0 ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	if ( frozenRulesValid && frozenRulesRevision == rulesRevision && frozenRulesDigest == rulesDigest ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}

	const uint64_t previousRevision = sessionRevision;
	frozenRulesValid = true;
	frozenRulesRevision = rulesRevision;
	frozenRulesDigest = rulesDigest;
	lastForcedReadinessBlockers = 0;
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::ClearFrozenRules( uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != INACTIVE && phase != WARMUP ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( !frozenRulesValid ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}

	const uint64_t previousRevision = sessionRevision;
	frozenRulesValid = false;
	frozenRulesRevision = 0;
	frozenRulesDigest = 0;
	lastForcedReadinessBlockers = 0;
	return Applied( previousRevision );
}

bool mpMatchSession::HasFrozenRules( void ) const {
	return frozenRulesValid;
}

uint64_t mpMatchSession::GetFrozenRulesRevision( void ) const {
	return frozenRulesRevision;
}

uint64_t mpMatchSession::GetFrozenRulesDigest( void ) const {
	return frozenRulesDigest;
}

void mpMatchSession::ResetLivePeriod( void ) {
	livePeriod.period = 0;
	livePeriod.start = matchTime;
	livePeriod.deadline = matchTime;
	livePeriod.hasDeadline = false;
}

mpMatchMutationResult mpMatchSession::ConfigureRegulationPeriod( int durationMsec,
		uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != INACTIVE && phase != WARMUP ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( durationMsec < 0 || durationMsec > MP_MATCH_MAX_LIVE_PERIOD_MSEC ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	if ( regulationDurationMsec == durationMsec ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}
	const uint64_t previousRevision = sessionRevision;
	regulationDurationMsec = durationMsec;
	ResetLivePeriod();
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::BeginOvertimePeriod(
		uint32_t expectedCurrentPeriod, int durationMsec,
		uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != GAMEON || pause.state != MP_MATCH_PAUSE_RUNNING ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( durationMsec <= 0 || durationMsec > MP_MATCH_MAX_LIVE_PERIOD_MSEC ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	if ( expectedCurrentPeriod != livePeriod.period ) {
		return Rejected( MP_MATCH_REASON_LIVE_PERIOD_MISMATCH );
	}
	if ( livePeriod.period == UINT32_MAX ) {
		return Rejected( MP_MATCH_REASON_LIVE_PERIOD_EXHAUSTED );
	}
	if ( !livePeriod.hasDeadline || matchTime < livePeriod.deadline ) {
		return Rejected( MP_MATCH_REASON_LIVE_PERIOD_NOT_EXPIRED );
	}
	int64_t deadlineMsec = 0;
	if ( !AddNonNegativeMsec( livePeriod.deadline.Milliseconds(), durationMsec,
			deadlineMsec ) ) {
		return Rejected( MP_MATCH_REASON_CLOCK_OVERFLOW );
	}

	const uint64_t previousRevision = sessionRevision;
	++livePeriod.period;
	livePeriod.start = livePeriod.deadline;
	livePeriod.deadline = mpMatchTime::FromMilliseconds( deadlineMsec );
	livePeriod.hasDeadline = true;
	return Applied( previousRevision );
}

const mpMatchLivePeriodView &mpMatchSession::GetLivePeriod( void ) const {
	return livePeriod;
}

int mpMatchSession::GetRegulationDurationMsec( void ) const {
	return regulationDurationMsec;
}

/*
===============================================================================

	Clock accumulator and pause-frame advancement

===============================================================================
*/

bool mpMatchSession::IsLivePhase( void ) const {
	return phase == GAMEON || phase == SUDDENDEATH;
}

bool mpMatchSession::IsMatchClockRunning( void ) const {
	// A pending request is committed at the next frame boundary before that
	// frame's gameplay simulation.  Counting the boundary delta here would add
	// time for a simulation frame which is deliberately skipped.
	//
	// openQ4: this reports the published pause overlay, so AdvanceFrame cannot
	// use it directly for the frame which completes a resume: that frame is
	// simulated, and its delta belongs to the match clock.  See AdvanceFrame.
	return IsLivePhase() && pause.state == MP_MATCH_PAUSE_RUNNING;
}

bool mpMatchSession::AddEngineDuration( mpMatchEngineTime base, int durationMsec,
		mpMatchEngineTime &outTime ) const {
	int64_t result = 0;
	if ( durationMsec < 0 || !AddNonNegativeMsec( base.Milliseconds(), durationMsec, result ) ) {
		return false;
	}
	outTime = mpMatchEngineTime::FromMilliseconds( result );
	return true;
}

mpMatchMutationResult mpMatchSession::AdvanceFrame( mpMatchEngineTime engineNow ) {
	if ( !engineNow.IsValid() || engineNow < engineTime ) {
		return Rejected( MP_MATCH_REASON_CLOCK_REGRESSION );
	}

	const int64_t delta = engineNow.Milliseconds() - engineTime.Milliseconds();

	// openQ4: the pause overlay transitions at this frame boundary, before the
	// frame's gameplay simulation, and the adapter decides whether to simulate
	// from the post-transition overlay ( idMultiplayerGame::IsGameplayFrozen is
	// pause.state != MP_MATCH_PAUSE_RUNNING, evaluated after this call ).  The
	// transitions are therefore resolved first, and the accrual below is gated on
	// the post-transition overlay so that the frame which completes a resume -
	// which is simulated - is also counted.
	const bool commitPendingPause = pause.state == MP_MATCH_PAUSE_PENDING;
	// a team timeout always expires; a technical pause only when it was given a
	// duration (openQ4 MP-12: the disconnect pause)
	const bool timeoutExpires = pause.state == MP_MATCH_PAUSED &&
		( pause.kind == MP_MATCH_PAUSE_KIND_TEAM_TIMEOUT ||
			( pause.kind == MP_MATCH_PAUSE_KIND_TECHNICAL && technicalPauseExpires ) ) &&
		engineNow >= pause.pauseExpiry;
	const bool resumeCompletes = pause.state == MP_MATCH_RESUME_COUNTDOWN &&
		engineNow >= pause.resumeDeadline;
	const bool semanticMutation = commitPendingPause || timeoutExpires || resumeCompletes;

	// Only the resume completion publishes MP_MATCH_PAUSE_RUNNING, so this is
	// exactly "the overlay is running once this boundary has been applied".
	const bool clockRunsThisFrame = IsLivePhase() &&
		( pause.state == MP_MATCH_PAUSE_RUNNING || resumeCompletes );
	int64_t nextMatchMsec = matchTime.Milliseconds();
	if ( clockRunsThisFrame &&
		!AddNonNegativeMsec( matchTime.Milliseconds(), delta, nextMatchMsec ) ) {
		return Rejected( MP_MATCH_REASON_CLOCK_OVERFLOW );
	}

	if ( semanticMutation && !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( commitPendingPause && pause.kind == MP_MATCH_PAUSE_KIND_TEAM_TIMEOUT ) {
		const mpMatchTimeoutBudgetState &budget = timeoutBudgets[ pause.ownerSide ];
		if ( pause.budgetCharged || budget.remaining <= 0 ) {
			return Rejected( pause.budgetCharged ? MP_MATCH_REASON_TIMEOUT_ALREADY_CHARGED :
				MP_MATCH_REASON_TIMEOUT_UNAVAILABLE );
		}
	}

	mpMatchEngineTime pendingExpiry;
	mpMatchEngineTime automaticResumeDeadline;
	if ( commitPendingPause && pause.kind == MP_MATCH_PAUSE_KIND_TEAM_TIMEOUT &&
		!AddEngineDuration( engineNow, timeoutDurationMsec, pendingExpiry ) ) {
		return Rejected( MP_MATCH_REASON_CLOCK_OVERFLOW );
	}
	if ( commitPendingPause && pause.kind == MP_MATCH_PAUSE_KIND_TECHNICAL &&
		technicalPauseDurationMsec > 0 &&
		!AddEngineDuration( engineNow, technicalPauseDurationMsec, pendingExpiry ) ) {
		return Rejected( MP_MATCH_REASON_CLOCK_OVERFLOW );
	}
	if ( timeoutExpires && !AddEngineDuration( engineNow, resumeCountdownMsec,
			automaticResumeDeadline ) ) {
		return Rejected( MP_MATCH_REASON_CLOCK_OVERFLOW );
	}

	engineTime = engineNow;
	matchTime = mpMatchTime::FromMilliseconds( nextMatchMsec );

	if ( !semanticMutation ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}

	const uint64_t previousRevision = sessionRevision;
	if ( commitPendingPause ) {
		if ( pause.kind == MP_MATCH_PAUSE_KIND_TEAM_TIMEOUT ) {
			mpMatchTimeoutBudgetState &budget = timeoutBudgets[ pause.ownerSide ];
			--budget.remaining;
			++budget.consumed;
			pause.budgetCharged = true;
			pause.pauseExpiry = pendingExpiry;
		} else if ( pause.kind == MP_MATCH_PAUSE_KIND_TECHNICAL && technicalPauseDurationMsec > 0 ) {
			pause.pauseExpiry = pendingExpiry;
			technicalPauseExpires = true;
		}
		pause.state = MP_MATCH_PAUSED;
		pause.pausedAt = engineNow;
	} else if ( timeoutExpires ) {
		pause.state = MP_MATCH_RESUME_COUNTDOWN;
		pause.resumeDeadline = automaticResumeDeadline;
		pause.resumeConsentMask = 0;
	} else {
		ClearPause();
	}
	return Applied( previousRevision );
}

mpMatchEngineTime mpMatchSession::GetEngineTime( void ) const {
	return engineTime;
}

mpMatchTime mpMatchSession::GetMatchTime( void ) const {
	return matchTime;
}

mpMatchTime mpMatchSession::GetLivePeriodStartMatchTime( void ) const {
	return livePeriodStartMatchTime;
}

/*
===============================================================================

	Connection-scoped participants and generation-safe bindings

===============================================================================
*/

void mpMatchSession::ClearParticipantState( mpMatchParticipantState &participant ) {
	participant.id = mpParticipantId::Invalid();
	participant.slot = -1;
	participant.slotGeneration = 0;
	participant.connected = false;
	participant.human = false;
	participant.active = false;
	participant.side = MP_MATCH_SIDE_NONE;
	participant.ready = false;
	participant.roles = 0;
}

int mpMatchSession::FindParticipantIndex( mpParticipantId participant ) const {
	if ( !participant.IsValid() || participant.SessionPart() != sessionId ) {
		return -1;
	}
	for ( int index = 0; index < MP_MATCH_MAX_PARTICIPANTS; ++index ) {
		if ( participants[ index ].connected && participants[ index ].id == participant ) {
			return index;
		}
	}
	return -1;
}

int mpMatchSession::FindFreeParticipantIndex( void ) const {
	for ( int index = 0; index < MP_MATCH_MAX_PARTICIPANTS; ++index ) {
		if ( !participants[ index ].connected ) {
			return index;
		}
	}
	return -1;
}

mpMatchMutationResult mpMatchSession::BindParticipant( int slot, bool human,
		mpMatchRoleMask_t roles, uint64_t expectedRevision,
		mpParticipantId &outParticipant ) {
	outParticipant = mpParticipantId::Invalid();
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( slot < 0 || slot >= MP_MATCH_MAX_CONNECTION_SLOTS ) {
		return Rejected( MP_MATCH_REASON_OUT_OF_RANGE );
	}
	if ( slotParticipantIndex[ slot ] >= 0 ) {
		return Rejected( MP_MATCH_REASON_SLOT_OCCUPIED );
	}
	if ( !MPMatchRoleMaskIsValid( roles, true ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ROLE );
	}
	if ( nextParticipantSequence == 0 ) {
		return Rejected( MP_MATCH_REASON_PARTICIPANT_ID_EXHAUSTED );
	}
	if ( slotGenerations[ slot ] == UINT32_MAX ) {
		return Rejected( MP_MATCH_REASON_PARTICIPANT_ID_EXHAUSTED );
	}
	const int participantIndex = FindFreeParticipantIndex();
	if ( participantIndex < 0 ) {
		return Rejected( MP_MATCH_REASON_PARTICIPANT_CAPACITY );
	}

	const uint64_t previousRevision = sessionRevision;
	const uint32_t generation = slotGenerations[ slot ] + 1;
	const mpParticipantId participantId( sessionId, nextParticipantSequence );
	if ( nextParticipantSequence == UINT32_MAX ) {
		nextParticipantSequence = 0;
	} else {
		++nextParticipantSequence;
	}

	mpMatchParticipantState &participant = participants[ participantIndex ];
	participant.id = participantId;
	participant.slot = slot;
	participant.slotGeneration = generation;
	participant.connected = true;
	participant.human = human;
	participant.active = false;
	participant.side = MP_MATCH_SIDE_NONE;
	participant.ready = false;
	participant.roles = roles;
	slotGenerations[ slot ] = generation;
	slotParticipantIndex[ slot ] = participantIndex;
	outParticipant = participantId;
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::UnbindParticipant( int slot,
		uint32_t slotGeneration, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( slot < 0 || slot >= MP_MATCH_MAX_CONNECTION_SLOTS ) {
		return Rejected( MP_MATCH_REASON_OUT_OF_RANGE );
	}
	const int participantIndex = slotParticipantIndex[ slot ];
	if ( participantIndex < 0 ) {
		return Rejected( MP_MATCH_REASON_SLOT_EMPTY );
	}
	if ( slotGeneration == 0 || slotGeneration != slotGenerations[ slot ] ||
		participants[ participantIndex ].slotGeneration != slotGeneration ) {
		return Rejected( MP_MATCH_REASON_SLOT_GENERATION_STALE );
	}

	const uint64_t previousRevision = sessionRevision;
	const mpMatchParticipantState disconnectedState = participants[ participantIndex ];
	const mpParticipantId disconnected = disconnectedState.id;
	bool requiredPlayerLost = disconnectedState.active &&
		( disconnectedState.human || readinessPolicy.botPolicy == MP_MATCH_BOTS_COUNT_AS_READY );
	for ( int seat = 0; seat < MP_MATCH_MAX_ROSTER_SEATS; ++seat ) {
		if ( roster[ seat ].declared && roster[ seat ].occupant == disconnected ) {
			requiredPlayerLost = requiredPlayerLost || roster[ seat ].required;
			roster[ seat ].occupant = mpParticipantId::Invalid();
		}
	}
	ClearParticipantState( participants[ participantIndex ] );
	slotParticipantIndex[ slot ] = -1;
	if ( slotGenerations[ slot ] < UINT32_MAX ) {
		++slotGenerations[ slot ];
	}
	if ( requiredPlayerLost ) {
		ClearTeamReady();
	}

	if ( phase == COUNTDOWN && requiredPlayerLost ) {
		const mpGameState_t from = phase;
		phase = WARMUP;
		lastTransition.from = from;
		lastTransition.to = WARMUP;
		lastTransition.reason = MP_MATCH_TRANSITION_REQUIRED_PLAYER_LOST;
		lastTransition.authorizer = disconnected;
		lastTransition.forcedReadinessBlockers = 0;
		lastForcedReadinessBlockers = 0;
		ClearPause();
	}
	return Applied( previousRevision );
}

bool mpMatchSession::ResolveSlotBinding( int slot, uint32_t slotGeneration,
		mpParticipantId &outParticipant ) const {
	outParticipant = mpParticipantId::Invalid();
	if ( slot < 0 || slot >= MP_MATCH_MAX_CONNECTION_SLOTS || slotGeneration == 0 ) {
		return false;
	}
	const int participantIndex = slotParticipantIndex[ slot ];
	if ( participantIndex < 0 || participantIndex >= MP_MATCH_MAX_PARTICIPANTS ||
		slotGenerations[ slot ] != slotGeneration ) {
		return false;
	}
	const mpMatchParticipantState &participant = participants[ participantIndex ];
	if ( !participant.connected || participant.slot != slot ||
		participant.slotGeneration != slotGeneration ) {
		return false;
	}
	outParticipant = participant.id;
	return true;
}

bool mpMatchSession::GetSlotGeneration( int slot, uint32_t &outSlotGeneration ) const {
	outSlotGeneration = 0;
	if ( slot < 0 || slot >= MP_MATCH_MAX_CONNECTION_SLOTS ||
		slotParticipantIndex[ slot ] < 0 || slotGenerations[ slot ] == 0 ) {
		return false;
	}
	outSlotGeneration = slotGenerations[ slot ];
	return true;
}

bool mpMatchSession::ResolveParticipantSequence( uint32_t sequence,
		mpParticipantId &outParticipant ) const {
	outParticipant = mpParticipantId::Invalid();
	if ( sequence == 0 ) {
		return false;
	}
	for ( int index = 0; index < MP_MATCH_MAX_PARTICIPANTS; ++index ) {
		if ( participants[ index ].connected &&
			participants[ index ].id.SequencePart() == sequence ) {
			outParticipant = participants[ index ].id;
			return true;
		}
	}
	return false;
}

bool mpMatchSession::ResolveParticipant( mpParticipantId participant,
		int &outSlot, uint32_t &outSlotGeneration ) const {
	outSlot = -1;
	outSlotGeneration = 0;
	const int participantIndex = FindParticipantIndex( participant );
	if ( participantIndex < 0 ) {
		return false;
	}
	const mpMatchParticipantState &state = participants[ participantIndex ];
	if ( state.slot < 0 || state.slot >= MP_MATCH_MAX_CONNECTION_SLOTS ||
		slotParticipantIndex[ state.slot ] != participantIndex ||
		slotGenerations[ state.slot ] != state.slotGeneration ) {
		return false;
	}
	outSlot = state.slot;
	outSlotGeneration = state.slotGeneration;
	return true;
}

const mpMatchParticipantState *mpMatchSession::FindParticipant( mpParticipantId participant ) const {
	const int index = FindParticipantIndex( participant );
	return index >= 0 ? &participants[ index ] : NULL;
}

const mpMatchParticipantState *mpMatchSession::GetParticipantByIndex( int index ) const {
	if ( index < 0 || index >= MP_MATCH_MAX_PARTICIPANTS || !participants[ index ].connected ) {
		return NULL;
	}
	return &participants[ index ];
}

mpMatchMutationResult mpMatchSession::SetParticipantRoles( mpParticipantId participant,
		mpMatchRoleMask_t roles, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( !MPMatchRoleMaskIsValid( roles, true ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ROLE );
	}
	const int index = FindParticipantIndex( participant );
	if ( index < 0 ) {
		return Rejected( MP_MATCH_REASON_PARTICIPANT_UNKNOWN );
	}
	const int rosterSeat = FindRosterSeat( participant );
	if ( rosterSeat >= 0 && roles !=
		MPMatchPrincipalRolesForRosterRole( roster[ rosterSeat ].role ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ROLE );
	}
	if ( participants[ index ].active &&
		( roles & MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) ) == 0 ) {
		return Rejected( MP_MATCH_REASON_INVALID_ROLE );
	}
	if ( participants[ index ].roles == roles ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}
	const uint64_t previousRevision = sessionRevision;
	participants[ index ].roles = roles;
	return Applied( previousRevision );
}

mpMatchCapabilityMask_t mpMatchSession::GetParticipantCapabilities( mpParticipantId participant ) const {
	const mpMatchParticipantState *state = FindParticipant( participant );
	if ( state == NULL ) {
		return 0;
	}
	mpMatchCapabilityMask_t capabilities = MPMatchCapabilitiesForRoles( state->roles );
	if ( CanSelfLeaveRoster( participant ) ) {
		capabilities |= MPMatchCapabilityBit( MP_MATCH_CAP_SELF_ROSTER_LEAVE );
	}
	return capabilities;
}

bool mpMatchSession::CanSelfLeaveRoster( mpParticipantId participant ) const {
	const mpMatchParticipantState *state = FindParticipant( participant );
	if ( state == NULL || !state->connected || !state->human || state->active ) {
		return false;
	}
	const int seat = FindRosterSeat( participant );
	if ( seat < 0 ) {
		return false;
	}
	const mpMatchRosterRole_t role = roster[ seat ].role;
	return role == MP_MATCH_ROSTER_COACH ||
		role == MP_MATCH_ROSTER_SUBSTITUTE;
}

mpMatchMutationResult mpMatchSession::SetParticipantActive( mpParticipantId participant,
		bool active, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	const bool casualLiveJoin = IsLivePhase() &&
		readinessPolicy.policy == MP_MATCH_READY_DISABLED &&
		!readinessPolicy.requireDeclaredRosterSeats;
	// Joining/activation is phase constrained; withdrawal is always permitted
	// so a client userinfo change or disconnect can never leave a ghost player
	// active merely because the match is live.
	if ( active && phase != WARMUP && phase != COUNTDOWN && !casualLiveJoin ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	const int index = FindParticipantIndex( participant );
	if ( index < 0 ) {
		return Rejected( MP_MATCH_REASON_PARTICIPANT_UNKNOWN );
	}
	if ( active && ( participants[ index ].roles &
		MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) ) == 0 ) {
		return Rejected( MP_MATCH_REASON_INVALID_ROLE );
	}
	const int rosterSeat = FindRosterSeat( participant );
	if ( rosterSeat >= 0 && active !=
		MPMatchRosterRoleIsActive( roster[ rosterSeat ].role ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ROLE );
	}
	if ( participants[ index ].active == active ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}
	const uint64_t previousRevision = sessionRevision;
	participants[ index ].active = active;
	if ( !active ) {
		participants[ index ].ready = false;
	}
	ClearTeamReady();
	InvalidateCountdownForRosterMutation();
	return Applied( previousRevision );
}

bool mpMatchSession::IsValidSide( int side ) const {
	return side >= 0 && side < MP_MATCH_SIDE_COUNT;
}

mpMatchMutationResult mpMatchSession::SetParticipantSide( mpParticipantId participant,
		int side, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	const bool casualLiveJoin = IsLivePhase() &&
		readinessPolicy.policy == MP_MATCH_READY_DISABLED &&
		!readinessPolicy.requireDeclaredRosterSeats;
	// Clearing a side is part of fail-closed withdrawal and remains legal in a
	// live managed match.  Moving onto a side still uses the normal join window.
	if ( side != MP_MATCH_SIDE_NONE && phase != WARMUP && phase != COUNTDOWN &&
		!casualLiveJoin ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( side != MP_MATCH_SIDE_NONE && !IsValidSide( side ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_SIDE );
	}
	if ( !readinessPolicy.teamMode && side != MP_MATCH_SIDE_NONE ) {
		return Rejected( MP_MATCH_REASON_INVALID_SIDE );
	}
	const int index = FindParticipantIndex( participant );
	if ( index < 0 ) {
		return Rejected( MP_MATCH_REASON_PARTICIPANT_UNKNOWN );
	}
	const int rosterSeat = FindRosterSeat( participant );
	if ( rosterSeat >= 0 && side != roster[ rosterSeat ].side ) {
		return Rejected( MP_MATCH_REASON_INVALID_SIDE );
	}
	if ( participants[ index ].side == side ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}
	const uint64_t previousRevision = sessionRevision;
	participants[ index ].side = side;
	participants[ index ].ready = false;
	ClearTeamReady();
	InvalidateCountdownForRosterMutation();
	return Applied( previousRevision );
}

// Trusted game-mode mutation, separate from admission and voluntary team moves.
// The caller must derive allowRoundSideChanges from the committed gametype.
// Roster seats remain stable so a conversion cannot steal an opponent's seat
// or bypass a lock to admit a spectator, substitute or replacement connection.
mpMatchMutationResult mpMatchSession::SetParticipantRoundSide( mpParticipantId participant,
		int side, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( !readinessPolicy.allowRoundSideChanges || !readinessPolicy.teamMode ||
		!IsLivePhase() || pause.state != MP_MATCH_PAUSE_RUNNING ||
		( roundState != RS_ACTIVE && roundState != RS_COUNTDOWN ) ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( !IsValidSide( side ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_SIDE );
	}
	const int index = FindParticipantIndex( participant );
	if ( index < 0 ) {
		return Rejected( MP_MATCH_REASON_PARTICIPANT_UNKNOWN );
	}
	if ( !participants[ index ].active || !IsValidSide( participants[ index ].side ) ||
		( participants[ index ].roles & MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) ) == 0 ) {
		return Rejected( MP_MATCH_REASON_INVALID_ROLE );
	}
	if ( participants[ index ].side == side ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}
	const uint64_t previousRevision = sessionRevision;
	participants[ index ].side = side;
	participants[ index ].ready = false;
	ClearTeamReady();
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::SetParticipantReady( mpParticipantId participant,
		bool ready, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != WARMUP && phase != COUNTDOWN ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( readinessPolicy.policy != MP_MATCH_READY_INDIVIDUAL &&
		readinessPolicy.policy != MP_MATCH_READY_INDIVIDUAL_AND_TEAM ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	const int index = FindParticipantIndex( participant );
	if ( index < 0 ) {
		return Rejected( MP_MATCH_REASON_PARTICIPANT_UNKNOWN );
	}
	const mpMatchParticipantState &state = participants[ index ];
	if ( !state.human || ( ready && ( !state.active ||
		( readinessPolicy.teamMode && !IsValidSide( state.side ) ) ) ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	if ( state.ready == ready ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}
	const uint64_t previousRevision = sessionRevision;
	participants[ index ].ready = ready;
	if ( !ready ) {
		InvalidateCountdownForRosterMutation();
	}
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::SetTeamReady( int side, bool ready,
		uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != WARMUP && phase != COUNTDOWN ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	const bool usesTeamReady = readinessPolicy.policy == MP_MATCH_READY_TEAM ||
		readinessPolicy.policy == MP_MATCH_READY_INDIVIDUAL_AND_TEAM;
	if ( !readinessPolicy.teamMode || !usesTeamReady || !IsValidSide( side ) ||
		!( readinessPolicy.requiredSideMask & ( static_cast<uint32_t>( 1 ) << side ) ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_SIDE );
	}
	if ( teamReady[ side ] == ready ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}
	const uint64_t previousRevision = sessionRevision;
	teamReady[ side ] = ready;
	if ( !ready ) {
		InvalidateCountdownForRosterMutation();
	}
	return Applied( previousRevision );
}

bool mpMatchSession::IsTeamReady( int side ) const {
	return IsValidSide( side ) && teamReady[ side ];
}

/*
===============================================================================

	Readiness and bounded roster

===============================================================================
*/

void mpMatchSession::ClearTeamReady( void ) {
	for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
		teamReady[ side ] = false;
	}
}

mpMatchMutationResult mpMatchSession::ConfigureReadiness(
		const mpMatchReadinessPolicy &newPolicy, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != INACTIVE && phase != WARMUP ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( newPolicy.policy < MP_MATCH_READY_DISABLED ||
		newPolicy.policy >= MP_MATCH_READY_POLICY_COUNT ||
		newPolicy.botPolicy < MP_MATCH_BOTS_EXCLUDED ||
		newPolicy.botPolicy >= MP_MATCH_BOT_POLICY_COUNT ||
		newPolicy.minimumActiveHumans < 0 ||
		newPolicy.minimumActiveHumans > MP_MATCH_MAX_PARTICIPANTS ||
		newPolicy.minimumActiveParticipants < 0 ||
		newPolicy.minimumActiveParticipants > MP_MATCH_MAX_PARTICIPANTS ||
		newPolicy.minimumActiveOnAnySide < 0 ||
		newPolicy.minimumActiveOnAnySide > MP_MATCH_MAX_PARTICIPANTS ||
		newPolicy.minimumActivePerRequiredSide < 0 ||
		newPolicy.minimumActivePerRequiredSide > MP_MATCH_MAX_PARTICIPANTS ||
		newPolicy.readyThresholdBasisPoints > 10000 ||
		newPolicy.maximumActivePerSide < 0 ||
		newPolicy.maximumActivePerSide > MP_MATCH_MAX_PARTICIPANTS ||
		( newPolicy.requiredSideMask & ~static_cast<uint32_t>( ( 1 << MP_MATCH_SIDE_COUNT ) - 1 ) ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	const bool usesTeamReady = newPolicy.policy == MP_MATCH_READY_TEAM ||
		newPolicy.policy == MP_MATCH_READY_INDIVIDUAL_AND_TEAM;
	if ( !newPolicy.allowRoundSideChanges ) {
		for ( int seat = 0; seat < MP_MATCH_MAX_ROSTER_SEATS; ++seat ) {
			const mpMatchParticipantState *occupant = FindParticipant( roster[ seat ].occupant );
			if ( roster[ seat ].declared && occupant != NULL && occupant->side != roster[ seat ].side ) {
				return Rejected( MP_MATCH_REASON_INVALID_SIDE );
			}
		}
	}
	if ( !newPolicy.teamMode && ( usesTeamReady || newPolicy.allowRoundSideChanges || newPolicy.maximumActivePerSide != 0 ||
		newPolicy.minimumActiveOnAnySide != 0 ||
		newPolicy.minimumActivePerRequiredSide != 0 || newPolicy.requiredSideMask != 0 ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	if ( usesTeamReady && newPolicy.requiredSideMask == 0 ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	if ( newPolicy.minimumActivePerRequiredSide > 0 &&
		newPolicy.requiredSideMask == 0 ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	if ( newPolicy.maximumActivePerSide > 0 &&
		( newPolicy.minimumActivePerRequiredSide > newPolicy.maximumActivePerSide ||
			newPolicy.minimumActiveOnAnySide > newPolicy.maximumActivePerSide ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	int requiredSideCount = 0;
	for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
		if ( newPolicy.requiredSideMask & ( static_cast<uint32_t>( 1 ) << side ) ) {
			++requiredSideCount;
		}
	}
	if ( requiredSideCount > 0 && newPolicy.minimumActivePerRequiredSide >
		MP_MATCH_MAX_PARTICIPANTS / requiredSideCount ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	for ( int seat = 0; seat < MP_MATCH_MAX_ROSTER_SEATS; ++seat ) {
		if ( !roster[ seat ].declared ) {
			continue;
		}
		const bool compatibleSide = newPolicy.teamMode ? IsValidSide( roster[ seat ].side ) :
			roster[ seat ].side == MP_MATCH_SIDE_NONE;
		if ( !compatibleSide ) {
			return Rejected( MP_MATCH_REASON_INVALID_SIDE );
		}
	}

	if ( readinessPolicy.policy == newPolicy.policy &&
		readinessPolicy.botPolicy == newPolicy.botPolicy &&
		readinessPolicy.teamMode == newPolicy.teamMode &&
		readinessPolicy.minimumActiveHumans == newPolicy.minimumActiveHumans &&
		readinessPolicy.minimumActiveParticipants == newPolicy.minimumActiveParticipants &&
		readinessPolicy.minimumActiveOnAnySide == newPolicy.minimumActiveOnAnySide &&
		readinessPolicy.minimumActivePerRequiredSide ==
			newPolicy.minimumActivePerRequiredSide &&
		readinessPolicy.readyThresholdBasisPoints == newPolicy.readyThresholdBasisPoints &&
		readinessPolicy.maximumActivePerSide == newPolicy.maximumActivePerSide &&
		readinessPolicy.requiredSideMask == newPolicy.requiredSideMask &&
		readinessPolicy.requireDeclaredRosterSeats == newPolicy.requireDeclaredRosterSeats &&
		readinessPolicy.allowRoundSideChanges == newPolicy.allowRoundSideChanges ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}

	const uint64_t previousRevision = sessionRevision;
	readinessPolicy = newPolicy;
	ClearTeamReady();
	for ( int index = 0; index < MP_MATCH_MAX_PARTICIPANTS; ++index ) {
		if ( participants[ index ].connected ) {
			participants[ index ].ready = false;
			if ( !newPolicy.teamMode ) {
				participants[ index ].side = MP_MATCH_SIDE_NONE;
			}
		}
	}
	lastForcedReadinessBlockers = 0;
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::SetExternalReadinessBlockers(
		mpMatchReadinessBlockerMask_t blockers, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != INACTIVE && phase != WARMUP && phase != COUNTDOWN ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( blockers & ~ExternalReadinessBlockerBits() ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	if ( externalReadinessBlockers == blockers ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}
	const uint64_t previousRevision = sessionRevision;
	externalReadinessBlockers = blockers;
	lastForcedReadinessBlockers = 0;
	if ( phase == COUNTDOWN && blockers != 0 ) {
		const mpGameState_t from = phase;
		phase = WARMUP;
		lastTransition.from = from;
		lastTransition.to = WARMUP;
		lastTransition.reason = blockers & MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_RULES_INVALID ) ?
			MP_MATCH_TRANSITION_RULES_INVALIDATED : MP_MATCH_TRANSITION_COUNTDOWN_ABORTED;
		lastTransition.authorizer = mpParticipantId::Invalid();
		lastTransition.forcedReadinessBlockers = 0;
		ClearPause();
	}
	return Applied( previousRevision );
}

const mpMatchReadinessPolicy &mpMatchSession::GetReadinessPolicy( void ) const {
	return readinessPolicy;
}

mpMatchReadinessView mpMatchSession::EvaluateReadiness( void ) const {
	mpMatchReadinessView result;
	result.blockers = externalReadinessBlockers;
	result.activeHumans = 0;
	result.activeParticipants = 0;
	result.activePerSide[ 0 ] = 0;
	result.activePerSide[ 1 ] = 0;
	result.readyEligibleParticipants = 0;
	result.readyParticipants = 0;
	result.requiredReadyParticipants = 0;
	result.unreadyParticipants = 0;
	result.unreadySideMask = 0;
	result.insufficientActiveSideMask = 0;
	result.vacantRequiredSeats = 0;

	if ( !frozenRulesValid ) {
		result.blockers |= MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_RULES_NOT_FROZEN );
	}

	const bool usesIndividualReady = readinessPolicy.policy == MP_MATCH_READY_INDIVIDUAL ||
		readinessPolicy.policy == MP_MATCH_READY_INDIVIDUAL_AND_TEAM;
	const bool usesTeamReady = readinessPolicy.policy == MP_MATCH_READY_TEAM ||
		readinessPolicy.policy == MP_MATCH_READY_INDIVIDUAL_AND_TEAM;

	for ( int index = 0; index < MP_MATCH_MAX_PARTICIPANTS; ++index ) {
		const mpMatchParticipantState &participant = participants[ index ];
		if ( !participant.connected || !participant.active ) {
			continue;
		}
		++result.activeParticipants;
		if ( participant.human ) {
			++result.activeHumans;
		}

		// Bot readiness policy controls only whether a bot participates in the
		// ready vote.  Every active participant still consumes capacity and must
		// have a valid side in team modes.
		if ( readinessPolicy.teamMode ) {
			if ( !IsValidSide( participant.side ) ) {
				result.blockers |= MPMatchReadinessBlockerBit(
					MP_MATCH_BLOCKER_ACTIVE_PARTICIPANT_UNASSIGNED );
			} else {
				++result.activePerSide[ participant.side ];
			}
		}

		const bool readinessEligible = participant.human ||
			readinessPolicy.botPolicy == MP_MATCH_BOTS_COUNT_AS_READY;
		if ( !readinessEligible ) {
			continue;
		}

		if ( usesIndividualReady ) {
			++result.readyEligibleParticipants;
			// COUNT_AS_READY bots do not expose mutable ready input; eligibility
			// itself is their affirmative ready state.
			if ( !participant.human || participant.ready ) {
				++result.readyParticipants;
			} else {
				++result.unreadyParticipants;
			}
		}
	}

	if ( usesIndividualReady ) {
		result.requiredReadyParticipants =
			( result.readyEligibleParticipants * static_cast<int>( readinessPolicy.readyThresholdBasisPoints ) + 9999 ) / 10000;
		if ( result.readyParticipants < result.requiredReadyParticipants ) {
			result.blockers |= MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_PARTICIPANT_NOT_READY );
		}
	}

	if ( result.activeHumans < readinessPolicy.minimumActiveHumans ) {
		result.blockers |= MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_HUMANS );
	}
	if ( result.activeParticipants < readinessPolicy.minimumActiveParticipants ) {
		result.blockers |= MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_PARTICIPANTS );
	}
	if ( readinessPolicy.teamMode && readinessPolicy.minimumActiveOnAnySide > 0 ) {
		bool enoughOnOneSide = false;
		for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
			enoughOnOneSide |= result.activePerSide[ side ] >= readinessPolicy.minimumActiveOnAnySide;
		}
		if ( !enoughOnOneSide ) {
			result.insufficientActiveSideMask = ( 1u << MP_MATCH_SIDE_COUNT ) - 1;
			result.blockers |= MPMatchReadinessBlockerBit(
				MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_CONTESTANTS_PER_SIDE );
		}
	}
	if ( readinessPolicy.teamMode &&
		readinessPolicy.minimumActivePerRequiredSide > 0 ) {
		for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
			const uint32_t sideBit = static_cast<uint32_t>( 1 ) << side;
			if ( ( readinessPolicy.requiredSideMask & sideBit ) &&
				result.activePerSide[ side ] <
					readinessPolicy.minimumActivePerRequiredSide ) {
				result.insufficientActiveSideMask |= sideBit;
				result.blockers |= MPMatchReadinessBlockerBit(
					MP_MATCH_BLOCKER_INSUFFICIENT_ACTIVE_CONTESTANTS_PER_SIDE );
			}
		}
	}
	if ( readinessPolicy.teamMode && readinessPolicy.maximumActivePerSide > 0 ) {
		for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
			if ( result.activePerSide[ side ] > readinessPolicy.maximumActivePerSide ) {
				result.blockers |= MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_TEAM_OVERSIZED );
			}
		}
	}
	if ( usesTeamReady ) {
		for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
			const uint32_t sideBit = static_cast<uint32_t>( 1 ) << side;
			if ( ( readinessPolicy.requiredSideMask & sideBit ) && !teamReady[ side ] ) {
				result.unreadySideMask |= sideBit;
				result.blockers |= MPMatchReadinessBlockerBit( MP_MATCH_BLOCKER_TEAM_NOT_READY );
			}
		}
	}
	if ( readinessPolicy.requireDeclaredRosterSeats ) {
		for ( int seat = 0; seat < MP_MATCH_MAX_ROSTER_SEATS; ++seat ) {
			if ( roster[ seat ].declared && roster[ seat ].required &&
				!roster[ seat ].occupant.IsValid() ) {
				++result.vacantRequiredSeats;
				result.blockers |= MPMatchReadinessBlockerBit(
					MP_MATCH_BLOCKER_VACANT_REQUIRED_ROSTER_SEAT );
			}
		}
	}
	return result;
}

uint32_t mpMatchSession::GetLastForcedReadinessBlockers( void ) const {
	return lastForcedReadinessBlockers;
}

void mpMatchSession::InvalidateCountdownForRosterMutation( void ) {
	lastForcedReadinessBlockers = 0;
	if ( phase != COUNTDOWN || EvaluateReadiness().IsReady() ) {
		return;
	}
	const mpGameState_t from = phase;
	phase = WARMUP;
	lastTransition.from = from;
	lastTransition.to = WARMUP;
	lastTransition.reason = MP_MATCH_TRANSITION_ROSTER_INVALIDATED;
	lastTransition.authorizer = mpParticipantId::Invalid();
	lastTransition.forcedReadinessBlockers = 0;
	ClearPause();
}

mpMatchMutationResult mpMatchSession::DeclareRosterSeat( int seat, int side,
		mpMatchRosterRole_t role, bool required, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != INACTIVE && phase != WARMUP && phase != COUNTDOWN ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( seat < 0 || seat >= MP_MATCH_MAX_ROSTER_SEATS ) {
		return Rejected( MP_MATCH_REASON_OUT_OF_RANGE );
	}
	if ( ( readinessPolicy.teamMode && !IsValidSide( side ) ) ||
		( !readinessPolicy.teamMode && side != MP_MATCH_SIDE_NONE ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_SIDE );
	}
	if ( !MPMatchRosterRoleIsValid( role ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ROLE );
	}
	if ( roster[ seat ].declared && roster[ seat ].occupant.IsValid() &&
		( roster[ seat ].side != side || roster[ seat ].role != role ) ) {
		return Rejected( MP_MATCH_REASON_ROSTER_SEAT_OCCUPIED );
	}
	if ( roster[ seat ].declared && roster[ seat ].side == side &&
		roster[ seat ].role == role && roster[ seat ].required == required ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}

	const uint64_t previousRevision = sessionRevision;
	roster[ seat ].declared = true;
	roster[ seat ].required = required;
	roster[ seat ].side = side;
	roster[ seat ].role = role;
	ClearTeamReady();
	InvalidateCountdownForRosterMutation();
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::ClearRosterSeat( int seat, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != INACTIVE && phase != WARMUP && phase != COUNTDOWN ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( seat < 0 || seat >= MP_MATCH_MAX_ROSTER_SEATS ) {
		return Rejected( MP_MATCH_REASON_OUT_OF_RANGE );
	}
	if ( !roster[ seat ].declared ) {
		return NoChange( MP_MATCH_REASON_ROSTER_SEAT_UNDECLARED );
	}
	const uint64_t previousRevision = sessionRevision;
	roster[ seat ].declared = false;
	roster[ seat ].required = false;
	roster[ seat ].side = MP_MATCH_SIDE_NONE;
	roster[ seat ].role = MP_MATCH_ROSTER_PLAYER;
	roster[ seat ].occupant = mpParticipantId::Invalid();
	ClearTeamReady();
	InvalidateCountdownForRosterMutation();
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::AssignRosterSeat( int seat,
		mpParticipantId participant, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != INACTIVE && phase != WARMUP && phase != COUNTDOWN ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( seat < 0 || seat >= MP_MATCH_MAX_ROSTER_SEATS ) {
		return Rejected( MP_MATCH_REASON_OUT_OF_RANGE );
	}
	if ( !roster[ seat ].declared ) {
		return Rejected( MP_MATCH_REASON_ROSTER_SEAT_UNDECLARED );
	}
	if ( roster[ seat ].occupant.IsValid() ) {
		return roster[ seat ].occupant == participant ? NoChange( MP_MATCH_REASON_NONE ) :
			Rejected( MP_MATCH_REASON_ROSTER_SEAT_OCCUPIED );
	}
	const int participantIndex = FindParticipantIndex( participant );
	if ( participantIndex < 0 ) {
		return Rejected( MP_MATCH_REASON_PARTICIPANT_UNKNOWN );
	}
	if ( FindRosterSeat( participant ) >= 0 ) {
		return Rejected( MP_MATCH_REASON_PARTICIPANT_ALREADY_ROSTERED );
	}
	if ( readinessPolicy.teamMode && participants[ participantIndex ].side != MP_MATCH_SIDE_NONE &&
		participants[ participantIndex ].side != roster[ seat ].side ) {
		return Rejected( MP_MATCH_REASON_INVALID_SIDE );
	}
	if ( participants[ participantIndex ].side != roster[ seat ].side ||
		participants[ participantIndex ].active !=
			MPMatchRosterRoleIsActive( roster[ seat ].role ) ||
		participants[ participantIndex ].roles !=
			MPMatchPrincipalRolesForRosterRole( roster[ seat ].role ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ROLE );
	}
	const uint64_t previousRevision = sessionRevision;
	roster[ seat ].occupant = participant;
	ClearTeamReady();
	InvalidateCountdownForRosterMutation();
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::VacateRosterSeat( int seat, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	// Vacating is also used by fail-closed withdrawal/disconnect handling.
	// Admission and reassignment remain phase-gated by AssignRosterSeat and the
	// typed operation descriptors.
	if ( seat < 0 || seat >= MP_MATCH_MAX_ROSTER_SEATS ) {
		return Rejected( MP_MATCH_REASON_OUT_OF_RANGE );
	}
	if ( !roster[ seat ].declared ) {
		return Rejected( MP_MATCH_REASON_ROSTER_SEAT_UNDECLARED );
	}
	if ( !roster[ seat ].occupant.IsValid() ) {
		return NoChange( MP_MATCH_REASON_ROSTER_SEAT_EMPTY );
	}
	const uint64_t previousRevision = sessionRevision;
	roster[ seat ].occupant = mpParticipantId::Invalid();
	ClearTeamReady();
	InvalidateCountdownForRosterMutation();
	return Applied( previousRevision );
}

const mpMatchRosterSeat *mpMatchSession::GetRosterSeat( int seat ) const {
	if ( seat < 0 || seat >= MP_MATCH_MAX_ROSTER_SEATS || !roster[ seat ].declared ) {
		return NULL;
	}
	return &roster[ seat ];
}

int mpMatchSession::FindRosterSeat( mpParticipantId participant ) const {
	if ( !participant.IsValid() ) {
		return -1;
	}
	for ( int seat = 0; seat < MP_MATCH_MAX_ROSTER_SEATS; ++seat ) {
		if ( roster[ seat ].declared && roster[ seat ].occupant == participant ) {
			return seat;
		}
	}
	return -1;
}

/*
===============================================================================

	Timeout budgets and pause overlay operations

===============================================================================
*/

void mpMatchSession::ClearPause( void ) {
	pause.state = MP_MATCH_PAUSE_RUNNING;
	pause.kind = MP_MATCH_PAUSE_KIND_NONE;
	pause.reason = MP_MATCH_PAUSE_REASON_NONE;
	pause.ownerSide = MP_MATCH_SIDE_NONE;
	pause.resumeConsentMask = 0;
	pause.budgetCharged = false;
	pause.requestedAt = mpMatchEngineTime::FromMilliseconds( 0 );
	pause.pausedAt = mpMatchEngineTime::FromMilliseconds( 0 );
	pause.pauseExpiry = mpMatchEngineTime::FromMilliseconds( 0 );
	pause.resumeDeadline = mpMatchEngineTime::FromMilliseconds( 0 );
	technicalPauseDurationMsec = 0;
	technicalPauseExpires = false;
}

mpMatchMutationResult mpMatchSession::ConfigureTimeouts( int perSide,
		int durationMsec, bool allowDuringCountdown, int newResumeCountdownMsec,
		mpMatchResumePolicy_t newResumePolicy, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != INACTIVE && phase != WARMUP ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( pause.state != MP_MATCH_PAUSE_RUNNING ) {
		return Rejected( MP_MATCH_REASON_PAUSE_ALREADY_ACTIVE );
	}
	if ( perSide < 0 || perSide > MP_MATCH_MAX_TIMEOUTS_PER_SIDE ||
		durationMsec < 0 || durationMsec > MP_MATCH_MAX_TIMEOUT_MSEC ||
		( ( perSide > 0 ) != ( durationMsec > 0 ) ) ||
		newResumeCountdownMsec < 0 ||
		newResumeCountdownMsec > MP_MATCH_MAX_RESUME_COUNTDOWN_MSEC ||
		newResumePolicy < MP_MATCH_RESUME_OWNER_OR_AUTHORITY ||
		newResumePolicy >= MP_MATCH_RESUME_POLICY_COUNT ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	if ( timeoutBudgets[ 0 ].configured == perSide && timeoutBudgets[ 0 ].remaining == perSide &&
		timeoutBudgets[ 1 ].configured == perSide && timeoutBudgets[ 1 ].remaining == perSide &&
		timeoutDurationMsec == durationMsec &&
		timeoutAllowedDuringCountdown == allowDuringCountdown &&
		resumeCountdownMsec == newResumeCountdownMsec && resumePolicy == newResumePolicy ) {
		return NoChange( MP_MATCH_REASON_NONE );
	}

	const uint64_t previousRevision = sessionRevision;
	for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
		timeoutBudgets[ side ].configured = perSide;
		timeoutBudgets[ side ].remaining = perSide;
		timeoutBudgets[ side ].consumed = 0;
	}
	timeoutDurationMsec = durationMsec;
	timeoutAllowedDuringCountdown = allowDuringCountdown;
	resumeCountdownMsec = newResumeCountdownMsec;
	resumePolicy = newResumePolicy;
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::RequestTeamTimeout( int side,
		mpMatchPauseReason_t reason, uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( !IsLivePhase() && !( phase == COUNTDOWN && timeoutAllowedDuringCountdown ) ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( !IsValidSide( side ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_SIDE );
	}
	if ( !IsValidTeamTimeoutReason( reason ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	if ( pause.state != MP_MATCH_PAUSE_RUNNING ) {
		return Rejected( MP_MATCH_REASON_PAUSE_ALREADY_ACTIVE );
	}
	if ( timeoutBudgets[ side ].remaining <= 0 ) {
		return Rejected( MP_MATCH_REASON_TIMEOUT_UNAVAILABLE );
	}
	mpMatchEngineTime expiry;
	if ( !AddEngineDuration( engineTime, timeoutDurationMsec, expiry ) ) {
		return Rejected( MP_MATCH_REASON_CLOCK_OVERFLOW );
	}

	const uint64_t previousRevision = sessionRevision;
	ClearPause();
	pause.state = MP_MATCH_PAUSE_PENDING;
	pause.kind = MP_MATCH_PAUSE_KIND_TEAM_TIMEOUT;
	pause.reason = reason;
	pause.ownerSide = side;
	pause.requestedAt = engineTime;
	// expiry is recomputed at the authoritative commit boundary.
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::RequestTechnicalPause( mpMatchPauseReason_t reason,
		uint64_t expectedRevision, int durationMsec ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( phase != COUNTDOWN && !IsLivePhase() ) {
		return Rejected( MP_MATCH_REASON_WRONG_PHASE );
	}
	if ( !IsValidTechnicalPauseReason( reason ) ) {
		return Rejected( MP_MATCH_REASON_INVALID_ARGUMENT );
	}
	if ( pause.state != MP_MATCH_PAUSE_RUNNING ) {
		return Rejected( MP_MATCH_REASON_PAUSE_ALREADY_ACTIVE );
	}
	const uint64_t previousRevision = sessionRevision;
	ClearPause();
	pause.state = MP_MATCH_PAUSE_PENDING;
	pause.kind = MP_MATCH_PAUSE_KIND_TECHNICAL;
	pause.reason = reason;
	pause.requestedAt = engineTime;
	technicalPauseDurationMsec = durationMsec > 0 ? durationMsec : 0;
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::RequestResumeByTeam( int side,
		uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( pause.state != MP_MATCH_PAUSED ) {
		return Rejected( MP_MATCH_REASON_PAUSE_NOT_ACTIVE );
	}
	if ( pause.kind != MP_MATCH_PAUSE_KIND_TEAM_TIMEOUT || !IsValidSide( side ) ) {
		return Rejected( MP_MATCH_REASON_RESUME_NOT_PERMITTED );
	}
	if ( resumePolicy == MP_MATCH_RESUME_AUTHORITY_ONLY ||
		( resumePolicy == MP_MATCH_RESUME_OWNER_OR_AUTHORITY && side != pause.ownerSide ) ) {
		return Rejected( MP_MATCH_REASON_RESUME_NOT_PERMITTED );
	}
	const uint32_t sideBit = static_cast<uint32_t>( 1 ) << side;
	if ( pause.resumeConsentMask & sideBit ) {
		return NoChange( MP_MATCH_REASON_RESUME_ALREADY_REQUESTED );
	}
	const uint32_t newConsentMask = pause.resumeConsentMask | sideBit;
	const bool beginResume = resumePolicy == MP_MATCH_RESUME_OWNER_OR_AUTHORITY ||
		newConsentMask == static_cast<uint32_t>( ( 1 << MP_MATCH_SIDE_COUNT ) - 1 );
	mpMatchEngineTime deadline;
	if ( beginResume && !AddEngineDuration( engineTime, resumeCountdownMsec, deadline ) ) {
		return Rejected( MP_MATCH_REASON_CLOCK_OVERFLOW );
	}

	const uint64_t previousRevision = sessionRevision;
	pause.resumeConsentMask = newConsentMask;
	if ( beginResume ) {
		pause.state = MP_MATCH_RESUME_COUNTDOWN;
		pause.resumeDeadline = deadline;
	}
	return Applied( previousRevision );
}

mpMatchMutationResult mpMatchSession::RequestResumeByAuthority( uint64_t expectedRevision ) {
	if ( !IsExpectedRevision( expectedRevision ) ) {
		return Rejected( MP_MATCH_REASON_STALE_REVISION );
	}
	if ( !CanCommit() ) {
		return Rejected( MP_MATCH_REASON_REVISION_EXHAUSTED );
	}
	if ( pause.state != MP_MATCH_PAUSED ) {
		return Rejected( MP_MATCH_REASON_PAUSE_NOT_ACTIVE );
	}
	mpMatchEngineTime deadline;
	if ( !AddEngineDuration( engineTime, resumeCountdownMsec, deadline ) ) {
		return Rejected( MP_MATCH_REASON_CLOCK_OVERFLOW );
	}
	const uint64_t previousRevision = sessionRevision;
	pause.state = MP_MATCH_RESUME_COUNTDOWN;
	pause.resumeDeadline = deadline;
	return Applied( previousRevision );
}

const mpMatchPauseView &mpMatchSession::GetPause( void ) const {
	return pause;
}

const mpMatchTimeoutBudgetState &mpMatchSession::GetTimeoutBudget( int side ) const {
	static const mpMatchTimeoutBudgetState invalidBudget = { 0, 0, 0 };
	return IsValidSide( side ) ? timeoutBudgets[ side ] : invalidBudget;
}

int mpMatchSession::GetTimeoutDurationMsec( void ) const {
	return timeoutDurationMsec;
}

bool mpMatchSession::IsTimeoutAllowedDuringCountdown( void ) const {
	return timeoutAllowedDuringCountdown;
}

int mpMatchSession::GetResumeCountdownMsec( void ) const {
	return resumeCountdownMsec;
}

mpMatchResumePolicy_t mpMatchSession::GetResumePolicy( void ) const {
	return resumePolicy;
}

/*
===============================================================================

	Invariant validation

===============================================================================
*/

bool mpMatchSession::ValidateInvariants( void ) const {
	if ( sessionId == 0 || sessionRevision == 0 || !IsValidPhase( phase ) ||
		!engineTime.IsValid() || !matchTime.IsValid() || !livePeriodStartMatchTime.IsValid() ||
		livePeriodStartMatchTime > matchTime ) {
		return false;
	}
	if ( frozenRulesValid && frozenRulesRevision == 0 ) {
		return false;
	}
	if ( !frozenRulesValid && ( frozenRulesRevision != 0 || frozenRulesDigest != 0 ) ) {
		return false;
	}
	if ( !IsValidRoundState( roundState ) ||
		!IsValidRoundState( lastRoundTransition.from ) ||
		!IsValidRoundState( lastRoundTransition.to ) ||
		lastRoundTransition.reason < MP_MATCH_ROUND_TRANSITION_NONE ||
		lastRoundTransition.reason >= MP_MATCH_ROUND_TRANSITION_REASON_COUNT ||
		( !IsLivePhase() && roundState != RS_INACTIVE ) ) {
		return false;
	}
	if ( regulationDurationMsec < 0 ||
		regulationDurationMsec > MP_MATCH_MAX_LIVE_PERIOD_MSEC ||
		!livePeriod.start.IsValid() || !livePeriod.deadline.IsValid() ||
		( livePeriod.hasDeadline && livePeriod.deadline < livePeriod.start ) ||
		( livePeriod.period > 0 && !livePeriod.hasDeadline ) ) {
		return false;
	}
	if ( lastTransition.from < INACTIVE || lastTransition.from >= STATE_COUNT ||
		lastTransition.to < INACTIVE || lastTransition.to >= STATE_COUNT ||
		lastTransition.reason < MP_MATCH_TRANSITION_NONE ||
		lastTransition.reason >= MP_MATCH_TRANSITION_REASON_COUNT ||
		( lastTransition.forcedReadinessBlockers & ~AllReadinessBlockerBits() ) ) {
		return false;
	}
	if ( lastTransition.reason == MP_MATCH_TRANSITION_NONE ) {
		if ( lastTransition.from != INACTIVE || lastTransition.to != INACTIVE ) {
			return false;
		}
	} else if ( !IsLegalPhaseTransition( lastTransition.from, lastTransition.to,
		lastTransition.reason ) ) {
		return false;
	}
	if ( lastRoundTransition.reason == MP_MATCH_ROUND_TRANSITION_NONE ) {
		if ( lastRoundTransition.from != RS_INACTIVE ||
			lastRoundTransition.to != RS_INACTIVE ) {
			return false;
		}
	} else if ( !IsLegalRoundTransition( lastRoundTransition.from,
		lastRoundTransition.to, lastRoundTransition.reason ) ) {
		return false;
	}

	for ( int first = 0; first < numLegalPhaseTransitions; ++first ) {
		const mpLegalPhaseTransition &entry = legalPhaseTransitions[ first ];
		if ( !IsValidPhase( entry.from ) || !IsValidPhase( entry.to ) ||
			entry.from == entry.to || !IsValidTransitionReason( entry.reason ) ) {
			return false;
		}
		for ( int second = first + 1; second < numLegalPhaseTransitions; ++second ) {
			if ( entry.from == legalPhaseTransitions[ second ].from &&
				entry.to == legalPhaseTransitions[ second ].to &&
				entry.reason == legalPhaseTransitions[ second ].reason ) {
				return false;
			}
		}
	}
	for ( int first = 0; first < numLegalRoundTransitions; ++first ) {
		const mpLegalRoundTransition &entry = legalRoundTransitions[ first ];
		if ( !IsValidRoundState( entry.from ) || !IsValidRoundState( entry.to ) ||
			entry.from == entry.to || !IsValidRoundTransitionReason( entry.reason ) ) {
			return false;
		}
		for ( int second = first + 1; second < numLegalRoundTransitions; ++second ) {
			if ( entry.from == legalRoundTransitions[ second ].from &&
				entry.to == legalRoundTransitions[ second ].to &&
				entry.reason == legalRoundTransitions[ second ].reason ) {
				return false;
			}
		}
	}

	if ( pause.state < MP_MATCH_PAUSE_RUNNING || pause.state >= MP_MATCH_PAUSE_STATE_COUNT ||
		pause.kind < MP_MATCH_PAUSE_KIND_NONE || pause.kind >= MP_MATCH_PAUSE_KIND_COUNT ||
		pause.reason < MP_MATCH_PAUSE_REASON_NONE || pause.reason >= MP_MATCH_PAUSE_REASON_COUNT ||
		!pause.requestedAt.IsValid() || !pause.pausedAt.IsValid() ||
		!pause.pauseExpiry.IsValid() || !pause.resumeDeadline.IsValid() ) {
		return false;
	}
	if ( pause.resumeConsentMask &
		~static_cast<uint32_t>( ( 1 << MP_MATCH_SIDE_COUNT ) - 1 ) ) {
		return false;
	}
	if ( pause.state == MP_MATCH_PAUSE_RUNNING ) {
		if ( pause.kind != MP_MATCH_PAUSE_KIND_NONE || pause.reason != MP_MATCH_PAUSE_REASON_NONE ||
			pause.ownerSide != MP_MATCH_SIDE_NONE || pause.budgetCharged ||
			pause.resumeConsentMask != 0 ) {
			return false;
		}
	} else {
		if ( phase != COUNTDOWN && !IsLivePhase() ) {
			return false;
		}
		if ( pause.kind == MP_MATCH_PAUSE_KIND_NONE || !IsValidPauseReason( pause.reason ) ) {
			return false;
		}
		if ( pause.kind == MP_MATCH_PAUSE_KIND_TEAM_TIMEOUT ) {
			if ( !IsValidSide( pause.ownerSide ) ||
				( pause.state == MP_MATCH_PAUSE_PENDING && pause.budgetCharged ) ||
				( pause.state != MP_MATCH_PAUSE_PENDING && !pause.budgetCharged ) ||
				timeoutBudgets[ pause.ownerSide ].configured <= 0 ||
				( pause.state != MP_MATCH_PAUSE_PENDING &&
					pause.pauseExpiry < pause.pausedAt ) ) {
				return false;
			}
		} else if ( pause.ownerSide != MP_MATCH_SIDE_NONE || pause.budgetCharged ||
			pause.resumeConsentMask != 0 ) {
			return false;
		}
	}

	for ( int slot = 0; slot < MP_MATCH_MAX_CONNECTION_SLOTS; ++slot ) {
		const int index = slotParticipantIndex[ slot ];
		if ( index < -1 || index >= MP_MATCH_MAX_PARTICIPANTS ) {
			return false;
		}
		if ( index >= 0 ) {
			const mpMatchParticipantState &participant = participants[ index ];
			if ( !participant.connected || participant.slot != slot ||
				participant.slotGeneration == 0 ||
				participant.slotGeneration != slotGenerations[ slot ] ) {
				return false;
			}
		}
	}
	for ( int index = 0; index < MP_MATCH_MAX_PARTICIPANTS; ++index ) {
		const mpMatchParticipantState &participant = participants[ index ];
		if ( !participant.connected ) {
			if ( participant.id.IsValid() || participant.slot != -1 ||
				participant.slotGeneration != 0 || participant.active || participant.ready ||
				participant.human || participant.side != MP_MATCH_SIDE_NONE ||
				participant.roles != 0 ) {
				return false;
			}
			continue;
		}
		if ( !participant.id.IsValid() || participant.id.SessionPart() != sessionId ||
			participant.slot < 0 || participant.slot >= MP_MATCH_MAX_CONNECTION_SLOTS ||
			slotParticipantIndex[ participant.slot ] != index ||
			!MPMatchRoleMaskIsValid( participant.roles, true ) ||
			( participant.side != MP_MATCH_SIDE_NONE && !IsValidSide( participant.side ) ) ||
			( !readinessPolicy.teamMode && participant.side != MP_MATCH_SIDE_NONE ) ||
			( participant.active && ( participant.roles &
				MPMatchRoleBit( MP_MATCH_ROLE_PLAYER ) ) == 0 ) ||
			( participant.ready && ( !participant.human || !participant.active ||
				( readinessPolicy.teamMode && !IsValidSide( participant.side ) ) ) ) ) {
			return false;
		}
		for ( int other = index + 1; other < MP_MATCH_MAX_PARTICIPANTS; ++other ) {
			if ( participants[ other ].connected && participants[ other ].id == participant.id ) {
				return false;
			}
		}
	}

	for ( int seat = 0; seat < MP_MATCH_MAX_ROSTER_SEATS; ++seat ) {
		const mpMatchRosterSeat &entry = roster[ seat ];
		if ( !entry.declared ) {
			if ( entry.required || entry.side != MP_MATCH_SIDE_NONE || entry.occupant.IsValid() ) {
				return false;
			}
			continue;
		}
		if ( !MPMatchRosterRoleIsValid( entry.role ) ||
			( entry.required && !MPMatchRosterRoleIsActive( entry.role ) ) ||
			( readinessPolicy.teamMode ? !IsValidSide( entry.side ) : entry.side != MP_MATCH_SIDE_NONE ) ) {
			return false;
		}
		if ( entry.occupant.IsValid() ) {
			const int occupantIndex = FindParticipantIndex( entry.occupant );
			if ( occupantIndex < 0 ) {
				return false;
			}
			const mpMatchParticipantState &occupant = participants[ occupantIndex ];
			if ( ( occupant.side != entry.side &&
				!( readinessPolicy.allowRoundSideChanges && occupant.active ) ) || occupant.active !=
				MPMatchRosterRoleIsActive( entry.role ) || occupant.roles !=
				MPMatchPrincipalRolesForRosterRole( entry.role ) ) {
				return false;
			}
		}
		for ( int other = seat + 1; other < MP_MATCH_MAX_ROSTER_SEATS; ++other ) {
			if ( entry.occupant.IsValid() && roster[ other ].declared &&
				roster[ other ].occupant == entry.occupant ) {
				return false;
			}
		}
	}

	int invariantRequiredSideCount = 0;
	for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
		if ( readinessPolicy.requiredSideMask &
			( static_cast<uint32_t>( 1 ) << side ) ) {
			++invariantRequiredSideCount;
		}
	}
	if ( readinessPolicy.policy < MP_MATCH_READY_DISABLED ||
		readinessPolicy.policy >= MP_MATCH_READY_POLICY_COUNT ||
		readinessPolicy.botPolicy < MP_MATCH_BOTS_EXCLUDED ||
		readinessPolicy.botPolicy >= MP_MATCH_BOT_POLICY_COUNT ||
		readinessPolicy.minimumActiveHumans < 0 ||
		readinessPolicy.minimumActiveHumans > MP_MATCH_MAX_PARTICIPANTS ||
		readinessPolicy.minimumActiveParticipants < 0 ||
		readinessPolicy.minimumActiveParticipants > MP_MATCH_MAX_PARTICIPANTS ||
		readinessPolicy.minimumActiveOnAnySide < 0 ||
		readinessPolicy.minimumActiveOnAnySide > MP_MATCH_MAX_PARTICIPANTS ||
		readinessPolicy.minimumActivePerRequiredSide < 0 ||
		readinessPolicy.minimumActivePerRequiredSide > MP_MATCH_MAX_PARTICIPANTS ||
		( invariantRequiredSideCount > 0 &&
			readinessPolicy.minimumActivePerRequiredSide >
				MP_MATCH_MAX_PARTICIPANTS / invariantRequiredSideCount ) ||
		readinessPolicy.readyThresholdBasisPoints > 10000 ||
		readinessPolicy.maximumActivePerSide < 0 ||
		readinessPolicy.maximumActivePerSide > MP_MATCH_MAX_PARTICIPANTS ||
		( readinessPolicy.requiredSideMask &
			~static_cast<uint32_t>( ( 1 << MP_MATCH_SIDE_COUNT ) - 1 ) ) ||
		( externalReadinessBlockers & ~ExternalReadinessBlockerBits() ) ) {
		return false;
	}
	const bool invariantUsesTeamReady = readinessPolicy.policy == MP_MATCH_READY_TEAM ||
		readinessPolicy.policy == MP_MATCH_READY_INDIVIDUAL_AND_TEAM;
	if ( ( invariantUsesTeamReady &&
		( !readinessPolicy.teamMode || readinessPolicy.requiredSideMask == 0 ) ) ||
		( !readinessPolicy.teamMode &&
			( readinessPolicy.allowRoundSideChanges || readinessPolicy.maximumActivePerSide != 0 ||
				readinessPolicy.minimumActiveOnAnySide != 0 ||
				readinessPolicy.minimumActivePerRequiredSide != 0 ||
				readinessPolicy.requiredSideMask != 0 ) ) ) {
		return false;
	}
	if ( readinessPolicy.minimumActivePerRequiredSide > 0 &&
		readinessPolicy.requiredSideMask == 0 ) {
		return false;
	}
	if ( readinessPolicy.maximumActivePerSide > 0 &&
		( readinessPolicy.minimumActivePerRequiredSide > readinessPolicy.maximumActivePerSide ||
			readinessPolicy.minimumActiveOnAnySide > readinessPolicy.maximumActivePerSide ) ) {
		return false;
	}
	for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
		const mpMatchTimeoutBudgetState &budget = timeoutBudgets[ side ];
		if ( budget.configured < 0 || budget.configured > MP_MATCH_MAX_TIMEOUTS_PER_SIDE ||
			budget.remaining < 0 || budget.consumed < 0 ||
			budget.remaining + budget.consumed != budget.configured ) {
			return false;
		}
	}
	if ( timeoutDurationMsec < 0 || timeoutDurationMsec > MP_MATCH_MAX_TIMEOUT_MSEC ||
		resumeCountdownMsec < 0 || resumeCountdownMsec > MP_MATCH_MAX_RESUME_COUNTDOWN_MSEC ||
		resumePolicy < MP_MATCH_RESUME_OWNER_OR_AUTHORITY ||
		resumePolicy >= MP_MATCH_RESUME_POLICY_COUNT ) {
		return false;
	}
	if ( timeoutBudgets[ 0 ].configured != timeoutBudgets[ 1 ].configured ||
		( timeoutBudgets[ 0 ].configured == 0 && timeoutDurationMsec != 0 ) ||
		( timeoutBudgets[ 0 ].configured > 0 && timeoutDurationMsec == 0 ) ) {
		return false;
	}
	return true;
}
