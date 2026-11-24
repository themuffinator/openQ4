//----------------------------------------------------------------
// MatchDisclosurePolicy.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_DISCLOSURE_STANDALONE_TEST )
	#include "MatchDisclosurePolicy.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchDisclosurePolicy.h"
#endif

#include <limits.h>
#include <string.h>

static_assert( MP_MATCH_SIDE_COUNT == MP_MATCH_VIEW_SIDE_COUNT,
	"session and recipient-view side domains diverged" );
static_assert( MP_MATCH_SIDE_NONE == MP_MATCH_VIEW_SIDE_NONE,
	"session and recipient-view no-side values diverged" );

namespace {

static bool IsSide( int side ) {
	return side >= 0 && side < MP_MATCH_SIDE_COUNT;
}

static bool HasRole( mpMatchRoleMask_t roles, mpMatchRole_t role ) {
	return ( roles & MPMatchRoleBit( role ) ) != 0;
}

static int ObserverRoleCount( mpMatchRoleMask_t roles ) {
	int count = 0;
	count += HasRole( roles, MP_MATCH_ROLE_COACH ) ? 1 : 0;
	count += HasRole( roles, MP_MATCH_ROLE_BROADCASTER ) ? 1 : 0;
	count += HasRole( roles, MP_MATCH_ROLE_REFEREE ) ? 1 : 0;
	return count;
}

static mpMatchViewPublicRoleMask_t PublicRoleMask( mpMatchRoleMask_t roles ) {
	mpMatchViewPublicRoleMask_t result = 0;
	if ( HasRole( roles, MP_MATCH_ROLE_PLAYER ) ) {
		result |= MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_PLAYER );
	}
	if ( HasRole( roles, MP_MATCH_ROLE_CAPTAIN ) ) {
		result |= MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_CAPTAIN );
	}
	if ( HasRole( roles, MP_MATCH_ROLE_COACH ) ) {
		result |= MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_COACH );
	}
	if ( HasRole( roles, MP_MATCH_ROLE_BROADCASTER ) ) {
		result |= MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_BROADCASTER );
	}
	if ( HasRole( roles, MP_MATCH_ROLE_REFEREE ) ) {
		result |= MPMatchViewRoleBit( MP_MATCH_VIEW_ROLE_REFEREE );
	}
	return result;
}

static mpMatchViewAudience_t SpectatorAudienceForSide( int side ) {
	return side == 0 ? MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0 :
		MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1;
}

static bool RecipientShapeIsValid( const mpMatchDisclosureRecipient_t &recipient,
	mpMatchDisclosureReason_t &reason ) {
	reason = MP_MATCH_DISCLOSURE_REASON_INVALID_RECIPIENT;
	if ( recipient.sessionId == 0 || recipient.sessionRevision == 0 ||
		recipient.participantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
		recipient.slot < 0 || recipient.slot >= MP_MATCH_MAX_CONNECTION_SLOTS ||
		recipient.bindingGeneration == 0 ||
		( recipient.side != MP_MATCH_SIDE_NONE && !IsSide( recipient.side ) ) ||
		( recipient.invitationSideMask & ~MPMatchDisclosureAllSideBits() ) != 0 ||
		( recipient.repeater && recipient.active ) ) {
		return false;
	}
	if ( !MPMatchRoleMaskIsValid( recipient.roles, true ) ) {
		reason = MP_MATCH_DISCLOSURE_REASON_ROLE_CONFLICT;
		return false;
	}

	const bool player = HasRole( recipient.roles, MP_MATCH_ROLE_PLAYER );
	const bool captain = HasRole( recipient.roles, MP_MATCH_ROLE_CAPTAIN );
	const bool coach = HasRole( recipient.roles, MP_MATCH_ROLE_COACH );
	const bool broadcaster = HasRole( recipient.roles, MP_MATCH_ROLE_BROADCASTER );
	const bool referee = HasRole( recipient.roles, MP_MATCH_ROLE_REFEREE );
	// FFA, Duel and Tourney participants are intentionally unsided in the match
	// session.  An active ordinary player is therefore valid with SIDE_NONE;
	// captains and every team-observation role still require a real side.
	const bool activePlayerSideValid = IsSide( recipient.side ) ||
		( player && !captain && recipient.side == MP_MATCH_SIDE_NONE );
	const int observerRoleCount = ObserverRoleCount( recipient.roles );
	if ( ( captain && !player ) || observerRoleCount > 1 ||
		( observerRoleCount != 0 && ( player || captain ) ) ||
		( recipient.active && ( !player || !activePlayerSideValid ||
			coach || broadcaster || referee ) ) ||
		( coach && !IsSide( recipient.side ) ) ||
		( ( broadcaster || referee ) && recipient.side != MP_MATCH_SIDE_NONE ) ||
		( captain && !IsSide( recipient.side ) ) ) {
		reason = MP_MATCH_DISCLOSURE_REASON_ROLE_CONFLICT;
		return false;
	}

	const mpMatchCapabilityMask_t capabilities =
		MPMatchCapabilitiesForRoles( recipient.roles );
	if ( ( coach && ( capabilities & MPMatchCapabilityBit(
			MP_MATCH_CAP_TEAM_OBSERVATION ) ) == 0 ) ||
		( broadcaster && ( capabilities & MPMatchCapabilityBit(
			MP_MATCH_CAP_BROADCAST_OBSERVATION ) ) == 0 ) ) {
		reason = MP_MATCH_DISCLOSURE_REASON_ROLE_CONFLICT;
		return false;
	}
	return true;
}

