//----------------------------------------------------------------
// MatchTeams.h
//
// Bounded competitive team locks, join queue and roster invitations.
//
// This core reads mpMatchSession only to derive authoritative participant,
// population and roster facts.  It never mutates a session.  Accepted joins
// and substitutions are returned as typed transaction plans so an adapter can
// validate both copied cores, apply the session changes to a copy, then publish
// both copies together.
//----------------------------------------------------------------

#ifndef __MP_MATCH_TEAMS_H__
#define __MP_MATCH_TEAMS_H__

#include "MatchSession.h"

#include <stdint.h>

static const int MP_MATCH_TEAMS_MAX_QUEUE_ENTRIES = MP_MATCH_MAX_PARTICIPANTS;
static const int MP_MATCH_TEAMS_MAX_INVITATIONS = MP_MATCH_MAX_ROSTER_SEATS;
static const int MP_MATCH_TEAMS_MAX_INVITATION_MSEC = 24 * 60 * 60 * 1000;
static const uint32_t MP_MATCH_TEAMS_MAX_INVITATION_ID = 2147483647u;

typedef uint64_t mpMatchTeamsRevision_t;
typedef uint64_t mpMatchQueueTicketId_t;
typedef uint32_t mpMatchRosterInvitationId_t;

// Append only.  These values may be recorded in evidence and mapped to
// localized presentation reasons by the adapter.
typedef enum {
	MP_MATCH_TEAMS_MUTATION_APPLIED = 0,
	MP_MATCH_TEAMS_MUTATION_NO_CHANGE,
	MP_MATCH_TEAMS_MUTATION_REJECTED,
	MP_MATCH_TEAMS_MUTATION_CODE_COUNT
} mpMatchTeamsMutationCode_t;

typedef enum {
	MP_MATCH_TEAMS_REASON_NONE = 0,
	MP_MATCH_TEAMS_REASON_INVALID_ARGUMENT,
	MP_MATCH_TEAMS_REASON_SESSION_MISMATCH,
	MP_MATCH_TEAMS_REASON_STALE_REVISION,
	MP_MATCH_TEAMS_REASON_STALE_SESSION_REVISION,
	MP_MATCH_TEAMS_REASON_REVISION_EXHAUSTED,
	MP_MATCH_TEAMS_REASON_CLOCK_REGRESSION,
	MP_MATCH_TEAMS_REASON_CLOCK_OVERFLOW,
	MP_MATCH_TEAMS_REASON_WRONG_PHASE,
	MP_MATCH_TEAMS_REASON_INVALID_SIDE,
	MP_MATCH_TEAMS_REASON_INVALID_ROLE,
	MP_MATCH_TEAMS_REASON_POLICY_MISMATCH,
	MP_MATCH_TEAMS_REASON_PARTICIPANT_INVALID,
	MP_MATCH_TEAMS_REASON_PARTICIPANT_UNKNOWN,
	MP_MATCH_TEAMS_REASON_PARTICIPANT_NOT_HUMAN,
	MP_MATCH_TEAMS_REASON_PARTICIPANT_ACTIVE,
	MP_MATCH_TEAMS_REASON_PARTICIPANT_ALREADY_ROSTERED,
	MP_MATCH_TEAMS_REASON_TEAM_LOCKED,
	MP_MATCH_TEAMS_REASON_ACTIVE_CAPACITY,
	MP_MATCH_TEAMS_REASON_TEAM_CAPACITY,
	MP_MATCH_TEAMS_REASON_ROSTER_REQUIRED,
	MP_MATCH_TEAMS_REASON_ROSTER_ALIGNMENT,
	MP_MATCH_TEAMS_REASON_ROSTER_SEAT_UNAVAILABLE,
	MP_MATCH_TEAMS_REASON_QUEUE_DISABLED,
	MP_MATCH_TEAMS_REASON_QUEUE_CAPACITY,
	MP_MATCH_TEAMS_REASON_QUEUE_TICKET_EXHAUSTED,
	MP_MATCH_TEAMS_REASON_ALREADY_QUEUED,
	MP_MATCH_TEAMS_REASON_NOT_QUEUED,
	MP_MATCH_TEAMS_REASON_QUEUE_TICKET_MISMATCH,
	MP_MATCH_TEAMS_REASON_QUEUE_WAIT,
	MP_MATCH_TEAMS_REASON_INVITATION_CAPACITY,
	MP_MATCH_TEAMS_REASON_INVITATION_ID_EXHAUSTED,
	MP_MATCH_TEAMS_REASON_INVITATION_DUPLICATE,
	MP_MATCH_TEAMS_REASON_INVITATION_UNKNOWN,
	MP_MATCH_TEAMS_REASON_INVITATION_TARGET_MISMATCH,
	MP_MATCH_TEAMS_REASON_INVITATION_EXPIRED,
	MP_MATCH_TEAMS_REASON_INVITATION_REQUIRED,
	MP_MATCH_TEAMS_REASON_INVITATION_NOT_ISSUER,
	MP_MATCH_TEAMS_REASON_INVITATION_ISSUER_STALE,
	MP_MATCH_TEAMS_REASON_SUBSTITUTION_DISABLED,
	MP_MATCH_TEAMS_REASON_SUBSTITUTION_SAME_PARTICIPANT,
	MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_INVALID,
	MP_MATCH_TEAMS_REASON_TRANSACTION_PLAN_STALE,
	MP_MATCH_TEAMS_REASON_INVARIANT,
	MP_MATCH_TEAMS_REASON_COUNT
} mpMatchTeamsReason_t;

