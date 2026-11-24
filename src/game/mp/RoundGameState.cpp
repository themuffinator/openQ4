//----------------------------------------------------------------
// RoundGameState.cpp
//
// openQ4 round based match progression.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "RoundGameState.h"

gameStateType_t rvRoundGameState::type = GS_ROUND;

// tags for the round half of the gamestate packet
typedef enum {
	MSG_ROUND_STATE = 0,
	MSG_ROUND_NUMBER,
	MSG_ROUND_STARTTIME,
	MSG_ROUND_STATETIME,
	MSG_ROUND_WINNER,
	MSG_ROUND_COUNT
} msgRound_t;

/*
================
rvRoundGameState::rvRoundGameState
================
*/
rvRoundGameState::rvRoundGameState( bool allocPrevious ) : rvGameState( false ) {
	Clear();

	if ( allocPrevious ) {
		previousGameState = new rvRoundGameState( false );
	} else {
		previousGameState = NULL;
	}

	trackPrevious = allocPrevious;
}

/*
================
rvRoundGameState::Clear
================
*/
void rvRoundGameState::Clear( void ) {
	int i;

	rvGameState::Clear();

	if ( previousGameState ) {
		previousGameState->Clear();
	}

	roundState = RS_INACTIVE;
	roundNumber = 0;
	roundStartTime = 0;
	roundStateTime = 0;
	roundWinner = TEAM_NONE;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		eliminated[ i ] = false;
	}
}

void rvRoundGameState::ShiftMatchTime( int deltaMsec ) {
	rvGameState::ShiftMatchTime( deltaMsec );
	if ( deltaMsec <= 0 ) {
		return;
	}
	const int maxInt = 0x7fffffff;
	int *deadlines[] = { &roundStartTime, &roundStateTime };
	for ( int index = 0; index < 2; ++index ) {
		int &deadline = *deadlines[ index ];
		if ( deadline > 0 ) {
			deadline = deadline > maxInt - deltaMsec ? maxInt : deadline + deltaMsec;
		}
	}
}

/*
================
rvRoundGameState::IsEliminated
================
*/
bool rvRoundGameState::IsEliminated( int clientNum ) const {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return false;
	}

	return eliminated[ clientNum ];
}

/*
================
rvRoundGameState::SetEliminated
================
*/
void rvRoundGameState::SetEliminated( int clientNum, bool value ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}

	eliminated[ clientNum ] = value;
}

/*
================
rvRoundGameState::PlayerIsAlive

A player counts towards their team while they are on a team, are not sitting
out by choice, and have not been eliminated from this round.
================
*/
bool rvRoundGameState::PlayerIsAlive( idPlayer* player ) const {
	if ( player == NULL ) {
		return false;
	}

	if ( player->wantSpectate || player->team < 0 || player->team >= TEAM_MAX ) {
		return false;
	}

	if ( IsEliminated( player->entityNumber ) ) {
		return false;
	}

	// dead but not yet eliminated only happens outside elimination modes
	return ( player->health > 0 || !IsEliminationMode() );
}

/*
================
rvRoundGameState::IsEliminationMode
================
*/
bool rvRoundGameState::IsEliminationMode( void ) const {
	return MPGameTypeHasAny( gameLocal.gameType, GTF_ELIMINATION );
}