static void AddOwnSideAudience( int side, mpMatchDisclosureGrant_t &grant ) {
	grant.viewPolicy.audiences |=
		MPMatchViewAudienceBit( MP_MATCH_VIEW_AUDIENCE_OWN_SIDE );
	grant.viewPolicy.ownSide = side;
}

static void AddObserverKind( mpMatchViewObserverKind_t kind,
	mpMatchDisclosureGrant_t &grant ) {
	grant.viewPolicy.observerKinds |= MPMatchViewObserverKindBit( kind );
}

static void AddSpectatorFollowSides( mpMatchDisclosureSideMask_t sides,
	mpMatchDisclosureGrant_t &grant ) {
	grant.followSideMask = sides & MPMatchDisclosureAllSideBits();
	for ( int side = 0; side < MP_MATCH_SIDE_COUNT; ++side ) {
		if ( ( grant.followSideMask & MPMatchDisclosureSideBit( side ) ) != 0 ) {
			grant.viewPolicy.audiences |= MPMatchViewAudienceBit(
				SpectatorAudienceForSide( side ) );
		}
	}
	if ( grant.followSideMask != 0 ) {
		AddObserverKind( MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET, grant );
	}
}

static bool SourceMatchesRecipient( const mpMatchViewSource_t &source,
	const mpMatchDisclosureRecipient_t &recipient ) {
	const mpMatchViewRecipient_t &sourceRecipient = source.publicState.recipient;
	return source.publicState.sessionId == recipient.sessionId &&
		source.publicState.sessionRevision == recipient.sessionRevision &&
		sourceRecipient.participantId == recipient.participantId &&
		static_cast<int>( sourceRecipient.slot ) == recipient.slot &&
		sourceRecipient.bindingGeneration == recipient.bindingGeneration &&
		sourceRecipient.side == recipient.side &&
		sourceRecipient.active == recipient.active &&
		sourceRecipient.publicRoleMask == PublicRoleMask( recipient.roles );
}

static bool SpectatorAudienceMatchesSide( mpMatchViewAudience_t audience,
	int side ) {
	return ( audience == MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_0 && side == 0 ) ||
		( audience == MP_MATCH_VIEW_AUDIENCE_SPECTATOR_SIDE_1 && side == 1 );
}

