//----------------------------------------------------------------
// MatchEvidenceObserver.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_EVIDENCE_OBSERVER_STANDALONE_TEST )
	#include "MatchEvidenceObserver.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchEvidenceObserver.h"
#endif

#include <string.h>

mpMatchEvidenceObserver::mpMatchEvidenceObserver( void ) {
	initialized = false;
	sessionId = 0;
	phase = INACTIVE;
	round = RS_INACTIVE;
	memset( &pause, 0, sizeof( pause ) );
	memset( participants, 0, sizeof( participants ) );
	memset( roster, 0, sizeof( roster ) );
	memset( proposal, 0, sizeof( proposal ) );
}

mpEvidencePauseState_t mpMatchEvidenceObserver::EvidencePauseState(
		mpMatchPauseState_t state ) {
	switch ( state ) {
		case MP_MATCH_PAUSE_RUNNING: return MP_EVIDENCE_PAUSE_RUNNING;
		case MP_MATCH_PAUSE_PENDING: return MP_EVIDENCE_PAUSE_PENDING;
		case MP_MATCH_PAUSED: return MP_EVIDENCE_PAUSED;
		case MP_MATCH_RESUME_COUNTDOWN: return MP_EVIDENCE_RESUME_COUNTDOWN;
		default: return MP_EVIDENCE_PAUSE_STATE_COUNT;
	}
}

mpEvidencePauseKind_t mpMatchEvidenceObserver::EvidencePauseKind(
		mpMatchPauseKind_t kind ) {
	switch ( kind ) {
		case MP_MATCH_PAUSE_KIND_NONE: return MP_EVIDENCE_PAUSE_NONE;
		case MP_MATCH_PAUSE_KIND_TEAM_TIMEOUT: return MP_EVIDENCE_PAUSE_TEAM_TIMEOUT;
		case MP_MATCH_PAUSE_KIND_TECHNICAL: return MP_EVIDENCE_PAUSE_TECHNICAL;
		default: return MP_EVIDENCE_PAUSE_KIND_COUNT;
	}
}

mpEvidenceRosterRole_t mpMatchEvidenceObserver::EvidenceRosterRole(
		mpMatchRosterRole_t role ) {
	switch ( role ) {
		case MP_MATCH_ROSTER_PLAYER: return MP_EVIDENCE_ROSTER_PLAYER;
		case MP_MATCH_ROSTER_CAPTAIN: return MP_EVIDENCE_ROSTER_CAPTAIN;
		case MP_MATCH_ROSTER_COACH: return MP_EVIDENCE_ROSTER_COACH;
		case MP_MATCH_ROSTER_SUBSTITUTE: return MP_EVIDENCE_ROSTER_SUBSTITUTE;
		default: return MP_EVIDENCE_ROSTER_ROLE_COUNT;
	}
}

int mpMatchEvidenceObserver::ProposalSide( mpProposalScope_t scope ) {
	switch ( scope ) {
		case MP_PROPOSAL_SCOPE_GLOBAL: return -1;
		case MP_PROPOSAL_SCOPE_TEAM_A: return 0;
		case MP_PROPOSAL_SCOPE_TEAM_B: return 1;
		default: return -2;
	}
}

mpEvidenceProposalAction_t mpMatchEvidenceObserver::ProposalAction(
		mpProposalStatus_t status ) {
	switch ( status ) {
		case MP_PROPOSAL_STATUS_PASSED: return MP_EVIDENCE_PROPOSAL_PASSED;
		case MP_PROPOSAL_STATUS_FAILED: return MP_EVIDENCE_PROPOSAL_FAILED;
		case MP_PROPOSAL_STATUS_EXPIRED: return MP_EVIDENCE_PROPOSAL_EXPIRED;
		case MP_PROPOSAL_STATUS_CANCELLED:
		case MP_PROPOSAL_STATUS_PHASE_INVALIDATED:
			return MP_EVIDENCE_PROPOSAL_CANCELLED;
		default: return MP_EVIDENCE_PROPOSAL_ACTION_COUNT;
	}
}