/*
================
rvRoundGameState::CountLiveTeamPlayers
================
*/
int rvRoundGameState::CountLiveTeamPlayers( int team ) const {
	int i, count = 0;

	for ( i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[ i ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *p = static_cast< idPlayer * >( ent );
		if ( p->team != team ) {
			continue;
		}
		if ( PlayerIsAlive( p ) ) {
			count++;
		}
	}

	return count;
}

/*
================
rvRoundGameState::CountTeamHealth

Quake Live breaks a timed-out Clan Arena round on players left first and total
health second, so both tallies are needed.
================
*/
int rvRoundGameState::CountTeamHealth( int team ) const {
	int i, total = 0;

	for ( i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[ i ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *p = static_cast< idPlayer * >( ent );
		if ( p->team != team || !PlayerIsAlive( p ) ) {
			continue;
		}

		total += Max( 0, p->health );
	}

	return total;
}

/*
================
rvRoundGameState::LastTeamStanding
================
*/
int rvRoundGameState::LastTeamStanding( void ) const {
	int marine = CountLiveTeamPlayers( TEAM_MARINE );
	int strogg = CountLiveTeamPlayers( TEAM_STROGG );

	if ( marine > 0 && strogg == 0 ) {
		return TEAM_MARINE;
	}
	if ( strogg > 0 && marine == 0 ) {
		return TEAM_STROGG;
	}

	return TEAM_NONE;
}

/*
================
rvRoundGameState::CheckRoundEnd

Default rule: the round ends when one side is wiped out.  Modes that end on
an objective override this and call down for the elimination case.
================
*/
bool rvRoundGameState::CheckRoundEnd( int &winningTeam ) {
	int marine = CountLiveTeamPlayers( TEAM_MARINE );
	int strogg = CountLiveTeamPlayers( TEAM_STROGG );

	// both sides gone in the same frame is a draw
	if ( !marine && !strogg ) {
		winningTeam = TEAM_NONE;
		return true;
	}

	if ( !marine || !strogg ) {
		winningTeam = marine ? TEAM_MARINE : TEAM_STROGG;
		return true;
	}

	return false;
}

/*
================
rvRoundGameState::ResolveRoundTimeout

Players left decides it; equal counts fall through to total health; equal
health is a draw.
================
*/
int rvRoundGameState::ResolveRoundTimeout( void ) {
	int marine = CountLiveTeamPlayers( TEAM_MARINE );
	int strogg = CountLiveTeamPlayers( TEAM_STROGG );

	if ( marine != strogg ) {
		return ( marine > strogg ) ? TEAM_MARINE : TEAM_STROGG;
	}

	marine = CountTeamHealth( TEAM_MARINE );
	strogg = CountTeamHealth( TEAM_STROGG );

	if ( marine != strogg ) {
		return ( marine > strogg ) ? TEAM_MARINE : TEAM_STROGG;
	}

	return TEAM_NONE;
}

/*
================
rvRoundGameState::GetRoundTimeRemaining
================
*/
int rvRoundGameState::GetRoundTimeRemaining( void ) const {
	int roundTimeLimit;

	if ( roundState != RS_ACTIVE ) {
		return -1;
	}

	roundTimeLimit = gameLocal.serverInfo.GetInt( "si_roundTimeLimit" );
	if ( roundTimeLimit <= 0 ) {
		return -1;
	}

	return Max( 0, ( roundStartTime + roundTimeLimit * 1000 ) - gameLocal.time );
}

/*
================
rvRoundGameState::RoundBegin
================
*/
void rvRoundGameState::RoundBegin( void ) {
	// Last chance for anybody who died during the countdown.  Every subclass
	// chains to this first, so the revive is ordered ahead of a mode's own
	// round-start loadout - Clan Arena would otherwise write full health onto a
	// body that is still a ragdoll.
	ReviveForRound();
}

/*
================
rvRoundGameState::PrepareNextRound
================
*/
void rvRoundGameState::PrepareNextRound( void ) {
}

/*
================
rvRoundGameState::RoundEnd
================
*/
void rvRoundGameState::RoundEnd( int winningTeam ) {
}

/*
================
rvRoundGameState::AwardRound

A round win is worth one match point, exactly as in Quake Live.  A draw scores
nothing for either side.
================
*/
void rvRoundGameState::AwardRound( int winningTeam ) {
	int i;

	roundWinner = winningTeam;

	if ( winningTeam == TEAM_NONE ) {
		gameLocal.mpGame.CenterPrint( -1, "#str_41334" );
		return;
	}

	gameLocal.mpGame.AddTeamScore( winningTeam, RoundWinScore() );
	gameLocal.mpGame.CenterPrint( -1, "#str_41333", idMultiplayerGame::CPARM_TEAM, winningTeam );

	// credit everyone who was still contesting the round with the win, which
	// is what drives the per-player round column on the scoreboard
	for ( i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[ i ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *p = static_cast< idPlayer * >( ent );
		if ( p->team == winningTeam && !p->wantSpectate ) {
			gameLocal.mpGame.AddPlayerWin( p, 1 );
		}
	}
}

/*
================
rvRoundGameState::CheckMatchEnd

Round modes own their own exit rules, the same way Quake Live's CheckExitRules
returns outright for Clan Arena, Attack & Defend and Red Rover.
================
*/
bool rvRoundGameState::CheckMatchEnd( int &winningTeam ) {
	int roundLimit, marine, strogg, mercyTeam;

	winningTeam = TEAM_NONE;

	marine = gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE );
	strogg = gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG );

	// a side left with nobody hands the match over
	if ( gameLocal.mpGame.ForfeitTeam() >= 0 ) {
		winningTeam = gameLocal.mpGame.ForfeitTeam();
		gameLocal.mpGame.CenterPrint( -1, "#str_41316", idMultiplayerGame::CPARM_TEAM, winningTeam );
		return true;
	}

	mercyTeam = gameLocal.mpGame.MercyLimitHit();
	if ( mercyTeam >= 0 ) {
		winningTeam = mercyTeam;
		gameLocal.mpGame.CenterPrint( -1, "#str_41317", idMultiplayerGame::CPARM_TEAM, winningTeam );
		return true;
	}

	// "first to N rounds", the reading players actually experience
	roundLimit = gameLocal.serverInfo.GetInt( "si_roundLimit" );
	if ( roundLimit > 0 && ( marine >= roundLimit || strogg >= roundLimit ) ) {
		winningTeam = ( marine > strogg ) ? TEAM_MARINE : ( ( strogg > marine ) ? TEAM_STROGG : TEAM_NONE );
		return true;
	}

	if ( gameLocal.mpGame.TimeLimitHit() ) {
		if ( marine == strogg ) {
			// tied on the buzzer: extend, or fall through to a drawn match
			if ( StartOvertime() ) {
				return false;
			}
		}

		gameLocal.mpGame.PrintMessageEvent( -1, MSG_TIMELIMIT );
		winningTeam = ( marine > strogg ) ? TEAM_MARINE : ( ( strogg > marine ) ? TEAM_STROGG : TEAM_NONE );
		return true;
	}

	return false;
}

/*
================
rvRoundGameState::ResetRound

Wipes the arena between rounds.  Quake Live issues a real map_restart with a
g_restarted handshake; Quake 4 can do it in place, which is both cleaner and
what NewState( GAMEON ) already does at the start of a match.
================
*/
void rvRoundGameState::ResetRound( void ) {
	idBitMsg	outMsg;
	byte		msgBuf[ MAX_GAME_MESSAGE_SIZE ];
	int			i;

	if ( gameLocal.isClient ) {
		return;
	}

	gameLocal.LocalMapRestart();

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_ROUNDRESTART );
	networkSystem->ServerSendReliableMessage( -1, outMsg );

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		eliminated[ i ] = false;
	}

	for ( i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[ i ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *p = static_cast< idPlayer * >( ent );

		p->SetLeader( false );
		p->JoinInstance( 0 );

		// everyone who wants to play comes back for the new round
		if ( !gameLocal.IsTeamGame() || p->team != -1 ) {
			p->ServerSpectate( p->wantSpectate );
		}
	}
}