typedef struct mpMatchTeamsMutationResult_s {
	mpMatchTeamsMutationCode_t code;
	mpMatchTeamsReason_t reason;
	mpMatchTeamsRevision_t previousRevision;
	mpMatchTeamsRevision_t currentRevision;
	mpMatchQueueTicketId_t queueTicketId;
	mpMatchRosterInvitationId_t invitationId;

	bool WasApplied( void ) const;
	bool WasRejected( void ) const;
} mpMatchTeamsMutationResult_t;

typedef struct mpMatchTeamsPolicy_s {
	bool teamMode;
	bool queueEnabled;
	bool requireRosterMembership;
	bool invitationBypassesLock;
	bool requireInvitationForSubstitution;
	bool allowLiveJoin;
	bool allowLiveSubstitution;
	int maximumActiveTotal;
	int maximumActivePerSide;

	void Clear( void );
	bool IsValid( void ) const;
} mpMatchTeamsPolicy_t;

// Roster-role edits replace the player/captain/coach bundle.  Referee and
// broadcaster identities are deliberately rejected: tactical observer and
// roster audiences are mutually exclusive for one connection.
mpMatchRoleMask_t MPMatchTeamsPrincipalRoleMask( void );
bool MPMatchTeamsAssignRosterRole( mpMatchRoleMask_t existingRoles,
	mpMatchRosterRole_t rosterRole, mpMatchRoleMask_t &outRoles );
bool MPMatchTeamsClearRosterRole( mpMatchRoleMask_t existingRoles,
	mpMatchRoleMask_t &outRoles );

typedef struct mpMatchQueueEntry_s {
	mpMatchQueueTicketId_t ticketId;
	mpParticipantId participant;
	int requestedSide;
	mpMatchEngineTime enqueuedAt;
	mpMatchEngineTime positionedAt;
	uint32_t deferralCount;

	void Clear( void );
	bool IsOccupied( void ) const;
} mpMatchQueueEntry_t;

typedef struct mpMatchRosterInvitation_s {
	mpMatchRosterInvitationId_t invitationId;
	uint64_t sessionId;
	mpParticipantId target;
	int side;
	mpMatchRosterRole_t role;
	mpParticipantId issuer;
	// Only the trusted local-operator adapter may set this connection-scoped
	// grant. Network roles are otherwise revalidated whenever the invite is used.
	bool operatorAuthorized;
	int rosterSeat;
	mpMatchEngineTime issuedAt;
	mpMatchEngineTime expiresAt;

	void Clear( void );
	bool IsOccupied( void ) const;
	bool IsActiveAt( mpMatchEngineTime engineNow ) const;
} mpMatchRosterInvitation_t;