void mpMatchEvidenceObserver::Capture( const mpMatchSession &session,
		const mpProposalService &proposals ) {
	sessionId = session.GetSessionId();
	phase = session.GetPhase();
	round = session.GetRoundState();
	pause = session.GetPause();
	for ( int index = 0; index < MP_MATCH_MAX_PARTICIPANTS; ++index ) {
		participantState_t &target = participants[ index ];
		memset( &target, 0, sizeof( target ) );
		const mpMatchParticipantState *source = session.GetParticipantByIndex( index );
		if ( source != NULL && source->id.IsValid() ) {
			target.occupied = true;
			target.sequence = source->id.SequencePart();
			target.roles = source->roles;
		}
	}
	for ( int index = 0; index < MP_MATCH_MAX_ROSTER_SEATS; ++index ) {
		rosterState_t &target = roster[ index ];
		memset( &target, 0, sizeof( target ) );
		const mpMatchRosterSeat *source = session.GetRosterSeat( index );
		if ( source != NULL && source->declared ) {
			target.declared = true;
			target.side = source->side;
			target.role = source->role;
			target.occupant = source->occupant.IsValid() ?
				source->occupant.SequencePart() : 0;
		}
	}
	for ( int rawScope = 0; rawScope < MP_PROPOSAL_SCOPE_COUNT; ++rawScope ) {
		proposalState_t &target = proposal[ rawScope ];
		memset( &target, 0, sizeof( target ) );
		const mpProposalScope_t scope = static_cast<mpProposalScope_t>( rawScope );
		const mpProposalRecord_t *source = proposals.GetProposal( scope );
		if ( source != NULL && source->IsOccupied() ) {
			target.occupied = true;
			target.proposalId = source->proposalId;
			target.status = source->status;
			target.castCount = source->castCount;
		}
	}
	initialized = true;
}

void mpMatchEvidenceObserver::Reset( const mpMatchSession &session,
		const mpProposalService &proposals ) {
	Capture( session, proposals );
}