/*
================
rvRoundGameState::ScheduleNextRound
================
*/
bool rvRoundGameState::ScheduleNextRound( void ) {
	int warmupDelay = gameLocal.serverInfo.GetInt( "si_roundWarmupDelay" );
	// Commit the authoritative edge before resetting players, counters, scores,
	// or deadlines.  A rejected transition must leave the active/completed round
	// entirely untouched so the next frame can retry or report the invariant.
	if ( !SetRoundState( RS_COUNTDOWN ) ) {
		return false;
	}

	// Every round is prepared, including the first.  Red Rover's reshuffle lives
	// here, and skipping it for round one left the opening round on whatever
	// split the players happened to pick - which in a mode that is decided by
	// absorbing the other side is the whole starting position.
	PrepareNextRound();

	// NewState( GAMEON ) has just restarted the map for the first round, so
	// resetting again here would respawn everyone twice in two frames
	if ( roundNumber > 0 ) {
		ResetRound();
	} else {
		int i;

		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			eliminated[ i ] = false;
		}
	}

	roundNumber++;
	roundWinner = TEAM_NONE;

	roundStateTime = gameLocal.time + Max( 0, warmupDelay ) * 1000;
	roundStartTime = roundStateTime;
	return true;
}

/*
================
rvRoundGameState::ReviveForRound

Puts anybody who is dead but still in the round back on their feet.  Only the
respawn is forced here; CheckRespawns still decides whether it is allowed, so an
eliminated player is not let back in by this.
================
*/
void rvRoundGameState::ReviveForRound( void ) {
	int i;

	if ( gameLocal.isClient ) {
		return;
	}

	for ( i = 0; i < gameLocal.numClients; i++ ) {
		idEntity *ent = gameLocal.entities[ i ];

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}

		idPlayer *p = static_cast< idPlayer * >( ent );
		if ( p->wantSpectate || p->health > 0 || IsEliminated( i ) ) {
			continue;
		}
		if ( gameLocal.IsTeamGame() && ( p->team < 0 || p->team >= TEAM_MAX ) ) {
			continue;
		}

		p->forceRespawn = true;
	}
}

