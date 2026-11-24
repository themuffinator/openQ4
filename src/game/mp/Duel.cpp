//----------------------------------------------------------------
// Duel.cpp
//
// openQ4 Duel, carried over from Quake Live.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "Duel.h"

gameStateType_t rvDuelGameState::type = GS_DUEL;

/*
================
rvDuelGameState::rvDuelGameState
================
*/
rvDuelGameState::rvDuelGameState( bool allocPrevious ) : rvDMGameState( allocPrevious ) {
	Clear();

	trackPrevious = allocPrevious;
}

/*
================
rvDuelGameState::Clear
================
*/
void rvDuelGameState::Clear( void ) {
	rvGameState::Clear();

	contenders[ 0 ] = -1;
	contenders[ 1 ] = -1;
	previousContenders[ 0 ] = -1;
	previousContenders[ 1 ] = -1;
	forfeitingContender = -1;
	turnoverCancelled = false;
	queue.Clear();
}

void rvDuelGameState::PackState( idBitMsg &msg ) {
	rvGameState::PackState( msg );
	msg.WriteByte( contenders[ 0 ] < 0 ? 255 : contenders[ 0 ] );
	msg.WriteByte( contenders[ 1 ] < 0 ? 255 : contenders[ 1 ] );
}

void rvDuelGameState::SendState( const idMessageSender &sender, int clientNum ) {
	assert( ( gameLocal.isServer || gameLocal.isRepeater ) && trackPrevious );
	if ( clientNum == -1 && *this == *previousGameState &&
		contenders[ 0 ] == previousContenders[ 0 ] &&
		contenders[ 1 ] == previousContenders[ 1 ] ) {
		return;
	}
	idBitMsg msg;
	byte buffer[ MAX_GAME_MESSAGE_SIZE ];
	msg.Init( buffer, sizeof( buffer ) );
	msg.WriteByte( GAME_RELIABLE_MESSAGE_GAMESTATE );
	WriteState( msg );
	sender.Send( msg );
	if ( clientNum == -1 ) {
		msg.ReadByte();
		ReceiveState( msg );
	}
}

void rvDuelGameState::ReceiveState( const idBitMsg &msg ) {
	// The base header is 16 bytes, followed by two contender slots. Decode
	// into a temporary base state so malformed seats cannot partly change phase.
	if ( msg.GetRemainingReadBits() != 18 * 8 ) {
		gameLocal.Warning( "Ignoring malformed Duel state length" );
		return;
	}
	rvGameState decoded( false );
	if ( !decoded.BaseUnpackState( msg ) ) {
		return;
	}
	const int first = msg.ReadByte();
	const int second = msg.ReadByte();
	if ( ( first >= MAX_CLIENTS && first != 255 ) ||
		( second >= MAX_CLIENTS && second != 255 ) ||
		( first == second && first != 255 ) ||
		decoded.GetMPGameState() < INACTIVE || decoded.GetMPGameState() > NEXTGAME ||
		decoded.GetNextMPGameState() < INACTIVE || decoded.GetNextMPGameState() > NEXTGAME ||
		decoded.GetNextMPGameStateTime() < 0 || decoded.GetOvertimeMsec() < 0 ||
		decoded.GetOvertimeStartTime() < 0 ) {
		gameLocal.Warning( "Ignoring invalid Duel state" );
		return;
	}
	rvGameState::operator=( decoded );
	contenders[ 0 ] = first == 255 ? -1 : first;
	contenders[ 1 ] = second == 255 ? -1 : second;
	if ( gameLocal.localClientNum >= 0 ) {
		GameStateChanged();
	}
	*previousGameState = *this;
	previousContenders[ 0 ] = contenders[ 0 ];
	previousContenders[ 1 ] = contenders[ 1 ];
}

/*
================
rvDuelGameState::GetQueuePosition

1 for next up, 2 for the one after, and so on.  0 means not queued.
================
*/
int rvDuelGameState::GetQueuePosition( int clientNum ) const {
	int i;

	for ( i = 0; i < queue.Num(); i++ ) {
		if ( queue[ i ] == clientNum ) {
			return i + 1;
		}
	}

	return 0;
}