typedef enum {
	MP_MATCH_TEAMS_TRANSACTION_NONE = 0,
	MP_MATCH_TEAMS_TRANSACTION_DIRECT_JOIN,
	MP_MATCH_TEAMS_TRANSACTION_QUEUE_ADMISSION,
	MP_MATCH_TEAMS_TRANSACTION_ROSTER_ACCEPTANCE,
	MP_MATCH_TEAMS_TRANSACTION_SUBSTITUTION,
	MP_MATCH_TEAMS_TRANSACTION_KIND_COUNT
} mpMatchTeamsTransactionKind_t;

// This plan contains only declarative session edits.  It is bound to both
// independent revisions and is re-derived byte-for-byte by CommitTransactionPlan
// before the team core consumes a queue ticket or invitation.
typedef struct mpMatchTeamsTransactionPlan_s {
	mpMatchTeamsTransactionKind_t kind;
	uint64_t sessionId;
	mpMatchTeamsRevision_t expectedTeamsRevision;
	uint64_t expectedSessionRevision;
	mpParticipantId incomingParticipant;
	mpParticipantId outgoingParticipant;
	mpParticipantId invitationIssuer;
	int side;
	mpMatchRosterRole_t rosterRole;
	int rosterSeat;
	// A persistent bench swap has two destinations: rosterSeat receives the
	// incoming substitute and outgoingRosterSeat receives the outgoing player
	// as a substitute.  outgoingRosterSeat remains -1 for an emergency
	// unrostered replacement.
	mpMatchRosterRole_t outgoingRosterRole;
	int outgoingRosterSeat;
	mpMatchQueueTicketId_t queueTicketId;
	mpMatchRosterInvitationId_t invitationId;
	bool consumeQueueTicket;
	bool consumeInvitation;
	bool setIncomingActive;
	bool incomingActive;
	bool setIncomingSide;
	int incomingSide;
	bool assignIncomingRosterRole; // use MPMatchTeamsAssignRosterRole
	bool setOutgoingActive;
	bool outgoingActive;
	bool setOutgoingSide;
	int outgoingSide;
	bool clearOutgoingRosterRole; // use MPMatchTeamsClearRosterRole
	bool assignOutgoingRosterRole; // use MPMatchTeamsAssignRosterRole
	bool vacateRosterSeat;
	bool vacateOutgoingRosterSeat;
	bool assignRosterSeat;
	bool assignOutgoingRosterSeat;

	void Clear( void );
	bool IsValid( void ) const;
	bool IsPersistentBenchSwap( void ) const;
} mpMatchTeamsTransactionPlan_t;

typedef enum {
	MP_MATCH_TEAMS_JOIN_DENY = 0,
	MP_MATCH_TEAMS_JOIN_QUEUE,
	MP_MATCH_TEAMS_JOIN_ALLOW,
	MP_MATCH_TEAMS_JOIN_NO_CHANGE,
	MP_MATCH_TEAMS_JOIN_DISPOSITION_COUNT
} mpMatchTeamsJoinDisposition_t;

typedef struct mpMatchTeamsJoinDecision_s {
	mpMatchTeamsJoinDisposition_t disposition;
	mpMatchTeamsReason_t reason;
	mpMatchTeamsTransactionPlan_t plan;

	void Clear( void );
	bool IsAllowed( void ) const;
} mpMatchTeamsJoinDecision_t;

// A recipient projection intentionally exposes no other participant identity.
// Roster invitations are copied only when their target is the recipient.
typedef struct mpMatchTeamsRecipientSnapshot_s {
	uint64_t sessionId;
	mpMatchTeamsRevision_t revision;
	bool sideLocked[ MP_MATCH_SIDE_COUNT ];
	int queueCount;
	bool recipientQueued;
	int recipientQueuePosition;
	mpMatchQueueTicketId_t recipientQueueTicketId;
	int recipientRequestedSide;
	int invitationCount;
	mpMatchRosterInvitation_t invitations[ MP_MATCH_TEAMS_MAX_INVITATIONS ];

	void Clear( void );
} mpMatchTeamsRecipientSnapshot_t;