static bool ObserverSourceIsSafe( const mpMatchDisclosurePolicy_t &policy,
	const mpMatchViewSource_t &source ) {
	if ( source.observerCandidateCount > MP_MATCH_VIEW_MAX_OBSERVER_CANDIDATES ) {
		return false;
	}
	for ( int index = 0; index < source.observerCandidateCount; ++index ) {
		const mpMatchViewObserverCandidate_t &candidate =
			source.observerCandidates[ index ];
		const mpMatchViewAudience_t audience = candidate.authorization.audience;
		switch ( candidate.kind ) {
			case MP_MATCH_VIEW_OBSERVER_TEAM_VITAL:
				if ( audience == MP_MATCH_VIEW_AUDIENCE_OWN_SIDE ) {
					break;
				}
				if ( audience == MP_MATCH_VIEW_AUDIENCE_BROADCASTER &&
					policy.allowLiveBroadcasterObservation ) {
					break;
				}
				if ( audience == MP_MATCH_VIEW_AUDIENCE_REFEREE &&
					policy.allowRefereeObservation ) {
					break;
				}
				return false;

			case MP_MATCH_VIEW_OBSERVER_ITEM_TIMING:
				if ( audience == MP_MATCH_VIEW_AUDIENCE_BROADCASTER &&
					policy.allowBroadcasterItemTiming ) {
					break;
				}
				if ( audience == MP_MATCH_VIEW_AUDIENCE_REFEREE &&
					policy.allowRefereeItemTiming ) {
					break;
				}
				return false;

			case MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET:
				if ( audience == MP_MATCH_VIEW_AUDIENCE_OWN_SIDE &&
					policy.allowCoachObservation ) {
					break;
				}
				if ( audience == MP_MATCH_VIEW_AUDIENCE_BROADCASTER &&
					policy.allowLiveBroadcasterObservation ) {
					break;
				}
				if ( audience == MP_MATCH_VIEW_AUDIENCE_REFEREE &&
					policy.allowRefereeObservation ) {
					break;
				}
				if ( SpectatorAudienceMatchesSide( audience,
					candidate.participantSide ) ) {
					break;
				}
				return false;

			default:
				return false;
		}
	}
	return true;
}

static bool RepeaterSourceIsPublicOnly( const mpMatchViewSource_t &source ) {
	if ( source.proposalCandidateCount != 0 ||
		source.stagedRulesCandidateCount != 0 ||
		source.rosterSeatCandidateCount != 0 ||
		source.invitationCandidateCount != 0 ||
		source.queueEntryCandidateCount != 0 ||
		source.observerCandidateCount != 0 ||
		source.publicState.allowedOperations != 0 ||
		source.publicState.operationAvailabilityCount >
			MP_MATCH_VIEW_MAX_OPERATION_AVAILABILITIES ||
		source.publicState.recipient.ready ||
		source.publicState.recipient.readyEligible ||
		source.publicState.recipient.queueState != MP_MATCH_VIEW_QUEUE_NONE ||
		source.publicState.recipient.hasQueuePosition ||
		source.publicState.recipient.queuePosition != 0 ||
		source.publicState.recipient.resumeConsented ||
		( source.publicState.globalProposal.present &&
			( source.publicState.globalProposal.recipientEligible ||
			source.publicState.globalProposal.recipientBallot !=
				MP_MATCH_VIEW_BALLOT_NONE ) ) ) {
		return false;
	}
	for ( int index = 0;
		index < source.publicState.operationAvailabilityCount; ++index ) {
		if ( source.publicState.operationAvailability[ index ].reason ==
			MP_MATCH_PROTOCOL_REASON_OK ) {
			return false;
		}
	}
	return true;
}

static void SetReason( mpMatchDisclosureReason_t *target,
	mpMatchDisclosureReason_t reason ) {
	if ( target != NULL ) {
		*target = reason;
	}
}

} // namespace

void mpMatchDisclosurePolicy_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	schemaVersion = MP_MATCH_DISCLOSURE_POLICY_VERSION;
	lockedSpectatorSideMask = MPMatchDisclosureAllSideBits();
}

bool mpMatchDisclosurePolicy_t::Validate( void ) const {
	return schemaVersion == MP_MATCH_DISCLOSURE_POLICY_VERSION &&
		( lockedSpectatorSideMask & ~MPMatchDisclosureAllSideBits() ) == 0 &&
		itemTimingDelayMsec >= 0 &&
		itemTimingDelayMsec <= MP_MATCH_DISCLOSURE_MAX_ITEM_TIMING_DELAY_MSEC &&
		( !allowBroadcasterItemTiming || allowLiveBroadcasterObservation ) &&
		( !allowRefereeItemTiming || allowRefereeObservation ) &&
		( itemTimingDelayMsec == 0 || allowBroadcasterItemTiming ||
			allowRefereeItemTiming );
}