/*
================
rvDuelGameState::UpdateQueue

Keeps the waiting list honest: everyone who wants to play and is not one of
the two contenders joins the back of the queue, and anyone who has left or
gone to spectate by choice drops off it.
================
*/
void rvDuelGameState::UpdateQueue( void ) {
	int i;

	// drop stale entries
	for ( i = queue.Num() - 1; i >= 0; i-- ) {
		idEntity *ent = gameLocal.entities[ queue[ i ] ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ||
			 static_cast< idPlayer * >( ent )->wantSpectate ) {
			queue.RemoveIndex( i );
		}
	}

	for ( i = 0; i < 2; i++ ) {
		if ( contenders[ i ] < 0 ) {
			continue;
		}

		idEntity *ent = gameLocal.entities[ contenders[ i ] ];
		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ||
			 static_cast< idPlayer * >( ent )->wantSpectate ) {
			contenders[ i ] = -1;
		}
	}

	// enrol anyone new who wants in
	for ( i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[ i ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *p = static_cast< idPlayer * >( ent );
		if ( p->wantSpectate ) {
			continue;
		}

		if ( i == contenders[ 0 ] || i == contenders[ 1 ] ) {
			continue;
		}

		if ( GetQueuePosition( i ) == 0 ) {
			queue.Append( i );
		}
	}
}

/*
================
rvDuelGameState::PromoteFromQueue
================
*/
void rvDuelGameState::PromoteFromQueue( void ) {
	int i;

	for ( i = 0; i < 2 && queue.Num() > 0; i++ ) {
		if ( contenders[ i ] >= 0 ) {
			continue;
		}

		contenders[ i ] = queue[ 0 ];
		queue.RemoveIndex( 0 );
	}
}

/*
================
rvDuelGameState::PreferHumanContenders

Bots keep a duel server from standing empty; they must never be the reason a
person cannot play.  Casual Duel only starts its countdown with a human in a
seat, and seats change only between matches, so two seated bots used to hold
a joining human in the queue for good: warmup never ended, so the rotation
that would have let them in never ran.  A bot in a seat while a human waits
goes to the back of the queue and the first waiting human takes the seat.
================
*/
void rvDuelGameState::PreferHumanContenders( void ) {
	for ( int seat = 0; seat < 2; seat++ ) {
		if ( contenders[ seat ] < 0 || !botManager.IsBot( contenders[ seat ] ) ) {
			continue;
		}

		int waiting = -1;
		for ( int i = 0; i < queue.Num(); i++ ) {
			if ( !botManager.IsBot( queue[ i ] ) ) {
				waiting = i;
				break;
			}
		}
		if ( waiting < 0 ) {
			return;
		}

		const int bot = contenders[ seat ];
		const int human = queue[ waiting ];
		contenders[ seat ] = human;
		queue.RemoveIndex( waiting );
		queue.Append( bot );

		// The person was held in spectator by the loop at the end of Run();
		// ask for the respawn their join would have made.  The bot is put
		// back in spectator by the same loop on this frame.
		idEntity *ent = gameLocal.entities[ human ];
		if ( ent && ent->IsType( idPlayer::GetClassType() ) ) {
			static_cast< idPlayer * >( ent )->forceRespawn = true;
		}
	}
}

/*
================
rvDuelGameState::RotateLoser

Winner stays on.  When a match ends the loser goes to the back of the queue
and the next challenger takes their place.
================
*/
void rvDuelGameState::RotateLoser( void ) {
	idPlayer *first, *second, *loser;

	if ( contenders[ 0 ] < 0 || contenders[ 1 ] < 0 ) {
		return;
	}

	first = static_cast< idPlayer * >( gameLocal.entities[ contenders[ 0 ] ] );
	second = static_cast< idPlayer * >( gameLocal.entities[ contenders[ 1 ] ] );

	if ( first == NULL || second == NULL ) {
		return;
	}
	if ( gameLocal.mpGame.IsManagedMatch() ) {
		// A validated forfeit retires its actor even while they lead on frags.
		// An unresolved terminal identity must never become a score-based win.
		if ( turnoverCancelled || forfeitingContender == -2 ) {
			return;
		}
		const int firstScore = gameLocal.mpGame.GetScore( first );
		const int secondScore = gameLocal.mpGame.GetScore( second );
		const int losingSlot = forfeitingContender >= 0 ? forfeitingContender :
			( firstScore == secondScore ? -1 :
				( firstScore < secondScore ? contenders[ 0 ] : contenders[ 1 ] ) );
		if ( gameLocal.mpGame.RotateManagedDuelQueue( contenders[ 0 ],
			contenders[ 1 ], losingSlot ) ) {
			for ( int index = 0; index < 2; ++index ) {
				if ( losingSlot < 0 || contenders[ index ] == losingSlot ) {
					contenders[ index ] = -1;
				}
			}
		}
		return;
	}

	// nobody waiting means the same pair simply plays again
	if ( queue.Num() == 0 ) {
		return;
	}

	if ( gameLocal.mpGame.GetScore( first ) == gameLocal.mpGame.GetScore( second ) ) {
		// a drawn duel puts both back in line, as Quake Live does
		queue.Append( contenders[ 0 ] );
		queue.Append( contenders[ 1 ] );
		contenders[ 0 ] = -1;
		contenders[ 1 ] = -1;
		return;
	}

	if ( gameLocal.mpGame.GetScore( first ) < gameLocal.mpGame.GetScore( second ) ) {
		loser = first;
		contenders[ 0 ] = -1;
	} else {
		loser = second;
		contenders[ 1 ] = -1;
	}

	queue.Append( loser->entityNumber );
}