class mpMatchTeams {
public:
	mpMatchTeams( void );

	bool Reset( uint64_t sessionId, mpMatchEngineTime initialEngineTime );
	uint64_t GetSessionId( void ) const;
	mpMatchTeamsRevision_t GetRevision( void ) const;
	mpMatchEngineTime GetLastEngineTime( void ) const;

	bool IsSideLocked( int side ) const;
	mpMatchTeamsMutationResult_t SetSideLocked( const mpMatchSession &session,
		int side, bool locked, mpMatchTeamsRevision_t expectedRevision );

	int GetQueueCount( void ) const;
	const mpMatchQueueEntry_t *GetQueueEntry( int index ) const;
	int FindQueuePosition( mpParticipantId participant ) const;
	mpMatchTeamsMutationResult_t JoinQueue( const mpMatchSession &session,
		mpParticipantId participant, int requestedSide,
		const mpMatchTeamsPolicy_t &policy, mpMatchEngineTime engineNow,
		mpMatchTeamsRevision_t expectedRevision );
	mpMatchTeamsMutationResult_t DeferQueue( const mpMatchSession &session,
		mpParticipantId participant, mpMatchQueueTicketId_t expectedTicketId,
		mpMatchEngineTime engineNow, mpMatchTeamsRevision_t expectedRevision );
	mpMatchTeamsMutationResult_t LeaveQueue( uint64_t requestedSessionId,
		mpParticipantId participant, mpMatchQueueTicketId_t expectedTicketId,
		mpMatchTeamsRevision_t expectedRevision );

	int GetInvitationCount( void ) const;
	const mpMatchRosterInvitation_t *GetInvitationByIndex( int index ) const;
	const mpMatchRosterInvitation_t *FindInvitation(
		mpMatchRosterInvitationId_t invitationId ) const;
	mpMatchTeamsMutationResult_t IssueRosterInvitation( const mpMatchSession &session,
		mpParticipantId target, int side, mpMatchRosterRole_t role,
		mpParticipantId issuer, int lifetimeMsec, mpMatchEngineTime engineNow,
		mpMatchTeamsRevision_t expectedRevision,
		mpMatchRosterInvitationId_t &outInvitationId,
		bool operatorAuthorized = false );
	mpMatchTeamsMutationResult_t RevokeRosterInvitation( uint64_t requestedSessionId,
		mpMatchRosterInvitationId_t invitationId, mpParticipantId requester,
		bool authorityOverride, mpMatchEngineTime engineNow,
		mpMatchTeamsRevision_t expectedRevision );
	mpMatchTeamsMutationResult_t ExpireRosterInvitations( const mpMatchSession &session,
		mpMatchEngineTime engineNow, mpMatchTeamsRevision_t expectedRevision );

	// Removing a disconnected identity also revokes every authority-bearing
	// invitation it issued and every invitation addressed to it.
	mpMatchTeamsMutationResult_t RemoveParticipant( uint64_t requestedSessionId,
		mpParticipantId participant, mpMatchEngineTime engineNow,
		mpMatchTeamsRevision_t expectedRevision );

	mpMatchTeamsJoinDecision_t EvaluateJoin( const mpMatchSession &session,
		mpParticipantId participant, int requestedSide,
		mpMatchRosterInvitationId_t invitationId,
		const mpMatchTeamsPolicy_t &policy, mpMatchEngineTime engineNow ) const;
	mpMatchTeamsJoinDecision_t PlanNextQueueAdmission( const mpMatchSession &session,
		const mpMatchTeamsPolicy_t &policy, mpMatchEngineTime engineNow ) const;
	mpMatchTeamsJoinDecision_t PlanRosterInvitationAcceptance(
		const mpMatchSession &session, mpParticipantId target,
		mpMatchRosterInvitationId_t invitationId,
		const mpMatchTeamsPolicy_t &policy, mpMatchEngineTime engineNow ) const;
	mpMatchTeamsJoinDecision_t PlanSubstitution( const mpMatchSession &session,
		mpParticipantId outgoingParticipant, mpParticipantId incomingParticipant,
		int side, mpMatchRosterInvitationId_t invitationId,
		const mpMatchTeamsPolicy_t &policy, mpMatchEngineTime engineNow ) const;