/*
================
rvRoundGameState::SealRound

Quake Live sends anyone who arrives after a Clan Arena round has started into
follow-spectate until the next one.  openQ4 gets the same result out of the
elimination flag it already has, by treating "not in the round" and "out of the
round" as the same thing.

Every slot that is not a live participant right now is marked, INCLUDING the
empty ones - a client that connects into an empty slot mid-round inherits the
mark and so waits for the next round rather than walking into a fight that is
already half decided.  ResetRound clears the whole array, so nothing here
outlives the round it belongs to.
================
*/
void rvRoundGameState::SealRound( void ) {
	int i;

	if ( gameLocal.isClient || !IsEliminationMode() ) {
		return;
	}

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		idEntity *ent = ( i < gameLocal.numClients ) ? gameLocal.entities[ i ] : NULL;

		if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
			SetEliminated( i, true );
			continue;
		}

		idPlayer *p = static_cast< idPlayer * >( ent );
		// Deliberately not a health test.  CheckRespawns runs before the game
		// state each frame, so a player who died in the last moments of the
		// countdown can still be a corpse here even though ReviveForRound has
		// already asked for them back; they belong to this round.  What marks
		// somebody as arriving rather than playing is that they never left
		// spectator - a mid-round join has not spawned yet.
		const bool participating = !p->wantSpectate && !p->spectating &&
			( !gameLocal.IsTeamGame() || ( p->team >= 0 && p->team < TEAM_MAX ) );

		SetEliminated( i, !participating );
	}
}

/*
================
rvRoundGameState::SetRoundState
================
*/
bool rvRoundGameState::SetRoundState( roundState_t newState ) {
	if ( !gameLocal.mpGame.CommitMatchRoundTransition( newState ) ) {
		return false;
	}
	roundState = newState;
	return true;
}