/*
================
rvDuelGameState::NewState
================
*/
bool rvDuelGameState::NewState( mpGameState_t newState ) {
	if ( !rvDMGameState::NewState( newState ) ) {
		return false;
	}

	if ( gameLocal.isClient ) {
		return true;
	}

	// Keep both finalists seated throughout review, including on remote HUDs.
	// NEXTGAME still sees the final scores before warmup resets them.
	if ( newState == NEXTGAME ) {
		RotateLoser();
	} else if ( newState == WARMUP ) {
		forfeitingContender = -1;
		turnoverCancelled = false;
	}
	return true;
}

/*
================
rvDuelGameState::ClientDisconnect
================
*/
void rvDuelGameState::ClientDisconnect( idPlayer* player ) {
	int i;

	rvDMGameState::ClientDisconnect( player );

	if ( player == NULL ) {
		return;
	}

	for ( i = 0; i < 2; i++ ) {
		if ( contenders[ i ] == player->entityNumber ) {
			contenders[ i ] = -1;
		}
	}

	i = GetQueuePosition( player->entityNumber );
	if ( i > 0 ) {
		queue.RemoveIndex( i - 1 );
	}
}

/*
================
rvDuelGameState::AllowRespawn

Vetoing the respawn is what keeps the queue out of the arena.  The loop at the
end of Run() only puts a waiting player back into spectator after they have
already been spawned, which costs a spawn point, a spawn effect and a KillBox
every time their respawn timer comes round.
================
*/
bool rvDuelGameState::AllowRespawn( idPlayer* player ) {
	if ( player == NULL ) {
		return false;
	}

	// before the seats are filled there is nothing to keep anyone out of
	if ( contenders[ 0 ] < 0 && contenders[ 1 ] < 0 ) {
		return rvDMGameState::AllowRespawn( player );
	}

	if ( !IsContender( player->entityNumber ) ) {
		return false;
	}

	return rvDMGameState::AllowRespawn( player );
}

/*
================
rvDuelGameState::Run

Everything above the queue is plain deathmatch: Duel is deathmatch with the
arena restricted to two people at a time.
================
*/
void rvDuelGameState::Run( void ) {
	int i;

	// Seed the two seats before the base state machine runs.  WARMUP can become
	// COUNTDOWN inside rvDMGameState::Run() on the very frame the second player
	// joins; doing this afterwards misses the only setup-state promotion window
	// and leaves both players permanently classified as waiting spectators.
	if ( !gameLocal.isClient ) {
		UpdateQueue();

		// Only fill empty seats while a match is being set up, so a duel is never
		// interrupted by a third player walking in.
		if ( currentState == WARMUP || currentState == INACTIVE || currentState == NEXTGAME ) {
			PromoteFromQueue();
			PreferHumanContenders();
		}
	}

	rvDMGameState::Run();

	if ( gameLocal.isClient ) {
		return;
	}

	// hold everyone who is not a contender in spectator
	for ( i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[ i ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *p = static_cast< idPlayer * >( ent );
		if ( p->wantSpectate ) {
			continue;
		}

		bool isContender = ( i == contenders[ 0 ] || i == contenders[ 1 ] );

		if ( !isContender && !p->spectating ) {
			p->ServerSpectate( true );
		}
	}
}

/*
================
rvDuelGameState::IsType
================
*/
bool rvDuelGameState::IsType( gameStateType_t t ) const {
	return ( t == rvDuelGameState::type );
}

/*
================
rvDuelGameState::GetClassType
================
*/
gameStateType_t rvDuelGameState::GetClassType( void ) {
	return rvDuelGameState::type;
}