void mpMatchEvidenceObserver::Observe( mpMatchEvidence &evidence,
		const mpEvidenceCommittedStamp &stamp, const mpMatchSession &session,
		const mpProposalService &proposals, mpEvidenceActorRef actor ) {
	if ( !initialized || sessionId != session.GetSessionId() ) {
		Capture( session, proposals );
		return;
	}
	const bool validActor = actor.kind >= MP_EVIDENCE_ACTOR_SYSTEM &&
		actor.kind < MP_EVIDENCE_ACTOR_KIND_COUNT &&
		( actor.kind == MP_EVIDENCE_ACTOR_PARTICIPANT ?
			actor.participantSequence != 0 : actor.participantSequence == 0 );
	if ( !evidence.IsInitialized() ||
		evidence.GetMetadata().sessionId != sessionId ||
		proposals.GetSessionId() != sessionId || !validActor ||
		stamp.sessionRevision == 0 ||
		stamp.sessionRevision != session.GetSessionRevision() ||
		stamp.sessionRevision < evidence.GetLastSessionRevision() ) {
		// Preserve the last committed snapshot so a caller can retry with the
		// correct evidence instance, actor or authoritative session stamp.
		return;
	}

	if ( phase != session.GetPhase() ) {
		const mpMatchTransitionView &transition = session.GetLastTransition();
		mpEvidencePhaseTransition event;
		event.from = phase;
		event.to = session.GetPhase();
		event.reason = static_cast<uint16_t>( transition.reason );
		event.actor = actor.kind == MP_EVIDENCE_ACTOR_SERVER_OPERATOR ? actor :
			( transition.authorizer.IsValid() ?
				MPEvidenceParticipantActor( transition.authorizer.SequencePart() ) : actor );
		evidence.AppendPhaseTransition( stamp, event );
	}
	if ( round != session.GetRoundState() ) {
		const mpMatchRoundTransitionView &transition = session.GetLastRoundTransition();
		mpEvidenceRoundTransition event;
		event.from = round;
		event.to = session.GetRoundState();
		event.reason = static_cast<uint16_t>( transition.reason );
		evidence.AppendRoundTransition( stamp, event );
	}

	const mpMatchPauseView &currentPause = session.GetPause();
	if ( pause.state != currentPause.state ) {
		mpEvidencePauseTransition event;
		event.from = EvidencePauseState( pause.state );
		event.to = EvidencePauseState( currentPause.state );
		const mpMatchPauseKind_t effectiveKind = currentPause.kind !=
			MP_MATCH_PAUSE_KIND_NONE ? currentPause.kind : pause.kind;
		event.kind = EvidencePauseKind( effectiveKind );
		event.ownerSide = effectiveKind == MP_MATCH_PAUSE_KIND_TEAM_TIMEOUT ?
			static_cast<int8_t>( currentPause.ownerSide >= 0 ?
				currentPause.ownerSide : pause.ownerSide ) : -1;
		const mpMatchPauseReason_t effectiveReason = currentPause.reason !=
			MP_MATCH_PAUSE_REASON_NONE ? currentPause.reason : pause.reason;
		event.reason = static_cast<uint16_t>( effectiveReason );
		event.actor = actor;
		evidence.AppendPauseTransition( stamp, event );
	}

	for ( int index = 0; index < MP_MATCH_MAX_PARTICIPANTS; ++index ) {
		const mpMatchParticipantState *current = session.GetParticipantByIndex( index );
		const bool occupied = current != NULL && current->id.IsValid();
		const uint32_t sequence = occupied ? current->id.SequencePart() : 0;
		const mpMatchRoleMask_t roles = occupied ? current->roles : 0;
		const participantState_t &previous = participants[ index ];
		if ( previous.occupied && previous.roles != 0 &&
			( !occupied || previous.sequence != sequence ) ) {
			mpEvidenceRoleChange event;
			event.targetParticipant = previous.sequence;
			event.previousRoles = previous.roles;
			event.currentRoles = 0;
			event.authorizer = actor;
			evidence.AppendRoleChange( stamp, event );
		}
		if ( occupied && ( !previous.occupied || previous.sequence != sequence ) ) {
			if ( roles != 0 ) {
				mpEvidenceRoleChange event;
				event.targetParticipant = sequence;
				event.previousRoles = 0;
				event.currentRoles = roles;
				event.authorizer = actor;
				evidence.AppendRoleChange( stamp, event );
			}
		} else if ( occupied && previous.roles != roles ) {
			mpEvidenceRoleChange event;
			event.targetParticipant = sequence;
			event.previousRoles = previous.roles;
			event.currentRoles = roles;
			event.authorizer = actor;
			evidence.AppendRoleChange( stamp, event );
		}
	}

	for ( int index = 0; index < MP_MATCH_MAX_ROSTER_SEATS; ++index ) {
		const mpMatchRosterSeat *source = session.GetRosterSeat( index );
		rosterState_t current;
		memset( &current, 0, sizeof( current ) );
		if ( source != NULL && source->declared ) {
			current.declared = true;
			current.side = source->side;
			current.role = source->role;
			current.occupant = source->occupant.IsValid() ?
				source->occupant.SequencePart() : 0;
		}
		const rosterState_t &previous = roster[ index ];
		const bool definitionChanged = previous.declared && current.declared &&
			( previous.side != current.side || previous.role != current.role );
		mpEvidenceRosterEvent event;
		memset( &event, 0, sizeof( event ) );
		event.seat = static_cast<uint8_t>( index );
		event.authorizer = actor;
		if ( definitionChanged ) {
			// A declared seat's side/role is replaced atomically by the session.
			// Project that replacement as old-seat teardown followed by new-seat
			// declaration so the persisted journal never silently changes meaning.
			event.side = static_cast<int8_t>( previous.side );
			event.role = EvidenceRosterRole( previous.role );
			if ( previous.occupant != 0 ) {
				event.action = MP_EVIDENCE_ROSTER_PARTICIPANT_VACATED;
				event.participant = previous.occupant;
				evidence.AppendRosterChange( stamp, event );
				event.participant = 0;
			}
			event.action = MP_EVIDENCE_ROSTER_SEAT_CLEARED;
			evidence.AppendRosterChange( stamp, event );
			event.side = static_cast<int8_t>( current.side );
			event.role = EvidenceRosterRole( current.role );
			event.action = MP_EVIDENCE_ROSTER_SEAT_DECLARED;
			evidence.AppendRosterChange( stamp, event );
			if ( current.occupant != 0 ) {
				event.action = MP_EVIDENCE_ROSTER_PARTICIPANT_ASSIGNED;
				event.participant = current.occupant;
				evidence.AppendRosterChange( stamp, event );
			}
			continue;
		}

		event.side = static_cast<int8_t>( current.declared ? current.side : previous.side );
		event.role = EvidenceRosterRole( current.declared ? current.role : previous.role );
		if ( !previous.declared && current.declared ) {
			event.action = MP_EVIDENCE_ROSTER_SEAT_DECLARED;
			evidence.AppendRosterChange( stamp, event );
		}
		if ( previous.occupant == 0 && current.occupant != 0 ) {
			event.action = MP_EVIDENCE_ROSTER_PARTICIPANT_ASSIGNED;
			event.participant = current.occupant;
			evidence.AppendRosterChange( stamp, event );
		} else if ( previous.occupant != 0 && current.occupant == 0 ) {
			event.action = MP_EVIDENCE_ROSTER_PARTICIPANT_VACATED;
			event.participant = previous.occupant;
			evidence.AppendRosterChange( stamp, event );
		} else if ( previous.occupant != 0 && current.occupant != 0 &&
			previous.occupant != current.occupant ) {
			event.action = MP_EVIDENCE_ROSTER_SUBSTITUTED;
			event.participant = previous.occupant;
			event.replacementParticipant = current.occupant;
			evidence.AppendRosterChange( stamp, event );
		}
		if ( previous.declared && !current.declared ) {
			event.action = MP_EVIDENCE_ROSTER_SEAT_CLEARED;
			event.participant = 0;
			event.replacementParticipant = 0;
			evidence.AppendRosterChange( stamp, event );
		}
	}

	for ( int rawScope = 0; rawScope < MP_PROPOSAL_SCOPE_COUNT; ++rawScope ) {
		const mpProposalScope_t scope = static_cast<mpProposalScope_t>( rawScope );
		const mpProposalRecord_t *current = proposals.GetProposal( scope );
		const proposalState_t &previous = proposal[ rawScope ];
		if ( current == NULL || !current->IsOccupied() ) {
			continue;
		}
		const bool created = !previous.occupied ||
			previous.proposalId != current->proposalId;
		mpEvidenceProposalEvent event;
		event.proposalId = current->proposalId;
		event.opcode = static_cast<uint16_t>( current->operation.opcode );
		event.scopeSide = static_cast<int8_t>( ProposalSide( scope ) );
		event.targetParticipant = current->operation.hasParticipantTarget ?
			current->operation.participantTarget : 0;
		event.actor = actor;
		if ( created ) {
			event.action = MP_EVIDENCE_PROPOSAL_CREATED;
			evidence.AppendProposal( stamp, event );
		}
		if ( ( created && current->castCount != 0 ) ||
			( !created && current->castCount != previous.castCount ) ) {
			event.action = MP_EVIDENCE_PROPOSAL_BALLOT_CAST;
			evidence.AppendProposal( stamp, event );
		}
		if ( ( created && current->status != MP_PROPOSAL_STATUS_ACTIVE ) ||
			( !created && current->status != previous.status &&
				current->status != MP_PROPOSAL_STATUS_ACTIVE ) ) {
			event.action = ProposalAction( current->status );
			evidence.AppendProposal( stamp, event );
		}
	}

	Capture( session, proposals );
}