/*
================
rvRoundGameState::Run
================
*/
void rvRoundGameState::Run( void ) {
	int winningTeam;

	rvGameState::Run();

	if ( gameLocal.isClient ) {
		return;
	}

	// rounds only turn over while the match itself is running
	if ( currentState != GAMEON ) {
		if ( roundState != RS_INACTIVE ) {
			if ( SetRoundState( RS_INACTIVE ) ) {
				roundNumber = 0;
				roundStartTime = 0;
				roundStateTime = 0;
				roundWinner = TEAM_NONE;
			}
		}
		return;
	}

	switch ( roundState ) {
		case RS_INACTIVE: {
			// between rounds: see whether the match is already decided
			if ( CheckMatchEnd( winningTeam ) ) {
				if ( winningTeam != TEAM_NONE ) {
					gameLocal.mpGame.CenterPrint( -1, "#str_41314", idMultiplayerGame::CPARM_TEAM, winningTeam );
				}
				NewState( GAMEREVIEW );
				break;
			}

			ScheduleNextRound();
			break;
		}

		case RS_COUNTDOWN: {
			// A death during the countdown is not an elimination, so the victim
			// is entitled to be back on their feet before "FIGHT".  Left alone
			// they would wait out idPlayer's ordinary respawn delay - up to
			// thirteen seconds, longer than the countdown itself - and the round
			// would open with a corpse on the field that still counts as a
			// living body for the round-end test.
			ReviveForRound();

			if ( gameLocal.time >= roundStateTime ) {
				if ( SetRoundState( RS_ACTIVE ) ) {
					roundStartTime = gameLocal.time;
					roundStateTime = 0;
					SealRound();
					RoundBegin();
				}
			}
			break;
		}

		case RS_ACTIVE: {
			int roundTimeLimit = gameLocal.serverInfo.GetInt( "si_roundTimeLimit" );

			if ( CheckRoundEnd( winningTeam ) ) {
				if ( SetRoundState( RS_COMPLETE ) ) {
					AwardRound( winningTeam );
					RoundEnd( winningTeam );
					roundStateTime = gameLocal.time + Max( 1, gameLocal.serverInfo.GetInt( "si_roundEndDelay" ) ) * 1000;
				}
				break;
			}

			if ( roundTimeLimit > 0 && gameLocal.time >= roundStartTime + roundTimeLimit * 1000 ) {
				winningTeam = ResolveRoundTimeout();
				if ( SetRoundState( RS_COMPLETE ) ) {
					AwardRound( winningTeam );
					RoundEnd( winningTeam );
					roundStateTime = gameLocal.time + Max( 1, gameLocal.serverInfo.GetInt( "si_roundEndDelay" ) ) * 1000;
				}
			}
			break;
		}

		case RS_COMPLETE: {
			if ( gameLocal.time >= roundStateTime ) {
				if ( CheckMatchEnd( winningTeam ) ) {
					if ( winningTeam != TEAM_NONE ) {
						gameLocal.mpGame.CenterPrint( -1, "#str_41314",
							idMultiplayerGame::CPARM_TEAM, winningTeam );
					}
					NewState( GAMEREVIEW );
				} else {
					// A completed round commits its result before moving directly to
					// the next round countdown. RS_ACTIVE -> RS_COUNTDOWN shortcuts
					// and a synthetic live RS_INACTIVE interstitial are both illegal.
					ScheduleNextRound();
				}
			}
			break;
		}
	}
}

/*
================
rvRoundGameState::NewState
================
*/
bool rvRoundGameState::NewState( mpGameState_t newState ) {
	int i;

	if ( !rvGameState::NewState( newState ) ) {
		return false;
	}

	if ( newState == GAMEON ) {
		// the first round is scheduled by Run() on the next frame, so the
		// GAMEON entry restart is not immediately undone by a round reset
		SetRoundState( RS_INACTIVE );
		roundNumber = 0;
		roundStartTime = 0;
		roundStateTime = 0;
		roundWinner = TEAM_NONE;

		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			eliminated[ i ] = false;
		}
	}
	return true;
}