void mpMatchDisclosureRecipient_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	side = MP_MATCH_SIDE_NONE;
	slot = -1;
}

void mpMatchDisclosureGrant_t::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	principal = MP_MATCH_DISCLOSURE_PRINCIPAL_INVALID;
	reason = MP_MATCH_DISCLOSURE_REASON_INVALID_RECIPIENT;
	viewPolicy.Clear();
}

mpMatchDisclosureSideMask_t MPMatchDisclosureSideBit( int side ) {
	if ( !IsSide( side ) ) {
		return 0;
	}
	return static_cast<mpMatchDisclosureSideMask_t>( 1u ) << side;
}

mpMatchDisclosureSideMask_t MPMatchDisclosureAllSideBits( void ) {
	return ( static_cast<mpMatchDisclosureSideMask_t>( 1u ) <<
		MP_MATCH_SIDE_COUNT ) - 1u;
}

bool MPMatchDisclosureBuildGrant( const mpMatchDisclosurePolicy_t &policy,
	const mpMatchDisclosureRecipient_t &recipient,
	mpMatchDisclosureGrant_t &grant ) {
	grant.Clear();
	if ( !policy.Validate() ) {
		grant.reason = MP_MATCH_DISCLOSURE_REASON_INVALID_POLICY;
		return false;
	}
	mpMatchDisclosureReason_t validationReason;
	if ( !RecipientShapeIsValid( recipient, validationReason ) ) {
		grant.reason = validationReason;
		return false;
	}

	grant.viewPolicy.recipientId = recipient.participantId;
	if ( recipient.repeater ) {
		grant.valid = true;
		grant.principal = MP_MATCH_DISCLOSURE_PRINCIPAL_REPEATER;
		grant.reason = MP_MATCH_DISCLOSURE_REASON_REPEATER_PUBLIC_ONLY;
		grant.repeaterPublicOnly = true;
		return true;
	}
	if ( IsSide( recipient.side ) ) {
		AddOwnSideAudience( recipient.side, grant );
	}

	if ( recipient.active ) {
		grant.valid = true;
		grant.principal = HasRole( recipient.roles, MP_MATCH_ROLE_CAPTAIN ) ?
			MP_MATCH_DISCLOSURE_PRINCIPAL_CAPTAIN :
			MP_MATCH_DISCLOSURE_PRINCIPAL_PLAYER;
		grant.reason = MP_MATCH_DISCLOSURE_REASON_NONE;
		// An unsided FFA player gets the public match projection only.  Giving it
		// OWN_SIDE/TEAM_VITAL through a synthetic side would classify every FFA
		// opponent as a teammate and disclose their tactical health information.
		if ( IsSide( recipient.side ) ) {
			AddObserverKind( MP_MATCH_VIEW_OBSERVER_TEAM_VITAL, grant );
		}
		return true;
	}

	if ( HasRole( recipient.roles, MP_MATCH_ROLE_COACH ) ) {
		grant.valid = true;
		grant.principal = MP_MATCH_DISCLOSURE_PRINCIPAL_COACH;
		if ( !policy.allowCoachObservation ) {
			grant.reason = MP_MATCH_DISCLOSURE_REASON_COACH_OBSERVATION_DISABLED;
			return true;
		}
		grant.reason = MP_MATCH_DISCLOSURE_REASON_NONE;
		grant.followSideMask = MPMatchDisclosureSideBit( recipient.side );
		AddObserverKind( MP_MATCH_VIEW_OBSERVER_TEAM_VITAL, grant );
		AddObserverKind( MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET, grant );
		return true;
	}

	if ( HasRole( recipient.roles, MP_MATCH_ROLE_BROADCASTER ) ) {
		grant.valid = true;
		grant.principal = MP_MATCH_DISCLOSURE_PRINCIPAL_BROADCASTER;
		grant.viewPolicy.audiences |=
			MPMatchViewAudienceBit( MP_MATCH_VIEW_AUDIENCE_BROADCASTER );
		if ( !policy.allowLiveBroadcasterObservation ) {
			grant.reason = MP_MATCH_DISCLOSURE_REASON_BROADCAST_OBSERVATION_DISABLED;
			return true;
		}
		grant.reason = MP_MATCH_DISCLOSURE_REASON_NONE;
		grant.followSideMask = MPMatchDisclosureAllSideBits();
		AddObserverKind( MP_MATCH_VIEW_OBSERVER_TEAM_VITAL, grant );
		AddObserverKind( MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET, grant );
		if ( policy.allowBroadcasterItemTiming ) {
			grant.itemTimingAllowed = true;
			AddObserverKind( MP_MATCH_VIEW_OBSERVER_ITEM_TIMING, grant );
		}
		return true;
	}

	if ( HasRole( recipient.roles, MP_MATCH_ROLE_REFEREE ) ) {
		grant.valid = true;
		grant.principal = MP_MATCH_DISCLOSURE_PRINCIPAL_REFEREE;
		grant.viewPolicy.audiences |=
			MPMatchViewAudienceBit( MP_MATCH_VIEW_AUDIENCE_REFEREE );
		if ( !policy.allowRefereeObservation ) {
			grant.reason = MP_MATCH_DISCLOSURE_REASON_REFEREE_OBSERVATION_DISABLED;
			return true;
		}
		grant.reason = MP_MATCH_DISCLOSURE_REASON_NONE;
		grant.followSideMask = MPMatchDisclosureAllSideBits();
		AddObserverKind( MP_MATCH_VIEW_OBSERVER_TEAM_VITAL, grant );
		AddObserverKind( MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET, grant );
		if ( policy.allowRefereeItemTiming ) {
			grant.itemTimingAllowed = true;
			AddObserverKind( MP_MATCH_VIEW_OBSERVER_ITEM_TIMING, grant );
		}
		return true;
	}

	grant.valid = true;
	grant.principal = MP_MATCH_DISCLOSURE_PRINCIPAL_SPECTATOR;
	mpMatchDisclosureSideMask_t allowedSides = 0;
	if ( IsSide( recipient.side ) ) {
		// A team-affiliated inactive player/substitute never gains enemy POV by
		// waiting, being eliminated or changing follow target.
		allowedSides = MPMatchDisclosureSideBit( recipient.side );
	} else {
		allowedSides = MPMatchDisclosureAllSideBits() &
			~policy.lockedSpectatorSideMask;
		if ( policy.allowSpectatorInvitations ) {
			allowedSides |= recipient.invitationSideMask;
		}
	}
	AddSpectatorFollowSides( allowedSides, grant );
	grant.reason = allowedSides != 0 ? MP_MATCH_DISCLOSURE_REASON_NONE :
		MP_MATCH_DISCLOSURE_REASON_SPECTATOR_LOCKED;
	return true;
}