	// This commits only team-core resources.  The session argument is const and
	// must still be at plan.expectedSessionRevision.  Integration should invoke
	// this on a copied mpMatchTeams, apply the declarative plan to a copied
	// mpMatchSession, validate both, and publish the copies together.  For a
	// persistent bench substitution it must vacate both rosterSeat and
	// outgoingRosterSeat, assign the incoming active-role bundle/seat, assign
	// the outgoing substitute-role bundle/outgoingRosterSeat, and publish only
	// after every session mutation and both invariant checks succeed.
	mpMatchTeamsMutationResult_t CommitTransactionPlan(
		const mpMatchTeamsTransactionPlan_t &plan,
		const mpMatchSession &session, const mpMatchTeamsPolicy_t &policy,
		mpMatchEngineTime engineNow, mpMatchTeamsRevision_t expectedRevision );

	bool BuildRecipientSnapshot( const mpMatchSession &session,
		mpParticipantId recipient,
		mpMatchEngineTime engineNow, mpMatchTeamsRecipientSnapshot_t &out ) const;
	bool ValidateInvariants( void ) const;

private:
	bool CanMutate( uint64_t requestedSessionId,
		mpMatchTeamsRevision_t expectedRevision, mpMatchTeamsReason_t &reason ) const;
	bool ValidateTime( mpMatchEngineTime engineNow, mpMatchTeamsReason_t &reason ) const;
	bool ValidateSession( const mpMatchSession &session,
		const mpMatchTeamsPolicy_t *policy, mpMatchTeamsReason_t &reason ) const;
	bool IsParticipantForSession( mpParticipantId participant ) const;
	int FindInvitationIndex( mpMatchRosterInvitationId_t invitationId ) const;
	int FindReservableRosterSeat( const mpMatchSession &session, int side,
		mpMatchRosterRole_t role ) const;
	void RemoveQueueAt( int index );
	void RemoveInvitationAt( int index );
	int RemoveExpiredInvitations( const mpMatchSession &session,
		mpMatchEngineTime engineNow );
	mpMatchTeamsJoinDecision_t EvaluateJoinInternal( const mpMatchSession &session,
		mpParticipantId participant, int requestedSide,
		mpMatchRosterInvitationId_t invitationId,
		const mpMatchTeamsPolicy_t &policy, mpMatchEngineTime engineNow,
		bool requireInvitation, bool queueHeadOnly ) const;
	bool PlansEqual( const mpMatchTeamsTransactionPlan_t &lhs,
		const mpMatchTeamsTransactionPlan_t &rhs ) const;
	mpMatchTeamsMutationResult_t Applied( mpMatchQueueTicketId_t queueTicketId,
		mpMatchRosterInvitationId_t invitationId );
	mpMatchTeamsMutationResult_t NoChange( mpMatchTeamsReason_t reason,
		mpMatchQueueTicketId_t queueTicketId = 0,
		mpMatchRosterInvitationId_t invitationId = 0 ) const;
	mpMatchTeamsMutationResult_t ObservedNoChange( mpMatchEngineTime engineNow,
		mpMatchTeamsReason_t reason, mpMatchQueueTicketId_t queueTicketId = 0,
		mpMatchRosterInvitationId_t invitationId = 0 );
	mpMatchTeamsMutationResult_t Rejected( mpMatchTeamsReason_t reason ) const;

	uint64_t sessionId;
	mpMatchTeamsRevision_t revision;
	mpMatchEngineTime lastEngineTime;
	bool sideLocked[ MP_MATCH_SIDE_COUNT ];
	mpMatchQueueTicketId_t nextQueueTicketId;
	mpMatchQueueEntry_t queue[ MP_MATCH_TEAMS_MAX_QUEUE_ENTRIES ];
	int queueCount;
	mpMatchRosterInvitationId_t nextInvitationId;
	mpMatchRosterInvitation_t invitations[ MP_MATCH_TEAMS_MAX_INVITATIONS ];
	int invitationCount;
};

#endif // __MP_MATCH_TEAMS_H__