/*
================
rvRoundGameState::AllowRespawn

Round-locked respawn.  This generalizes the tourney arena rule that a player
may only respawn while their arena is live and they are one of its players.
================
*/
bool rvRoundGameState::AllowRespawn( idPlayer* player ) {
	if ( player == NULL ) {
		return false;
	}

	if ( currentState != GAMEON ) {
		return true;
	}

	if ( !IsEliminationMode() ) {
		return true;
	}

	// a player who died this round waits for the next one
	return !IsEliminated( player->entityNumber );
}

/*
================
rvRoundGameState::PlayerDeath
================
*/
void rvRoundGameState::PlayerDeath( idPlayer* dead, idPlayer* killer ) {
	if ( dead == NULL || gameLocal.isClient ) {
		return;
	}

	if ( currentState != GAMEON || roundState != RS_ACTIVE ) {
		return;
	}

	if ( !IsEliminationMode() ) {
		return;
	}

	SetEliminated( dead->entityNumber, true );

	// Tell the last player on the side that just lost someone that it is down to
	// them.  Only that side can have crossed the threshold on this death, and
	// announcing for both would re-tell an already lone survivor on the other
	// team every time their opponents trade a kill.
	if ( dead->team >= 0 && dead->team < TEAM_MAX && CountLiveTeamPlayers( dead->team ) == 1 ) {
		int j;

		for ( j = 0; j < gameLocal.numClients; j++ ) {
			idEntity *ent = gameLocal.entities[ j ];

			if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
				continue;
			}

			idPlayer *p = static_cast< idPlayer * >( ent );
			if ( p->team == dead->team && PlayerIsAlive( p ) ) {
				gameLocal.mpGame.CenterPrint( j, "#str_41336" );
				// the announcer queue is client local, so this has to be sent
				gameLocal.mpGame.AnnounceTo( j, AS_MATCH_LAST_STANDING );
				break;
			}
		}
	}
}

/*
================
rvRoundGameState::WeaponsLocked

Quake Live sets PMF_ATTACK_LOCKOUT on everyone while a round is being set up
or has just been decided, so nobody can open fire during the countdown.
================
*/
bool rvRoundGameState::WeaponsLocked( void ) const {
	if ( rvGameState::WeaponsLocked() ) {
		return true;
	}

	if ( currentState != GAMEON ) {
		return false;
	}

	return ( roundState == RS_COUNTDOWN || roundState == RS_COMPLETE );
}

/*
================
rvRoundGameState::ClientDisconnect
================
*/
void rvRoundGameState::ClientDisconnect( idPlayer* player ) {
	rvGameState::ClientDisconnect( player );

	if ( player == NULL ) {
		return;
	}

	// A freed slot stays sealed for the rest of a live round.  Clearing it would
	// hand the next client to connect into that slot a way into a round it was
	// never part of - which is the same hole SealRound closes for fresh slots.
	SetEliminated( player->entityNumber,
		( currentState == GAMEON && roundState != RS_INACTIVE && IsEliminationMode() ) );
}

/*
================
rvRoundGameState::PlayerWithdrew

A team change kills the player with nodamage, which never reaches
idPlayer::Killed and so never reaches PlayerDeath.  Without this, changing sides
while cornered was a way to leave a round you were about to lose and come back
alive on the other team a frame later.

Only elimination modes care: in Red Rover a change of side IS the rule, and
marking the converted player would take them out of the very count that decides
the round.
================
*/
void rvRoundGameState::PlayerWithdrew( idPlayer* player ) {
	if ( player == NULL || gameLocal.isClient ) {
		return;
	}

	if ( currentState != GAMEON || roundState == RS_INACTIVE || !IsEliminationMode() ) {
		return;
	}

	SetEliminated( player->entityNumber, true );
}