bool MPMatchDisclosureCanFollow( const mpMatchDisclosurePolicy_t &policy,
	const mpMatchDisclosureRecipient_t &recipient,
	mpMatchProtocolParticipantId_t targetParticipantId, int targetSide,
	bool targetActive ) {
	if ( targetParticipantId == MP_MATCH_INVALID_PARTICIPANT_ID ||
		!targetActive || !IsSide( targetSide ) ) {
		return false;
	}
	mpMatchDisclosureGrant_t grant;
	if ( !MPMatchDisclosureBuildGrant( policy, recipient, grant ) ) {
		return false;
	}
	return ( grant.viewPolicy.observerKinds & MPMatchViewObserverKindBit(
		MP_MATCH_VIEW_OBSERVER_FOLLOW_TARGET ) ) != 0 &&
		( grant.followSideMask & MPMatchDisclosureSideBit( targetSide ) ) != 0;
}

bool MPMatchDisclosureBuildView( const mpMatchDisclosurePolicy_t &policy,
	const mpMatchDisclosureRecipient_t &recipient,
	const mpMatchViewSource_t &source, mpSessionView &view,
	mpMatchDisclosureReason_t *reason, mpMatchViewError_t *viewError ) {
	SetReason( reason, MP_MATCH_DISCLOSURE_REASON_NONE );
	mpMatchDisclosureGrant_t grant;
	if ( !MPMatchDisclosureBuildGrant( policy, recipient, grant ) ) {
		SetReason( reason, grant.reason );
		return false;
	}
	if ( !SourceMatchesRecipient( source, recipient ) ) {
		SetReason( reason, MP_MATCH_DISCLOSURE_REASON_SOURCE_BINDING_MISMATCH );
		return false;
	}
	if ( !ObserverSourceIsSafe( policy, source ) ) {
		SetReason( reason, MP_MATCH_DISCLOSURE_REASON_UNSAFE_SOURCE );
		return false;
	}
	if ( grant.repeaterPublicOnly && !RepeaterSourceIsPublicOnly( source ) ) {
		SetReason( reason, MP_MATCH_DISCLOSURE_REASON_UNSAFE_SOURCE );
		return false;
	}
	if ( !MPMatchViewBuild( source, grant.viewPolicy, view, viewError ) ) {
		SetReason( reason, MP_MATCH_DISCLOSURE_REASON_VIEW_REJECTED );
		return false;
	}
	return true;
}

