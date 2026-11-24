//----------------------------------------------------------------
// MatchDisclosurePolicy.h
//
// Fail-closed spectator and tactical-information disclosure policy.
//
// This boundary translates a committed policy plus a trusted session identity
// into the only MatchView recipient policy the adapter should use.  It also
// validates follow requests on the server and holds item intelligence on the
// match clock before a candidate can enter a recipient view.  It owns no game
// objects, cvars, GUI state, transport, credentials or persistence.
//----------------------------------------------------------------

#ifndef __MP_MATCH_DISCLOSURE_POLICY_H__
#define __MP_MATCH_DISCLOSURE_POLICY_H__

#include "MatchSession.h"
#include "MatchView.h"

#include <stdint.h>

static const uint16_t MP_MATCH_DISCLOSURE_POLICY_VERSION = 1;
static const int64_t MP_MATCH_DISCLOSURE_MAX_ITEM_TIMING_DELAY_MSEC =
	24LL * 60LL * 60LL * 1000LL;

typedef uint32_t mpMatchDisclosureSideMask_t;

typedef enum {
	MP_MATCH_DISCLOSURE_PRINCIPAL_INVALID = 0,
	MP_MATCH_DISCLOSURE_PRINCIPAL_PLAYER,
	MP_MATCH_DISCLOSURE_PRINCIPAL_CAPTAIN,
	MP_MATCH_DISCLOSURE_PRINCIPAL_COACH,
	MP_MATCH_DISCLOSURE_PRINCIPAL_SPECTATOR,
	MP_MATCH_DISCLOSURE_PRINCIPAL_BROADCASTER,
	MP_MATCH_DISCLOSURE_PRINCIPAL_REFEREE,
	MP_MATCH_DISCLOSURE_PRINCIPAL_REPEATER,
	MP_MATCH_DISCLOSURE_PRINCIPAL_COUNT
} mpMatchDisclosurePrincipal_t;

// A valid grant may still carry a non-NONE reason when the recipient receives
// a deliberately reduced public/control projection.  Invalid inputs never
// produce a usable view policy.
typedef enum {
	MP_MATCH_DISCLOSURE_REASON_NONE = 0,
	MP_MATCH_DISCLOSURE_REASON_INVALID_POLICY,
	MP_MATCH_DISCLOSURE_REASON_INVALID_RECIPIENT,
	MP_MATCH_DISCLOSURE_REASON_ROLE_CONFLICT,
	MP_MATCH_DISCLOSURE_REASON_REPEATER_PUBLIC_ONLY,
	MP_MATCH_DISCLOSURE_REASON_SPECTATOR_LOCKED,
	MP_MATCH_DISCLOSURE_REASON_COACH_OBSERVATION_DISABLED,
	MP_MATCH_DISCLOSURE_REASON_BROADCAST_OBSERVATION_DISABLED,
	MP_MATCH_DISCLOSURE_REASON_REFEREE_OBSERVATION_DISABLED,
	MP_MATCH_DISCLOSURE_REASON_SOURCE_BINDING_MISMATCH,
	MP_MATCH_DISCLOSURE_REASON_UNSAFE_SOURCE,
	MP_MATCH_DISCLOSURE_REASON_VIEW_REJECTED,
	MP_MATCH_DISCLOSURE_REASON_COUNT
} mpMatchDisclosureReason_t;

// Side locks are independent so each captain can protect their own POV.  A
// neutral spectator can observe an unlocked side or a side for which their
// current ParticipantId has an invitation.  A team-affiliated observer is
// always restricted to their own side.  Coaches, broadcasters and referees use
// their explicit role paths instead of spectator invitations.
typedef struct mpMatchDisclosurePolicy_s {
	uint16_t				schemaVersion;
	mpMatchDisclosureSideMask_t lockedSpectatorSideMask;
	bool					allowSpectatorInvitations;
	bool					allowCoachObservation;
	bool					allowLiveBroadcasterObservation;
	bool					allowBroadcasterItemTiming;
	bool					allowRefereeObservation;
	bool					allowRefereeItemTiming;
	int64_t				itemTimingDelayMsec;

	void					Clear( void );
	bool					Validate( void ) const;
} mpMatchDisclosurePolicy_t;