/*
================
rvRoundGameState::Spectate

Choosing to sit out mid-round takes you out of that round for good, so a
player cannot spectate away an imminent elimination and come straight back.
================
*/
void rvRoundGameState::Spectate( idPlayer* player ) {
	rvGameState::Spectate( player );

	if ( player == NULL || gameLocal.isClient ) {
		return;
	}

	// Only elimination modes. In a non-elimination round mode nobody is ever
	// "out", so marking a spectate toggle as eliminated removes a player who is
	// alive and fighting from every live-count for the rest of the round - and
	// the round then ends as soon as their team mates die. Every other
	// elimination write in this class is gated the same way.
	if ( currentState == GAMEON && roundState == RS_ACTIVE && player->wantSpectate &&
		 IsEliminationMode() ) {
		SetEliminated( player->entityNumber, true );
	}
}

/*
================
rvRoundGameState::GameStateChanged
================
*/
void rvRoundGameState::GameStateChanged( void ) {
	rvRoundGameState *previous = static_cast< rvRoundGameState * >( previousGameState );
	idPlayer *localPlayer = gameLocal.GetLocalPlayer();

	rvGameState::GameStateChanged();

	if ( localPlayer == NULL || previous == NULL ) {
		return;
	}

	if ( roundState == previous->roundState && roundNumber == previous->roundNumber ) {
		return;
	}

	switch ( roundState ) {
		case RS_COUNTDOWN: {
			localPlayer->GUIMainNotice( va( common->GetLocalizedString( "#str_41330" ), va( "%d", roundNumber ) ), true );

			gameLocal.mpGame.RemoveAnnouncerSound( AS_ROUND_PREPARE );
			gameLocal.mpGame.RemoveAnnouncerSound( AS_GENERAL_THREE );
			gameLocal.mpGame.RemoveAnnouncerSound( AS_GENERAL_TWO );
			gameLocal.mpGame.RemoveAnnouncerSound( AS_GENERAL_ONE );

			gameLocal.mpGame.ScheduleAnnouncerSound( AS_ROUND_PREPARE, gameLocal.time );
			gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_THREE, roundStateTime - 3000 );
			gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_TWO, roundStateTime - 2000 );
			gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_ONE, roundStateTime - 1000 );
			break;
		}

		case RS_ACTIVE: {
			localPlayer->GUIMainNotice( common->GetLocalizedString( "#str_41332" ) );
			gameLocal.mpGame.ScheduleAnnouncerSound( AS_ROUND_FIGHT, gameLocal.time );
			break;
		}

		case RS_COMPLETE: {
			if ( roundWinner == TEAM_NONE ) {
				gameLocal.mpGame.ScheduleAnnouncerSound( AS_ROUND_DRAW, gameLocal.time );
			} else if ( roundWinner == localPlayer->team ) {
				gameLocal.mpGame.ScheduleAnnouncerSound( AS_ROUND_YOU_WIN, gameLocal.time );
			} else {
				gameLocal.mpGame.ScheduleAnnouncerSound( AS_ROUND_YOU_LOSE, gameLocal.time );
			}
			break;
		}

		default:
			break;
	}
}

/*
================
rvRoundGameState::SendState
================
*/
void rvRoundGameState::SendState( const idMessageSender &sender, int clientNum ) {
	idBitMsg	outMsg;
	byte		msgBuf[ MAX_GAME_MESSAGE_SIZE ];

	assert( ( gameLocal.isServer || gameLocal.isRepeater ) && trackPrevious );

	if ( clientNum == -1 && (rvRoundGameState&)(*this) == (rvRoundGameState&)(*previousGameState) ) {
		return;
	}

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_GAMESTATE );

	WriteState( outMsg );

	sender.Send( outMsg );

	if ( clientNum == -1 ) {
		outMsg.ReadByte();
		ReceiveState( outMsg );
	}
}

/*
================
rvRoundGameState::WriteState
================
*/
void rvRoundGameState::WriteState( idBitMsg &msg ) {
	rvGameState::PackState( msg );
	PackState( msg );
}

/*
================
rvRoundGameState::ReceiveState
================
*/
void rvRoundGameState::ReceiveState( const idBitMsg& msg ) {
	if ( !rvGameState::BaseUnpackState( msg ) ) {
		return;
	}

	UnpackState( msg );

	if ( gameLocal.localClientNum >= 0 ) {
		GameStateChanged();
	}

	(rvRoundGameState&)(*previousGameState) = (rvRoundGameState&)(*this);
}