mpMatchDisclosureItemResult_t MPMatchDisclosureSetItemTimingCandidate(
	const mpMatchDisclosurePolicy_t &policy,
	mpMatchViewAudience_t audience,
	mpMatchTime currentMatchTime, mpMatchTime observedAtMatchTime,
	const char *itemToken, mpMatchTime matchDeadline, bool available,
	mpMatchViewObserverCandidate_t &candidate,
	mpMatchTime *notBeforeMatchTime ) {
	candidate.Clear();
	if ( notBeforeMatchTime != NULL ) {
		*notBeforeMatchTime = mpMatchTime::FromMilliseconds( 0 );
	}
	if ( !policy.Validate() || !currentMatchTime.IsValid() ||
		!observedAtMatchTime.IsValid() || !matchDeadline.IsValid() ) {
		return MP_MATCH_DISCLOSURE_ITEM_REJECTED;
	}
	if ( ( audience == MP_MATCH_VIEW_AUDIENCE_BROADCASTER &&
			!policy.allowBroadcasterItemTiming ) ||
		( audience == MP_MATCH_VIEW_AUDIENCE_REFEREE &&
			!policy.allowRefereeItemTiming ) ||
		( audience != MP_MATCH_VIEW_AUDIENCE_BROADCASTER &&
			audience != MP_MATCH_VIEW_AUDIENCE_REFEREE ) ) {
		return MP_MATCH_DISCLOSURE_ITEM_NOT_PERMITTED;
	}

	const int64_t currentMsec = currentMatchTime.Milliseconds();
	const int64_t observedMsec = observedAtMatchTime.Milliseconds();
	if ( currentMsec < observedMsec ) {
		return MP_MATCH_DISCLOSURE_ITEM_CLOCK_REGRESSION;
	}
	if ( observedMsec > INT64_MAX - policy.itemTimingDelayMsec ) {
		return MP_MATCH_DISCLOSURE_ITEM_CLOCK_OVERFLOW;
	}
	const int64_t notBeforeMsec = observedMsec + policy.itemTimingDelayMsec;
	if ( notBeforeMatchTime != NULL ) {
		*notBeforeMatchTime = mpMatchTime::FromMilliseconds( notBeforeMsec );
	}
	if ( currentMsec < notBeforeMsec ) {
		return MP_MATCH_DISCLOSURE_ITEM_DELAYED;
	}
	if ( !candidate.SetItemTiming( audience, MP_MATCH_VIEW_SIDE_NONE,
		itemToken, static_cast<unsigned long long>(
			matchDeadline.Milliseconds() ), available ) ) {
		candidate.Clear();
		return MP_MATCH_DISCLOSURE_ITEM_REJECTED;
	}
	return MP_MATCH_DISCLOSURE_ITEM_READY;
}