// The adapter constructs this value exclusively from the current authoritative
// session and trusted transport binding.  invitationSideMask must be bound to
// this ParticipantId, not a slot, name, userinfo value or prior connection.
typedef struct mpMatchDisclosureRecipient_s {
	uint64_t				sessionId;
	uint64_t				sessionRevision;
	mpMatchProtocolParticipantId_t participantId;
	int					slot;
	uint32_t			bindingGeneration;
	// Active non-team players remain SIDE_NONE and receive only the public
	// projection; team-affiliated active players use their authoritative side.
	int					side;
	mpMatchRoleMask_t	roles;
	bool					active;
	bool					repeater;
	mpMatchDisclosureSideMask_t invitationSideMask;

	void					Clear( void );
} mpMatchDisclosureRecipient_t;

typedef struct mpMatchDisclosureGrant_s {
	bool					valid;
	mpMatchDisclosurePrincipal_t principal;
	mpMatchDisclosureReason_t reason;
	mpMatchViewRecipientPolicy_t viewPolicy;
	mpMatchDisclosureSideMask_t followSideMask;
	bool					itemTimingAllowed;
	bool					repeaterPublicOnly;

	void					Clear( void );
} mpMatchDisclosureGrant_t;

typedef enum {
	MP_MATCH_DISCLOSURE_ITEM_REJECTED = 0,
	MP_MATCH_DISCLOSURE_ITEM_NOT_PERMITTED,
	MP_MATCH_DISCLOSURE_ITEM_CLOCK_REGRESSION,
	MP_MATCH_DISCLOSURE_ITEM_CLOCK_OVERFLOW,
	MP_MATCH_DISCLOSURE_ITEM_DELAYED,
	MP_MATCH_DISCLOSURE_ITEM_READY,
	MP_MATCH_DISCLOSURE_ITEM_RESULT_COUNT
} mpMatchDisclosureItemResult_t;

mpMatchDisclosureSideMask_t MPMatchDisclosureSideBit( int side );
mpMatchDisclosureSideMask_t MPMatchDisclosureAllSideBits( void );

// Grants are presentation values, not reusable authorization tokens.  Follow
// requests and view construction deliberately accept the current policy and
// recipient again so a cached grant cannot survive a role, lock, invitation,
// slot-generation or session-revision change.
bool MPMatchDisclosureBuildGrant( const mpMatchDisclosurePolicy_t &policy,
	const mpMatchDisclosureRecipient_t &recipient,
	mpMatchDisclosureGrant_t &grant );

bool MPMatchDisclosureCanFollow( const mpMatchDisclosurePolicy_t &policy,
	const mpMatchDisclosureRecipient_t &recipient,
	mpMatchProtocolParticipantId_t targetParticipantId, int targetSide,
	bool targetActive );

// This is the canonical recipient-view entry point.  Besides computing the
// grant, it binds the source to the current session identity and rejects unsafe
// observer tags (including public vitals, public item timers and public follow
// targets) before MatchView performs its generic validation and filtering.
bool MPMatchDisclosureBuildView( const mpMatchDisclosurePolicy_t &policy,
	const mpMatchDisclosureRecipient_t &recipient,
	const mpMatchViewSource_t &source, mpSessionView &view,
	mpMatchDisclosureReason_t *reason = 0,
	mpMatchViewError_t *viewError = 0 );

// observedAtMatchTime is when the authoritative item state changed or first
// became knowable.  The holdback uses match time, so a timeout/technical pause
// cannot consume the delay.  This is an item-intelligence delay only; it must
// never be presented as a delayed audio/video broadcast.
mpMatchDisclosureItemResult_t MPMatchDisclosureSetItemTimingCandidate(
	const mpMatchDisclosurePolicy_t &policy,
	mpMatchViewAudience_t audience,
	mpMatchTime currentMatchTime, mpMatchTime observedAtMatchTime,
	const char *itemToken, mpMatchTime matchDeadline, bool available,
	mpMatchViewObserverCandidate_t &candidate,
	mpMatchTime *notBeforeMatchTime = 0 );

#endif // __MP_MATCH_DISCLOSURE_POLICY_H__