/*
================
rvRoundGameState::PackState
================
*/
void rvRoundGameState::PackState( idBitMsg& outMsg ) {
	rvRoundGameState *previous = static_cast< rvRoundGameState * >( previousGameState );

	if ( roundState != previous->roundState ) {
		outMsg.WriteByte( MSG_ROUND_STATE );
		outMsg.WriteByte( roundState );
	}

	if ( roundNumber != previous->roundNumber ) {
		outMsg.WriteByte( MSG_ROUND_NUMBER );
		outMsg.WriteByte( idMath::ClampInt( 0, 255, roundNumber ) );
	}

	if ( roundStartTime != previous->roundStartTime ) {
		outMsg.WriteByte( MSG_ROUND_STARTTIME );
		outMsg.WriteLong( roundStartTime );
	}

	if ( roundStateTime != previous->roundStateTime ) {
		outMsg.WriteByte( MSG_ROUND_STATETIME );
		outMsg.WriteLong( roundStateTime );
	}

	if ( roundWinner != previous->roundWinner ) {
		outMsg.WriteByte( MSG_ROUND_WINNER );
		outMsg.WriteChar( roundWinner );
	}
}

/*
================
rvRoundGameState::UnpackState
================
*/
void rvRoundGameState::UnpackState( const idBitMsg& inMsg ) {
	while ( inMsg.GetRemainingData() ) {
		int index = inMsg.ReadByte();

		switch ( index ) {
			case MSG_ROUND_STATE:
				roundState = (roundState_t)inMsg.ReadByte();
				break;
			case MSG_ROUND_NUMBER:
				roundNumber = inMsg.ReadByte();
				break;
			case MSG_ROUND_STARTTIME:
				roundStartTime = inMsg.ReadLong();
				break;
			case MSG_ROUND_STATETIME:
				roundStateTime = inMsg.ReadLong();
				break;
			case MSG_ROUND_WINNER:
				roundWinner = inMsg.ReadChar();
				break;
			default:
				gameLocal.Error( "rvRoundGameState::UnpackState() - Unknown data identifier '%d'\n", index );
				return;
		}
	}
}

/*
================
rvRoundGameState::SendInitialState
================
*/
void rvRoundGameState::SendInitialState( const idMessageSender &sender, int clientNum ) {
	rvRoundGameState *previousState = static_cast< rvRoundGameState * >( previousGameState );
	rvRoundGameState invalidState;

	previousGameState = &invalidState;

	SendState( sender, clientNum );

	previousGameState = previousState;
}

/*
================
rvRoundGameState::operator==
================
*/
bool rvRoundGameState::operator==( const rvRoundGameState& rhs ) const {
	return (
		rvGameState::operator==( rhs ) &&
		( roundState == rhs.roundState ) &&
		( roundNumber == rhs.roundNumber ) &&
		( roundStartTime == rhs.roundStartTime ) &&
		( roundStateTime == rhs.roundStateTime ) &&
		( roundWinner == rhs.roundWinner )
		);
}

/*
================
rvRoundGameState::operator=
================
*/
rvRoundGameState& rvRoundGameState::operator=( const rvRoundGameState& rhs ) {
	rvGameState::operator=( rhs );

	roundState = rhs.roundState;
	roundNumber = rhs.roundNumber;
	roundStartTime = rhs.roundStartTime;
	roundStateTime = rhs.roundStateTime;
	roundWinner = rhs.roundWinner;

	return (*this);
}

/*
================
rvRoundGameState::IsType
================
*/
bool rvRoundGameState::IsType( gameStateType_t t ) const {
	return ( t == rvRoundGameState::type );
}

/*
================
rvRoundGameState::GetClassType
================
*/
gameStateType_t rvRoundGameState::GetClassType( void ) {
	return rvRoundGameState::type;
}
